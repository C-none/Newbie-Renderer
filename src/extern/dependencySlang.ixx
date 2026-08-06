module;
#include <slang.h>
#include <slang-com-ptr.h>

export module dependency.slang;

export namespace Slang
{
using ::Slang::ComPtr;
} // namespace Slang

export namespace slang
{
inline constexpr auto unboundedSize = SLANG_UNBOUNDED_SIZE;
using ::slang::BindingType;
using ::slang::CompilerOptionEntry;
using ::slang::CompilerOptionName;
using ::slang::CompilerOptionValue;
using ::slang::CompilerOptionValueKind;
using ::slang::createGlobalSession;
using ::slang::EntryPointReflection;
using ::slang::IBlob;
using ::slang::IComponentType;
using ::slang::IEntryPoint;
using ::slang::IGlobalSession;
using ::slang::IModule;
using ::slang::ISession;
using ::slang::ParameterCategory;
using ::slang::PreprocessorMacroDesc;
using ::slang::ProgramLayout;
using ::slang::SessionDesc;
using ::slang::TargetDesc;
using ::slang::TypeLayoutReflection;
using ::slang::TypeReflection;
using ::slang::VariableLayoutReflection;
} // namespace slang

export using ::SlangCompileTarget;
export using ::SlangInt;
export using ::SlangInt32;
export using ::SlangResourceAccess;
export using ::SlangResourceShape;
export using ::SlangResult;
export using ::SlangStage;
export using ::SlangUInt;
export using ::slang_createBlob;
export using ::SLANG_DEBUG_INFO_LEVEL_MAXIMAL;
export using ::SLANG_DEBUG_INFO_LEVEL_NONE;
export using ::SLANG_OPTIMIZATION_LEVEL_MAXIMAL;
export using ::SLANG_OPTIMIZATION_LEVEL_NONE;
export using ::SLANG_PARAMETER_CATEGORY_UNIFORM;
export using ::SLANG_PROFILE_UNKNOWN;
export using ::SLANG_SPIRV;
export using ::SLANG_STAGE_AMPLIFICATION;
export using ::SLANG_STAGE_ANY_HIT;
export using ::SLANG_STAGE_CALLABLE;
export using ::SLANG_STAGE_CLOSEST_HIT;
export using ::SLANG_STAGE_COMPUTE;
export using ::SLANG_STAGE_DOMAIN;
export using ::SLANG_STAGE_FRAGMENT;
export using ::SLANG_STAGE_GEOMETRY;
export using ::SLANG_STAGE_HULL;
export using ::SLANG_STAGE_INTERSECTION;
export using ::SLANG_STAGE_MESH;
export using ::SLANG_STAGE_MISS;
export using ::SLANG_STAGE_NONE;
export using ::SLANG_STAGE_RAY_GENERATION;
export using ::SLANG_STAGE_VERTEX;
