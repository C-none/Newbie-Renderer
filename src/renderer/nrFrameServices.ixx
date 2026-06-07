export module nr.renderer:frameServices;

import std;

export namespace nr::renderer
{
class FrameServices
{
  public:
    template <typename T>
    void set(std::reference_wrapper<T> service)
    {
        using ServiceType = std::remove_cvref_t<T>;
        services_.insert_or_assign(std::type_index(typeid(ServiceType)), std::ref(service.get()));
    }

    template <typename T>
    [[nodiscard]] std::optional<std::reference_wrapper<T>> tryGet() noexcept
    {
        using ServiceType = std::remove_cvref_t<T>;
        auto it = services_.find(std::type_index(typeid(ServiceType)));
        if (it == services_.end())
        {
            return std::nullopt;
        }

        auto* service = std::any_cast<std::reference_wrapper<ServiceType>>(&it->second);
        if (service == nullptr)
        {
            return std::nullopt;
        }

        return std::ref(service->get());
    }

    template <typename T>
    [[nodiscard]] std::optional<std::reference_wrapper<const T>> tryGet() const noexcept
    {
        using ServiceType = std::remove_cvref_t<T>;
        auto it = services_.find(std::type_index(typeid(ServiceType)));
        if (it == services_.end())
        {
            return std::nullopt;
        }

        auto* service = std::any_cast<std::reference_wrapper<ServiceType>>(&it->second);
        if (service == nullptr)
        {
            return std::nullopt;
        }

        return std::cref(service->get());
    }

    void clear() noexcept
    {
        services_.clear();
    }

  private:
    std::map<std::type_index, std::any> services_{};
};
} // namespace nr::renderer
