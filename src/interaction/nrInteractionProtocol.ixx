export module nr.interaction:protocol;

import dependency.network;
import nr.options;
import std;

export namespace nr::interaction
{
class OptionRpcProtocol
{
  public:
    explicit OptionRpcProtocol(nr::options::OptionSystem &optionSystem, std::size_t maximumResponseBytes = 256u * 1024u) noexcept;

    [[nodiscard]] dependency::network::TextMessageResult handleText(std::string_view payload, const dependency::network::MessageContext &context, std::string &responseSlot) const;

  private:
    std::reference_wrapper<nr::options::OptionSystem> optionSystem_;
    std::size_t maximumResponseBytes_;
};
} // namespace nr::interaction
