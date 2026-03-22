import std;
import dependency;
import nr.load;
import nr.rhi;
import nr.scene;

namespace
{
[[nodiscard]] std::filesystem::path projectRoot()
{
	return std::filesystem::path{NR_PROJECT_ROOT_DIR};
}

void printSceneAssetSummary(std::string_view label, const nr::load::SceneAsset &sceneAsset)
{
	std::println("[asset] {} source='{}' rootNodeIndex={} nodes={} meshes={} materials={} textures={} vertices={} indices={}",
				 label,
				 sceneAsset.sourcePath.generic_string(),
				 sceneAsset.rootNodeIndex,
				 sceneAsset.stats.nodeCount,
				 sceneAsset.stats.meshCount,
				 sceneAsset.stats.materialCount,
				 sceneAsset.stats.textureCount,
				 sceneAsset.stats.vertexCount,
				 sceneAsset.stats.indexCount);
}

void printSceneStatistics(std::string_view label, const nr::scene::SceneStatistics &stats)
{
	std::println("[stats] {} templates={} instances={} meshAssets={} materialAssets={} textureAssets={}",
				 label,
				 stats.templateCount,
				 stats.instanceCount,
				 stats.meshAssetCount,
				 stats.materialAssetCount,
				 stats.textureAssetCount);
}

[[nodiscard]] auto loadSceneFromRelative(const std::filesystem::path &relativePath) -> std::expected<nr::load::SceneAsset, std::string>
{
	auto absolutePath = projectRoot() / relativePath;
	std::println("[load] source='{}'", absolutePath.generic_string());

	nr::load::SceneLoadRequest request{};
	request.sourcePath = absolutePath;

	auto importResult = nr::load::loadScene(request);
	if (!importResult.has_value())
	{
		auto const &error = importResult.error();
		auto message = std::format("backend='{}' code={} path='{}' message='{}'",
								   error.backend,
								   static_cast<unsigned>(error.code),
								   error.sourcePath.generic_string(),
								   error.message);
		return std::unexpected(message);
	}

	return std::move(importResult.value());
}

[[nodiscard]] bool require(bool condition, std::string_view message)
{
	if (!condition)
	{
		std::println("[fail] {}", message);
		return false;
	}
	return true;
}

[[nodiscard]] bool checkBridgePlanWithRealAsset()
{
	std::println("\n=== Case: checkBridgePlanWithRealAsset ===");
	auto importedScene = loadSceneFromRelative(
		std::filesystem::path{"assets/glTF-Sample-Assets/Models/Triangle/glTF/Triangle.gltf"});
	if (!importedScene.has_value())
	{
		std::println("[fail] loadScene failed: {}", importedScene.error());
		return false;
	}

	auto const &sceneAsset = importedScene.value();
	printSceneAssetSummary("Triangle", sceneAsset);

	auto plan = nr::scene::SceneBridge::buildPlan(sceneAsset);

	std::println("[plan] source='{}' textures={} materials={} meshes={} valid={}",
				 plan.sourcePath.generic_string(),
				 plan.textures.size(),
				 plan.materials.size(),
				 plan.meshes.size(),
				 plan.valid());

	if (!require(plan.valid(), "Bridge plan should be valid for Triangle.gltf"))
	{
		return false;
	}

	if (!require(plan.materials.size() == sceneAsset.materials.size(), "Material bridge entry count mismatch"))
	{
		return false;
	}
	if (!require(plan.meshes.size() == sceneAsset.meshes.size(), "Mesh bridge entry count mismatch"))
	{
		return false;
	}
	if (!require(plan.textures.size() == sceneAsset.textures.size(), "Texture bridge entry count mismatch"))
	{
		return false;
	}

	auto printPreview = [&](std::string_view kind, auto const &entries) {
		auto previewCount = std::min<std::size_t>(entries.size(), 6);
		for (std::size_t i = 0; i < previewCount; ++i)
		{
			std::println("  [plan:{}#{}] sourceIndex={} key='{}'",
						 kind,
						 i,
						 entries[i].sourceIndex,
						 entries[i].canonicalKey);
		}
	};

	printPreview("texture", plan.textures);
	printPreview("material", plan.materials);
	printPreview("mesh", plan.meshes);

	for (std::size_t materialIndex = 0; materialIndex < plan.materials.size(); ++materialIndex)
	{
		auto expected = nr::scene::SceneBridge::makeMaterialCanonicalKey(sceneAsset, static_cast<std::uint32_t>(materialIndex));
		if (!require(plan.materials[materialIndex].canonicalKey == expected, "Material canonical key mismatch"))
		{
			return false;
		}
	}

	for (std::size_t meshIndex = 0; meshIndex < plan.meshes.size(); ++meshIndex)
	{
		auto expected = nr::scene::SceneBridge::makeMeshCanonicalKey(sceneAsset, static_cast<std::uint32_t>(meshIndex));
		if (!require(plan.meshes[meshIndex].canonicalKey == expected, "Mesh canonical key mismatch"))
		{
			return false;
		}
	}

	auto textureKeysNonEmpty = std::ranges::all_of(plan.textures, [](const nr::scene::TextureBridgeInput &entry) {
		return !entry.canonicalKey.empty();
	});
	if (!require(textureKeysNonEmpty, "Texture canonical keys must not be empty"))
	{
		return false;
	}

	return true;
}

[[nodiscard]] bool checkTemplateRegistrationAndAssetRegistry()
{
	std::println("\n=== Case: checkTemplateRegistrationAndAssetRegistry ===");
	auto importedScene = loadSceneFromRelative(
		std::filesystem::path{"assets/glTF-Sample-Assets/Models/DamagedHelmet/glTF/DamagedHelmet.gltf"});
	if (!importedScene.has_value())
	{
		std::println("[fail] loadScene failed: {}", importedScene.error());
		return false;
	}

	auto const &sceneAsset = importedScene.value();
	printSceneAssetSummary("DamagedHelmet", sceneAsset);

	nr::rhi::Device device{};
	nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

	auto templateHandle = scene.registerTemplate(sceneAsset);
	std::println("[template] handle(slot={}, generation={}) valid={}",
				 templateHandle.slot,
				 templateHandle.generation,
				 templateHandle.valid());
	if (!require(templateHandle.valid(), "Template handle must be valid after successful registration"))
	{
		return false;
	}

	auto statistics = scene.statistics();
	printSceneStatistics("after registerTemplate", statistics);

	if (!require(statistics.templateCount == 1, "Template count should be 1 after first registration"))
	{
		return false;
	}

	auto templateRecordOpt = scene.tryGetTemplate(templateHandle);
	if (!require(templateRecordOpt.has_value(), "Template record lookup must succeed"))
	{
		return false;
	}

	auto const &templateRecord = templateRecordOpt->get();
	std::println("[template] stableKey='{}' liveInstances={} meshPins={} materialPins={} texturePins={} prefabAlive={}",
				 templateRecord.stableKey,
				 templateRecord.liveInstanceCount,
				 templateRecord.pins.meshes.size(),
				 templateRecord.pins.materials.size(),
				 templateRecord.pins.textures.size(),
				 templateRecord.prefabRoot.is_alive());

	if (!require(templateRecord.prefabRoot.is_alive(), "Template prefab root must be alive"))
	{
		return false;
	}

	if (!require(!templateRecord.pins.meshes.empty(), "Template mesh pin set should not be empty"))
	{
		return false;
	}
	if (!require(!templateRecord.pins.materials.empty(), "Template material pin set should not be empty"))
	{
		return false;
	}

	auto const templateRefComponent = templateRecord.prefabRoot.try_get<nr::scene::SceneTemplateRef>();
	if (!require(templateRefComponent != nullptr, "Template prefab should carry SceneTemplateRef"))
	{
		return false;
	}
	if (!require(templateRefComponent->handle == templateHandle, "SceneTemplateRef handle mismatch"))
	{
		return false;
	}

	auto firstMeshKey = nr::scene::SceneBridge::makeMeshCanonicalKey(sceneAsset, 0);
	auto meshHandleByKey = scene.findMeshHandleByStableKey(firstMeshKey);
	std::println("[mesh] firstKey='{}' found={}", firstMeshKey, meshHandleByKey.has_value());
	if (!require(meshHandleByKey.has_value(), "First mesh key should be discoverable from registry"))
	{
		return false;
	}

	auto firstMeshRecordOpt = scene.tryGetMeshAsset(*meshHandleByKey);
	if (!require(firstMeshRecordOpt.has_value(), "Mesh record lookup by discovered handle must succeed"))
	{
		return false;
	}

	auto const &firstMeshRecord = firstMeshRecordOpt->get();
	std::println("[mesh] handle(slot={}, generation={}) stableKey='{}' templatePins={}",
				 firstMeshRecord.handle.slot,
				 firstMeshRecord.handle.generation,
				 firstMeshRecord.stableKey,
				 firstMeshRecord.liveTemplatePins);
	if (!require(firstMeshRecord.liveTemplatePins > 0, "Pinned mesh record should have liveTemplatePins > 0"))
	{
		return false;
	}

	auto firstMaterialKey = nr::scene::SceneBridge::makeMaterialCanonicalKey(sceneAsset, 0);
	auto materialHandleByKey = scene.findMaterialHandleByStableKey(firstMaterialKey);
	std::println("[material] firstKey='{}' found={}", firstMaterialKey, materialHandleByKey.has_value());
	if (!require(materialHandleByKey.has_value(), "First material key should be discoverable from registry"))
	{
		return false;
	}

	auto firstTextureKey = nr::scene::SceneBridge::makeTextureCanonicalKey(sceneAsset.textures.front());
	auto textureHandleByKey = scene.findTextureHandleByStableKey(firstTextureKey);
	std::println("[texture] firstKey='{}' found={}", firstTextureKey, textureHandleByKey.has_value());
	if (!require(textureHandleByKey.has_value(), "First texture key should be discoverable from registry"))
	{
		return false;
	}

	auto textureRecordOpt = scene.tryGetTextureAsset(*textureHandleByKey);
	if (!require(textureRecordOpt.has_value(), "Texture record lookup by discovered handle must succeed"))
	{
		return false;
	}
	auto const &textureRecord = textureRecordOpt->get();
	std::println("[texture] handle(slot={}, generation={}) stableKey='{}' templatePins={}",
				 textureRecord.handle.slot,
				 textureRecord.handle.generation,
				 textureRecord.stableKey,
				 textureRecord.liveTemplatePins);

	return true;
}

[[nodiscard]] bool checkTemplateRegistrationIsIdempotent()
{
	std::println("\n=== Case: checkTemplateRegistrationIsIdempotent ===");
	auto importedScene = loadSceneFromRelative(
		std::filesystem::path{"assets/glTF-Sample-Assets/Models/Triangle/glTF/Triangle.gltf"});
	if (!importedScene.has_value())
	{
		std::println("[fail] loadScene failed: {}", importedScene.error());
		return false;
	}

	auto const &sceneAsset = importedScene.value();

	nr::rhi::Device device{};
	nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

	auto firstHandle = scene.registerTemplate(sceneAsset, nr::scene::SceneTemplateCreateInfo{.debugName = "triangleA"});
	auto firstStats = scene.statistics();

	auto secondHandle = scene.registerTemplate(sceneAsset, nr::scene::SceneTemplateCreateInfo{.debugName = "triangleB"});
	auto secondStats = scene.statistics();

	std::println("[template] first(slot={}, gen={}) second(slot={}, gen={}) equal={}",
				 firstHandle.slot,
				 firstHandle.generation,
				 secondHandle.slot,
				 secondHandle.generation,
				 firstHandle == secondHandle);
	printSceneStatistics("first registration", firstStats);
	printSceneStatistics("second registration", secondStats);

	if (!require(firstHandle.valid(), "First template handle must be valid"))
	{
		return false;
	}
	if (!require(secondHandle.valid(), "Second template handle must be valid"))
	{
		return false;
	}
	if (!require(firstHandle == secondHandle, "Duplicate template registration should return existing handle"))
	{
		return false;
	}

	if (!require(firstStats.templateCount == secondStats.templateCount, "Template count should remain unchanged on duplicate registration"))
	{
		return false;
	}
	if (!require(firstStats.meshAssetCount == secondStats.meshAssetCount, "Mesh asset count should remain unchanged on duplicate registration"))
	{
		return false;
	}
	if (!require(firstStats.materialAssetCount == secondStats.materialAssetCount, "Material asset count should remain unchanged on duplicate registration"))
	{
		return false;
	}
	if (!require(firstStats.textureAssetCount == secondStats.textureAssetCount, "Texture asset count should remain unchanged on duplicate registration"))
	{
		return false;
	}

	return true;
}

[[nodiscard]] bool checkInstantiateAndDestroyLifecycle()
{
	std::println("\n=== Case: checkInstantiateAndDestroyLifecycle ===");
	auto importedScene = loadSceneFromRelative(
		std::filesystem::path{"assets/glTF-Sample-Assets/Models/Triangle/glTF/Triangle.gltf"});
	if (!importedScene.has_value())
	{
		std::println("[fail] loadScene failed: {}", importedScene.error());
		return false;
	}

	auto const &sceneAsset = importedScene.value();

	nr::rhi::Device device{};
	nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

	auto templateHandle = scene.registerTemplate(sceneAsset);
	if (!require(templateHandle.valid(), "Template handle must be valid before instantiation"))
	{
		return false;
	}

	auto firstInstance = scene.instantiate(templateHandle);
	auto secondInstance = scene.instantiate(templateHandle, nr::scene::SceneInstantiateInfo{.activate = false});

	std::println("[instance] first(slot={}, gen={}, valid={}) second(slot={}, gen={}, valid={})",
				 firstInstance.slot,
				 firstInstance.generation,
				 firstInstance.valid(),
				 secondInstance.slot,
				 secondInstance.generation,
				 secondInstance.valid());

	if (!require(firstInstance.valid(), "First instance handle should be valid"))
	{
		return false;
	}
	if (!require(secondInstance.valid(), "Second instance handle should be valid"))
	{
		return false;
	}

	auto firstInstanceRecord = scene.tryGetInstance(firstInstance);
	auto secondInstanceRecord = scene.tryGetInstance(secondInstance);
	if (!require(firstInstanceRecord.has_value(), "First instance record should exist"))
	{
		return false;
	}
	if (!require(secondInstanceRecord.has_value(), "Second instance record should exist"))
	{
		return false;
	}

	auto const &firstRoot = firstInstanceRecord->get().root;
	auto const &secondRoot = secondInstanceRecord->get().root;
	std::println("[instance] firstRootAlive={} secondRootAlive={} firstActive={} secondActive={}",
				 firstRoot.is_alive(),
				 secondRoot.is_alive(),
				 firstInstanceRecord->get().active,
				 secondInstanceRecord->get().active);

	if (!require(firstRoot.is_alive(), "First instance root entity should be alive"))
	{
		return false;
	}
	if (!require(secondRoot.is_alive(), "Second instance root entity should be alive"))
	{
		return false;
	}

	auto firstRef = firstRoot.try_get<nr::scene::SceneInstanceRef>();
	auto secondRef = secondRoot.try_get<nr::scene::SceneInstanceRef>();
	if (!require(firstRef != nullptr, "First root should carry SceneInstanceRef"))
	{
		return false;
	}
	if (!require(secondRef != nullptr, "Second root should carry SceneInstanceRef"))
	{
		return false;
	}

	if (!require(firstRef->templateHandle == templateHandle, "First SceneInstanceRef.templateHandle mismatch"))
	{
		return false;
	}
	if (!require(secondRef->templateHandle == templateHandle, "Second SceneInstanceRef.templateHandle mismatch"))
	{
		return false;
	}

	auto templateRecordOpt = scene.tryGetTemplate(templateHandle);
	if (!require(templateRecordOpt.has_value(), "Template record should exist"))
	{
		return false;
	}

	std::println("[template] liveInstanceCount={} (expected 2)", templateRecordOpt->get().liveInstanceCount);
	if (!require(templateRecordOpt->get().liveInstanceCount == 2, "Template liveInstanceCount should be 2 after two instantiations"))
	{
		return false;
	}

	auto statisticsBeforeDestroy = scene.statistics();
	printSceneStatistics("before destroy", statisticsBeforeDestroy);
	if (!require(statisticsBeforeDestroy.instanceCount == 2, "Instance count should be 2 before destroy"))
	{
		return false;
	}

	auto firstRootCopy = firstRoot;
	scene.destroyInstance(firstInstance);

	auto statisticsAfterFirstDestroy = scene.statistics();
	printSceneStatistics("after first destroy", statisticsAfterFirstDestroy);
	if (!require(statisticsAfterFirstDestroy.instanceCount == 1, "Instance count should be 1 after first destroy"))
	{
		return false;
	}
	if (!require(!firstRootCopy.is_alive(), "Destroyed first instance root should not be alive"))
	{
		return false;
	}

	templateRecordOpt = scene.tryGetTemplate(templateHandle);
	if (!require(templateRecordOpt.has_value(), "Template record should still exist after destroy"))
	{
		return false;
	}
	std::println("[template] liveInstanceCount={} (expected 1)", templateRecordOpt->get().liveInstanceCount);
	if (!require(templateRecordOpt->get().liveInstanceCount == 1, "Template liveInstanceCount should be 1 after first destroy"))
	{
		return false;
	}

	scene.destroyInstance(secondInstance);
	auto statisticsAfterSecondDestroy = scene.statistics();
	printSceneStatistics("after second destroy", statisticsAfterSecondDestroy);
	if (!require(statisticsAfterSecondDestroy.instanceCount == 0, "Instance count should be 0 after second destroy"))
	{
		return false;
	}

	scene.destroyInstance(secondInstance);
	auto statisticsAfterNoOpDestroy = scene.statistics();
	printSceneStatistics("after duplicate destroy", statisticsAfterNoOpDestroy);
	if (!require(statisticsAfterNoOpDestroy.instanceCount == 0, "Destroying unknown instance should be no-op"))
	{
		return false;
	}

	return true;
}

[[nodiscard]] bool checkDiagnosticsFailurePath()
{
	std::println("\n=== Case: checkDiagnosticsFailurePath ===");

	nr::rhi::Device device{};
	nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

	nr::load::SceneAsset invalidAsset{};
	invalidAsset.sourcePath.clear();

	auto failedHandle = scene.registerTemplate(invalidAsset);

	std::println("[template] failedHandle(slot={}, gen={}, valid={})",
				 failedHandle.slot,
				 failedHandle.generation,
				 failedHandle.valid());

	if (!require(!failedHandle.valid(), "registerTemplate must fail when both sourcePath and stableKey are empty"))
	{
		return false;
	}

	nr::load::SceneAsset manualKeyAsset{};
	auto manualHandle = scene.registerTemplate(manualKeyAsset, nr::scene::SceneTemplateCreateInfo{
																   .debugName = "manual_scene",
																   .stableKey = "manual://scene/template",
															   });
	std::println("[template] manualHandle(slot={}, gen={}, valid={})",
				 manualHandle.slot,
				 manualHandle.generation,
				 manualHandle.valid());

	if (!require(manualHandle.valid(), "registerTemplate should succeed when explicit stableKey is provided"))
	{
		return false;
	}

	auto templateRecord = scene.tryGetTemplate(manualHandle);
	if (!require(templateRecord.has_value(), "Manual-key template record should exist"))
	{
		return false;
	}

	std::println("[template] manual stableKey='{}'", templateRecord->get().stableKey);
	if (!require(templateRecord->get().stableKey == "manual://scene/template", "Manual stableKey mismatch"))
	{
		return false;
	}

	return true;
}

} // namespace

int main()
{
	auto const cases = std::array{
		std::pair{"checkBridgePlanWithRealAsset", &checkBridgePlanWithRealAsset},
		std::pair{"checkTemplateRegistrationAndAssetRegistry", &checkTemplateRegistrationAndAssetRegistry},
		std::pair{"checkTemplateRegistrationIsIdempotent", &checkTemplateRegistrationIsIdempotent},
		std::pair{"checkInstantiateAndDestroyLifecycle", &checkInstantiateAndDestroyLifecycle},
		std::pair{"checkDiagnosticsFailurePath", &checkDiagnosticsFailurePath},
	};

	std::size_t passedCount = 0;
	for (auto const &[name, fn] : cases)
	{
		std::println("\n[run] {}", name);
		auto const ok = fn();
		std::println("[result] {} => {}", name, ok ? "PASS" : "FAIL");
		if (ok)
		{
			++passedCount;
		}
	}

	std::println("\n[summary] passed={} failed={}", passedCount, cases.size() - passedCount);
	if (passedCount != cases.size())
	{
		return 1;
	}

	return 0;
}
