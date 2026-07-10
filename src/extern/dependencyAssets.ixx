module;
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/metadata.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <Imath/half.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfOutputFile.h>
#include <stb_image.h>
#include <stb_image_write.h>
#include <turbojpeg.h>

export module dependency.assets;

export namespace Assimp
{
using ::Assimp::Importer;
} // namespace Assimp

export namespace nr::dependency::openexr
{
using Channel = ::OPENEXR_IMF_NAMESPACE::Channel;
using FrameBuffer = ::OPENEXR_IMF_NAMESPACE::FrameBuffer;
using Header = ::OPENEXR_IMF_NAMESPACE::Header;
using OutputFile = ::OPENEXR_IMF_NAMESPACE::OutputFile;
using PixelType = ::OPENEXR_IMF_NAMESPACE::PixelType;
using Slice = ::OPENEXR_IMF_NAMESPACE::Slice;

inline constexpr PixelType halfPixelType = ::OPENEXR_IMF_NAMESPACE::HALF;
inline constexpr PixelType floatPixelType = ::OPENEXR_IMF_NAMESPACE::FLOAT;
} // namespace nr::dependency::openexr

export namespace nr::dependency::imath
{
using Half = ::IMATH_NAMESPACE::half;
} // namespace nr::dependency::imath


export using ::ai_real;
export using ::aiColor3D;
export using ::aiColor4D;
export using ::aiGetMaterialColor;
export using ::aiGetMaterialFloatArray;
export using ::aiGetMaterialIntegerArray;
export using ::aiGetMaterialString;
export using ::aiLightSource_AMBIENT;
export using ::aiLightSource_AREA;
export using ::aiLightSource_DIRECTIONAL;
export using ::aiLightSource_POINT;
export using ::aiLightSource_SPOT;
export using ::aiLightSource_UNDEFINED;
export using ::aiLightSourceType;
export using ::aiMaterial;
export using ::aiMatrix4x4;
export using ::aiMetadata;
export using ::aiNode;
export using ::aiPostProcessSteps;
export using ::aiProcess_CalcTangentSpace;
export using ::aiProcess_GenSmoothNormals;
export using ::aiProcess_JoinIdenticalVertices;
export using ::aiProcess_OptimizeGraph;
export using ::aiProcess_OptimizeMeshes;
export using ::aiProcess_PreTransformVertices;
export using ::aiProcess_SortByPType;
export using ::aiProcess_Triangulate;
export using ::aiProcess_ValidateDataStructure;
export using ::aiReturn_SUCCESS;
export using ::aiScene;
export using ::aiShadingMode;
export using ::aiString;
export using ::aiTexture;
export using ::aiTextureType;
export using ::aiTextureType_AMBIENT;
export using ::aiTextureType_AMBIENT_OCCLUSION;
export using ::aiTextureType_ANISOTROPY;
export using ::aiTextureType_BASE_COLOR;
export using ::aiTextureType_CLEARCOAT;
export using ::aiTextureType_DIFFUSE;
export using ::aiTextureType_DIFFUSE_ROUGHNESS;
export using ::aiTextureType_DISPLACEMENT;
export using ::aiTextureType_EMISSION_COLOR;
export using ::aiTextureType_EMISSIVE;
export using ::aiTextureType_GLTF_METALLIC_ROUGHNESS;
export using ::aiTextureType_HEIGHT;
export using ::aiTextureType_LIGHTMAP;
export using ::aiTextureType_MAYA_BASE;
export using ::aiTextureType_MAYA_SPECULAR;
export using ::aiTextureType_MAYA_SPECULAR_COLOR;
export using ::aiTextureType_MAYA_SPECULAR_ROUGHNESS;
export using ::aiTextureType_METALNESS;
export using ::aiTextureType_NONE;
export using ::aiTextureType_NORMAL_CAMERA;
export using ::aiTextureType_NORMALS;
export using ::aiTextureType_OPACITY;
export using ::aiTextureType_REFLECTION;
export using ::aiTextureType_SHEEN;
export using ::aiTextureType_SHININESS;
export using ::aiTextureType_SPECULAR;
export using ::aiTextureType_TRANSMISSION;
export using ::aiTextureType_UNKNOWN;
export using ::aiVector3D;

export inline constexpr unsigned int assimpTextureTypeMax = static_cast<unsigned int>(AI_TEXTURE_TYPE_MAX);

// KHR_materials_unlit maps to Assimp's no-shading mode (aiShadingMode_Unlit alias). Exported as a
// project-named integer constant so the load boundary can classify unlit materials without spreading
// AI_MATKEY_* macros or raw Assimp headers into engine modules.
export inline constexpr int assimpShadingModeUnlit = static_cast<int>(aiShadingMode_NoShading);

export using ::stbi_failure_reason;
export using ::stbi_image_free;
export using ::stbi_load_from_memory;
export using ::stbi_uc;
export using ::stbi_write_png;
export using ::tjDecompress2;
export using ::tjDecompressHeader3;
export using ::tjDestroy;
export using ::tjGetErrorStr2;
export using ::tjhandle;
export using ::tjInitDecompress;
export using ::TJPF_RGBA;


#ifdef TJFLAG_FASTDCT
#undef TJFLAG_FASTDCT
#endif
export inline constexpr unsigned int TJFLAG_FASTDCT = 2048u;
