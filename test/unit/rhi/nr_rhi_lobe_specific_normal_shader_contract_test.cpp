import std;
import dependency.math;
import dependency.vulkan;
import nr.rhi;
import nr.test;

namespace
{
constexpr auto kReferenceMinLengthSquared = 1.0e-8f;

[[nodiscard]] glm::vec3 safeNormalizeReference(
    glm::vec3 value,
    glm::vec3 fallback) noexcept
{
    auto const lengthSquared = glm::dot(value, value);
    if (lengthSquared > kReferenceMinLengthSquared)
    {
        return value / std::sqrt(lengthSquared);
    }
    return fallback;
}

[[nodiscard]] glm::vec3 reflectReference(
    glm::vec3 incidentDirection,
    glm::vec3 normal) noexcept
{
    return incidentDirection -
           2.0f * glm::dot(incidentDirection, normal) * normal;
}

[[nodiscard]] glm::vec3 facingGeometryNormalReference(
    glm::vec3 geometryNormal,
    glm::vec3 shadingNormal,
    glm::vec3 viewDirection) noexcept
{
    auto const rawNormal = safeNormalizeReference(
        shadingNormal,
        glm::vec3{0.0f, 1.0f, 0.0f});
    auto const facingNormal = safeNormalizeReference(
        geometryNormal,
        rawNormal);
    return glm::dot(facingNormal, viewDirection) >= 0.0f
               ? facingNormal
               : -facingNormal;
}

[[nodiscard]] glm::vec3 adjustSpecularNormalReference(
    glm::vec3 shadingNormal,
    glm::vec3 geometryNormal,
    glm::vec3 viewDirection) noexcept
{
    auto const facingGeometryNormal = facingGeometryNormalReference(
        geometryNormal,
        shadingNormal,
        viewDirection);
    auto const rawNormal = safeNormalizeReference(
        shadingNormal,
        facingGeometryNormal);
    auto const safeViewDirection = safeNormalizeReference(
        viewDirection,
        facingGeometryNormal);
    auto const incidentDirection = -safeViewDirection;
    auto const reflectedDirection = reflectReference(
        incidentDirection,
        rawNormal);
    auto const geometryCosine = glm::dot(
        reflectedDirection,
        facingGeometryNormal);

    auto specularNormal = rawNormal;
    if (geometryCosine < 0.0f)
    {
        auto const fallbackReflection = reflectReference(
            incidentDirection,
            facingGeometryNormal);
        auto const clippedReflection = safeNormalizeReference(
            reflectedDirection -
                geometryCosine * facingGeometryNormal,
            fallbackReflection);
        specularNormal = safeNormalizeReference(
            clippedReflection - incidentDirection,
            facingGeometryNormal);
    }

    return glm::dot(specularNormal, safeViewDirection) >= 0.0f
               ? specularNormal
               : -specularNormal;
}

[[nodiscard]] bool finite(glm::vec3 value) noexcept
{
    return
        std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool nearlyEqual(
    glm::vec3 lhs,
    glm::vec3 rhs,
    float epsilon = 1.0e-5f) noexcept
{
    return glm::length(lhs - rhs) <= epsilon;
}

const nr::test::CaseRegistrar lobeSpecificNormalShaderMatrixCase{
    "lobe-specific normal shader contract instantiates all 16 lit material masks",
    [] {
        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto program = shaderService.compileProgramByFile(
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{
                    "test/rt/materialLobeSpecificNormalContract"},
            });
        nr::test::require(
            program.valid(),
            "lobe-specific normal contract should compile all physical-layer and anisotropy combinations");
        nr::test::require(
            program.entryPointBlob("computeMain") != nullptr,
            "lobe-specific normal contract should expose compute SPIR-V");

        auto layout = nr::rhi::ShaderDescriptorLayout::create(program);
        nr::test::require(
            layout.valid(),
            "lobe-specific normal contract descriptor layout should be valid");
        nr::test::require(
            layout.rootCursor()["gMaterialHeader"].valid() &&
                layout.rootCursor()["gInstantiationResults"].valid(),
            "lobe-specific normal contract resources should reflect");
    }};

const nr::test::CaseRegistrar lobeSpecificNormalIdentityAndGrazingCase{
    "lobe-specific normal adjustment preserves identity and clips grazing reflection",
    [] {
        auto const geometryNormal = glm::vec3{0.0f, 1.0f, 0.0f};
        auto const viewDirection = geometryNormal;

        auto const identity = adjustSpecularNormalReference(
            geometryNormal,
            geometryNormal,
            viewDirection);
        nr::test::require(
            nearlyEqual(identity, geometryNormal),
            "matching raw and geometry normals should remain unchanged");

        auto const rawGrazingNormal = safeNormalizeReference(
            glm::vec3{0.8660254f, 0.5f, 0.0f},
            geometryNormal);
        auto const unadjustedReflection = reflectReference(
            -viewDirection,
            rawGrazingNormal);
        nr::test::require(
            glm::dot(unadjustedReflection, geometryNormal) < 0.0f,
            "reference grazing normal should reflect below the geometric surface");

        auto const adjustedNormal = adjustSpecularNormalReference(
            rawGrazingNormal,
            geometryNormal,
            viewDirection);
        auto const adjustedReflection = reflectReference(
            -viewDirection,
            adjustedNormal);
        nr::test::require(
            finite(adjustedNormal) &&
                std::abs(glm::length(adjustedNormal) - 1.0f) <= 1.0e-5f,
            "adjusted grazing normal should remain finite and normalized");
        nr::test::require(
            glm::dot(adjustedReflection, geometryNormal) >= -1.0e-5f,
            "adjusted ideal reflection should not cross below the geometric surface");
        nr::test::require(
            glm::dot(adjustedNormal, viewDirection) >= 0.0f,
            "adjusted microfacet representative should face the view");
    }};

const nr::test::CaseRegistrar lobeSpecificNormalDegenerateAndDoubleSidedCase{
    "lobe-specific normal adjustment handles degenerate projection and double-sided facing",
    [] {
        auto const upward = glm::vec3{0.0f, 1.0f, 0.0f};
        auto const tangentRawNormal = glm::vec3{1.0f, 0.0f, 0.0f};
        auto const degenerate = adjustSpecularNormalReference(
            tangentRawNormal,
            upward,
            upward);
        nr::test::require(
            finite(degenerate) && nearlyEqual(degenerate, upward),
            "zero tangent-plane reflection projection should use the finite geometry-normal fallback");

        auto const storedBackfaceNormal = -upward;
        auto const rawBackfaceShadingNormal = safeNormalizeReference(
            glm::vec3{0.8660254f, 0.5f, 0.0f},
            upward);
        auto const facing = facingGeometryNormalReference(
            storedBackfaceNormal,
            rawBackfaceShadingNormal,
            upward);
        nr::test::require(
            nearlyEqual(facing, upward),
            "double-sided BSDF geometry normal should face the current view");

        auto const adjustedFromStoredBackface =
            adjustSpecularNormalReference(
                rawBackfaceShadingNormal,
                storedBackfaceNormal,
                upward);
        auto const adjustedFromFacingNormal =
            adjustSpecularNormalReference(
                rawBackfaceShadingNormal,
                upward,
                upward);
        nr::test::require(
            nearlyEqual(
                adjustedFromStoredBackface,
                adjustedFromFacingNormal),
            "stored boundary orientation must not change double-sided specular clipping");
    }};

const nr::test::CaseRegistrar lobeSpecificNormalThresholdCase{
    "lobe-specific normal adjustment remains finite and continuous at the clipping threshold",
    [] {
        constexpr auto threshold = 0.7853981633974483f;
        constexpr auto delta = 1.0e-4f;
        auto const geometryNormal = glm::vec3{0.0f, 1.0f, 0.0f};
        auto const viewDirection = geometryNormal;
        auto rawNormalAt = [](float angle) {
            return glm::vec3{
                std::sin(angle),
                std::cos(angle),
                0.0f,
            };
        };

        auto const below = adjustSpecularNormalReference(
            rawNormalAt(threshold - delta),
            geometryNormal,
            viewDirection);
        auto const at = adjustSpecularNormalReference(
            rawNormalAt(threshold),
            geometryNormal,
            viewDirection);
        auto const above = adjustSpecularNormalReference(
            rawNormalAt(threshold + delta),
            geometryNormal,
            viewDirection);

        nr::test::require(
            finite(below) && finite(at) && finite(above),
            "both sides of the clipping threshold should remain finite");
        nr::test::require(
            glm::length(below - at) <= 5.0e-4f &&
                glm::length(above - at) <= 5.0e-4f,
            "specular normal should be continuous as ideal reflection reaches the geometric tangent plane");

        auto const reflectedAbove = reflectReference(
            -viewDirection,
            above);
        nr::test::require(
            glm::dot(reflectedAbove, geometryNormal) >= -1.0e-5f,
            "the corrected side of the threshold should stay in the geometric hemisphere");
    }};
} // namespace
