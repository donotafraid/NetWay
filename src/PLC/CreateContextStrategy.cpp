#include "PLC/CreateContextStrategy.h"
#include "PLC/OPCUAManager.h"

ContextStrategyFactory::ContextStrategyFactory() {
  registerStrategy(
      [](const QString &id) { return id.startsWith("opc.tcp://"); },
      []() { return std::make_unique<InlineBrowseStrategy>(); });
  registerStrategy(
      [](const QString &id) {
        return id.endsWith(".xml") || id.endsWith(".csv");
      },
      []() { return std::make_unique<FileContextStrategy>(); });
}

void ContextStrategyFactory::registerStrategy(
    std::function<bool(const QString &)> matcher,
    std::function<std::unique_ptr<IContextCreationStrategy>()> creator) {
  creators_.push_back({std::move(matcher), std::move(creator)});
}

std::unique_ptr<IContextCreationStrategy>
ContextStrategyFactory::createStrategy(const QString &identifier) const {
  for (const auto &[matcher, creator] : creators_) {
    if (matcher(identifier)) {
      return creator();
    }
  }
  return nullptr; // 或抛出异常
}

//===============InlineBrowse====================
  Result<std::shared_ptr<OPCUADataBlockContext>, RichError>
 InlineBrowseStrategy::create(const QString &identifier, const QString &dataBlockName,
       const ServiceRegistry &registry) const {

  auto context = std::make_unique<OPCUADataBlockContext>();
  context->key = OPCUADataBlockKey(identifier, dataBlockName);
  context->createTime = QDateTime::currentDateTime();
  context->lastAccessTime = context->createTime;

  // 创建独立的 MVC 组件
  context->view = std::make_shared<OPCUADataBlockView>();
  context->model = std::make_shared<OPCUADataBlockModel>();
  context->delegate = std::make_shared<OPCUADataDelegate>();
  context->controller = std::make_shared<OPCUADataBlockController>();
  context->view->setParent(&context->m_controllWidget);

  // 初始化Controller,View
  context->controller->initialize(context->view, context->model, identifier,
                                  registry);
  context->controller->buildConnection();

  if (!context->view || !context->model || !context->delegate) {
    return Result<std::shared_ptr<OPCUADataBlockContext>, RichError>(
        RichError{"view || model || delegate is nullptr"});
  }

  // 组装 MVC
  context->view->getModel(context->model.get());
  context->view->getDelegate(context->delegate.get());

  return Result<std::shared_ptr<OPCUADataBlockContext>, RichError>(
      std::move(context));
  ;
}


//===============FileContextStrategy====================
  Result<std::shared_ptr<OPCUADataBlockContext>, RichError>
 FileContextStrategy::create(const QString &identifier, const QString &dataBlockName,
       const ServiceRegistry &registry) const {

  auto context = std::make_unique<OPCUADataBlockContext>();
  context->key = OPCUADataBlockKey(identifier, dataBlockName);
  context->createTime = QDateTime::currentDateTime();
  context->lastAccessTime = context->createTime;

  // 创建独立的 MVC 组件
  context->view = std::make_shared<OPCUADataBlockView>();
  context->model = std::make_shared<OPCUADataBlockModel>();
  context->delegate = std::make_shared<OPCUADataDelegate>();
  context->controller = std::make_shared<OPCUADataBlockController>();
  context->view->setParent(&context->m_controllWidget);

  // 初始化Controller,View
  context->controller->initialize(context->view, context->model, identifier,
                                  registry);
  context->controller->buildConnection();

  if (!context->view || !context->model || !context->delegate) {
    return Result<std::shared_ptr<OPCUADataBlockContext>, RichError>(
        RichError{"view || model || delegate is nullptr"});
  }

  // 组装 MVC
  context->view->getModel(context->model.get());
  context->view->getDelegate(context->delegate.get());

  return Result<std::shared_ptr<OPCUADataBlockContext>, RichError>(
      std::move(context));
  ;
}