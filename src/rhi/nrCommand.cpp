module nr.rhi;
import :command;
import dependency.vulkan;
import nr.utils;
import std;

namespace nr::rhi
{
void CommandRecorder::beginPrimary(
        const vk::raii::CommandBuffer& commandBuffer,
        vk::CommandBufferUsageFlags flags)
{
        vk::CommandBufferBeginInfo beginInfo{flags};
        commandBuffer.begin(beginInfo);
    }

void CommandRecorder::beginSecondary(
        const vk::raii::CommandBuffer& commandBuffer,
        const vk::CommandBufferInheritanceInfo& inheritanceInfo,
        vk::CommandBufferUsageFlags flags)
{
        vk::CommandBufferBeginInfo beginInfo{flags, &inheritanceInfo};
        commandBuffer.begin(beginInfo);
    }

void CommandRecorder::end(const vk::raii::CommandBuffer& commandBuffer)
{
        commandBuffer.end();
    }

ScopedCommandBuffer::ScopedCommandBuffer(
        const vk::raii::CommandBuffer& commandBuffer,
        vk::CommandBufferUsageFlags flags) : commandBuffer_(commandBuffer)
{
        CommandRecorder::beginPrimary(commandBuffer, flags);
    }

ScopedCommandBuffer::ScopedCommandBuffer(
        const vk::raii::CommandBuffer& commandBuffer,
        const vk::CommandBufferInheritanceInfo& inheritanceInfo,
        vk::CommandBufferUsageFlags flags) : commandBuffer_(commandBuffer)
{
        CommandRecorder::beginSecondary(commandBuffer, inheritanceInfo, flags);
    }

ScopedCommandBuffer::~ScopedCommandBuffer()
{
        // Reference is always valid (bound at construction)
        commandBuffer_.end();
    }

[[nodiscard]] const vk::raii::CommandBuffer& ScopedCommandBuffer::get() const noexcept
{ return commandBuffer_; }

ScopedCommandBufferDebugLabel::ScopedCommandBufferDebugLabel(const vk::raii::CommandBuffer& commandBuffer, std::string_view label)
        : commandBuffer_(std::cref(commandBuffer))
        , label_(label)
{
        if constexpr (nr::isDebugMode) {
            if (label_.empty()) {
                return;
            }

            auto debugLabel = vk::DebugUtilsLabelEXT{};
            debugLabel.pLabelName = label_.c_str();

            commandBuffer_->get().beginDebugUtilsLabelEXT(debugLabel);
            active_ = true;
        }
    }

ScopedCommandBufferDebugLabel::~ScopedCommandBufferDebugLabel()
{
        close();
    }

void ScopedCommandBufferDebugLabel::close()
{
        if (!active_ || !commandBuffer_.has_value()) {
            return;
        }

        commandBuffer_->get().endDebugUtilsLabelEXT();
        active_ = false;
    }
} // namespace nr::rhi
