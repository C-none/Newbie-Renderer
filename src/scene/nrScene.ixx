module;
export module nr.scene:scene;

import dependency;
import nr.rhi;
import :type;

namespace nr::scene::detail
{
struct FlecsSceneModule
{
    explicit FlecsSceneModule(flecs::world &world)
    {
        world.module<FlecsSceneModule>("nr.scene");
    }
};
} // namespace nr::scene::detail

export namespace nr::scene
{
class Scene
{
  public:
    explicit Scene(SceneCreateInfo createInfo)
        : device_(createInfo.device)
    {
        world_.import<detail::FlecsSceneModule>();
    }

    explicit Scene(nr::rhi::Device &device)
        : Scene(SceneCreateInfo{device})
    {
    }

    Scene() = delete;
    Scene(const Scene &) = delete;
    Scene &operator=(const Scene &) = delete;
    Scene(Scene &&) = delete;
    Scene &operator=(Scene &&) = delete;
    ~Scene() = default;

    [[nodiscard]] nr::rhi::Device &device() noexcept
    {
        return device_;
    }

    [[nodiscard]] const nr::rhi::Device &device() const noexcept
    {
        return device_;
    }

    [[nodiscard]] flecs::world &world() noexcept
    {
        return world_;
    }

    [[nodiscard]] const flecs::world &world() const noexcept
    {
        return world_;
    }

  private:
    nr::rhi::Device &device_;
    flecs::world world_{};
};
} // namespace nr::scene
