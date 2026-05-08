#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "FMUConfiguration.h"
#include "ActorSpawningSubsystem.generated.h"

// Forward declarations
class UStaticMesh;
class UMaterialInstanceDynamic;
class UAIScene;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSimulationActorSpawned, const FString&, ComponentName, AActor*, SpawnedActor);

/**
 * Manages actor spawning, visual component creation, and frame actor management
 * Handles the creation of simulation objects based on configuration
 */
UCLASS(BlueprintType)
class PROXSIMA_API UActorSpawningSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // USubsystem interface
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /**
     * Spawn simulation actors based on visual components configuration
     * @param World World to spawn actors in
     * @param VisualComponents Array of visual components to create
     * @param LoadedAssets Array of pre-loaded assets to use
     */
    UFUNCTION(BlueprintCallable, Category = "Actor Spawning")
    void SpawnSimulationActors(UWorld* World, const TArray<FFMUVisualComponent>& VisualComponents, const TArray<UObject*>& LoadedAssets);

    /**
     * Create frame actors for sensors that don't have associated visual components
     * @param World World to spawn actors in
     * @param SensorConfigs Array of sensor configurations
     */
    UFUNCTION(BlueprintCallable, Category = "Actor Spawning")
    void CreateFrameActorsForSensors(UWorld* World, const TArray<FFMUSensorConfig>& SensorConfigs);

    /**
     * Spawn a single visual component actor
     * @param World World to spawn the actor in
     * @param VisualComponent Visual component configuration
     * @param LoadedAssets Available loaded assets
     * @return Spawned actor or nullptr if failed
     */
    UFUNCTION(BlueprintCallable, Category = "Actor Spawning")
    AActor* SpawnVisualComponentActor(UWorld* World, const FFMUVisualComponent& VisualComponent, const TArray<UObject*>& LoadedAssets);

    /**
     * Create mesh component for standard shapes (sphere, box, cylinder, etc.)
     * @param Actor Parent actor
     * @param VisualComponent Visual component configuration
     * @param LoadedAssets Available loaded assets
     * @return Created mesh component or nullptr if failed
     */
    UFUNCTION(BlueprintCallable, Category = "Actor Spawning")
    UStaticMeshComponent* CreateStandardMeshComponent(AActor* Actor, const FFMUVisualComponent& VisualComponent, const TArray<UObject*>& LoadedAssets);

    /**
     * Create mesh components from imported 3D models
     * @param Actor Parent actor
     * @param VisualComponent Visual component configuration
     * @param ImportedScene Imported Assimp scene
     * @return Array of created mesh components
     */
    UFUNCTION(BlueprintCallable, Category = "Actor Spawning")
    TArray<UStaticMeshComponent*> CreateAssimpMeshComponents(AActor* Actor, const FFMUVisualComponent& VisualComponent, UAIScene* ImportedScene);

    /**
     * Apply dimensions and scaling to a mesh component
     * @param MeshComponent Component to scale
     * @param VisualComponent Visual component configuration containing dimensions
     */
    UFUNCTION(BlueprintCallable, Category = "Actor Spawning")
    void ApplyDimensionsToMesh(UStaticMeshComponent* MeshComponent, const FFMUVisualComponent& VisualComponent);

    /**
     * Apply appearance settings (color, opacity, material properties) to a mesh
     * @param MeshComponent Component to apply appearance to
     * @param VisualComponent Visual component configuration containing appearance
     * @param BaseMaterial Base material to use for creating dynamic instance
     */
    UFUNCTION(BlueprintCallable, Category = "Actor Spawning")
    void ApplyAppearanceToMesh(UStaticMeshComponent* MeshComponent, const FFMUVisualComponent& VisualComponent, class UMaterialInterface* BaseMaterial);

    /**
     * Get spawned actor by component name
     * @param ComponentName Name of the component
     * @return Actor associated with the component name
     */
    UFUNCTION(BlueprintPure, Category = "Actor Spawning")
    AActor* GetSpawnedActor(const FString& ComponentName) const;

    /**
     * Get all spawned actors
     * @return Map of component names to spawned actors
     */
    UFUNCTION(BlueprintPure, Category = "Actor Spawning")
    const TMap<FString, AActor*>& GetAllSpawnedActors() const { return SpawnedActors; }

    /**
     * Clear all spawned actors from the world
     * @param World World to clear actors from
     */
    UFUNCTION(BlueprintCallable, Category = "Actor Spawning")
    void ClearSpawnedActors(UWorld* World);

public:
    /** Event fired when an actor is spawned */
    UPROPERTY(BlueprintAssignable, Category = "Actor Spawning|Events")
    FOnSimulationActorSpawned OnActorSpawned;

private:
    /** Create a basic scene component hierarchy for an actor */
    void SetupActorComponents(AActor* Actor);

    /** Add a visual component to an existing actor */
    void AddVisualComponentToActor(AActor* Actor, const FFMUVisualComponent& VisualComponent, const TArray<UObject*>& LoadedAssets);

    /** Find a loaded asset by path */
    UObject* FindLoadedAsset(const TArray<UObject*>& LoadedAssets, const FString& AssetPath) const;

    /** Generate unique component name */
    FString GenerateUniqueComponentName(const FString& BaseName) const;

    /** Calculate scale vector for different shape types */
    FVector CalculateScaleForShape(const FString& ShapeType, const TMap<FString, float>& Dimensions) const;

private:
    /** Map of component names to spawned actors */
    UPROPERTY()
    TMap<FString, AActor*> SpawnedActors;

    /** Counter for generating unique component names */
    int32 ComponentCounter;
};