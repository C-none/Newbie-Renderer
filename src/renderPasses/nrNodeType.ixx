export module nr.renderPasses:nodeType;

import nr.renderer;

export namespace nr::renderPasses
{
using NodeConfig = nr::renderer::NodeConfig;
using NodeFrameParameters = nr::renderer::NodeFrameParameters;
using NodeInitContext = nr::renderer::NodeInitContext;
using NodeShutdownContext = nr::renderer::NodeShutdownContext;
using NodeBuildContext = nr::renderer::NodeBuildContext;
using NodeUiBuildContext = nr::renderer::NodeUiBuildContext;
using NodeUiWriter = nr::renderer::NodeUiWriter;
using NodeUiSection = nr::renderer::NodeUiSection;
using Node = nr::renderer::NodeRuntime;
} // namespace nr::renderPasses
