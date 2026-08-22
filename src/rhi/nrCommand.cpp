module nr.rhi;
import :command;
import dependency.vulkan;
import nr.utils;
import std;

namespace nr::rhi
{
ScopedCommandBufferDebugLabel::ScopedCommandBufferDebugLabel(const vk::raii::CommandBuffer &commandBuffer,
                                                             std::string_view label)
    : commandBuffer_(std::cref(commandBuffer)), label_(label)
{
    if constexpr (nr::gpuDebugNamesEnabled)
    {
        if (label_.empty())
        {
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
    if (!active_ || !commandBuffer_.has_value())
    {
        return;
    }

    commandBuffer_->get().endDebugUtilsLabelEXT();
    active_ = false;
}
} // namespace nr::rhi
