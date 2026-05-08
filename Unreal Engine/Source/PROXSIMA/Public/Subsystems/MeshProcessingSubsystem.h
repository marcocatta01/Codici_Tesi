#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "MeshProcessingSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMeshProcessingCompleteDelegate);

/**
 * Tracks mesh processing state to ensure simulation doesn't start until
 * mesh distance fields and mesh cards are ready for imported custom 3D models
 */
UCLASS(BlueprintType)
class PROXSIMA_API UMeshProcessingSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // USubsystem interface
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /**
     * Register a static mesh component that needs to be tracked for processing completion
     * @param MeshComponent The mesh component to track
     * @param ComponentName Identifier for the component (for logging)
     */
    UFUNCTION(BlueprintCallable, Category = "Mesh Processing")
    void RegisterMeshComponent(UStaticMeshComponent* MeshComponent, const FString& ComponentName);

    /**
     * Start tracking mesh processing status
     * This should be called after all mesh components are registered
     */
    UFUNCTION(BlueprintCallable, Category = "Mesh Processing")
    void StartMeshProcessingTracking();

    /**
     * Check if all registered mesh components have completed processing
     * @return True if all meshes are ready, false otherwise
     */
    UFUNCTION(BlueprintPure, Category = "Mesh Processing")
    bool AreAllMeshesReady() const;

    /**
     * Get the number of meshes still being processed
     * @return Number of meshes not yet ready
     */
    UFUNCTION(BlueprintPure, Category = "Mesh Processing")
    int32 GetPendingMeshCount() const;

    /**
     * Reset the tracking system for a new simulation
     */
    UFUNCTION(BlueprintCallable, Category = "Mesh Processing")
    void ResetTracking();

    /**
     * Event fired when all mesh processing is complete
     */
    UPROPERTY(BlueprintAssignable, Category = "Mesh Processing")
    FOnMeshProcessingCompleteDelegate OnMeshProcessingComplete;

protected:
    /**
     * Check if a specific mesh component is ready
     * @param MeshComponent The component to check
     * @return True if the mesh distance fields and cards are ready
     */
    bool IsMeshComponentReady(UStaticMeshComponent* MeshComponent) const;

    /**
     * Timer function to periodically check mesh readiness
     */
    void CheckMeshReadiness();

private:
    /** Array of mesh components being tracked */
    UPROPERTY()
    TArray<TWeakObjectPtr<UStaticMeshComponent>> TrackedMeshComponents;

    /** Names of tracked components for logging */
    TArray<FString> ComponentNames;

    /** Timer handle for periodic checking */
    FTimerHandle MeshCheckTimerHandle;

    /** Flag indicating if tracking is active */
    bool bIsTracking;

    /** Number of meshes that were ready in the last check (for change detection) */
    int32 LastReadyCount;

    /** Time when tracking started (for timeout detection) */
    float TrackingStartTime;

    /** Maximum time to wait for mesh processing before giving up (in seconds) */
    static constexpr float MaxTrackingTime = 30.0f;

    /** Interval for checking mesh readiness (in seconds) */
    static constexpr float CheckInterval = 0.1f;
};