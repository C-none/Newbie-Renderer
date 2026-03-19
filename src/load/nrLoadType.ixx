module;
export module nr.load:type;
import std;

export namespace nr::load
{
using std::uint32_t;
using std::uint8_t;

inline constexpr uint32_t invalidIndex = std::numeric_limits<uint32_t>::max();

enum class LoadErrorCode : uint8_t
{
    invalidArgument,
    fileNotFound,
    unsupportedFormat,
    importFailed,
    invalidScene,
    textureDataUnsupported,
    internalError,
};

struct LoadError
{
    LoadErrorCode code = LoadErrorCode::internalError;
    std::string backend{};
    std::filesystem::path sourcePath{};
    std::string message{};
};

struct SceneLoadRequest
{
    std::filesystem::path sourcePath{};
    std::filesystem::path searchRoot{};
    bool triangulate = true;
    bool joinIdenticalVertices = true;
    bool generateNormals = true;
    bool generateTangents = true;
    bool validateDataStructure = true;
    bool preTransformVertices = false;
    bool optimizeMeshes = false;
    bool optimizeGraph = false;
    bool strict = true;
};

enum class TexturePayloadKind : uint8_t
{
    externalReference,
    embeddedRawRgba8,
    embeddedCompressedBlob,
};

struct EmbeddedRawTexture
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::byte> rgba8{};
};

struct EmbeddedCompressedTexture
{
    std::string formatHint{};
    std::vector<std::byte> bytes{};
};

struct Image
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 0;
    std::vector<uint8_t> pixels{};
};

struct TextureAsset
{
    std::string key{};
    std::filesystem::path resolvedPath{};
    TexturePayloadKind payloadKind = TexturePayloadKind::externalReference;
    std::optional<EmbeddedRawTexture> rawRgba8{};
    std::optional<EmbeddedCompressedTexture> compressed{};
    std::optional<Image> decodedImage{};
    std::string decodeBackend{};
};

struct MaterialTextureBinding
{
    uint32_t textureIndex = invalidIndex;
    uint32_t uvChannel = 0;
    uint32_t textureTypeRaw = 0;
    std::string semantic{};
};

struct MaterialAsset
{
    std::string name{};
    std::array<float, 4> baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> emissiveFactor{0.0f, 0.0f, 0.0f};
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    float opacity = 1.0f;
    std::vector<MaterialTextureBinding> textures{};
};

struct VertexAsset
{
    std::array<float, 3> position{};
    std::array<float, 3> normal{0.0f, 0.0f, 1.0f};
    std::array<float, 4> tangent{1.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 2> texCoord0{};
    std::array<float, 4> color0{1.0f, 1.0f, 1.0f, 1.0f};
};

struct MeshAsset
{
    std::string name{};
    std::vector<VertexAsset> vertices{};
    std::vector<uint32_t> indices{};
    uint32_t materialIndex = invalidIndex;
};

struct NodeAsset
{
    std::string name{};
    uint32_t parentIndex = invalidIndex;
    std::vector<uint32_t> childIndices{};
    std::vector<uint32_t> meshIndices{};
    std::array<float, 16> localTransform{
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };
};

struct SceneImportStatistics
{
    uint32_t nodeCount = 0;
    uint32_t meshCount = 0;
    uint32_t materialCount = 0;
    uint32_t textureCount = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
};

struct SceneAsset
{
    std::filesystem::path sourcePath{};
    uint32_t rootNodeIndex = invalidIndex;
    std::vector<NodeAsset> nodes{};
    std::vector<MeshAsset> meshes{};
    std::vector<MaterialAsset> materials{};
    std::vector<TextureAsset> textures{};
    SceneImportStatistics stats{};
};

using SceneImportResult = std::expected<SceneAsset, LoadError>;

} // namespace nr::load
