import std;
import dependency.math;
import dependency.vulkan;
import nr.rhi;
import nr.test;

namespace
{
constexpr auto kReferenceMinLengthSquared = 1.0e-8f;

[[nodiscard]] glm::vec3 safeNormalizeReference(glm::vec3 value, glm::vec3 fallback) noexcept
{
    auto const lengthSquared = glm::dot(value, value);
    if (lengthSquared > kReferenceMinLengthSquared)
    {
        return value / std::sqrt(lengthSquared);
    }
    return fallback;
}

[[nodiscard]] glm::vec3 reflectReference(glm::vec3 incidentDirection, glm::vec3 normal) noexcept
{
    return incidentDirection - 2.0f * glm::dot(incidentDirection, normal) * normal;
}

struct GgxSpecularEnergyTermsReference
{
    glm::vec3 W{1.0f};
    glm::vec3 E{1.0f};
};

[[nodiscard]] float correlatedSmithG2Reference(float alphaT, float alphaB, glm::vec3 localView,
                                               glm::vec3 localLight) noexcept
{
    auto const noV = std::abs(localView.z);
    auto const noL = std::abs(localLight.z);
    if (noV <= 0.0f || noL <= 0.0f)
    {
        return 0.0f;
    }

    auto const lenV = glm::length(glm::vec3{
        alphaT * localView.x,
        alphaB * localView.y,
        noV,
    });
    auto const lenL = glm::length(glm::vec3{
        alphaT * localLight.x,
        alphaB * localLight.y,
        noL,
    });
    return 2.0f * noV * noL / std::max(noL * lenV + noV * lenL, 1.0e-7f);
}

[[nodiscard]] float separableSmithG2Reference(float alphaT, float alphaB, glm::vec3 localView,
                                              glm::vec3 localLight) noexcept
{
    auto smithG1 = [alphaT, alphaB](glm::vec3 direction) {
        auto const noX = std::abs(direction.z);
        if (noX <= 0.0f)
        {
            return 0.0f;
        }

        auto const lenX = glm::length(glm::vec3{
            alphaT * direction.x,
            alphaB * direction.y,
            noX,
        });
        return 2.0f * noX / std::max(noX + lenX, 1.0e-7f);
    };
    return smithG1(localView) * smithG1(localLight);
}

[[nodiscard]] glm::vec2 ggxDirectionalAlbedoAnalyticReference(float roughness, float noV) noexcept
{
    constexpr auto minRoughness = 0.045f;
    auto const r = std::max(roughness, minRoughness);
    auto const c = std::clamp(noV, 0.0f, 1.0f);
    auto const directionalAlbedo =
        1.0f - std::clamp(std::pow(r, c / r) * ((r * c + 0.0266916f) / (0.466495f + c)), 0.0f, 1.0f);
    auto const oneMinusC = 1.0f - c;
    auto const fresnelDirectionalAlbedo = oneMinusC * oneMinusC * oneMinusC * oneMinusC * oneMinusC *
                                          std::pow(2.36651f * std::pow(c, 4.7703f * r) + 0.0387332f, r);
    return glm::vec2{directionalAlbedo, fresnelDirectionalAlbedo};
}

[[nodiscard]] GgxSpecularEnergyTermsReference ggxSpecularEnergyTermsReference(float roughness, float noV, glm::vec3 f0,
                                                                              glm::vec3 f90) noexcept
{
    auto const splitSum = ggxDirectionalAlbedoAnalyticReference(roughness, noV);
    auto const safeDirectionalAlbedo = std::max(splitSum.x, 1.0e-4f);

    GgxSpecularEnergyTermsReference result;
    result.W = glm::vec3{1.0f} + f0 * ((1.0f - safeDirectionalAlbedo) / safeDirectionalAlbedo);
    result.E = result.W * (splitSum.x * f0 + splitSum.y * (f90 - f0));
    return result;
}

[[nodiscard]] glm::vec3 mirrorReflectionDirectionReference(glm::vec3 facingGeometryNormal, glm::vec3 direction) noexcept
{
    auto const safeGeometryNormal = safeNormalizeReference(facingGeometryNormal, glm::vec3{0.0f, 1.0f, 0.0f});
    auto const safeDirection = safeNormalizeReference(direction, safeGeometryNormal);
    return safeNormalizeReference(
        safeDirection - 2.0f * glm::dot(safeDirection, safeGeometryNormal) * safeGeometryNormal, safeDirection);
}

[[nodiscard]] glm::vec3 foldReflectionDirectionReference(glm::vec3 facingGeometryNormal, glm::vec3 direction) noexcept
{
    auto const safeGeometryNormal = safeNormalizeReference(facingGeometryNormal, glm::vec3{0.0f, 1.0f, 0.0f});
    auto const safeDirection = safeNormalizeReference(direction, safeGeometryNormal);
    return glm::dot(safeGeometryNormal, safeDirection) >= 0.0f
               ? safeDirection
               : mirrorReflectionDirectionReference(safeGeometryNormal, safeDirection);
}

[[nodiscard]] float cosineHemispherePdfReference(glm::vec3 normal, glm::vec3 direction) noexcept
{
    auto const safeNormal = safeNormalizeReference(normal, glm::vec3{0.0f, 1.0f, 0.0f});
    auto const safeDirection = safeNormalizeReference(direction, safeNormal);
    return std::max(glm::dot(safeNormal, safeDirection), 0.0f) / std::numbers::pi_v<float>;
}

[[nodiscard]] float foldedCosineHemispherePdfReference(glm::vec3 shadingNormal, glm::vec3 facingGeometryNormal,
                                                       glm::vec3 exteriorDirection) noexcept
{
    auto const safeGeometryNormal = safeNormalizeReference(facingGeometryNormal, glm::vec3{0.0f, 1.0f, 0.0f});
    auto const safeDirection = safeNormalizeReference(exteriorDirection, safeGeometryNormal);
    if (glm::dot(safeGeometryNormal, safeDirection) < 0.0f)
    {
        return 0.0f;
    }

    auto result = cosineHemispherePdfReference(shadingNormal, safeDirection);
    if (glm::dot(safeGeometryNormal, safeDirection) > 0.0f)
    {
        result += cosineHemispherePdfReference(shadingNormal,
                                               mirrorReflectionDirectionReference(safeGeometryNormal, safeDirection));
    }
    return result;
}

[[nodiscard]] glm::vec3 facingGeometryNormalReference(glm::vec3 geometryNormal, glm::vec3 shadingNormal,
                                                      glm::vec3 viewDirection) noexcept
{
    auto const rawNormal = safeNormalizeReference(shadingNormal, glm::vec3{0.0f, 1.0f, 0.0f});
    auto const facingNormal = safeNormalizeReference(geometryNormal, rawNormal);
    return glm::dot(facingNormal, viewDirection) >= 0.0f ? facingNormal : -facingNormal;
}

[[nodiscard]] glm::vec3 adjustSpecularNormalReference(glm::vec3 shadingNormal, glm::vec3 geometryNormal,
                                                      glm::vec3 viewDirection) noexcept
{
    auto const facingGeometryNormal = facingGeometryNormalReference(geometryNormal, shadingNormal, viewDirection);
    auto const rawNormal = safeNormalizeReference(shadingNormal, facingGeometryNormal);
    auto const safeViewDirection = safeNormalizeReference(viewDirection, facingGeometryNormal);
    auto const incidentDirection = -safeViewDirection;
    auto const reflectedDirection = reflectReference(incidentDirection, rawNormal);
    auto const geometryCosine = glm::dot(reflectedDirection, facingGeometryNormal);

    auto specularNormal = rawNormal;
    if (geometryCosine < 0.0f)
    {
        auto const fallbackReflection = reflectReference(incidentDirection, facingGeometryNormal);
        auto const clippedReflection =
            safeNormalizeReference(reflectedDirection - geometryCosine * facingGeometryNormal, fallbackReflection);
        specularNormal = safeNormalizeReference(clippedReflection - incidentDirection, facingGeometryNormal);
    }

    return glm::dot(specularNormal, safeViewDirection) >= 0.0f ? specularNormal : -specularNormal;
}

[[nodiscard]] bool finite(glm::vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool nearlyEqual(glm::vec3 lhs, glm::vec3 rhs, float epsilon = 1.0e-5f) noexcept
{
    return glm::length(lhs - rhs) <= epsilon;
}

const nr::test::CaseRegistrar lobeSpecificNormalShaderMatrixCase{
    "lobe-specific normal shader contract instantiates all 16 lit material masks", [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/rt/materialLobeSpecificNormalContract"},
        });
        nr::test::require(
            program.valid(),
            "lobe-specific normal contract should compile all physical-layer and anisotropy combinations");
        nr::test::require(program.entryPoint()->spirv != nullptr && !program.entryPoint()->spirv->empty(),
                          "lobe-specific normal contract should expose compute SPIR-V");

        auto layout = nr::rhi::ShaderDescriptorLayout::create(program);
        nr::test::require(layout.valid(), "lobe-specific normal contract descriptor layout should be valid");
        nr::test::require(layout.rootCursor()["gMaterialHeader"].valid() &&
                              layout.rootCursor()["gInstantiationResults"].valid(),
                          "lobe-specific normal contract resources should reflect");
    }};

const nr::test::CaseRegistrar lobeSpecificNormalIdentityAndGrazingCase{
    "lobe-specific normal adjustment preserves identity and clips grazing reflection", [] {
        auto const geometryNormal = glm::vec3{0.0f, 1.0f, 0.0f};
        auto const viewDirection = geometryNormal;

        auto const identity = adjustSpecularNormalReference(geometryNormal, geometryNormal, viewDirection);
        nr::test::require(nearlyEqual(identity, geometryNormal),
                          "matching raw and geometry normals should remain unchanged");

        auto const rawGrazingNormal = safeNormalizeReference(glm::vec3{0.8660254f, 0.5f, 0.0f}, geometryNormal);
        auto const unadjustedReflection = reflectReference(-viewDirection, rawGrazingNormal);
        nr::test::require(glm::dot(unadjustedReflection, geometryNormal) < 0.0f,
                          "reference grazing normal should reflect below the geometric surface");

        auto const adjustedNormal = adjustSpecularNormalReference(rawGrazingNormal, geometryNormal, viewDirection);
        auto const adjustedReflection = reflectReference(-viewDirection, adjustedNormal);
        nr::test::require(finite(adjustedNormal) && std::abs(glm::length(adjustedNormal) - 1.0f) <= 1.0e-5f,
                          "adjusted grazing normal should remain finite and normalized");
        nr::test::require(glm::dot(adjustedReflection, geometryNormal) >= -1.0e-5f,
                          "adjusted ideal reflection should not cross below the geometric surface");
        nr::test::require(glm::dot(adjustedNormal, viewDirection) >= 0.0f,
                          "adjusted microfacet representative should face the view");
    }};

const nr::test::CaseRegistrar lobeSpecificNormalDegenerateAndDoubleSidedCase{
    "lobe-specific normal adjustment handles degenerate projection and double-sided facing", [] {
        auto const upward = glm::vec3{0.0f, 1.0f, 0.0f};
        auto const tangentRawNormal = glm::vec3{1.0f, 0.0f, 0.0f};
        auto const degenerate = adjustSpecularNormalReference(tangentRawNormal, upward, upward);
        nr::test::require(finite(degenerate) && nearlyEqual(degenerate, upward),
                          "zero tangent-plane reflection projection should use the finite geometry-normal fallback");

        auto const storedBackfaceNormal = -upward;
        auto const rawBackfaceShadingNormal = safeNormalizeReference(glm::vec3{0.8660254f, 0.5f, 0.0f}, upward);
        auto const facing = facingGeometryNormalReference(storedBackfaceNormal, rawBackfaceShadingNormal, upward);
        nr::test::require(nearlyEqual(facing, upward),
                          "double-sided BSDF geometry normal should face the current view");

        auto const adjustedFromStoredBackface =
            adjustSpecularNormalReference(rawBackfaceShadingNormal, storedBackfaceNormal, upward);
        auto const adjustedFromFacingNormal = adjustSpecularNormalReference(rawBackfaceShadingNormal, upward, upward);
        nr::test::require(nearlyEqual(adjustedFromStoredBackface, adjustedFromFacingNormal),
                          "stored boundary orientation must not change double-sided specular clipping");
    }};

const nr::test::CaseRegistrar lobeSpecificNormalThresholdCase{
    "lobe-specific normal adjustment remains finite and continuous at the clipping threshold", [] {
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

        auto const below = adjustSpecularNormalReference(rawNormalAt(threshold - delta), geometryNormal, viewDirection);
        auto const at = adjustSpecularNormalReference(rawNormalAt(threshold), geometryNormal, viewDirection);
        auto const above = adjustSpecularNormalReference(rawNormalAt(threshold + delta), geometryNormal, viewDirection);

        nr::test::require(finite(below) && finite(at) && finite(above),
                          "both sides of the clipping threshold should remain finite");
        nr::test::require(
            glm::length(below - at) <= 5.0e-4f && glm::length(above - at) <= 5.0e-4f,
            "specular normal should be continuous as ideal reflection reaches the geometric tangent plane");

        auto const reflectedAbove = reflectReference(-viewDirection, above);
        nr::test::require(glm::dot(reflectedAbove, geometryNormal) >= -1.0e-5f,
                          "the corrected side of the threshold should stay in the geometric hemisphere");
    }};

const nr::test::CaseRegistrar correlatedSmithGeometryCase{
    "GGX uses joint correlated Smith masking and shadowing", [] {
        constexpr auto alphaT = 0.8f;
        constexpr auto alphaB = 0.35f;
        auto const localView = glm::normalize(glm::vec3{0.72f, 0.12f, 0.68f});
        auto const localLight = glm::normalize(glm::vec3{-0.43f, 0.71f, 0.56f});
        auto const correlated = correlatedSmithG2Reference(alphaT, alphaB, localView, localLight);
        auto const separable = separableSmithG2Reference(alphaT, alphaB, localView, localLight);

        nr::test::require(std::isfinite(correlated) && correlated > separable + 1.0e-4f && correlated <= 1.0f,
                          "height correlation should retain more valid rough-lobe energy than independent G1 products");
        nr::test::require(std::abs(correlatedSmithG2Reference(alphaT, alphaB, glm::vec3{0.0f, 0.0f, 1.0f},
                                                              glm::vec3{0.0f, 0.0f, 1.0f}) -
                                   1.0f) <= 1.0e-6f,
                          "joint Smith masking should remain one at normal incidence");
        nr::test::require(std::abs(correlatedSmithG2Reference(alphaT, alphaB, localView,
                                                              glm::vec3{localLight.x, localLight.y, -localLight.z}) -
                                   correlated) <= 1.0e-6f,
                          "the shared correlated G2 helper should use the opposite-side cosine for transmission");
    }};

const nr::test::CaseRegistrar ggxSpecularEnergyCompensationCase{
    "GGX Spec.W restores rough-lobe loss and Spec.E remains bounded", [] {
        auto const terms = ggxSpecularEnergyTermsReference(0.85f, 0.2f, glm::vec3{0.04f, 0.2f, 0.8f}, glm::vec3{1.0f});
        nr::test::require(finite(terms.W) && finite(terms.E) && terms.W.x >= 1.0f && terms.W.y >= 1.0f &&
                              terms.W.z >= 1.0f && terms.E.x >= 0.0f && terms.E.y >= 0.0f && terms.E.z >= 0.0f &&
                              terms.E.x <= 1.0f + 1.0e-5f && terms.E.y <= 1.0f + 1.0e-5f && terms.E.z <= 1.0f + 1.0e-5f,
                          "rough GGX energy terms should be finite, compensating, and energy bounded");

        auto const perfectReflector = ggxSpecularEnergyTermsReference(0.85f, 0.2f, glm::vec3{1.0f}, glm::vec3{1.0f});
        nr::test::require(nearlyEqual(perfectReflector.E, glm::vec3{1.0f}, 1.0e-5f),
                          "Spec.W should restore a Schlick perfect reflector to unit directional albedo");

        auto const smoothTerms = ggxSpecularEnergyTermsReference(0.045f, 1.0f, glm::vec3{0.04f}, glm::vec3{1.0f});
        nr::test::require(nearlyEqual(smoothTerms.W, glm::vec3{1.0f}, 1.0e-5f),
                          "nearly smooth normal-incidence GGX should not receive material compensation");
    }};

const nr::test::CaseRegistrar reflectionDirectionFoldGeometryCase{
    "reflection direction fold is an exterior-preserving mirror involution", [] {
        auto const geometryNormal = glm::vec3{0.0f, 1.0f, 0.0f};
        auto const interiorDirection = safeNormalizeReference(glm::vec3{0.6f, -0.7f, 0.25f}, geometryNormal);
        auto const exteriorDirection = safeNormalizeReference(glm::vec3{-0.35f, 0.8f, 0.48f}, geometryNormal);

        auto const mirrored = mirrorReflectionDirectionReference(geometryNormal, interiorDirection);
        auto const roundTrip = mirrorReflectionDirectionReference(geometryNormal, mirrored);
        nr::test::require(nearlyEqual(roundTrip, interiorDirection),
                          "reflection-plane mirroring should be an involution");
        nr::test::require(glm::dot(geometryNormal, mirrored) > 0.0f &&
                              glm::dot(geometryNormal, interiorDirection) < 0.0f,
                          "mirroring should move an interior reflection direction to the exterior hemisphere");

        auto const foldedInterior = foldReflectionDirectionReference(geometryNormal, interiorDirection);
        auto const foldedExterior = foldReflectionDirectionReference(geometryNormal, exteriorDirection);
        nr::test::require(nearlyEqual(foldedInterior, mirrored) && nearlyEqual(foldedExterior, exteriorDirection),
                          "folding should mirror only directions below the facing geometry plane");

        auto const interiorTangent = interiorDirection - glm::dot(interiorDirection, geometryNormal) * geometryNormal;
        auto const mirroredTangent = mirrored - glm::dot(mirrored, geometryNormal) * geometryNormal;
        nr::test::require(std::abs(glm::length(mirrored) - glm::length(interiorDirection)) <= 1.0e-5f &&
                              nearlyEqual(mirroredTangent, interiorTangent),
                          "reflection-plane mirroring should preserve direction length and tangent components");
    }};

const nr::test::CaseRegistrar reflectionDirectionFoldPdfCase{
    "reflection direction fold preserves normalized push-forward probability", [] {
        constexpr auto cosineStepCount = 256u;
        constexpr auto azimuthStepCount = 512u;
        auto const geometryNormal = glm::vec3{0.0f, 1.0f, 0.0f};
        auto const shadingNormal = safeNormalizeReference(glm::vec3{0.8660254f, 0.5f, 0.0f}, geometryNormal);
        auto const solidAngleStep =
            (2.0 * std::numbers::pi_v<double>) / static_cast<double>(cosineStepCount * azimuthStepCount);
        auto integratedProbability = 0.0;

        std::ranges::for_each(std::views::iota(0u, cosineStepCount), [&](std::uint32_t cosineIndex) {
            auto const geometryCosine = (static_cast<float>(cosineIndex) + 0.5f) / static_cast<float>(cosineStepCount);
            auto const sine = std::sqrt(std::max(1.0f - geometryCosine * geometryCosine, 0.0f));
            std::ranges::for_each(std::views::iota(0u, azimuthStepCount), [&](std::uint32_t azimuthIndex) {
                auto const azimuth = 2.0f * std::numbers::pi_v<float> * (static_cast<float>(azimuthIndex) + 0.5f) /
                                     static_cast<float>(azimuthStepCount);
                auto const exteriorDirection = glm::vec3{
                    sine * std::cos(azimuth),
                    geometryCosine,
                    sine * std::sin(azimuth),
                };
                integratedProbability += static_cast<double>(foldedCosineHemispherePdfReference(
                                             shadingNormal, geometryNormal, exteriorDirection)) *
                                         solidAngleStep;
            });
        });

        nr::test::require(
            std::abs(integratedProbability - 1.0) <= 2.0e-3,
            "the sum of both reflection preimage PDFs should integrate to one over the exterior hemisphere");
        nr::test::require(foldedCosineHemispherePdfReference(shadingNormal, geometryNormal, -geometryNormal) == 0.0f,
                          "the folded reflection PDF should have no support inside the geometry boundary");
    }};
} // namespace
