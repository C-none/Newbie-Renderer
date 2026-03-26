import std;
import nr.renderer;
import nr.renderPasses;
import nr.scene;

namespace
{
[[nodiscard]] bool require(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::println("[fail] {}", message);
        return false;
    }
    return true;
}

template <typename T>
concept HasRasterPassField = requires(T value) {
    value.rasterPass;
};

template <typename T>
concept HasComputePassField = requires(T value) {
    value.computePass;
};

template <typename T>
concept HasCopyPassField = requires(T value) {
    value.copyPass;
};

template <typename T>
concept HasSwapchainImageInputField = requires(T value) {
    value.swapchainImage;
};

template <typename T>
concept HasSourceColorInputField = requires(T value) {
    value.sourceColor;
};

template <typename T>
concept HasPlannedDrawCountField = requires(T value) {
    value.plannedDrawCount;
};

static_assert(!HasRasterPassField<nr::renderPasses::NormalViewNodeOutput>);
static_assert(!HasComputePassField<nr::renderPasses::PresentNodeOutput>);
static_assert(!HasCopyPassField<nr::renderPasses::PresentNodeOutput>);
static_assert(!HasSwapchainImageInputField<nr::renderPasses::PresentNodeInput>);
static_assert(!HasSourceColorInputField<nr::renderPasses::PresentNodeInput>);
static_assert(HasPlannedDrawCountField<nr::renderPasses::NormalViewNodeOutput>);

[[nodiscard]] bool checkNodeDescriptions()
{
    auto normalView = nr::renderPasses::NormalViewNode{};
    auto present = nr::renderPasses::PresentNode{};

    auto normalViewDescription = normalView.describe();
    if (!require(normalViewDescription.name == "NormalView",
                 "NormalView should expose stable node name."))
    {
        return false;
    }

    if (!require(normalViewDescription.outputPorts.size() == 2u,
                 "NormalView should publish color/depth output ports."))
    {
        return false;
    }

    auto presentDescription = present.describe();
    if (!require(presentDescription.name == "Present",
                 "Present should expose stable node name."))
    {
        return false;
    }

    if (!require(presentDescription.inputPorts.size() == 1u &&
                     presentDescription.inputPorts.front().name == "sourceColor",
                 "Present should consume sourceColor input port only."))
    {
        return false;
    }

    if (!require(presentDescription.outputPorts.size() == 1u &&
                     presentDescription.outputPorts.front().name == "swapchain",
                 "Present should publish swapchain output port."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkNormalViewSceneDrivenBuildPlanning()
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto graphNode = builder.addNode("Stage6.NormalView", nr::renderer::QueueDomain::Graphics);

    auto published = std::map<std::string, nr::renderer::GraphResourceHandle>{};
    auto buildContext = nr::renderer::NodeBuildContext{
        .graphBuilder = std::ref(builder),
        .nodeHandle = graphNode,
        .resolveInputPort = [](std::string_view) {
            return nr::renderer::GraphResourceHandle{};
        },
        .publishOutputPort = [&](std::string_view portName, nr::renderer::GraphResourceHandle handle) {
            published.insert_or_assign(std::string{portName}, handle);
        },
    };

    auto bridgeFrame = nr::scene::SceneBridgeFrame{};
    bridgeFrame.domain = nr::scene::ScenePacketDomain::rasterDraw;
    bridgeFrame.frameConstants.cameraWorld = glm::vec3{1.0f, 2.0f, 3.0f};
    bridgeFrame.rasterDraws = {
        nr::scene::SceneBridgeDrawPacket{
            .renderable = {},
            .submeshIndex = 0u,
            .world = glm::mat4{1.0f},
            .worldBounds = {},
            .sortKey = 10u,
            .meshBindless = 100u,
            .materialBindless = 200u,
        },
        nr::scene::SceneBridgeDrawPacket{
            .renderable = {},
            .submeshIndex = 1u,
            .world = glm::mat4{1.0f},
            .worldBounds = {},
            .sortKey = 20u,
            .meshBindless = 101u,
            .materialBindless = 201u,
        },
    };

    auto frameParameters = nr::renderer::NodeFrameParameters{
        .frameIndex = 0u,
        .swapchainImageIndex = 0u,
        .swapchainExtent = vk::Extent2D{1280u, 720u},
        .swapchainFormat = vk::Format::eB8G8R8A8Srgb,
        .sceneBridgeFrame = std::cref(bridgeFrame),
        .scenePackets = std::nullopt,
        .primaryCamera = std::nullopt,
    };

    auto normalView = nr::renderPasses::NormalViewNode{};
    normalView.build(buildContext, frameParameters);

    if (!require(normalView.output.plannedDrawCount == bridgeFrame.rasterDraws.size(),
                 "NormalView should plan one draw per SceneBridgeFrame raster draw packet."))
    {
        return false;
    }

    if (!require(normalView.output.normalColor.valid() && normalView.output.depth.valid(),
                 "NormalView should produce color/depth graph resources."))
    {
        return false;
    }

    if (!require(published.contains("color") && published.contains("depth"),
                 "NormalView should publish color/depth output ports."))
    {
        return false;
    }

    auto frame = builder.build();
    if (!require(frame.passes.size() == 1u, "NormalView build should emit one raster pass."))
    {
        return false;
    }

    auto const &pass = frame.passes.front();
    if (!require(pass.queue == nr::renderer::QueueDomain::Graphics,
                 "NormalView pass queue should stay in graphics domain."))
    {
        return false;
    }

    if (!require(pass.debugName == "NormalView.Raster",
                 "NormalView should keep canonical raster pass name."))
    {
        return false;
    }

    if (!require(!pass.prepare,
                 "NormalView should not depend on prepare callback path."))
    {
        return false;
    }

    if (!require(static_cast<bool>(pass.record),
                 "NormalView should register record callback for push-constant prep at record stage."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkPresentCopyFinalBuildPlanning()
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto graphNode = builder.addPresentNode("Stage6.Present");

    auto sourceColor = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "Stage6.SourceColor",
        .lifetime = nr::renderer::ResourceLifetime::GraphTransient,
        .extent = vk::Extent3D{640u, 360u, 1u},
        .format = vk::Format::eB8G8R8A8Srgb,
        .usageIntents = {
            nr::renderer::ImageUsageIntent::Sampled,
            nr::renderer::ImageUsageIntent::TransferSrc,
        },
        .initialLayout = nr::renderer::ImageLayoutIntent::ShaderReadOnly,
        .aspect = nr::renderer::ImageAspectIntent::Color,
    });

    auto published = std::map<std::string, nr::renderer::GraphResourceHandle>{};
    auto buildContext = nr::renderer::NodeBuildContext{
        .graphBuilder = std::ref(builder),
        .nodeHandle = graphNode,
        .resolveInputPort = [sourceColor](std::string_view portName) {
            return portName == "sourceColor" ? sourceColor : nr::renderer::GraphResourceHandle{};
        },
        .publishOutputPort = [&](std::string_view portName, nr::renderer::GraphResourceHandle handle) {
            published.insert_or_assign(std::string{portName}, handle);
        },
    };

    auto frameParameters = nr::renderer::NodeFrameParameters{
        .frameIndex = 0u,
        .swapchainImageIndex = 0u,
        .swapchainExtent = vk::Extent2D{640u, 360u},
        .swapchainFormat = vk::Format::eB8G8R8A8Srgb,
        .sceneBridgeFrame = std::nullopt,
        .scenePackets = std::nullopt,
        .primaryCamera = std::nullopt,
    };

    auto present = nr::renderPasses::PresentNode{};
    present.build(buildContext, frameParameters);

    if (!require(present.output.swapchainImage.valid(), "Present should publish a valid swapchain resource handle."))
    {
        return false;
    }

    if (!require(published.contains("swapchain"), "Present should publish swapchain output port."))
    {
        return false;
    }

    auto frame = builder.build();
    if (!require(frame.passes.size() == 1u, "Present build should emit one direct-copy pass."))
    {
        return false;
    }

    auto const &pass = frame.passes.front();
    if (!require(pass.debugName == "Present.CopyToSwapchain",
                 "Present should emit canonical copy pass name."))
    {
        return false;
    }

    if (!require(pass.queue == nr::renderer::QueueDomain::Compute,
                 "Present copy pass should stay in compute queue domain."))
    {
        return false;
    }

    if (!require(pass.resourceUses.size() == 3u,
                 "Present copy pass should declare source, transfer-dst, and present-source intents."))
    {
        return false;
    }

    auto sourceUseExists = std::ranges::any_of(pass.resourceUses, [sourceColor](const nr::renderer::PassResourceUseDesc &use) {
        return use.resource == sourceColor &&
               use.imageUsage == nr::renderer::ImageUsageIntent::TransferSrc &&
               use.imageAccess == nr::renderer::ImageAccessIntent::TransferRead;
    });

    if (!require(sourceUseExists, "Present copy pass should read sourceColor as transfer source."))
    {
        return false;
    }

    auto transferDstExists = std::ranges::any_of(pass.resourceUses, [&present](const nr::renderer::PassResourceUseDesc &use) {
        return use.resource == present.output.swapchainImage &&
               use.imageUsage == nr::renderer::ImageUsageIntent::TransferDst &&
               use.imageAccess == nr::renderer::ImageAccessIntent::TransferWrite;
    });

    if (!require(transferDstExists, "Present copy pass should write swapchain image as transfer destination."))
    {
        return false;
    }

    auto presentSrcExists = std::ranges::any_of(pass.resourceUses, [&present](const nr::renderer::PassResourceUseDesc &use) {
        return use.resource == present.output.swapchainImage &&
               use.imageUsage == nr::renderer::ImageUsageIntent::PresentSource &&
               use.imageAccess == nr::renderer::ImageAccessIntent::PresentRead;
    });

    if (!require(presentSrcExists, "Present copy pass should declare swapchain image present-source intent."))
    {
        return false;
    }

    auto fallbackInputExists = std::ranges::any_of(frame.resources, [](const nr::renderer::GraphResourceDesc &resource) {
        if (!std::holds_alternative<nr::renderer::GraphTransientImageDesc>(resource.desc))
        {
            return false;
        }
        auto const &desc = std::get<nr::renderer::GraphTransientImageDesc>(resource.desc);
        return desc.debugName == "Present.FallbackInput";
    });

    if (!require(!fallbackInputExists, "Present should not generate fallback sourceColor resources."))
    {
        return false;
    }

    auto swapchainImports = std::ranges::count_if(frame.resources, [](const nr::renderer::GraphResourceDesc &resource) {
        return std::holds_alternative<nr::renderer::GraphImportedSwapchainImageDesc>(resource.desc);
    });

    if (!require(swapchainImports == 1u, "Present should import exactly one swapchain image per frame build."))
    {
        return false;
    }

    return true;
}
} // namespace

int main()
{
    if (!checkNodeDescriptions())
    {
        std::println("[FAIL] stage6 builtin node description contract failed");
        return 1;
    }

    if (!checkNormalViewSceneDrivenBuildPlanning())
    {
        std::println("[FAIL] stage6 NormalView scene-driven planning contract failed");
        return 2;
    }

    if (!checkPresentCopyFinalBuildPlanning())
    {
        std::println("[FAIL] stage6 Present copy-final planning contract failed");
        return 3;
    }

    std::println("[OK] renderer stage6 builtin nodes contract tests passed");
    return 0;
}
