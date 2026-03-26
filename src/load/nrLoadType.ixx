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

enum class MaterialAlphaModeHint : uint8_t
{
    opaque,
    blend,
    mask,
};

enum class MaterialWorkflowFlags : uint8_t
{
    metallicRoughness = 1 << 0,
    specularGlossiness = 1 << 1,
    anisotropy = 1 << 2,
};

struct MaterialAsset
{
    std::string name{};
    
    // Authoring: Base color and emissive
    std::array<float, 4> baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> emissiveFactor{0.0f, 0.0f, 0.0f};
    
    // Authoring: Metallic/Roughness workflow
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    
    // Authoring: Specular/Glossiness workflow
    std::optional<std::array<float, 3>> specularFactor{};
    std::optional<float> glossinessFactor{};
    
    // Authoring: Anisotropy
    std::optional<float> anisotropyFactor{};
    
    // Authoring: Transparency and rendering
    float opacity = 1.0f;
    MaterialAlphaModeHint alphaModeHint = MaterialAlphaModeHint::opaque;
    std::optional<float> alphaCutoff{};
    
    // Authoring: Surface properties
    bool doubleSided = false;
    std::optional<float> normalScale{};
    std::optional<float> occlusionStrength{};
    
    // Authoring: Texture bindings
    std::vector<MaterialTextureBinding> textures{};
    
    // Workflow classification (populated by bridge/loader)
    MaterialWorkflowFlags workflowFlags = MaterialWorkflowFlags::metallicRoughness;
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

struct CameraAsset
{
    std::string name{};
    std::string sourceNodeName{};
    uint32_t nodeIndex = invalidIndex;
    std::array<float, 3> position{};
    std::array<float, 3> lookAt{0.0f, 0.0f, -1.0f};
    std::array<float, 3> up{0.0f, 1.0f, 0.0f};
    float horizontalFov = 0.0f;
    float aspect = 0.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    float orthographicWidth = 0.0f;
};

struct LightAsset
{
    std::string name{};
    std::string sourceNodeName{};
    uint32_t nodeIndex = invalidIndex;
    uint32_t typeRaw = 0;
    std::string type{};
    std::array<float, 3> position{};
    std::array<float, 3> direction{0.0f, 0.0f, -1.0f};
    std::array<float, 3> up{0.0f, 1.0f, 0.0f};
    std::array<float, 3> colorDiffuse{};
    std::array<float, 3> colorSpecular{};
    std::array<float, 3> colorAmbient{};
    float attenuationConstant = 0.0f;
    float attenuationLinear = 0.0f;
    float attenuationQuadratic = 0.0f;
    float innerCone = 0.0f;
    float outerCone = 0.0f;
    std::array<float, 2> areaSize{};
};

struct SceneImportStatistics
{
    uint32_t nodeCount = 0;
    uint32_t meshCount = 0;
    uint32_t materialCount = 0;
    uint32_t textureCount = 0;
    uint32_t cameraCount = 0;
    uint32_t lightCount = 0;
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
    std::vector<CameraAsset> cameras{};
    std::vector<LightAsset> lights{};
    SceneImportStatistics stats{};
};

using SceneImportResult = std::expected<SceneAsset, LoadError>;

} // namespace nr::load
