// bench/bench_latency.cpp
//
// 8-thread (configurable) shared-client batchRead/batchWrite latency benchmark.
//
//   ./bench_latency --threads 8 --ops 8000 --batch 16
//   ./bench_latency --endpoint opc.tcp://127.0.0.1:4840 --write
//
// Prints one machine-readable summary line and one latency line (P50/P90/P99/
// P99.9/max/mean, milliseconds) to stdout. Exit status is non-zero if any call
// failed.
#include "bench_common.h"

#include "OPCUAPacking/ua.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Args {
  size_t threads = 8;
  size_t ops = 8000;
  size_t batch = 16;
  size_t repeats = 1;
  uint16_t port = 4890;
  std::string endpoint;
  bool write = false;
};

Args parse(int argc, char **argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    const std::string k = argv[i];
    // --help must be handled before the parser's final "unknown argument"
    // else-branch. It used to sit after that branch, so `--help` printed
    // "unknown argument: --help" and exited 2 instead of the usage text.
    if (k == "--help" || k == "-h") {
      std::printf(
          "usage: bench_latency [--threads N] [--ops N] [--batch N]\n"
          "                     [--repeats N] [--port N] [--endpoint URL]\n"
          "                     [--write]\n");
      std::exit(0);
    }
    auto next = [&]() -> const char * {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value for %s\n", k.c_str());
        std::exit(2);
      }
      return argv[++i];
    };
    if (k == "--threads") {
      a.threads = std::strtoul(next(), nullptr, 10);
    } else if (k == "--ops") {
      a.ops = std::strtoul(next(), nullptr, 10);
    } else if (k == "--batch") {
      a.batch = std::strtoul(next(), nullptr, 10);
    } else if (k == "--port") {
      a.port = static_cast<uint16_t>(std::strtoul(next(), nullptr, 10));
    } else if (k == "--endpoint") {
      a.endpoint = next();
    } else if (k == "--repeats") {
      a.repeats = std::strtoul(next(), nullptr, 10);
    } else if (k == "--write") {
      a.write = true;
    } else {
      std::fprintf(stderr, "unknown argument: %s\n", k.c_str());
      std::exit(2);
    }
  }
  if (a.threads == 0)
    a.threads = 1;
  if (a.ops == 0)
    a.ops = 1;
  if (a.batch == 0)
    a.batch = 1;
  if (a.repeats == 0)
    a.repeats = 1;
  return a;
}

} // namespace

int main(int argc, char **argv) {
  bench::initLogging();
  const Args args = parse(argc, argv);

  std::unique_ptr<bench::Server> server;
  std::string url = args.endpoint;
  const char *nodeName = "BenchInt";
  if (url.empty()) {
    server = std::make_unique<bench::Server>(args.port, nodeName);
    url = "opc.tcp://127.0.0.1:" + std::to_string(args.port);
  }

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.watchdogIntervalMs = 1000;
  cfg.giveUpThresholdMs = 60000;

  auto created = OPC_UA_Client::create(url, cfg, /*useDefault=*/false);
  if (created.is_fail()) {
    std::fprintf(stderr, "create failed: %s\n",
                 created.get_error()->codeName());
    return 2;
  }
  auto client = std::move(*created.get());

  std::vector<OPC_UA_Client::ReadValue> reads;
  std::vector<OPC_UA_Client::WriteValue> writes;
  for (size_t i = 0; i < args.batch; ++i) {
    reads.push_back({1, nodeName, S7DataType::INT});
    writes.push_back({{1, nodeName, S7DataType::INT}, static_cast<int16_t>(i)});
  }

  struct Run {
    size_t calls = 0;
    double wallMs = 0.0;
    double callsPerSec = 0.0;
    double p50 = 0.0, p90 = 0.0, p99 = 0.0, p999 = 0.0, max = 0.0, mean = 0.0;
    uint64_t errors = 0;
  };

  const size_t perThread = args.ops / args.threads;
  std::vector<Run> runs;
  runs.reserve(args.repeats);
  uint64_t totalErrors = 0;

  for (size_t rep = 0; rep < args.repeats; ++rep) {
    for (int i = 0; i < 3; ++i) { // warmup per run
      if (args.write)
        (void)client->batchWrite(writes);
      else
        (void)client->batchRead(reads);
    }

    std::vector<std::vector<double>> latencies(args.threads);
    std::atomic<uint64_t> errors{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> pool;
    pool.reserve(args.threads);

    for (size_t t = 0; t < args.threads; ++t) {
      pool.emplace_back([&, t] {
        latencies[t].reserve(perThread);
        while (!go.load(std::memory_order_acquire))
          std::this_thread::yield();
        for (size_t i = 0; i < perThread; ++i) {
          const auto t0 = bench::Clock::now();
          const bool ok = args.write ? client->batchWrite(writes).is_success()
                                     : client->batchRead(reads).is_success();
          latencies[t].push_back(bench::msSince(t0));
          if (!ok)
            errors.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }

    const auto wall0 = bench::Clock::now();
    go.store(true, std::memory_order_release);
    for (auto &th : pool)
      th.join();
    const double wallMs = bench::msSince(wall0);

    std::vector<double> all;
    for (const auto &v : latencies)
      all.insert(all.end(), v.begin(), v.end());
    all = bench::sorted(std::move(all));

    Run run;
    run.calls = all.size();
    run.wallMs = wallMs;
    run.callsPerSec =
        wallMs > 0.0 ? static_cast<double>(run.calls) * 1000.0 / wallMs : 0.0;
    run.p50 = bench::percentile(all, 50.0);
    run.p90 = bench::percentile(all, 90.0);
    run.p99 = bench::percentile(all, 99.0);
    run.p999 = bench::percentile(all, 99.9);
    run.max = all.empty() ? 0.0 : all.back();
    run.mean = bench::mean(all);
    run.errors = errors.load();
    totalErrors += run.errors;

    std::printf("run=%zu/%zu mode=%s threads=%zu ops=%zu batch=%zu calls=%zu "
                "wall_ms=%.1f calls_s=%.1f items_s=%.1f errors=%llu\n",
                rep + 1, args.repeats, args.write ? "write" : "read",
                args.threads, args.ops, args.batch, run.calls, run.wallMs,
                run.callsPerSec,
                run.callsPerSec * static_cast<double>(args.batch),
                static_cast<unsigned long long>(run.errors));
    std::printf("run=%zu latency_ms p50=%.3f p90=%.3f p99=%.3f p999=%.3f "
                "max=%.3f mean=%.3f\n",
                rep + 1, run.p50, run.p90, run.p99, run.p999, run.max, run.mean);
    runs.push_back(run);
  }

  // A single run's tail is not reproducible: host scheduling moves p99 by more
  // than an order of magnitude between runs. Print the median across runs so a
  // report can quote a stable number instead of one lucky/unlucky sample.
  if (runs.size() > 1) {
    auto medianOf = [&](double (*pick)(const Run &)) {
      std::vector<double> v;
      v.reserve(runs.size());
      for (const Run &r : runs)
        v.push_back(pick(r));
      v = bench::sorted(std::move(v));
      return bench::percentile(v, 50.0);
    };
    std::printf("median mode=%s threads=%zu ops=%zu batch=%zu repeats=%zu "
                "calls_s=%.1f p50=%.3f p90=%.3f p99=%.3f p999=%.3f max=%.3f "
                "mean=%.3f errors=%llu\n",
                args.write ? "write" : "read", args.threads, args.ops, args.batch,
                runs.size(),
                medianOf([](const Run &r) { return r.callsPerSec; }),
                medianOf([](const Run &r) { return r.p50; }),
                medianOf([](const Run &r) { return r.p90; }),
                medianOf([](const Run &r) { return r.p99; }),
                medianOf([](const Run &r) { return r.p999; }),
                medianOf([](const Run &r) { return r.max; }),
                medianOf([](const Run &r) { return r.mean; }),
                static_cast<unsigned long long>(totalErrors));
  }

  (void)client->shutdown();
  return totalErrors == 0 ? 0 : 1;
}
