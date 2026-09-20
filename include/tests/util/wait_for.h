
// tests/util/wait_for.h
#pragma once
#include <chrono>
#include <functional>
#include <thread>

template <typename Predicate, typename Duration>
bool waitFor(Predicate pred,
             Duration timeout,
             std::chrono::milliseconds pollInterval = std::chrono::milliseconds(50)) {
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(pollInterval);
    }
    return pred();
}