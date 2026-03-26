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

static_assert(std::is_invocable_r_v<
              nr::renderer::GraphPassHandle,
              decltype(&nr::renderer::RenderGraphBuilder::addPass),
              nr::renderer::RenderGraphBuilder&,
              std::string_view,
              nr::renderer::GraphNodeHandle,
              std::span<const nr::renderer::PassResourceUseDesc>,
              nr::renderer::PassRecordCallback,
              nr::renderer::PassPrepareCallback,
              bool>);

[[nodiscard]] bool checkAddPassRegistersIntentNameAndLambda()
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode("Phase2.AddPassNode", nr::renderer::QueueDomain::Compute);

    auto transientBuffer = builder.addResource(nr::renderer::GraphTransientBufferDesc{
        .debugName = "Phase2.AddPass.Buffer",
        .lifetime = nr::renderer::ResourceLifetime::GraphTransient,
        .size = 256,
        .usageIntents = {
            nr::renderer::BufferUsageIntent::StorageReadWrite,
        },
    });

    auto executeInvoked = false;
    auto intents = std::array{
        nr::renderer::PassResourceUseDesc{
            .resource = transientBuffer,
            .bufferUsage = nr::renderer::BufferUsageIntent::StorageReadWrite,
            .bufferAccess = nr::renderer::BufferAccessIntent::ShaderStorageWrite,
            .imageUsage = std::nullopt,
            .imageAccess = std::nullopt,
            .imageLayout = std::nullopt,
            .imageAspect = std::nullopt,
            .ownershipDomain = nr::renderer::ResourceOwnershipDomain::Undefined,
            .readOnly = false,
        },
    };

    auto pass = builder.addPass(
        "Phase2.CustomProfilePass",
        node,
        std::span<const nr::renderer::PassResourceUseDesc>{intents.data(), intents.size()},
        [&](const nr::renderer::PassRecordContext&) {
            executeInvoked = true;
        });

    if (!require(pass.valid(), "addPass should return a valid graph pass handle."))
    {
        return false;
    }

    auto frame = builder.build();
    if (!require(frame.passes.size() == 1u, "addPass should register exactly one pass."))
    {
        return false;
    }

    auto const& recordedPass = frame.passes.front();
    if (!require(recordedPass.debugName == "Phase2.CustomProfilePass",
                 "addPass should preserve custom pass debug/profile name."))
    {
        return false;
    }

    if (!require(recordedPass.resourceUses.size() == intents.size(),
                 "addPass should register the full intent list as resource uses."))
    {
        return false;
    }

    if (!require(!recordedPass.prepare,
                 "Canonical addPass path should not depend on prepare callbacks."))
    {
        return false;
    }

    if (!require(static_cast<bool>(recordedPass.record),
                 "addPass should register execute lambda as record callback."))
    {
        return false;
    }

    recordedPass.record(nr::renderer::PassRecordContext{});
    if (!require(executeInvoked, "Record callback from addPass should be invocable."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkCompilerKeepsNamesAndCompilesIntentBarriers()
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode("Phase2.CompilerNode", nr::renderer::QueueDomain::Compute);

    auto transientImage = builder.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "Phase2.Barrier.Image",
        .lifetime = nr::renderer::ResourceLifetime::GraphTransient,
        .extent = vk::Extent3D{64u, 64u, 1u},
        .format = vk::Format::eR8G8B8A8Unorm,
        .usageIntents = {
            nr::renderer::ImageUsageIntent::StorageWrite,
            nr::renderer::ImageUsageIntent::Sampled,
        },
        .initialLayout = nr::renderer::ImageLayoutIntent::General,
        .aspect = nr::renderer::ImageAspectIntent::Color,
    });

    auto writeIntents = std::array{
        nr::renderer::PassResourceUseDesc{
            .resource = transientImage,
            .bufferUsage = std::nullopt,
            .bufferAccess = std::nullopt,
            .imageUsage = nr::renderer::ImageUsageIntent::StorageWrite,
            .imageAccess = nr::renderer::ImageAccessIntent::StorageWrite,
            .imageLayout = nr::renderer::ImageLayoutIntent::General,
            .imageAspect = nr::renderer::ImageAspectIntent::Color,
            .ownershipDomain = nr::renderer::ResourceOwnershipDomain::Undefined,
            .readOnly = false,
        },
    };

    auto readIntents = std::array{
        nr::renderer::PassResourceUseDesc{
            .resource = transientImage,
            .bufferUsage = std::nullopt,
            .bufferAccess = std::nullopt,
            .imageUsage = nr::renderer::ImageUsageIntent::Sampled,
            .imageAccess = nr::renderer::ImageAccessIntent::SampledRead,
            .imageLayout = nr::renderer::ImageLayoutIntent::ShaderReadOnly,
            .imageAspect = nr::renderer::ImageAspectIntent::Color,
            .ownershipDomain = nr::renderer::ResourceOwnershipDomain::Undefined,
            .readOnly = true,
        },
    };

    [[maybe_unused]] auto writePassHandle = builder.addPass(
        "Phase2.Intent.Write",
        node,
        std::span<const nr::renderer::PassResourceUseDesc>{writeIntents.data(), writeIntents.size()},
        [](const nr::renderer::PassRecordContext&) {
        });

    [[maybe_unused]] auto readPassHandle = builder.addPass(
        "Phase2.Intent.Read",
        node,
        std::span<const nr::renderer::PassResourceUseDesc>{readIntents.data(), readIntents.size()},
        [](const nr::renderer::PassRecordContext&) {
        });

    auto compiler = nr::renderer::RenderGraphCompiler{};
    auto compiled = compiler.compile(builder.build());

    if (!require(compiled.submitBatches.size() == 1u,
                 "Single-queue addPass sequence should compile into one submit batch."))
    {
        return false;
    }

    auto const& passes = compiled.submitBatches.front().passes;
    if (!require(passes.size() == 2u, "Compiler should keep both addPass-registered passes."))
    {
        return false;
    }

    if (!require(passes[0].debugName == "Phase2.Intent.Write" &&
                     passes[1].debugName == "Phase2.Intent.Read",
                 "Compiled pass list should preserve addPass custom names in execution order."))
    {
        return false;
    }

    auto hasBarrierFromIntent = std::ranges::any_of(
        passes[1].preBarriers,
        [transientImage](const nr::renderer::ResourceStateTransition& transition) {
            return transition.resource == transientImage &&
                   transition.strength == nr::renderer::DependencyStrength::BarrierRequired;
        });

    if (!require(hasBarrierFromIntent,
                 "Compiler should derive barrier transitions from addPass intent list."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkBuiltinNodesUseCanonicalBuildPath()
{
    auto builder = nr::renderer::RenderGraphBuilder{};

    auto normalNodeHandle = builder.addNode("NormalView", nr::renderer::QueueDomain::Graphics);

    auto presentNodeHandle = builder.addPresentNode("Present");

    auto publishedOutputs = std::map<std::string, nr::renderer::GraphResourceHandle>{};

    auto bridgeFrame = nr::scene::SceneBridgeFrame{};
    bridgeFrame.domain = nr::scene::ScenePacketDomain::rasterDraw;
    bridgeFrame.rasterDraws = {
        nr::scene::SceneBridgeDrawPacket{
            .renderable = {},
            .submeshIndex = 0u,
            .world = glm::mat4{1.0f},
            .worldBounds = {},
            .sortKey = 1u,
            .meshBindless = 10u,
            .materialBindless = 20u,
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

    auto normalBuildContext = nr::renderer::NodeBuildContext{
        .graphBuilder = std::ref(builder),
        .nodeHandle = normalNodeHandle,
        .resolveInputPort = [](std::string_view) {
            return nr::renderer::GraphResourceHandle{};
        },
        .publishOutputPort = [&](std::string_view portName, nr::renderer::GraphResourceHandle handle) {
            publishedOutputs.insert_or_assign(std::format("NormalView::{}", portName), handle);
        },
    };

    auto normalNode = nr::renderPasses::NormalViewNode{};
    normalNode.build(normalBuildContext, frameParameters);

    auto submitBoundary = builder.addSubmitNode("Phase2.GraphicsToCompute", nr::renderer::SubmitBoundaryKind::Explicit);
    if (!require(submitBoundary.valid(), "Submit boundary handle should be valid in sample graph path."))
    {
        return false;
    }

    auto presentBuildContext = nr::renderer::NodeBuildContext{
        .graphBuilder = std::ref(builder),
        .nodeHandle = presentNodeHandle,
        .resolveInputPort = [&](std::string_view portName) {
            if (portName != "sourceColor")
            {
                return nr::renderer::GraphResourceHandle{};
            }

            auto it = publishedOutputs.find("NormalView::color");
            return it == publishedOutputs.end() ? nr::renderer::GraphResourceHandle{} : it->second;
        },
        .publishOutputPort = [&](std::string_view portName, nr::renderer::GraphResourceHandle handle) {
            publishedOutputs.insert_or_assign(std::format("Present::{}", portName), handle);
        },
    };

    auto presentNode = nr::renderPasses::PresentNode{};
    presentNode.build(presentBuildContext, frameParameters);

    auto frame = builder.build();

    if (!require(frame.passes.size() == 2u,
                 "NormalView (1 pass) + Present (1 copy pass, Phase 1) should register two passes in migrated canonical path."))
    {
        return false;
    }

    auto allPrepareEmpty = std::ranges::all_of(frame.passes, [](const nr::renderer::PassExecutionDesc& pass) {
        return !pass.prepare;
    });
    if (!require(allPrepareEmpty,
                 "Built-in nodes should not depend on prepare callbacks in Phase 2 path."))
    {
        return false;
    }

    auto hasNormalPass = std::ranges::any_of(frame.passes, [](const nr::renderer::PassExecutionDesc& pass) {
        return pass.debugName == "NormalView.Raster";
    });
    auto hasPresentCopy = std::ranges::any_of(frame.passes, [](const nr::renderer::PassExecutionDesc& pass) {
        return pass.debugName == "Present.CopyToSwapchain";
    });

    if (!require(hasNormalPass && hasPresentCopy,
                 "Built-in nodes should register canonical pass names through addPass path (Phase 1: PresentNode uses direct copy)."))
    {
        return false;
    }

    auto compiler = nr::renderer::RenderGraphCompiler{};
    auto compiled = compiler.compile(frame);
    if (!require(compiled.submitBatches.size() == 2u,
                 "Sample NormalView->submit boundary->Present path should compile into two submit batches."))
    {
        return false;
    }

    return true;
}
} // namespace

int main()
{
    if (!checkAddPassRegistersIntentNameAndLambda())
    {
        std::println("[FAIL] phase2 addPass contract failed");
        return 1;
    }

    if (!checkCompilerKeepsNamesAndCompilesIntentBarriers())
    {
        std::println("[FAIL] phase2 compiler intent/name contract failed");
        return 2;
    }

    if (!checkBuiltinNodesUseCanonicalBuildPath())
    {
        std::println("[FAIL] phase2 built-in node migration contract failed");
        return 3;
    }

    std::println("[OK] renderer phase2 contract tests passed");
    return 0;
}
