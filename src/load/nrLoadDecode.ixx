export module nr.load:decode;
import dependency;

import :type;
import :backend;
import std;

export namespace nr::load
{
struct TextureDecodeOptions
{
    std::uint32_t workerCount = 0;
    std::uint32_t requestedChannels = 4;
};

[[nodiscard]] inline std::expected<void, LoadError> decodeSceneTextureImages(SceneAsset &scene,
                                                                              const TextureDecodeOptions &options = TextureDecodeOptions{});

} // namespace nr::load

namespace nr::load::detail
{
class DecodeThreadPool
{
  public:
    explicit DecodeThreadPool(std::uint32_t workerCount)
    {
        auto normalizedWorkers = std::max(workerCount, 1u);
        workers.reserve(normalizedWorkers);

        auto workerSlots = std::views::iota(std::uint32_t{0}, normalizedWorkers);
        std::ranges::for_each(workerSlots, [this](std::uint32_t) {
            workers.emplace_back([this](std::stop_token stopToken) {
                workerLoop(stopToken);
            });
        });
    }

    DecodeThreadPool(const DecodeThreadPool &) = delete;
    DecodeThreadPool &operator=(const DecodeThreadPool &) = delete;

    ~DecodeThreadPool()
    {
        {
            std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
    }

    template <typename Fn>
    requires std::invocable<Fn &>
    [[nodiscard]] auto submit(Fn &&fn) -> std::future<std::invoke_result_t<Fn &>>
    {
        using Result = std::invoke_result_t<Fn &>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Fn>(fn));
        auto future = task->get_future();

        {
            std::scoped_lock lock(mutex_);
            if (stopping_)
            {
                throw std::runtime_error("DecodeThreadPool is stopping; cannot accept new tasks.");
            }

            tasks_.emplace([task]() {
                (*task)();
            });
        }

        wake_.notify_one();
        return future;
    }

  private:
    void workerLoop(const std::stop_token &stopToken)
    {
        while (true)
        {
            auto task = std::function<void()>{};
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [&]() {
                    return stopping_ || stopToken.stop_requested() || !tasks_.empty();
                });

                if ((stopping_ || stopToken.stop_requested()) && tasks_.empty())
                {
                    return;
                }

                task = std::move(tasks_.front());
                tasks_.pop();
            }

            task();
        }
    }

  private:
    std::vector<std::jthread> workers{};
    std::mutex mutex_{};
    std::condition_variable wake_{};
    std::queue<std::function<void()>> tasks_{};
    bool stopping_ = false;
};

struct TextureDecodeTaskResult
{
    std::uint32_t textureIndex = invalidIndex;
    std::optional<Image> image{};
    std::string decoder{};
    std::string error{};
};

struct DecodedPayload
{
    Image image{};
    std::string decoder{};
};

[[nodiscard]] inline std::string lowercase(std::string_view value)
{
    std::string normalized{value};
    std::ranges::transform(normalized, normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return normalized;
}

[[nodiscard]] inline bool isJpegExtension(const std::filesystem::path &path)
{
    auto extension = lowercase(path.extension().string());
    return extension == ".jpg" || extension == ".jpeg" || extension == ".jfif";
}

[[nodiscard]] inline bool isJpegHint(std::string_view hint)
{
    auto normalizedHint = lowercase(hint);
    return normalizedHint == "jpg" || normalizedHint == "jpeg" || normalizedHint == "jfif";
}

[[nodiscard]] inline bool hasJpegMagic(std::span<const std::byte> bytes)
{
    if (bytes.size() < 2)
    {
        return false;
    }

    return std::to_integer<unsigned char>(bytes[0]) == 0xFFu &&
           std::to_integer<unsigned char>(bytes[1]) == 0xD8u;
}

[[nodiscard]] inline std::expected<std::vector<std::byte>, std::string> readBinaryFile(const std::filesystem::path &path)
{
    std::error_code fileSizeError{};
    auto fileSize = std::filesystem::file_size(path, fileSizeError);
    if (fileSizeError)
    {
        return std::unexpected(std::format("Failed to query file size '{}': {}", path.generic_string(), fileSizeError.message()));
    }

    if (fileSize == 0)
    {
        return std::unexpected(std::format("Texture file '{}' is empty.", path.generic_string()));
    }

    if (fileSize > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()))
    {
        return std::unexpected(std::format("Texture file '{}' exceeds host addressable memory.", path.generic_string()));
    }

    auto byteCount = static_cast<std::size_t>(fileSize);
    if (byteCount > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
    {
        return std::unexpected(std::format("Texture file '{}' exceeds stream read limits.", path.generic_string()));
    }

    auto bytes = std::vector<std::byte>(byteCount);

    auto stream = std::ifstream(path, std::ios::binary);
    if (!stream)
    {
        return std::unexpected(std::format("Failed to open texture file '{}'.", path.generic_string()));
    }

    if (!stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(byteCount)))
    {
        return std::unexpected(std::format("Failed to read texture file '{}'.", path.generic_string()));
    }

    return bytes;
}

[[nodiscard]] inline std::expected<Image, std::string> decodeWithStb(std::span<const std::byte> encodedBytes,
                                                                      std::uint32_t requestedChannels)
{
    if (encodedBytes.empty())
    {
        return std::unexpected("Encoded texture bytes are empty.");
    }

    if (encodedBytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return std::unexpected("Encoded texture size exceeds stb_image input size limit.");
    }

    auto const channelCount = std::clamp(requestedChannels, 1u, 4u);

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    auto *decoded = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc *>(encodedBytes.data()),
        static_cast<int>(encodedBytes.size()),
        &width,
        &height,
        &sourceChannels,
        static_cast<int>(channelCount));

    if (decoded == nullptr)
    {
        auto const *reason = stbi_failure_reason();
        auto reasonString = reason == nullptr ? std::string{"stb_image decode failed."} : std::string{reason};
        return std::unexpected(std::move(reasonString));
    }

    auto decodedPixels = std::unique_ptr<stbi_uc, void (*)(void *)>{decoded, stbi_image_free};

    if (width <= 0 || height <= 0)
    {
        return std::unexpected("stb_image decoded non-positive dimensions.");
    }

    auto const texelCount = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    auto const byteCount = texelCount * channelCount;
    if (byteCount > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
    {
        return std::unexpected("Decoded texture exceeds host memory limits.");
    }

    Image image{};
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.channels = channelCount;
    image.pixels.resize(static_cast<std::size_t>(byteCount));
    std::memcpy(image.pixels.data(), decodedPixels.get(), image.pixels.size());

    return image;
}

[[nodiscard]] inline std::string turboJpegError(tjhandle handle)
{
    auto const *error = tjGetErrorStr2(handle);
    return error == nullptr ? std::string{"libjpeg-turbo decode failed."} : std::string{error};
}

struct TurboJpegHandleDeleter
{
    void operator()(std::remove_pointer_t<tjhandle> *handle) const noexcept
    {
        if (handle != nullptr)
        {
            tjDestroy(handle);
        }
    }
};

[[nodiscard]] inline std::expected<Image, std::string> decodeWithTurboJpeg(std::span<const std::byte> encodedBytes)
{
    if (encodedBytes.empty())
    {
        return std::unexpected("Encoded JPEG bytes are empty.");
    }

    if (encodedBytes.size() > static_cast<std::size_t>(std::numeric_limits<unsigned long>::max()))
    {
        return std::unexpected("Encoded JPEG exceeds libjpeg-turbo input size limit.");
    }

    auto handle = std::unique_ptr<std::remove_pointer_t<tjhandle>, TurboJpegHandleDeleter>{tjInitDecompress()};
    if (handle == nullptr)
    {
        return std::unexpected(turboJpegError(nullptr));
    }

    int width = 0;
    int height = 0;
    int jpegSubsample = 0;
    int jpegColorSpace = 0;
    auto headerResult = tjDecompressHeader3(
        handle.get(),
        reinterpret_cast<const unsigned char *>(encodedBytes.data()),
        static_cast<unsigned long>(encodedBytes.size()),
        &width,
        &height,
        &jpegSubsample,
        &jpegColorSpace);
    if (headerResult != 0)
    {
        return std::unexpected(turboJpegError(handle.get()));
    }

    if (width <= 0 || height <= 0)
    {
        return std::unexpected("libjpeg-turbo decoded non-positive dimensions.");
    }

    auto const texelCount = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    auto const byteCount = texelCount * 4u;
    if (byteCount > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
    {
        return std::unexpected("Decoded JPEG exceeds host memory limits.");
    }

    Image image{};
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.channels = 4;
    image.pixels.resize(static_cast<std::size_t>(byteCount));

    auto decodeResult = tjDecompress2(
        handle.get(),
        reinterpret_cast<const unsigned char *>(encodedBytes.data()),
        static_cast<unsigned long>(encodedBytes.size()),
        image.pixels.data(),
        width,
        0,
        height,
        TJPF_RGBA,
        TJFLAG_FASTDCT);
    if (decodeResult != 0)
    {
        return std::unexpected(turboJpegError(handle.get()));
    }

    return image;
}

[[nodiscard]] inline std::expected<Image, std::string> decodeEmbeddedRaw(const EmbeddedRawTexture &raw)
{
    if (raw.width == 0 || raw.height == 0)
    {
        return std::unexpected("Embedded raw texture dimensions are invalid.");
    }

    auto const expectedByteCount = static_cast<std::uint64_t>(raw.width) * static_cast<std::uint64_t>(raw.height) * 4u;
    if (expectedByteCount > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
    {
        return std::unexpected("Embedded raw texture exceeds host memory limits.");
    }

    if (raw.rgba8.size() != static_cast<std::size_t>(expectedByteCount))
    {
        return std::unexpected("Embedded raw texture byte count does not match width * height * 4.");
    }

    Image image{};
    image.width = raw.width;
    image.height = raw.height;
    image.channels = 4;
    image.pixels.resize(raw.rgba8.size());
    std::ranges::transform(raw.rgba8, image.pixels.begin(), [](std::byte value) {
        return std::to_integer<std::uint8_t>(value);
    });

    return image;
}

[[nodiscard]] inline std::expected<DecodedPayload, std::string> decodeEncodedTexture(const TextureAsset &texture,
                                                                                      std::span<const std::byte> encodedBytes,
                                                                                      std::uint32_t requestedChannels)
{
    auto decodeAsJpeg = false;
    if (texture.payloadKind == TexturePayloadKind::externalReference)
    {
        decodeAsJpeg = isJpegExtension(texture.resolvedPath) || hasJpegMagic(encodedBytes);
    }
    else if (texture.payloadKind == TexturePayloadKind::embeddedCompressedBlob)
    {
        auto hint = texture.compressed.has_value() ? texture.compressed->formatHint : std::string_view{};
        decodeAsJpeg = isJpegHint(hint) || hasJpegMagic(encodedBytes);
    }

    if (decodeAsJpeg)
    {
        auto turboResult = decodeWithTurboJpeg(encodedBytes);
        if (!turboResult.has_value())
        {
            return std::unexpected(std::format("libjpeg-turbo error: {}", turboResult.error()));
        }

        return DecodedPayload{
            .image = std::move(turboResult.value()),
            .decoder = "libjpeg-turbo",
        };
    }

    auto stbResult = decodeWithStb(encodedBytes, requestedChannels);
    if (!stbResult.has_value())
    {
        return std::unexpected(stbResult.error());
    }

    return DecodedPayload{
        .image = std::move(stbResult.value()),
        .decoder = "stb",
    };
}

[[nodiscard]] inline TextureDecodeTaskResult decodeTextureTask(std::uint32_t textureIndex,
                                                               const TextureAsset &texture,
                                                               const TextureDecodeOptions &options)
{
    TextureDecodeTaskResult result{};
    result.textureIndex = textureIndex;

    if (texture.payloadKind == TexturePayloadKind::embeddedRawRgba8)
    {
        if (!texture.rawRgba8.has_value())
        {
            result.error = "Texture payload kind is embedded raw, but raw bytes are missing.";
            return result;
        }

        auto decodeResult = decodeEmbeddedRaw(*texture.rawRgba8);
        if (!decodeResult.has_value())
        {
            result.error = decodeResult.error();
            return result;
        }

        result.image = std::move(decodeResult.value());
        result.decoder = "assimp-raw-copy";
        return result;
    }

    auto encodedBytes = std::vector<std::byte>{};
    if (texture.payloadKind == TexturePayloadKind::externalReference)
    {
        auto fileResult = readBinaryFile(texture.resolvedPath);
        if (!fileResult.has_value())
        {
            result.error = fileResult.error();
            return result;
        }

        encodedBytes = std::move(fileResult.value());
    }
    else if (texture.payloadKind == TexturePayloadKind::embeddedCompressedBlob)
    {
        if (!texture.compressed.has_value())
        {
            result.error = "Texture payload kind is embedded compressed, but compressed bytes are missing.";
            return result;
        }

        encodedBytes = texture.compressed->bytes;
    }
    else
    {
        result.error = "Unsupported texture payload kind for decode task.";
        return result;
    }

    auto decodeResult = decodeEncodedTexture(texture, encodedBytes, options.requestedChannels);
    if (!decodeResult.has_value())
    {
        result.error = decodeResult.error();
        return result;
    }

    result.image = std::move(decodeResult->image);
    result.decoder = std::move(decodeResult->decoder);
    return result;
}

[[nodiscard]] inline std::vector<std::uint32_t> collectReferencedTextureIndices(const SceneAsset &scene)
{
    auto referenced = std::set<std::uint32_t>{};

    std::ranges::for_each(scene.materials, [&](const MaterialAsset &material) {
        std::ranges::for_each(material.textures, [&](const MaterialTextureBinding &binding) {
            if (binding.textureIndex < scene.textures.size())
            {
                referenced.emplace(binding.textureIndex);
            }
        });
    });

    auto ordered = std::vector<std::uint32_t>{referenced.begin(), referenced.end()};
    return ordered;
}

[[nodiscard]] inline std::uint32_t resolveWorkerCount(std::uint32_t requestedWorkers, std::size_t taskCount)
{
    if (taskCount == 0)
    {
        return 1;
    }

    auto autoWorkers = std::thread::hardware_concurrency();
    if (autoWorkers == 0)
    {
        autoWorkers = 1;
    }

    auto normalizedRequested = requestedWorkers == 0 ? autoWorkers : requestedWorkers;
    auto clampedToTaskCount = std::min<std::uint64_t>(normalizedRequested, taskCount);
    return static_cast<std::uint32_t>(std::max<std::uint64_t>(1, clampedToTaskCount));
}

[[nodiscard]] inline std::string joinLines(const std::vector<std::string> &lines)
{
    if (lines.empty())
    {
        return {};
    }

    auto joined = lines.front();
    auto remaining = lines | std::views::drop(std::size_t{1});
    std::ranges::for_each(remaining, [&](const std::string &line) {
        joined.append("\n");
        joined.append(line);
    });

    return joined;
}

} // namespace nr::load::detail

export namespace nr::load
{
[[nodiscard]] inline std::expected<void, LoadError> decodeSceneTextureImages(SceneAsset &scene,
                                                                              const TextureDecodeOptions &options)
{
    auto decodeIndices = detail::collectReferencedTextureIndices(scene);
    if (decodeIndices.empty())
    {
        return {};
    }

    auto workerCount = detail::resolveWorkerCount(options.workerCount, decodeIndices.size());
    auto threadPool = detail::DecodeThreadPool{workerCount};

    auto futures = std::vector<std::future<detail::TextureDecodeTaskResult>>{};
    futures.reserve(decodeIndices.size());

    std::ranges::for_each(decodeIndices, [&](std::uint32_t textureIndex) {
        auto const *texture = &scene.textures[textureIndex];
        futures.push_back(threadPool.submit([textureIndex, texture, options]() {
            return detail::decodeTextureTask(textureIndex, *texture, options);
        }));
    });

    auto failures = std::vector<std::string>{};
    failures.reserve(futures.size());

    std::ranges::for_each(futures, [&](std::future<detail::TextureDecodeTaskResult> &future) {
        auto result = future.get();
        if (result.textureIndex >= scene.textures.size())
        {
            failures.push_back(std::format("Decode task returned invalid texture index {}.", result.textureIndex));
            return;
        }

        auto &texture = scene.textures[result.textureIndex];
        if (!result.error.empty())
        {
            failures.push_back(std::format("Texture '{}' decode failed: {}", texture.key, result.error));
            return;
        }

        texture.decodedImage = std::move(result.image);
        texture.decodeBackend = std::move(result.decoder);
    });

    if (!failures.empty())
    {
        auto message = std::format("Texture decode failed for {} texture(s):\n{}", failures.size(), detail::joinLines(failures));
        return std::unexpected(makeLoadError(
            LoadErrorCode::textureDataUnsupported,
            "decode",
            scene.sourcePath,
            std::move(message)));
    }

    return {};
}

} // namespace nr::load
