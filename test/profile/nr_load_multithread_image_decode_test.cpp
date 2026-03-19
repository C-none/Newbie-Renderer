import std;
import nr.load;

namespace
{
[[nodiscard]] std::filesystem::path projectRoot()
{
    return std::filesystem::path{NR_PROJECT_ROOT_DIR};
}

struct DecodedImageFingerprint
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t channels = 0;
    std::size_t byteCount = 0;
    std::uint64_t hash = 0;
    std::string backend{};
};

[[nodiscard]] std::vector<std::uint32_t> collectReferencedTextureIndices(const nr::load::SceneAsset &scene)
{
    auto referenced = std::unordered_set<std::uint32_t>{};
    referenced.reserve(scene.materials.size() * 2u);

    std::ranges::for_each(scene.materials, [&](const nr::load::MaterialAsset &material) {
        std::ranges::for_each(material.textures, [&](const nr::load::MaterialTextureBinding &binding) {
            if (binding.textureIndex < scene.textures.size())
            {
                referenced.emplace(binding.textureIndex);
            }
        });
    });

    auto ordered = std::vector<std::uint32_t>{referenced.begin(), referenced.end()};
    std::ranges::sort(ordered);
    return ordered;
}

[[nodiscard]] std::uint64_t fnv1a64(std::span<const std::uint8_t> bytes)
{
    constexpr std::uint64_t offsetBasis = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;

    auto hash = offsetBasis;
    std::ranges::for_each(bytes, [&](std::uint8_t byteValue) {
        hash ^= static_cast<std::uint64_t>(byteValue);
        hash *= prime;
    });

    return hash;
}

[[nodiscard]] std::optional<DecodedImageFingerprint> makeFingerprint(const nr::load::TextureAsset &texture)
{
    if (!texture.decodedImage.has_value())
    {
        return std::nullopt;
    }

    auto const &image = *texture.decodedImage;
    return DecodedImageFingerprint{
        .width = image.width,
        .height = image.height,
        .channels = image.channels,
        .byteCount = image.pixels.size(),
        .hash = fnv1a64(image.pixels),
        .backend = texture.decodeBackend,
    };
}

void clearDecodedTextures(nr::load::SceneAsset &scene)
{
    std::ranges::for_each(scene.textures, [](nr::load::TextureAsset &texture) {
        texture.decodedImage.reset();
        texture.decodeBackend.clear();
    });
}

[[nodiscard]] std::expected<std::unordered_map<std::uint32_t, DecodedImageFingerprint>, std::string>
decodeAndSnapshot(nr::load::SceneAsset &scene,
                  std::span<const std::uint32_t> referencedTextureIndices,
                  std::uint32_t workerCount)
{
    clearDecodedTextures(scene);

    nr::load::TextureDecodeOptions options{};
    options.workerCount = workerCount;
    options.requestedChannels = 4;
    options.preferTurboJpeg = true;

    auto decodeResult = nr::load::decodeSceneTextureImages(scene, options);
    if (!decodeResult.has_value())
    {
        auto const &error = decodeResult.error();
        return std::unexpected(std::format(
            "decode failed: backend='{}', code={}, path='{}', message='{}'",
            error.backend,
            static_cast<unsigned>(error.code),
            error.sourcePath.generic_string(),
            error.message));
    }

    auto snapshot = std::unordered_map<std::uint32_t, DecodedImageFingerprint>{};
    snapshot.reserve(referencedTextureIndices.size());

    auto missingKeys = std::vector<std::string>{};
    missingKeys.reserve(referencedTextureIndices.size());

    std::ranges::for_each(referencedTextureIndices, [&](std::uint32_t textureIndex) {
        auto fingerprint = makeFingerprint(scene.textures[textureIndex]);
        if (!fingerprint.has_value())
        {
            missingKeys.push_back(scene.textures[textureIndex].key);
            return;
        }

        snapshot.emplace(textureIndex, std::move(*fingerprint));
    });

    if (!missingKeys.empty())
    {
        auto message = std::format("{} referenced texture(s) are still missing decoded image data.", missingKeys.size());
        return std::unexpected(std::move(message));
    }

    return snapshot;
}

[[nodiscard]] bool compareSnapshots(std::span<const std::uint32_t> referencedTextureIndices,
                                    const std::unordered_map<std::uint32_t, DecodedImageFingerprint> &singleWorkerSnapshot,
                                    const std::unordered_map<std::uint32_t, DecodedImageFingerprint> &multiWorkerSnapshot)
{
    auto allMatched = true;

    std::ranges::for_each(referencedTextureIndices, [&](std::uint32_t textureIndex) {
        auto singleIt = singleWorkerSnapshot.find(textureIndex);
        auto multiIt = multiWorkerSnapshot.find(textureIndex);
        if (singleIt == singleWorkerSnapshot.end() || multiIt == multiWorkerSnapshot.end())
        {
            std::println("[error] missing snapshot entry for texture index {}.", textureIndex);
            allMatched = false;
            return;
        }

        auto const &single = singleIt->second;
        auto const &multi = multiIt->second;
        auto matches = single.width == multi.width &&
                       single.height == multi.height &&
                       single.channels == multi.channels &&
                       single.byteCount == multi.byteCount &&
                       single.hash == multi.hash &&
                       single.backend == multi.backend;

        if (!matches)
        {
            std::println(
                "[error] texture {} mismatch: single=(w={}, h={}, c={}, bytes={}, hash={}, backend='{}'), multi=(w={}, h={}, c={}, bytes={}, hash={}, backend='{}')",
                textureIndex,
                single.width,
                single.height,
                single.channels,
                single.byteCount,
                single.hash,
                single.backend,
                multi.width,
                multi.height,
                multi.channels,
                multi.byteCount,
                multi.hash,
                multi.backend);
            allMatched = false;
        }
    });

    return allMatched;
}

[[nodiscard]] bool runMultiThreadDecodeConsistencyCase()
{
    auto const sourcePath = projectRoot() /
                            std::filesystem::path{"assets/glTF-Sample-Assets/Models/FlightHelmet/glTF/FlightHelmet.gltf"};

    nr::load::SceneLoadRequest request{};
    request.sourcePath = sourcePath;

    auto importResult = nr::load::loadScene(request);
    if (!importResult.has_value())
    {
        auto const &error = importResult.error();
        std::println(
            "[error] import failed: backend='{}', code={}, path='{}', message='{}'",
            error.backend,
            static_cast<unsigned>(error.code),
            error.sourcePath.generic_string(),
            error.message);
        return false;
    }

    auto scene = std::move(importResult.value());
    auto referencedTextureIndices = collectReferencedTextureIndices(scene);

    if (referencedTextureIndices.size() < 2)
    {
        std::println(
            "[error] expected at least 2 referenced textures for multi-thread decode validation, got {}.",
            referencedTextureIndices.size());
        return false;
    }

    auto singleWorkerResult = decodeAndSnapshot(scene, referencedTextureIndices, 1);
    if (!singleWorkerResult.has_value())
    {
        std::println("[error] single-worker decode failed: {}", singleWorkerResult.error());
        return false;
    }

    auto hardwareWorkers = std::thread::hardware_concurrency();
    auto desiredWorkers = std::max<std::uint32_t>(2u, hardwareWorkers == 0 ? 2u : hardwareWorkers);
    auto multiWorkerCount = static_cast<std::uint32_t>(
        std::min<std::size_t>(desiredWorkers, referencedTextureIndices.size()));

    auto multiWorkerResult = decodeAndSnapshot(scene, referencedTextureIndices, multiWorkerCount);
    if (!multiWorkerResult.has_value())
    {
        std::println("[error] multi-worker decode failed: {}", multiWorkerResult.error());
        return false;
    }

    auto snapshotsMatch = compareSnapshots(
        referencedTextureIndices,
        singleWorkerResult.value(),
        multiWorkerResult.value());
    if (!snapshotsMatch)
    {
        return false;
    }

    auto decodeBackends = std::unordered_set<std::string>{};
    decodeBackends.reserve(multiWorkerResult->size());

    std::ranges::for_each(*multiWorkerResult, [&](const auto &entry) {
        decodeBackends.emplace(entry.second.backend);
    });

    auto backendPreview = std::vector<std::string>{decodeBackends.begin(), decodeBackends.end()};
    std::ranges::sort(backendPreview);

    auto backendSummary = std::string{"none"};
    if (!backendPreview.empty())
    {
        backendSummary = backendPreview.front();
        auto remaining = backendPreview | std::views::drop(std::size_t{1});
        std::ranges::for_each(remaining, [&](const std::string &backend) {
            backendSummary.append(", ");
            backendSummary.append(backend);
        });
    }

    std::println(
        "[ok] FlightHelmet decode consistency passed: referencedTextures={}, singleWorker=1, multiWorker={}, backends=[{}]",
        referencedTextureIndices.size(),
        multiWorkerCount,
        backendSummary);
    return true;
}

} // namespace

int main()
{
    if (!runMultiThreadDecodeConsistencyCase())
    {
        std::println("[FAIL] nr_load_multithread_image_decode_test failed.");
        return 1;
    }

    std::println("[OK] nr_load_multithread_image_decode_test passed.");
    return 0;
}
