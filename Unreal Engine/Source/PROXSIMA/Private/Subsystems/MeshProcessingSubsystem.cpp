#include "Subsystems/MeshProcessingSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UMeshProcessingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    bIsTracking = false;
    LastReadyCount = 0;
    TrackingStartTime = 0.0f;

    UE_LOG(LogTemp, Log, TEXT("MeshProcessingSubsystem initialized"));
}

void UMeshProcessingSubsystem::Deinitialize()
{
    // Clear timer
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(MeshCheckTimerHandle);
    }

    ResetTracking();
    Super::Deinitialize();
}

void UMeshProcessingSubsystem::RegisterMeshComponent(UStaticMeshComponent* MeshComponent, const FString& ComponentName)
{
    if (!MeshComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("Attempted to register null mesh component: %s"), *ComponentName);
        return;
    }

    // Only register components with custom imported meshes that need processing
    UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
    if (!StaticMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("Mesh component has no static mesh: %s"), *ComponentName);
        return;
    }

    // Check if this is an imported mesh (not a basic shape from Engine)
    FString MeshPath = StaticMesh->GetPathName();
    if (MeshPath.StartsWith(TEXT("/Engine/BasicShapes/")))
    {
        UE_LOG(LogTemp, Verbose, TEXT("Skipping basic shape mesh: %s"), *ComponentName);
        return;
    }

    TrackedMeshComponents.Add(MeshComponent);
    ComponentNames.Add(ComponentName);

    UE_LOG(LogTemp, Log, TEXT("Registered mesh component for processing tracking: %s"), *ComponentName);
}

void UMeshProcessingSubsystem::StartMeshProcessingTracking()
{
    if (TrackedMeshComponents.Num() == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("No mesh components to track - broadcasting completion immediately"));
        OnMeshProcessingComplete.Broadcast();
        return;
    }

    bIsTracking = true;
    LastReadyCount = 0;
    TrackingStartTime = GetWorld()->GetTimeSeconds();

    UE_LOG(LogTemp, Log, TEXT("Started tracking %d mesh components for processing completion"), TrackedMeshComponents.Num());

    // Start periodic checking with a slight delay to allow mesh building to start
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            MeshCheckTimerHandle,
            this,
            &UMeshProcessingSubsystem::CheckMeshReadiness,
            CheckInterval,
            true, // Loop
            1.0f  // Initial delay of 1 second to allow mesh building to start
        );
    }

    // Don't do immediate check - wait for the timer
}

bool UMeshProcessingSubsystem::AreAllMeshesReady() const
{
    if (!bIsTracking || TrackedMeshComponents.Num() == 0)
    {
        return true;
    }

    for (const TWeakObjectPtr<UStaticMeshComponent>& WeakComponent : TrackedMeshComponents)
    {
        if (UStaticMeshComponent* Component = WeakComponent.Get())
        {
            if (!IsMeshComponentReady(Component))
            {
                return false;
            }
        }
    }

    return true;
}

int32 UMeshProcessingSubsystem::GetPendingMeshCount() const
{
    if (!bIsTracking)
    {
        return 0;
    }

    int32 PendingCount = 0;
    for (const TWeakObjectPtr<UStaticMeshComponent>& WeakComponent : TrackedMeshComponents)
    {
        if (UStaticMeshComponent* Component = WeakComponent.Get())
        {
            if (!IsMeshComponentReady(Component))
            {
                PendingCount++;
            }
        }
    }

    return PendingCount;
}

void UMeshProcessingSubsystem::ResetTracking()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(MeshCheckTimerHandle);
    }

    TrackedMeshComponents.Empty();
    ComponentNames.Empty();
    bIsTracking = false;
    LastReadyCount = 0;
    TrackingStartTime = 0.0f;

    UE_LOG(LogTemp, Log, TEXT("Reset mesh processing tracking"));
}

bool UMeshProcessingSubsystem::IsMeshComponentReady(UStaticMeshComponent* MeshComponent) const
{
    if (!MeshComponent || !IsValid(MeshComponent))
    {
        UE_LOG(LogTemp, Verbose, TEXT("Mesh component invalid - marking as ready"));
        return true; // Consider invalid components as "ready" to avoid blocking
    }

    UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
    if (!StaticMesh)
    {
        UE_LOG(LogTemp, Verbose, TEXT("No static mesh - marking as ready"));
        return true; // No mesh means nothing to process
    }

    // Skip basic engine shapes - they don't need the complex processing pipeline
    FString MeshPath = StaticMesh->GetPathName();
    if (MeshPath.StartsWith(TEXT("/Engine/BasicShapes/")))
    {
        UE_LOG(LogTemp, Verbose, TEXT("Basic engine shape mesh %s - marking as ready"), *MeshPath);
        return true;
    }

    UE_LOG(LogTemp, Verbose, TEXT("Checking readiness for imported mesh: %s"), *MeshPath);

    // Check if the static mesh is still compiling/building
    if (StaticMesh->IsCompiling())
    {
        UE_LOG(LogTemp, Verbose, TEXT("Mesh %s is still compiling - not ready"), *MeshPath);
        return false;
    }

    // Check if render data is available and initialized
    if (!StaticMesh->GetRenderData() || !StaticMesh->GetRenderData()->IsInitialized())
    {
        UE_LOG(LogTemp, Verbose, TEXT("Mesh %s render data not available/initialized - not ready"), *MeshPath);
        return false;
    }

    // Check if LOD0 has valid vertex data (indicates build completion)
    if (StaticMesh->GetNumLODs() == 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("Mesh %s has no LODs - not ready"), *MeshPath);
        return false;
    }

    const FStaticMeshLODResources& LODResource = StaticMesh->GetRenderData()->LODResources[0];
    if (LODResource.GetNumVertices() == 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("Mesh %s LOD0 has no vertices - not ready"), *MeshPath);
        return false;
    }

    // For imported meshes, verify the mesh component has valid bounds after building
    if (!MeshComponent->IsRegistered())
    {
        UE_LOG(LogTemp, Verbose, TEXT("Mesh component %s not registered - not ready"), *MeshPath);
        return false;
    }

    FBoxSphereBounds Bounds = MeshComponent->Bounds;
    if (Bounds.BoxExtent.IsNearlyZero())
    {
        UE_LOG(LogTemp, Verbose, TEXT("Mesh component %s has zero bounds - not ready"), *MeshPath);
        return false;
    }

    // For newly imported meshes, wait longer to ensure distance field and mesh card generation
    // Use a more conservative approach - consider the mesh not ready until some time has passed
    float TimeSinceTracking = GetWorld()->GetTimeSeconds() - TrackingStartTime;
    if (TimeSinceTracking < 2.0f) // Wait at least 2 seconds
    {
        UE_LOG(LogTemp, Verbose, TEXT("Mesh %s still in grace period (%.1fs) - not ready"), *MeshPath, TimeSinceTracking);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("Mesh %s is ready after %.1fs"), *MeshPath, TimeSinceTracking);
    return true;
}

void UMeshProcessingSubsystem::CheckMeshReadiness()
{
    if (!bIsTracking)
    {
        return;
    }

    // Check for timeout
    float CurrentTime = GetWorld()->GetTimeSeconds();
    if ((CurrentTime - TrackingStartTime) > MaxTrackingTime)
    {
        UE_LOG(LogTemp, Warning, TEXT("Mesh processing tracking timed out after %.1f seconds - proceeding anyway"), 
               MaxTrackingTime);
        
        // Stop tracking and broadcast completion
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(MeshCheckTimerHandle);
        }
        bIsTracking = false;
        OnMeshProcessingComplete.Broadcast();
        return;
    }

    int32 ReadyCount = 0;
    int32 TotalCount = TrackedMeshComponents.Num();

    // Clean up invalid weak pointers and count ready meshes
    for (int32 i = TrackedMeshComponents.Num() - 1; i >= 0; i--)
    {
        if (UStaticMeshComponent* Component = TrackedMeshComponents[i].Get())
        {
            if (IsMeshComponentReady(Component))
            {
                ReadyCount++;
            }
        }
        else
        {
            // Remove invalid components
            TrackedMeshComponents.RemoveAt(i);
            if (ComponentNames.IsValidIndex(i))
            {
                ComponentNames.RemoveAt(i);
            }
            TotalCount--;
        }
    }

    // Log progress if there's a change
    if (ReadyCount != LastReadyCount)
    {
        UE_LOG(LogTemp, Log, TEXT("Mesh processing progress: %d/%d meshes ready"), ReadyCount, TotalCount);
        LastReadyCount = ReadyCount;
    }

    // Check if all meshes are ready
    if (ReadyCount >= TotalCount)
    {
        UE_LOG(LogTemp, Log, TEXT("All mesh processing completed! (%d meshes ready)"), ReadyCount);
        
        // Stop tracking
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(MeshCheckTimerHandle);
        }
        bIsTracking = false;

        // Broadcast completion
        OnMeshProcessingComplete.Broadcast();
    }
}