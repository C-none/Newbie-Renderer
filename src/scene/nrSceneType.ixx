module;
export module nr.scene:type;

import nr.rhi;

export namespace nr::scene
{
struct SceneCreateInfo
{
    nr::rhi::Device &device;
};
} // namespace nr::scene
