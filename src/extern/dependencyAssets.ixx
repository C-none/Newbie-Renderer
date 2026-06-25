module;
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <stb_image.h>
#include <turbojpeg.h>

export module dependency.assets;

export namespace Assimp
{
using ::Assimp::Importer;
} // namespace Assimp


export using ::ai_real;
export using ::aiColor3D;
export using ::aiColor4D;
export using ::aiGetMaterialColor;
export using ::aiGetMaterialFloatArray;
export using ::aiGetMaterialIntegerArray;
export using ::aiLightSource_AMBIENT;
export using ::aiLightSource_AREA;
export using ::aiLightSource_DIRECTIONAL;
export using ::aiLightSource_POINT;
export using ::aiLightSource_SPOT;
export using ::aiLightSource_UNDEFINED;
export using ::aiLightSourceType;
export using ::aiMaterial;
export using ::aiMatrix4x4;
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
export using ::aiString;
export using ::aiTexture;
export using ::aiTextureType;
export using ::aiTextureType_AMBIENT;
export using ::aiTextureType_DIFFUSE;
export using ::aiTextureType_DISPLACEMENT;
export using ::aiTextureType_EMISSIVE;
export using ::aiTextureType_HEIGHT;
export using ::aiTextureType_LIGHTMAP;
export using ::aiTextureType_NONE;
export using ::aiTextureType_NORMALS;
export using ::aiTextureType_OPACITY;
export using ::aiTextureType_REFLECTION;
export using ::aiTextureType_SHININESS;
export using ::aiTextureType_SPECULAR;
export using ::aiTextureType_UNKNOWN;
export using ::aiVector3D;


export using ::stbi_failure_reason;
export using ::stbi_image_free;
export using ::stbi_load_from_memory;
export using ::stbi_uc;
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
