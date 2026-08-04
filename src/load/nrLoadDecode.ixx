export module nr.load:decode;

import :type;
import std;

export namespace nr::load
{
struct TextureDecodeOptions
{
    std::uint32_t workerCount = 0;
    std::uint32_t requestedChannels = 4;
};

[[nodiscard]] std::expected<void, LoadError> decodeSceneTextureImages(
    SceneAsset &scene, const TextureDecodeOptions &options = TextureDecodeOptions{});
} // namespace nr::load
