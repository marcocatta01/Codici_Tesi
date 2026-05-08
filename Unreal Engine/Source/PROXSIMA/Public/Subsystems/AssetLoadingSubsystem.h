#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/StreamableManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "FMUConfiguration.h"
#include "AIScene.h"
#include "AIMesh.h"
#include "AIMaterial.h"
#include "AssetLoadingSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAssetsLoadedDelegate, const TArray<UObject *> &, LoadedAssets);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnAssetsLoadedSingleDelegate, const TArray<UObject *> &, LoadedAssets);

/**
 * Manages async asset loading, 3D model importing, and material creation for PROXSIMA
 * Handles UE4_Assimp integration and provides centralized asset management
 */
UCLASS(BlueprintType)
class PROXSIMA_API UAssetLoadingSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // USubsystem interface
    virtual void Initialize(FSubsystemCollectionBase &Collection) override;
    virtual void Deinitialize() override;

    /**
     * Load assets asynchronously for visual components
     * @param VisualComponents Array of visual components to load assets for
     * @param OnComplete Delegate called when all assets are loaded
     */
    // UFUNCTION(BlueprintCallable, Category = "Asset Loading")
    void LoadAssetsForVisualComponents(const TArray<FFMUVisualComponent> &VisualComponents, const FOnAssetsLoadedDelegate &OnComplete);

    /**
     * Load assets asynchronously for visual components (single delegate version)
     * @param VisualComponents Array of visual components to load assets for
     * @param OnComplete Single delegate called when all assets are loaded
     */
    void LoadAssetsForVisualComponents(const TArray<FFMUVisualComponent> &VisualComponents, const FOnAssetsLoadedSingleDelegate &OnComplete);

    /**
     * Get mesh path for a given shape type
     * @param ShapeType Type of shape (sphere, box, cylinder, cone, mesh)
     * @param MeshPath Custom mesh path for "mesh" type
     * @return Soft object path to the mesh asset
     */
    UFUNCTION(BlueprintPure, Category = "Asset Loading")
    FSoftObjectPath GetMeshPathForShape(const FString &ShapeType, const FString &MeshPath = TEXT("")) const;

    /**
     * Create a dynamic material instance with specified color
     * @param BaseMaterial Base material to instance
     * @param Color Color to apply to the material
     * @return Created dynamic material instance
     */
    UFUNCTION(BlueprintCallable, Category = "Asset Loading")
    UMaterialInstanceDynamic *CreateMaterialInstance(UMaterialInterface *BaseMaterial, const FLinearColor &Color);

    /**
     * Import 3D model using UE4_Assimp plugin
     * @param MeshPath Path to the 3D model file
     * @param World World context for importing
     * @return Imported scene object or nullptr if failed
     */
    UFUNCTION(BlueprintCallable, Category = "Asset Loading")
    class UAIScene *ImportMeshFromFile(const FString &MeshPath, UWorld *World);

    /**
     * Set material texture from Assimp material
     * @param Material Assimp material source
     * @param ImportedScene Scene containing embedded textures
     * @param MaterialInstance Target material instance
     * @param TextureType Type of texture to set (using uint8 to avoid enum conflicts)
     * @param TextureParameterName Parameter name in the material
     * @return True if texture was successfully set
     */
    bool SetMaterialTexture(UAIMaterial *Material, UAIScene *ImportedScene, UMaterialInstanceDynamic *MaterialInstance, EAiTextureType TextureType, FName TextureParameterName);

    /**
     * Create materials from imported scene
     * @param ImportedScene Scene containing material data
     * @param BaseMaterial Base material to use for creating instances
     * @return Array of created dynamic material instances
     */
    UFUNCTION(BlueprintCallable, Category = "Asset Loading")
    TArray<UMaterialInstanceDynamic *> CreateMaterialsFromScene(UAIScene *ImportedScene, UMaterialInterface *BaseMaterial);

    /**
     * Check if assets are currently being loaded
     * @return True if assets are being loaded
     */
    UFUNCTION(BlueprintPure, Category = "Asset Loading")
    bool IsLoadingAssets() const { return bIsLoadingAssets; }

    /**
     * Get the streamable manager for external use
     * @return Shared pointer to the streamable manager
     */
    TSharedPtr<FStreamableManager> GetStreamableManager() const { return StreamableManager; }

private:
    /** Internal callback for async asset loading completion */
    void OnAssetLoadComplete(const TArray<FSoftObjectPath> &LoadedPaths, const FOnAssetsLoadedDelegate &OnComplete);

    /** Internal callback for async asset loading completion (single delegate version) */
    void OnAssetLoadCompleteSingle(const TArray<FSoftObjectPath> &LoadedPaths, const FOnAssetsLoadedSingleDelegate &OnComplete);

    /** Gather all unique asset paths needed for visual components */
    void GatherAssetPaths(const TArray<FFMUVisualComponent> &VisualComponents, TArray<FSoftObjectPath> &OutAssetPaths);

private:
    /** Streamable manager for async loading */
    TSharedPtr<FStreamableManager> StreamableManager;

    /** Flag indicating if assets are currently being loaded */
    bool bIsLoadingAssets;

    /** Cache of loaded assets for reuse */
    UPROPERTY()
    TMap<FString, UObject *> LoadedAssetCache;
};