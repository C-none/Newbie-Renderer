import std;
import dependency.assets;
import dependency.vulkan;
import nr.load;
import nr.resource;
import nr.test;
import nr.utils;

namespace
{
struct FloatRgb
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

class TemporaryExr
{
  public:
    explicit TemporaryExr(std::string_view stem)
    {
        auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / std::format("nr_{}_{}.exr", stem, stamp);
    }

    ~TemporaryExr()
    {
        auto removeError = std::error_code{};
        std::filesystem::remove(path_, removeError);
    }

    TemporaryExr(const TemporaryExr &) = delete;
    TemporaryExr &operator=(const TemporaryExr &) = delete;

    [[nodiscard]] const std::filesystem::path &path() const noexcept
    {
        return path_;
    }

  private:
    std::filesystem::path path_{};
};

[[nodiscard]] bool writeFloatExr(const std::filesystem::path &path, std::span<FloatRgb> pixels, std::uint32_t width,
                                 std::uint32_t height, bool includeBlue)
{
    try
    {
        auto header = nr::dependency::openexr::Header{static_cast<int>(width), static_cast<int>(height)};
        header.channels().insert("R", nr::dependency::openexr::Channel(nr::dependency::openexr::floatPixelType));
        header.channels().insert("G", nr::dependency::openexr::Channel(nr::dependency::openexr::floatPixelType));
        if (includeBlue)
        {
            header.channels().insert("B", nr::dependency::openexr::Channel(nr::dependency::openexr::floatPixelType));
        }

        auto const xStride = sizeof(FloatRgb);
        auto const yStride = static_cast<std::size_t>(width) * xStride;
        auto frameBuffer = nr::dependency::openexr::FrameBuffer{};
        frameBuffer.insert("R", nr::dependency::openexr::Slice(nr::dependency::openexr::floatPixelType,
                                                               reinterpret_cast<char *>(&pixels.front().r), xStride,
                                                               yStride));
        frameBuffer.insert("G", nr::dependency::openexr::Slice(nr::dependency::openexr::floatPixelType,
                                                               reinterpret_cast<char *>(&pixels.front().g), xStride,
                                                               yStride));
        if (includeBlue)
        {
            frameBuffer.insert("B", nr::dependency::openexr::Slice(nr::dependency::openexr::floatPixelType,
                                                                   reinterpret_cast<char *>(&pixels.front().b), xStride,
                                                                   yStride));
        }

        auto const pathString = path.string();
        auto file = nr::dependency::openexr::OutputFile(pathString.c_str(), header);
        file.setFrameBuffer(frameBuffer);
        file.writePixels(static_cast<int>(height));
        return true;
    }
    catch (const std::exception &error)
    {
        nr::nrInfo<nr::LogLevel::error>(
            std::format("Failed to write OpenEXR test fixture '{}': {}", path.generic_string(), error.what()));
        return false;
    }
    catch (...)
    {
        nr::nrInfo<nr::LogLevel::error>(
            std::format("Failed to write OpenEXR test fixture '{}': unknown OpenEXR error.", path.generic_string()));
        return false;
    }
}

[[nodiscard]] float encodedHalf(const nr::resource::EnvironmentMap &environment, std::size_t pixelIndex,
                                std::size_t channelIndex)
{
    auto value = nr::dependency::imath::Half{};
    auto const &bytes = environment.radiance.levels.front().bytes;
    auto const offset = (pixelIndex * 4u + channelIndex) * sizeof(nr::dependency::imath::Half);
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return static_cast<float>(value);
}

[[nodiscard]] bool near(float lhs, float rhs, float tolerance) noexcept
{
    return std::abs(lhs - rhs) <= tolerance;
}

const nr::test::CaseRegistrar scaledFloatExrCase{
    "load OpenEXR environment scales FLOAT radiance into RGBA16F", [] {
        auto source = std::array{
            FloatRgb{.r = -1.0e-8f, .g = 1.0f, .b = 2.0f},      FloatRgb{.r = 3.0f, .g = 4.0f, .b = 5.0f},
            FloatRgb{.r = 75'000.0f, .g = 100.0f, .b = 200.0f}, FloatRgb{.r = 7.0f, .g = 8.0f, .b = 9.0f},
            FloatRgb{.r = 10.0f, .g = 11.0f, .b = 12.0f},       FloatRgb{.r = 13.0f, .g = 14.0f, .b = 15.0f},
            FloatRgb{.r = 16.0f, .g = 17.0f, .b = 18.0f},       FloatRgb{.r = 19.0f, .g = 20.0f, .b = 21.0f},
        };
        auto file = TemporaryExr{"scaled_environment"};
        nr::test::require(writeFloatExr(file.path(), source, 4u, 2u, true),
                          "FLOAT RGB OpenEXR fixture should be written");

        auto result = nr::load::loadExrEnvironmentMap(nr::load::ExrEnvironmentLoadRequest{
            .sourcePath = file.path(),
        });
        nr::test::require(result.has_value(), "valid FLOAT RGB OpenEXR should load");

        auto const &environment = *result;
        nr::test::require(environment.valid(), "loaded environment resource should validate");
        nr::test::requireEqual(environment.radiance.width, 4u);
        nr::test::requireEqual(environment.radiance.height, 2u);
        nr::test::require(environment.radiance.format == vk::Format::eR16G16B16A16Sfloat,
                          "environment storage format should be RGBA16F");
        nr::test::require(!environment.radiance.srgb, "environment texture must use linear sampling");
        nr::test::requireEqual(environment.radiance.levels.front().bytes.size(),
                               std::size_t{4u * 2u * 4u * sizeof(nr::dependency::imath::Half)});
        nr::test::require(near(environment.radianceDecodeScale, 1.25f, 1.0e-6f),
                          "75000 peak with a 60000 safe maximum should produce a 1.25 decode scale");

        nr::test::requireEqual(encodedHalf(environment, 0u, 0u), 0.0f);
        nr::test::requireEqual(encodedHalf(environment, 0u, 3u), 1.0f);
        nr::test::require(near(encodedHalf(environment, 2u, 0u) * environment.radianceDecodeScale, 75'000.0f, 64.0f),
                          "scaled HALF peak should reconstruct source radiance");
        nr::test::require(near(encodedHalf(environment, 1u, 2u) * environment.radianceDecodeScale, 5.0f, 0.01f),
                          "ordinary radiance should survive scale and HALF quantization");
    }};

const nr::test::CaseRegistrar missingChannelCase{
    "load OpenEXR environment rejects missing RGB channels", [] {
        auto source = std::array{FloatRgb{.r = 1.0f, .g = 2.0f, .b = 3.0f}};
        auto file = TemporaryExr{"missing_blue_environment"};
        nr::test::require(writeFloatExr(file.path(), source, 1u, 1u, false), "RG OpenEXR fixture should be written");

        auto result = nr::load::loadExrEnvironmentMap(nr::load::ExrEnvironmentLoadRequest{
            .sourcePath = file.path(),
        });
        nr::test::require(!result.has_value(), "OpenEXR missing B should fail");
        nr::test::require(result.error().code == nr::load::LoadErrorCode::textureDataUnsupported,
                          "missing B should report textureDataUnsupported");
        nr::test::require(result.error().message.find("'B'") != std::string::npos,
                          "missing-channel diagnostic should name B");
    }};
} // namespace
