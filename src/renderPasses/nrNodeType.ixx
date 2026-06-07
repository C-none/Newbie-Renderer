export module nr.renderPasses:nodeType;

import nr.renderer;

export namespace nr::renderPasses
{
using NodePort = nr::renderer::NodePort;
using NodeConfig = nr::renderer::NodeConfig;
using NodeDescription = nr::renderer::NodeDescription;
using NodeFrameParameters = nr::renderer::NodeFrameParameters;
using NodeInitContext = nr::renderer::NodeInitContext;
using NodeShutdownContext = nr::renderer::NodeShutdownContext;
using NodeBuildContext = nr::renderer::NodeBuildContext;
using Node = nr::renderer::NodeRuntime;
} // namespace nr::renderPasses