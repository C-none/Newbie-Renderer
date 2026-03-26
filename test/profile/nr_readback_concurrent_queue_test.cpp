import std;
import dependency;
import nr.rhi;

namespace
{
using nr::rhi::QueueRole;

[[nodiscard]] nr::rhi::GpuQueue& queueForRole(nr::rhi::Device& device, QueueRole role)
{
    if (role == QueueRole::Graphics)
    {
        return device.queueManager.graphics();
    }
    if (role == QueueRole::Compute)
    {
        return device.queueManager.compute();
    }
    if (role == QueueRole::Transfer)
    {
        return device.queueManager.transfer();
    }

    throw std::runtime_error("nr_readback_concurrent_queue_test requires a valid QueueRole.");
}

[[nodiscard]] uint32_t queueFamilyForRole(const nr::rhi::Device& device, QueueRole role)
{
    if (role == QueueRole::Graphics)
    {
        return device.queueManager.graphics().queueFamilyIndex();
    }
    if (role == QueueRole::Compute)
    {
        return device.queueManager.compute().queueFamilyIndex();
    }
    if (role == QueueRole::Transfer)
    {
        return device.queueManager.transfer().queueFamilyIndex();
    }

    throw std::runtime_error("nr_readback_concurrent_queue_test requires a valid QueueRole.");
}

[[nodiscard]] std::string_view roleName(QueueRole role)
{
    if (role == QueueRole::Graphics)
    {
        return "graphics";
    }
    if (role == QueueRole::Compute)
    {
        return "compute";
    }
    if (role == QueueRole::Transfer)
    {
        return "transfer";
    }
    return "invalid";
}

[[nodiscard]] vk::PipelineStageFlags2 readbackPreCopyStageForRole(QueueRole role)
{
    if (role == QueueRole::Graphics)
    {
        return vk::PipelineStageFlagBits2::eTransfer;
    }
    if (role == QueueRole::Compute)
    {
        return vk::PipelineStageFlagBits2::eTransfer;
    }
    throw std::runtime_error("nr_readback_concurrent_queue_test readback supports only graphics/compute roles.");
}

[[nodiscard]] vk::PipelineStageFlags2 readbackPostCopyConsumeStageForRole(QueueRole role)
{
    if (role == QueueRole::Graphics)
    {
        return vk::PipelineStageFlagBits2::eFragmentShader;
    }
    if (role == QueueRole::Compute)
    {
        return vk::PipelineStageFlagBits2::eComputeShader;
    }
    throw std::runtime_error("nr_readback_concurrent_queue_test readback supports only graphics/compute roles.");
}

[[nodiscard]] vk::AccessFlags2 readbackSourceAccess()
{
    return vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
}

[[nodiscard]] nr::rhi::ops::ReadbackSyncPlan readbackSyncPlanForRole(QueueRole role)
{
    auto access = readbackSourceAccess();
    return nr::rhi::ops::ReadbackSyncPlan{
        .preCopy = nr::rhi::ops::ReadbackSyncScope{
            .stages = readbackPreCopyStageForRole(role),
            .access = access,
        },
        .postCopy = nr::rhi::ops::ReadbackSyncScope{
            .stages = readbackPostCopyConsumeStageForRole(role),
            .access = access,
        },
    };
}

[[nodiscard]] std::vector<std::byte> repeatedPatternBytes(vk::DeviceSize size, std::uint32_t pattern)
{
    auto bytes = std::vector<std::byte>(static_cast<size_t>(size));
    auto patternBytes = std::bit_cast<std::array<std::byte, sizeof(std::uint32_t)>>(pattern);
    auto patternValues = std::views::iota(size_t{0}, bytes.size()) |
                         std::views::transform([&](size_t index) {
                             return patternBytes[index % patternBytes.size()];
                         });
    std::ranges::copy(patternValues, bytes.begin());
    return bytes;
}

[[nodiscard]] std::vector<std::byte> makeRandomBytesForRole(std::size_t size, QueueRole role)
{
    auto rng = std::mt19937{0x20260321u + static_cast<std::uint32_t>(role) * 97u};
    auto distribution = std::uniform_int_distribution<int>{0, 255};

    auto bytes = std::vector<std::byte>(size);
    auto indices = std::views::iota(std::size_t{0}, bytes.size());
    std::ranges::for_each(indices, [&](std::size_t index) {
        bytes[index] = static_cast<std::byte>(distribution(rng));
    });
    return bytes;
}

[[nodiscard]] std::string byteListString(std::span<const std::byte> bytes)
{
    auto text = std::string{};
    auto indices = std::views::iota(std::size_t{0}, bytes.size());
    std::ranges::for_each(indices, [&](std::size_t index) {
        if (!text.empty())
        {
            text += ", ";
        }
        text += std::to_string(std::to_integer<unsigned int>(bytes[index]));
    });
    return text;
}

void printByteList(std::string_view label, std::span<const std::byte> bytes)
{
    std::println("[data] {}: [{}]", label, byteListString(bytes));
}

[[nodiscard]] bool verifyOwnershipImageUploadAndReadbackOnQueue(nr::rhi::Device& device, QueueRole role)
{
    constexpr auto kWidth = uint32_t{32};
    constexpr auto kHeight = uint32_t{1};
    constexpr auto kUploadSize = size_t{kWidth * kHeight};

    auto ownerQueueFamily = queueFamilyForRole(device, role);
    auto transferQueueFamily = device.queueManager.transfer().queueFamilyIndex();

    if (ownerQueueFamily == transferQueueFamily)
    {
        std::println(
            "[skip] {} image-upload ownership roundtrip skipped: owner queue family {} equals transfer queue family {}",
            roleName(role),
            ownerQueueFamily,
            transferQueueFamily);
        return true;
    }

    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.format = vk::Format::eR8Uint;
    imageInfo.extent = vk::Extent3D{kWidth, kHeight, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst |
                      vk::ImageUsageFlagBits::eTransferSrc |
                      vk::ImageUsageFlagBits::eSampled;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;

    auto image = device.resourceFactory.createImage(
        imageInfo,
        nr::rhi::MemoryUsage::GpuOnly,
        std::format("upload_ownership_image_{}", roleName(role)));

    auto ownerReleasedToTransfer = nr::rhi::sync::createTimelineSemaphore(device.device, 0u);
    constexpr auto kOwnerReleaseSignalValue = std::uint64_t{1};

    nr::rhi::CommandPool ownerReleasePool(
        device.device,
        ownerQueueFamily,
        vk::CommandPoolCreateFlagBits::eTransient);
    auto ownerReleaseBuffers = ownerReleasePool.allocatePrimary(1);
    auto& ownerReleaseCommandBuffer = ownerReleaseBuffers.front();

    nr::rhi::CommandRecorder::beginPrimary(ownerReleaseCommandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    {
        auto raw = *ownerReleaseCommandBuffer;

        nr::rhi::ops::transitionImage(raw, image, vk::ImageMemoryBarrier2{
            vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::AccessFlags2{},
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferWrite,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eGeneral,
            nr::rhi::ops::kIgnoredQueueFamilyIndex,
            nr::rhi::ops::kIgnoredQueueFamilyIndex,
            vk::Image{},
            {},
            nullptr,
        });

        nr::rhi::ops::BarrierBatch ownerReleaseBarrier{};
        ownerReleaseBarrier.add(nr::rhi::ops::makeImageOwnershipBarrier<nr::rhi::ops::OwnershipBarrierPhase::Release>(
            image,
            vk::ImageLayout::eGeneral,
            vk::ImageLayout::eGeneral,
            nr::rhi::ops::QueueOwnershipRequest{
                .srcQueueFamilyIndex = ownerQueueFamily,
                .dstQueueFamilyIndex = transferQueueFamily,
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferWrite,
            }));
        nr::rhi::ops::pipelineBarrier(raw, ownerReleaseBarrier);
    }
    nr::rhi::CommandRecorder::end(ownerReleaseCommandBuffer);

    nr::rhi::CommandBatch ownerReleaseSubmission{};
    ownerReleaseSubmission.addCommandBuffer(ownerReleaseCommandBuffer);
    ownerReleaseSubmission.addSignal(
        ownerReleasedToTransfer,
        kOwnerReleaseSignalValue,
        0,
        vk::PipelineStageFlagBits2::eBottomOfPipe);
    queueForRole(device, role).submit(ownerReleaseSubmission);

    auto uploadPixels = makeRandomBytesForRole(kUploadSize, role);
    printByteList(std::format("{} image upload", roleName(role)), uploadPixels);

    nr::rhi::ops::BufferUploadOwnershipPlan ownershipPlan{};
    ownershipPlan.acquireToTransfer = nr::rhi::ops::QueueOwnershipTransfer{
        .release = nr::rhi::ops::QueueOwnershipRequest{
            .srcQueueFamilyIndex = ownerQueueFamily,
            .dstQueueFamilyIndex = transferQueueFamily,
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferWrite,
        },
        .acquire = nr::rhi::ops::QueueOwnershipRequest{
            .srcQueueFamilyIndex = ownerQueueFamily,
            .dstQueueFamilyIndex = transferQueueFamily,
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferWrite,
        },
        .waitSemaphore = *ownerReleasedToTransfer,
        .waitValue = kOwnerReleaseSignalValue,
    };
    ownershipPlan.releaseToDestination = nr::rhi::ops::QueueOwnershipTransfer{
        .release = nr::rhi::ops::QueueOwnershipRequest{
            .srcQueueFamilyIndex = transferQueueFamily,
            .dstQueueFamilyIndex = ownerQueueFamily,
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferWrite,
        },
        .acquire = nr::rhi::ops::QueueOwnershipRequest{
            .srcQueueFamilyIndex = transferQueueFamily,
            .dstQueueFamilyIndex = ownerQueueFamily,
            .stages = readbackPostCopyConsumeStageForRole(role),
            .access = vk::AccessFlagBits2::eShaderRead,
        },
    };

    auto& uploadReadback = device.uploadReadback();
    auto uploadTicket = uploadReadback.uploadImage(
        uploadPixels,
        image,
        vk::ImageLayout::eGeneral,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        ownershipPlan);

    if (!uploadTicket.valid())
    {
        std::println("[error] {} image upload ticket invalid in ownership roundtrip.", roleName(role));
        return false;
    }

    nr::rhi::CommandPool ownerAcquirePool(
        device.device,
        ownerQueueFamily,
        vk::CommandPoolCreateFlagBits::eTransient);
    auto ownerAcquireBuffers = ownerAcquirePool.allocatePrimary(1);
    auto& ownerAcquireCommandBuffer = ownerAcquireBuffers.front();

    nr::rhi::CommandRecorder::beginPrimary(ownerAcquireCommandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    {
        uploadReadback.recordImageAcquireBarrier(*ownerAcquireCommandBuffer, uploadTicket);
    }
    nr::rhi::CommandRecorder::end(ownerAcquireCommandBuffer);

    nr::rhi::CommandBatch ownerAcquireSubmission{};
    ownerAcquireSubmission.addWait(
        uploadReadback.uploadTimelineSemaphore(),
        uploadTicket.ownership->acquire.stages,
        uploadTicket.signalValue);
    ownerAcquireSubmission.addCommandBuffer(ownerAcquireCommandBuffer);
    queueForRole(device, role).submit(ownerAcquireSubmission);

    auto readbackSync = nr::rhi::ops::ReadbackSyncPlan{
        .preCopy = nr::rhi::ops::ReadbackSyncScope{
            .stages = readbackPostCopyConsumeStageForRole(role),
            .access = vk::AccessFlagBits2::eShaderRead,
        },
        .postCopy = nr::rhi::ops::ReadbackSyncScope{
            .stages = readbackPostCopyConsumeStageForRole(role),
            .access = vk::AccessFlagBits2::eShaderRead,
        },
    };

    auto readbackTicket = uploadReadback.readbackImage(
        image,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        role,
        readbackSync);
    auto readbackPixels = uploadReadback.readbackBytes(readbackTicket);
    printByteList(std::format("{} image readback", roleName(role)), readbackPixels);

    if (!std::ranges::equal(uploadPixels, readbackPixels))
    {
        auto mismatch = std::ranges::mismatch(uploadPixels, readbackPixels);
        auto mismatchIndex = static_cast<size_t>(std::distance(uploadPixels.begin(), mismatch.in1));
        std::println(
            "[error] {} image upload/readback mismatch at byte {}: upload={}, readback={}",
            roleName(role),
            mismatchIndex,
            std::to_integer<unsigned int>(*mismatch.in1),
            std::to_integer<unsigned int>(*mismatch.in2));
        return false;
    }

    std::println("[ok] {} ownership image upload/reacquire/readback verified ({} bytes)", roleName(role), readbackPixels.size());
    return true;
}

[[nodiscard]] bool verifyOwnershipUploadAndReadbackOnQueue(nr::rhi::Device& device, QueueRole role)
{
    constexpr auto kUploadSize = vk::DeviceSize{64};

    auto ownerQueueFamily = queueFamilyForRole(device, role);
    auto transferQueueFamily = device.queueManager.transfer().queueFamilyIndex();

    if (ownerQueueFamily == transferQueueFamily)
    {
        std::println(
            "[skip] {} upload-ownership roundtrip skipped: owner queue family {} equals transfer queue family {}",
            roleName(role),
            ownerQueueFamily,
            transferQueueFamily);
        return true;
    }

    vk::BufferCreateInfo dstInfo{};
    dstInfo.size = kUploadSize;
    dstInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc;
    dstInfo.sharingMode = vk::SharingMode::eExclusive;
    auto dstBuffer = device.resourceFactory.createBuffer(
        dstInfo,
        nr::rhi::MemoryUsage::GpuOnly,
        std::format("upload_ownership_dst_{}", roleName(role)));

    auto ownerReleasedToTransfer = nr::rhi::sync::createTimelineSemaphore(device.device, 0u);
    constexpr auto kOwnerReleaseSignalValue = std::uint64_t{1};

    nr::rhi::CommandPool ownerReleasePool(
        device.device,
        ownerQueueFamily,
        vk::CommandPoolCreateFlagBits::eTransient);
    auto ownerReleaseBuffers = ownerReleasePool.allocatePrimary(1);
    auto& ownerReleaseCommandBuffer = ownerReleaseBuffers.front();

    nr::rhi::CommandRecorder::beginPrimary(ownerReleaseCommandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    {
        auto raw = *ownerReleaseCommandBuffer;
        raw.fillBuffer(dstBuffer.handle(), 0, kUploadSize, 0u);

        nr::rhi::ops::BarrierBatch ownerReleaseBarrier{};
        ownerReleaseBarrier.add(nr::rhi::ops::makeBufferOwnershipBarrier<nr::rhi::ops::OwnershipBarrierPhase::Release>(
            dstBuffer,
            nr::rhi::ops::QueueOwnershipRequest{
                .srcQueueFamilyIndex = ownerQueueFamily,
                .dstQueueFamilyIndex = transferQueueFamily,
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferWrite,
            },
            0,
            kUploadSize));
        nr::rhi::ops::pipelineBarrier(raw, ownerReleaseBarrier);
    }
    nr::rhi::CommandRecorder::end(ownerReleaseCommandBuffer);

    nr::rhi::CommandBatch ownerReleaseSubmission{};
    ownerReleaseSubmission.addCommandBuffer(ownerReleaseCommandBuffer);
    ownerReleaseSubmission.addSignal(
        ownerReleasedToTransfer,
        kOwnerReleaseSignalValue,
        0,
        vk::PipelineStageFlagBits2::eBottomOfPipe);
    queueForRole(device, role).submit(ownerReleaseSubmission);

    auto uploadBytes = makeRandomBytesForRole(static_cast<std::size_t>(kUploadSize), role);
    printByteList(std::format("{} upload", roleName(role)), uploadBytes);

    auto ownerAccess = readbackSourceAccess();

    nr::rhi::ops::BufferUploadOwnershipPlan ownershipPlan{};
    ownershipPlan.acquireToTransfer = nr::rhi::ops::QueueOwnershipTransfer{
        .release = nr::rhi::ops::QueueOwnershipRequest{
            .srcQueueFamilyIndex = ownerQueueFamily,
            .dstQueueFamilyIndex = transferQueueFamily,
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferWrite,
        },
        .acquire = nr::rhi::ops::QueueOwnershipRequest{
            .srcQueueFamilyIndex = ownerQueueFamily,
            .dstQueueFamilyIndex = transferQueueFamily,
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferWrite,
        },
        .waitSemaphore = *ownerReleasedToTransfer,
        .waitValue = kOwnerReleaseSignalValue,
    };
    ownershipPlan.releaseToDestination = nr::rhi::ops::QueueOwnershipTransfer{
        .release = nr::rhi::ops::QueueOwnershipRequest{
            .srcQueueFamilyIndex = transferQueueFamily,
            .dstQueueFamilyIndex = ownerQueueFamily,
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferWrite,
        },
        .acquire = nr::rhi::ops::QueueOwnershipRequest{
            .srcQueueFamilyIndex = transferQueueFamily,
            .dstQueueFamilyIndex = ownerQueueFamily,
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = ownerAccess,
        },
    };

    auto& uploadReadback = device.uploadReadback();
    auto uploadTicket = uploadReadback.uploadBuffer(uploadBytes, dstBuffer, 0, ownershipPlan);

    if (!uploadTicket.valid())
    {
        std::println("[error] {} upload ticket invalid in ownership roundtrip.", roleName(role));
        return false;
    }

    nr::rhi::CommandPool ownerAcquirePool(
        device.device,
        ownerQueueFamily,
        vk::CommandPoolCreateFlagBits::eTransient);
    auto ownerAcquireBuffers = ownerAcquirePool.allocatePrimary(1);
    auto& ownerAcquireCommandBuffer = ownerAcquireBuffers.front();

    nr::rhi::CommandRecorder::beginPrimary(ownerAcquireCommandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    {
        uploadReadback.recordAcquireBarrier(*ownerAcquireCommandBuffer, uploadTicket);
    }
    nr::rhi::CommandRecorder::end(ownerAcquireCommandBuffer);

    nr::rhi::CommandBatch ownerAcquireSubmission{};
    ownerAcquireSubmission.addWait(
        uploadReadback.uploadTimelineSemaphore(),
        uploadTicket.ownership->acquire.stages,
        uploadTicket.signalValue);
    ownerAcquireSubmission.addCommandBuffer(ownerAcquireCommandBuffer);
    queueForRole(device, role).submit(ownerAcquireSubmission);

    auto readbackTicket = uploadReadback.readbackBuffer(
        dstBuffer,
        0,
        kUploadSize,
        role,
        nr::rhi::ops::ReadbackSyncPlan{
            .preCopy = nr::rhi::ops::ReadbackSyncScope{
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = ownerAccess,
            },
            .postCopy = nr::rhi::ops::ReadbackSyncScope{
                .stages = readbackPostCopyConsumeStageForRole(role),
                .access = ownerAccess,
            },
        });

    auto readbackBytes = uploadReadback.readbackBytes(readbackTicket);
    printByteList(std::format("{} readback", roleName(role)), readbackBytes);

    if (!std::ranges::equal(uploadBytes, readbackBytes))
    {
        auto mismatch = std::ranges::mismatch(uploadBytes, readbackBytes);
        auto mismatchIndex = static_cast<std::size_t>(std::distance(uploadBytes.begin(), mismatch.in1));
        std::println(
            "[error] {} upload/readback mismatch at byte {}: upload={}, readback={}",
            roleName(role),
            mismatchIndex,
            std::to_integer<unsigned int>(*mismatch.in1),
            std::to_integer<unsigned int>(*mismatch.in2));
        return false;
    }

    std::println("[ok] {} ownership release/upload/reacquire/readback verified ({} bytes)", roleName(role), readbackBytes.size());
    return true;
}

[[nodiscard]] bool verifyBufferReadbackOnQueue(nr::rhi::Device& device, QueueRole role)
{
    constexpr auto kReadbackSize = vk::DeviceSize{4096};
    constexpr auto kPattern = uint32_t{0x6B7A5C3D};

    vk::BufferCreateInfo srcInfo{};
    srcInfo.size = kReadbackSize;
    srcInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc;
    srcInfo.sharingMode = vk::SharingMode::eExclusive;
    auto srcBuffer = device.resourceFactory.createBuffer(
        srcInfo,
        nr::rhi::MemoryUsage::GpuOnly,
        std::format("readback_buffer_src_{}", roleName(role)));

    nr::rhi::CommandPool producerPool(
        device.device,
        queueFamilyForRole(device, role),
        vk::CommandPoolCreateFlagBits::eTransient);
    auto producerBuffers = producerPool.allocatePrimary(1);
    auto& producerCommandBuffer = producerBuffers.front();

    nr::rhi::CommandRecorder::beginPrimary(producerCommandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    {
        auto raw = *producerCommandBuffer;
        raw.fillBuffer(srcBuffer.handle(), 0, kReadbackSize, kPattern);

        auto sourceStage = readbackPreCopyStageForRole(role);
        auto sourceAccess = readbackSourceAccess();

        nr::rhi::ops::BarrierBatch toSourceState{};
        toSourceState.add(nr::rhi::ops::makeBufferBarrier(srcBuffer, vk::BufferMemoryBarrier2{
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferWrite,
            sourceStage,
            sourceAccess,
            nr::rhi::ops::kIgnoredQueueFamilyIndex,
            nr::rhi::ops::kIgnoredQueueFamilyIndex,
            vk::Buffer{},
            0,
            kReadbackSize,
            nullptr,
        }));
        nr::rhi::ops::pipelineBarrier(raw, toSourceState);
    }
    nr::rhi::CommandRecorder::end(producerCommandBuffer);
    queueForRole(device, role).submit(producerCommandBuffer);

    auto syncPlan = readbackSyncPlanForRole(role);

    auto& uploadReadback = device.uploadReadback();
    auto ticket = uploadReadback.readbackBuffer(
        srcBuffer,
        0,
        kReadbackSize,
        role,
        syncPlan);
    auto bytes = uploadReadback.readbackBytes(ticket);

    if (bytes.size() != static_cast<size_t>(kReadbackSize))
    {
        std::println(
            "[error] {} buffer readback size mismatch: expected={}, observed={}",
            roleName(role),
            static_cast<size_t>(kReadbackSize),
            bytes.size());
        return false;
    }

    auto expected = repeatedPatternBytes(kReadbackSize, kPattern);

    if (!std::ranges::equal(expected, bytes))
    {
        auto mismatch = std::ranges::mismatch(expected, bytes);
        auto mismatchIndex = static_cast<size_t>(std::distance(expected.begin(), mismatch.in1));
        std::println(
            "[error] {} buffer readback mismatch at byte {}: expected={}, observed={}",
            roleName(role),
            mismatchIndex,
            std::to_integer<unsigned int>(*mismatch.in1),
            std::to_integer<unsigned int>(*mismatch.in2));
        return false;
    }

    std::println("[ok] {} queue buffer readback verified ({} bytes)", roleName(role), bytes.size());
    return true;
}

[[nodiscard]] bool verifyImageReadbackOnQueue(nr::rhi::Device& device, QueueRole role)
{
    constexpr auto kWidth = uint32_t{256};
    constexpr auto kHeight = uint32_t{1};
    constexpr auto kPixelBytes = size_t{1};
    constexpr auto kUploadSize = vk::DeviceSize{static_cast<vk::DeviceSize>(kWidth) * static_cast<vk::DeviceSize>(kHeight) * static_cast<vk::DeviceSize>(kPixelBytes)};

    constexpr auto kClearValue = std::uint32_t{7u};
    std::vector<std::byte> uploadPixels(static_cast<size_t>(kUploadSize), std::byte{static_cast<unsigned char>(kClearValue)});

    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.format = vk::Format::eR8Uint;
    imageInfo.extent = vk::Extent3D{kWidth, kHeight, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst |
                      vk::ImageUsageFlagBits::eTransferSrc |
                      vk::ImageUsageFlagBits::eSampled;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;

    auto image = device.resourceFactory.createImage(
        imageInfo,
        nr::rhi::MemoryUsage::GpuOnly,
        std::format("readback_image_src_{}", roleName(role)));

    nr::rhi::CommandPool producerPool(
        device.device,
        queueFamilyForRole(device, role),
        vk::CommandPoolCreateFlagBits::eTransient);
    auto producerBuffers = producerPool.allocatePrimary(1);
    auto& producerCommandBuffer = producerBuffers.front();

    nr::rhi::CommandRecorder::beginPrimary(producerCommandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    {
        auto raw = *producerCommandBuffer;

        nr::rhi::ops::transitionImage(raw, image, vk::ImageMemoryBarrier2{
            vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::AccessFlags2{},
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferWrite,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal,
            nr::rhi::ops::kIgnoredQueueFamilyIndex,
            nr::rhi::ops::kIgnoredQueueFamilyIndex,
            vk::Image{},
            {},
            nullptr,
        });

        raw.clearColorImage(
            image.handle(),
            vk::ImageLayout::eTransferDstOptimal,
            vk::ClearColorValue{std::array<std::uint32_t, 4>{kClearValue, 0u, 0u, 0u}},
            {nr::rhi::ops::fullSubresourceRange(image)});
    }
    nr::rhi::CommandRecorder::end(producerCommandBuffer);
    queueForRole(device, role).submit(producerCommandBuffer);

    auto syncPlan = readbackSyncPlanForRole(role);

    auto& uploadReadback = device.uploadReadback();
    auto readbackTicket = uploadReadback.readbackImage(
        image,
        vk::ImageLayout::eTransferDstOptimal,
        role,
        syncPlan);
    auto readbackPixels = uploadReadback.readbackBytes(readbackTicket);

    if (!std::ranges::equal(uploadPixels, readbackPixels))
    {
        auto mismatch = std::ranges::mismatch(uploadPixels, readbackPixels);
        auto mismatchIndex = static_cast<size_t>(std::distance(uploadPixels.begin(), mismatch.in1));
        std::println(
            "[error] {} image readback mismatch at byte {}: expected={}, observed={}",
            roleName(role),
            mismatchIndex,
            std::to_integer<unsigned int>(*mismatch.in1),
            std::to_integer<unsigned int>(*mismatch.in2));
        return false;
    }

    // Run a second readback with the same source layout to verify restore barrier correctness.
    auto secondTicket = uploadReadback.readbackImage(
        image,
        vk::ImageLayout::eTransferDstOptimal,
        role,
        syncPlan);
    auto secondPixels = uploadReadback.readbackBytes(secondTicket);

    if (!std::ranges::equal(uploadPixels, secondPixels))
    {
        std::println("[error] {} image second readback mismatch after layout restore.", roleName(role));
        return false;
    }

    std::println("[ok] {} queue image readback verified ({} bytes)", roleName(role), readbackPixels.size());
    return true;
}

[[nodiscard]] bool verifyChunkedOffsetUploadAndReadbackOnQueue(nr::rhi::Device& device, QueueRole role)
{
    constexpr auto kForcedUploadRingSize = vk::DeviceSize{256};
    constexpr auto kForcedReadbackRingSize = vk::DeviceSize{4096};
    constexpr auto kBufferSize = vk::DeviceSize{2048};
    constexpr auto kUploadSize = vk::DeviceSize{777};
    constexpr auto kDstOffset = vk::DeviceSize{193};
    constexpr auto kInitialPattern = std::uint32_t{0x3A5C7E19u};

    auto ownerQueueFamily = queueFamilyForRole(device, role);
    auto transferQueueFamily = queueFamilyForRole(device, QueueRole::Transfer);

    if (ownerQueueFamily == transferQueueFamily)
    {
        std::println(
            "[skip] {} chunked offset upload skipped: owner queue family {} equals transfer queue family {}",
            roleName(role),
            ownerQueueFamily,
            transferQueueFamily);
        return true;
    }

    nr::rhi::ops::UploadReadbackContext chunkedContext(
        device.device,
        device.resourceFactory,
        device.queueManager,
        kForcedUploadRingSize,
        kForcedReadbackRingSize);

    vk::BufferCreateInfo dstInfo{};
    dstInfo.size = kBufferSize;
    dstInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc;
    dstInfo.sharingMode = vk::SharingMode::eExclusive;
    auto dstBuffer = device.resourceFactory.createBuffer(
        dstInfo,
        nr::rhi::MemoryUsage::GpuOnly,
        std::format("chunked_offset_dst_{}", roleName(role)));

    auto ownerReleasedToTransfer = nr::rhi::sync::createTimelineSemaphore(device.device, 0u);
    constexpr auto kOwnerReleaseSignalValue = std::uint64_t{1};

    nr::rhi::CommandPool ownerReleasePool(
        device.device,
        ownerQueueFamily,
        vk::CommandPoolCreateFlagBits::eTransient);
    auto ownerReleaseBuffers = ownerReleasePool.allocatePrimary(1);
    auto& ownerReleaseCommandBuffer = ownerReleaseBuffers.front();

    nr::rhi::CommandRecorder::beginPrimary(ownerReleaseCommandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    {
        auto raw = *ownerReleaseCommandBuffer;
        raw.fillBuffer(dstBuffer.handle(), 0, kBufferSize, kInitialPattern);

        nr::rhi::ops::BarrierBatch ownerReleaseBarrier{};
        ownerReleaseBarrier.add(nr::rhi::ops::makeBufferOwnershipBarrier<nr::rhi::ops::OwnershipBarrierPhase::Release>(
            dstBuffer,
            nr::rhi::ops::QueueOwnershipRequest{
                .srcQueueFamilyIndex = ownerQueueFamily,
                .dstQueueFamilyIndex = transferQueueFamily,
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferWrite,
            },
            kDstOffset,
            kUploadSize));
        nr::rhi::ops::pipelineBarrier(raw, ownerReleaseBarrier);
    }
    nr::rhi::CommandRecorder::end(ownerReleaseCommandBuffer);

    nr::rhi::CommandBatch ownerReleaseSubmission{};
    ownerReleaseSubmission.addCommandBuffer(ownerReleaseCommandBuffer);
    ownerReleaseSubmission.addSignal(
        ownerReleasedToTransfer,
        kOwnerReleaseSignalValue,
        0,
        vk::PipelineStageFlagBits2::eBottomOfPipe);
    queueForRole(device, role).submit(ownerReleaseSubmission);

    auto uploadBytes = makeRandomBytesForRole(static_cast<size_t>(kUploadSize), role);
    auto expectedBytes = repeatedPatternBytes(kBufferSize, kInitialPattern);
    auto expectedWriteBegin = expectedBytes.begin() + static_cast<std::ptrdiff_t>(kDstOffset);
    std::ranges::copy(uploadBytes, expectedWriteBegin);

    nr::rhi::ops::BufferUploadOwnershipPlan ownershipPlan{};
    ownershipPlan.acquireToTransfer = nr::rhi::ops::QueueOwnershipTransfer{
        .release = nr::rhi::ops::QueueOwnershipRequest{
            .srcQueueFamilyIndex = ownerQueueFamily,
            .dstQueueFamilyIndex = transferQueueFamily,
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferWrite,
        },
        .acquire = nr::rhi::ops::QueueOwnershipRequest{
            .srcQueueFamilyIndex = ownerQueueFamily,
            .dstQueueFamilyIndex = transferQueueFamily,
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferWrite,
        },
        .waitSemaphore = *ownerReleasedToTransfer,
        .waitValue = kOwnerReleaseSignalValue,
    };
    ownershipPlan.releaseToDestination = nr::rhi::ops::QueueOwnershipTransfer{
        .release = nr::rhi::ops::QueueOwnershipRequest{
            .srcQueueFamilyIndex = transferQueueFamily,
            .dstQueueFamilyIndex = ownerQueueFamily,
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferWrite,
        },
        .acquire = nr::rhi::ops::QueueOwnershipRequest{
            .srcQueueFamilyIndex = transferQueueFamily,
            .dstQueueFamilyIndex = ownerQueueFamily,
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = readbackSourceAccess(),
        },
    };

    auto uploadTicket = chunkedContext.uploadBuffer(uploadBytes, dstBuffer, kDstOffset, ownershipPlan);
    if (!uploadTicket.valid())
    {
        std::println("[error] {} chunked offset upload ticket invalid.", roleName(role));
        return false;
    }

    nr::rhi::CommandPool ownerAcquirePool(
        device.device,
        ownerQueueFamily,
        vk::CommandPoolCreateFlagBits::eTransient);
    auto ownerAcquireBuffers = ownerAcquirePool.allocatePrimary(1);
    auto& ownerAcquireCommandBuffer = ownerAcquireBuffers.front();

    nr::rhi::CommandRecorder::beginPrimary(ownerAcquireCommandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    {
        chunkedContext.recordAcquireBarrier(*ownerAcquireCommandBuffer, uploadTicket);
    }
    nr::rhi::CommandRecorder::end(ownerAcquireCommandBuffer);

    nr::rhi::CommandBatch ownerAcquireSubmission{};
    ownerAcquireSubmission.addWait(
        chunkedContext.uploadTimelineSemaphore(),
        uploadTicket.ownership->acquire.stages,
        uploadTicket.signalValue);
    ownerAcquireSubmission.addCommandBuffer(ownerAcquireCommandBuffer);
    queueForRole(device, role).submit(ownerAcquireSubmission);

    auto readbackTicket = chunkedContext.readbackBuffer(
        dstBuffer,
        0,
        kBufferSize,
        role,
        nr::rhi::ops::ReadbackSyncPlan{
            .preCopy = nr::rhi::ops::ReadbackSyncScope{
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = readbackSourceAccess(),
            },
            .postCopy = nr::rhi::ops::ReadbackSyncScope{
                .stages = readbackPostCopyConsumeStageForRole(role),
                .access = readbackSourceAccess(),
            },
        });
    auto readbackBytes = chunkedContext.readbackBytes(readbackTicket);
    chunkedContext.waitUploadComplete(uploadTicket.signalValue);
    chunkedContext.waitReadbackComplete(readbackTicket.signalValue);

    if (!std::ranges::equal(expectedBytes, readbackBytes))
    {
        auto mismatch = std::ranges::mismatch(expectedBytes, readbackBytes);
        auto mismatchIndex = static_cast<size_t>(std::distance(expectedBytes.begin(), mismatch.in1));
        std::println(
            "[error] {} chunked offset upload mismatch at byte {}: expected={}, observed={}",
            roleName(role),
            mismatchIndex,
            std::to_integer<unsigned int>(*mismatch.in1),
            std::to_integer<unsigned int>(*mismatch.in2));
        return false;
    }

    std::println(
        "[ok] {} chunked offset upload/readback verified (offset={}, bytes={}, ring={})",
        roleName(role),
        kDstOffset,
        kUploadSize,
        kForcedUploadRingSize);
    return true;
}

[[nodiscard]] bool verifyExternalOwnershipTransferForCrossQueueReadback(nr::rhi::Device& device)
{
    constexpr auto kBufferSize = vk::DeviceSize{1024};
    constexpr auto kPattern = std::uint32_t{0xD0C0B0A0u};

    auto graphicsQueueFamily = queueFamilyForRole(device, QueueRole::Graphics);
    auto computeQueueFamily = queueFamilyForRole(device, QueueRole::Compute);
    if (graphicsQueueFamily == computeQueueFamily)
    {
        std::println(
            "[skip] cross-queue ownership readback skipped: graphics queue family {} equals compute queue family {}",
            graphicsQueueFamily,
            computeQueueFamily);
        return true;
    }

    vk::BufferCreateInfo srcInfo{};
    srcInfo.size = kBufferSize;
    srcInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc;
    srcInfo.sharingMode = vk::SharingMode::eExclusive;
    auto srcBuffer = device.resourceFactory.createBuffer(
        srcInfo,
        nr::rhi::MemoryUsage::GpuOnly,
        "cross_queue_readback_src");

    auto graphicsReleasedToCompute = nr::rhi::sync::createTimelineSemaphore(device.device, 0u);
    constexpr auto kReleaseSignalValue = std::uint64_t{1};

    nr::rhi::CommandPool graphicsPool(
        device.device,
        graphicsQueueFamily,
        vk::CommandPoolCreateFlagBits::eTransient);
    auto graphicsBuffers = graphicsPool.allocatePrimary(1);
    auto& graphicsCommandBuffer = graphicsBuffers.front();

    nr::rhi::CommandRecorder::beginPrimary(graphicsCommandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    {
        auto raw = *graphicsCommandBuffer;
        raw.fillBuffer(srcBuffer.handle(), 0, kBufferSize, kPattern);

        nr::rhi::ops::BarrierBatch releaseBarrier{};
        releaseBarrier.add(nr::rhi::ops::makeBufferOwnershipBarrier<nr::rhi::ops::OwnershipBarrierPhase::Release>(
            srcBuffer,
            nr::rhi::ops::QueueOwnershipRequest{
                .srcQueueFamilyIndex = graphicsQueueFamily,
                .dstQueueFamilyIndex = computeQueueFamily,
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferWrite,
            },
            0,
            kBufferSize));
        nr::rhi::ops::pipelineBarrier(raw, releaseBarrier);
    }
    nr::rhi::CommandRecorder::end(graphicsCommandBuffer);

    nr::rhi::CommandBatch graphicsSubmission{};
    graphicsSubmission.addCommandBuffer(graphicsCommandBuffer);
    graphicsSubmission.addSignal(
        graphicsReleasedToCompute,
        kReleaseSignalValue,
        0,
        vk::PipelineStageFlagBits2::eBottomOfPipe);
    queueForRole(device, QueueRole::Graphics).submit(graphicsSubmission);

    nr::rhi::CommandPool computeAcquirePool(
        device.device,
        computeQueueFamily,
        vk::CommandPoolCreateFlagBits::eTransient);
    auto computeAcquireBuffers = computeAcquirePool.allocatePrimary(1);
    auto& computeAcquireCommandBuffer = computeAcquireBuffers.front();

    nr::rhi::CommandRecorder::beginPrimary(computeAcquireCommandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    {
        nr::rhi::ops::BarrierBatch acquireBarrier{};
        acquireBarrier.add(nr::rhi::ops::makeBufferOwnershipBarrier<nr::rhi::ops::OwnershipBarrierPhase::Acquire>(
            srcBuffer,
            nr::rhi::ops::QueueOwnershipRequest{
                .srcQueueFamilyIndex = graphicsQueueFamily,
                .dstQueueFamilyIndex = computeQueueFamily,
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = readbackSourceAccess(),
            },
            0,
            kBufferSize));
        nr::rhi::ops::pipelineBarrier(*computeAcquireCommandBuffer, acquireBarrier);
    }
    nr::rhi::CommandRecorder::end(computeAcquireCommandBuffer);

    nr::rhi::CommandBatch computeAcquireSubmission{};
    computeAcquireSubmission.addWait(
        graphicsReleasedToCompute,
        vk::PipelineStageFlagBits2::eTransfer,
        kReleaseSignalValue);
    computeAcquireSubmission.addCommandBuffer(computeAcquireCommandBuffer);
    queueForRole(device, QueueRole::Compute).submit(computeAcquireSubmission);
    queueForRole(device, QueueRole::Compute).waitIdle();

    auto& uploadReadback = device.uploadReadback();
    auto readbackTicket = uploadReadback.readbackBuffer(
        srcBuffer,
        0,
        kBufferSize,
        QueueRole::Compute,
        nr::rhi::ops::ReadbackSyncPlan{
            .preCopy = nr::rhi::ops::ReadbackSyncScope{
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = readbackSourceAccess(),
            },
            .postCopy = nr::rhi::ops::ReadbackSyncScope{
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = readbackSourceAccess(),
            },
        });
    auto readbackBytes = uploadReadback.readbackBytes(readbackTicket);

    auto expectedBytes = repeatedPatternBytes(kBufferSize, kPattern);
    if (!std::ranges::equal(expectedBytes, readbackBytes))
    {
        auto mismatch = std::ranges::mismatch(expectedBytes, readbackBytes);
        auto mismatchIndex = static_cast<size_t>(std::distance(expectedBytes.begin(), mismatch.in1));
        std::println(
            "[error] cross-queue ownership readback mismatch at byte {}: expected={}, observed={}",
            mismatchIndex,
            std::to_integer<unsigned int>(*mismatch.in1),
            std::to_integer<unsigned int>(*mismatch.in2));
        return false;
    }

    std::println("[ok] cross-queue ownership transfer + compute readback verified ({} bytes)", readbackBytes.size());
    return true;
}

[[nodiscard]] bool verifyInterleavedReadbackTicketsAcrossQueues(nr::rhi::Device& device)
{
    struct ReadbackScenario
    {
        QueueRole role = QueueRole::Graphics;
        vk::DeviceSize size = 0;
        std::uint32_t pattern = 0;
        nr::rhi::Buffer buffer{};
        nr::rhi::ops::ReadbackTicket ticket{};
    };

    auto scenarios = std::array{
        ReadbackScenario{QueueRole::Graphics, vk::DeviceSize{1024}, std::uint32_t{0x11223344u}},
        ReadbackScenario{QueueRole::Compute, vk::DeviceSize{1536}, std::uint32_t{0x55667788u}},
    };

    auto producerPools = std::array{
        nr::rhi::CommandPool(
            device.device,
            queueFamilyForRole(device, scenarios[0].role),
            vk::CommandPoolCreateFlagBits::eTransient),
        nr::rhi::CommandPool(
            device.device,
            queueFamilyForRole(device, scenarios[1].role),
            vk::CommandPoolCreateFlagBits::eTransient),
    };
    auto producerBuffers = std::array{
        producerPools[0].allocatePrimary(1),
        producerPools[1].allocatePrimary(1),
    };

    auto& uploadReadback = device.uploadReadback();
    auto anyFailure = false;

    auto indices = std::views::iota(size_t{0}, scenarios.size());
    std::ranges::for_each(indices, [&](size_t index) {
        auto& scenario = scenarios[index];
        vk::BufferCreateInfo srcInfo{};
        srcInfo.size = scenario.size;
        srcInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc;
        srcInfo.sharingMode = vk::SharingMode::eExclusive;
        scenario.buffer = device.resourceFactory.createBuffer(
            srcInfo,
            nr::rhi::MemoryUsage::GpuOnly,
            std::format("interleaved_readback_{}", roleName(scenario.role)));

        auto& producerCommandBuffer = producerBuffers[index].front();

        nr::rhi::CommandRecorder::beginPrimary(producerCommandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        {
            auto raw = *producerCommandBuffer;
            raw.fillBuffer(scenario.buffer.handle(), 0, scenario.size, scenario.pattern);

            nr::rhi::ops::BarrierBatch toSourceState{};
            toSourceState.add(nr::rhi::ops::makeBufferBarrier(scenario.buffer, vk::BufferMemoryBarrier2{
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferWrite,
                readbackPreCopyStageForRole(scenario.role),
                readbackSourceAccess(),
                nr::rhi::ops::kIgnoredQueueFamilyIndex,
                nr::rhi::ops::kIgnoredQueueFamilyIndex,
                vk::Buffer{},
                0,
                scenario.size,
                nullptr,
            }));
            nr::rhi::ops::pipelineBarrier(raw, toSourceState);
        }
        nr::rhi::CommandRecorder::end(producerCommandBuffer);
        queueForRole(device, scenario.role).submit(producerCommandBuffer);

        scenario.ticket = uploadReadback.readbackBuffer(
            scenario.buffer,
            0,
            scenario.size,
            scenario.role,
            readbackSyncPlanForRole(scenario.role));
    });

    auto reverseIndices = std::views::iota(size_t{0}, scenarios.size()) | std::views::reverse;
    std::ranges::for_each(reverseIndices, [&](size_t index) {
        auto& scenario = scenarios[index];
        auto readbackBytes = uploadReadback.readbackBytes(scenario.ticket);
        auto expectedBytes = repeatedPatternBytes(scenario.size, scenario.pattern);

        if (!std::ranges::equal(expectedBytes, readbackBytes))
        {
            auto mismatch = std::ranges::mismatch(expectedBytes, readbackBytes);
            auto mismatchIndex = static_cast<size_t>(std::distance(expectedBytes.begin(), mismatch.in1));
            std::println(
                "[error] interleaved {} readback mismatch at byte {}: expected={}, observed={}",
                roleName(scenario.role),
                mismatchIndex,
                std::to_integer<unsigned int>(*mismatch.in1),
                std::to_integer<unsigned int>(*mismatch.in2));
            anyFailure = true;
            return;
        }

        std::println(
            "[ok] interleaved {} readback verified ({} bytes, ticket value={})",
            roleName(scenario.role),
            readbackBytes.size(),
            scenario.ticket.signalValue);
    });

    uploadReadback.waitReadbackComplete();
    device.queueManager.waitAllIdle();

    return !anyFailure;
}

[[nodiscard]] bool runReadbackConcurrentQueueTest()
{
    nr::rhi::Device device;
    device.initialize("nr_readback_concurrent_queue_test", "nrrhi_test");

    auto allOk = true;
    allOk = verifyOwnershipUploadAndReadbackOnQueue(device, QueueRole::Graphics) && allOk;
    allOk = verifyOwnershipImageUploadAndReadbackOnQueue(device, QueueRole::Graphics) && allOk;
    allOk = verifyBufferReadbackOnQueue(device, QueueRole::Graphics) && allOk;
    allOk = verifyImageReadbackOnQueue(device, QueueRole::Graphics) && allOk;

    allOk = verifyOwnershipUploadAndReadbackOnQueue(device, QueueRole::Compute) && allOk;
    allOk = verifyOwnershipImageUploadAndReadbackOnQueue(device, QueueRole::Compute) && allOk;
    allOk = verifyBufferReadbackOnQueue(device, QueueRole::Compute) && allOk;
    allOk = verifyImageReadbackOnQueue(device, QueueRole::Compute) && allOk;

    allOk = verifyChunkedOffsetUploadAndReadbackOnQueue(device, QueueRole::Graphics) && allOk;
    allOk = verifyChunkedOffsetUploadAndReadbackOnQueue(device, QueueRole::Compute) && allOk;

    allOk = verifyExternalOwnershipTransferForCrossQueueReadback(device) && allOk;
    allOk = verifyInterleavedReadbackTicketsAcrossQueues(device) && allOk;

    device.waitIdle();
    return allOk;
}
} // namespace

int main()
{
    try
    {
        if (!runReadbackConcurrentQueueTest())
        {
            std::println("[FAIL] concurrent-queue readback test failed");
            return 1;
        }

        std::println("[OK] concurrent-queue readback test passed");
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::println("[error] exception: {}", exception.what());
        return 2;
    }
}
