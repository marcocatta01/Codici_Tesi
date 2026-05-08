#include "Subsystems/ActorSpawningSubsystem.h"
#include "Subsystems/AssetLoadingSubsystem.h"
#include "Subsystems/MeshProcessingSubsystem.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "AIScene.h"
#include "AIMesh.h"
#include "AIMaterial.h"


void UActorSpawningSubsystem::Initialize(FSubsystemCollectionBase &Collection)
{
    Super::Initialize(Collection);

    ComponentCounter = 0;

    UE_LOG(LogTemp, Log, TEXT("ActorSpawningSubsystem initialized"));
}

void UActorSpawningSubsystem::Deinitialize()
{
    SpawnedActors.Empty();
    ComponentCounter = 0;

    Super::Deinitialize();
}

void UActorSpawningSubsystem::SpawnSimulationActors(UWorld *World, const TArray<FFMUVisualComponent> &VisualComponents, const TArray<UObject *> &LoadedAssets)
{
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot spawn actors: Invalid world"));
        return;
    }

    for (const FFMUVisualComponent &VisualComp : VisualComponents)
    {
        // Determine the key to use for actor lookup
        FString ActorKey = VisualComp.frameName.IsEmpty() ? VisualComp.name : VisualComp.frameName;
        
        // Check if an actor for this key already exists
        AActor *ExistingActor = SpawnedActors.FindRef(ActorKey);

        if (ExistingActor)
        {
            // Add visual component to existing actor
            UE_LOG(LogTemp, Log, TEXT("Adding visual component '%s' to existing actor: %s"), *VisualComp.name, *ActorKey);
            AddVisualComponentToActor(ExistingActor, VisualComp, LoadedAssets);
        }
        else
        {
            // Create new actor
            AActor *SpawnedActor = SpawnVisualComponentActor(World, VisualComp, LoadedAssets);
            if (SpawnedActor)
            {
                SpawnedActors.Add(ActorKey, SpawnedActor);
                OnActorSpawned.Broadcast(ActorKey, SpawnedActor);
                
                if (VisualComp.frameName.IsEmpty())
                {
                    UE_LOG(LogTemp, Log, TEXT("Spawned static visual component: %s"), *VisualComp.name);
                }
                else
                {
                    UE_LOG(LogTemp, Log, TEXT("Spawned frame-attached visual component '%s' for frame: %s"), *VisualComp.name, *VisualComp.frameName);
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("Failed to spawn actor for visual component: %s"), *VisualComp.name);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Spawned %d unique simulation actors"), SpawnedActors.Num());
}

void UActorSpawningSubsystem::CreateFrameActorsForSensors(UWorld *World, const TArray<FFMUSensorConfig> &SensorConfigs)
{
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot create frame actors: Invalid world"));
        return;
    }

    TSet<FString> FramesNeededBySensors;
    for (const FFMUSensorConfig &SensorConfig : SensorConfigs)
    {
        if (!SensorConfig.frameName.IsEmpty())
        {
            FramesNeededBySensors.Add(SensorConfig.frameName);
        }
    }

    int32 CreatedFrameActors = 0;
    for (const FString &FrameName : FramesNeededBySensors)
    {
        // Only create if not already spawned
        if (!SpawnedActors.Contains(FrameName))
        {
            AActor *FrameActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
            if (FrameActor)
            {
                SetupActorComponents(FrameActor);
                SpawnedActors.Add(FrameName, FrameActor);
                OnActorSpawned.Broadcast(FrameName, FrameActor);
                CreatedFrameActors++;

                UE_LOG(LogTemp, Log, TEXT("Created frame actor for sensor: %s"), *FrameName);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("Failed to spawn frame actor for: %s"), *FrameName);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Created %d frame actors for sensors"), CreatedFrameActors);
}

AActor *UActorSpawningSubsystem::SpawnVisualComponentActor(UWorld *World, const FFMUVisualComponent &VisualComponent, const TArray<UObject *> &LoadedAssets)
{
    if (!World)
    {
        return nullptr;
    }

    // Determine spawn transform
    FTransform SpawnTransform = FTransform::Identity;
    if (VisualComponent.frameName.IsEmpty())
    {
        // For non-frame-attached components, use the rigidTransform as world position
        SpawnTransform = VisualComponent.rigidTransform;
    }

    // Spawn the actor
    AActor *Actor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnTransform);
    if (!Actor)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to spawn actor for: %s"), *VisualComponent.name);
        return nullptr;
    }

    // Set up basic components
    SetupActorComponents(Actor);

    // Handle different types of visual components
    if (VisualComponent.type.Equals(TEXT("mesh"), ESearchCase::IgnoreCase) && !VisualComponent.meshPath.IsEmpty())
    {
        // Import and create mesh from file using Assimp
        if (UAssetLoadingSubsystem *AssetLoadingSubsystem = GetGameInstance()->GetSubsystem<UAssetLoadingSubsystem>())
        {
            UAIScene *ImportedScene = AssetLoadingSubsystem->ImportMeshFromFile(VisualComponent.meshPath, World);
            if (ImportedScene)
            {
                TArray<UStaticMeshComponent *> MeshComponents = CreateAssimpMeshComponents(Actor, VisualComponent, ImportedScene);
                UE_LOG(LogTemp, Log, TEXT("Created %d mesh components from Assimp import for: %s"), MeshComponents.Num(), *VisualComponent.name);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("Failed to import mesh from: %s"), *VisualComponent.meshPath);
            }
        }
    }
    else
    {
        // Create standard mesh component for basic shapes
        UStaticMeshComponent *MeshComponent = CreateStandardMeshComponent(Actor, VisualComponent, LoadedAssets);
        if (!MeshComponent)
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to create mesh component for: %s"), *VisualComponent.name);
        }
    }

    return Actor;
}

UStaticMeshComponent *UActorSpawningSubsystem::CreateStandardMeshComponent(AActor *Actor, const FFMUVisualComponent &VisualComponent, const TArray<UObject *> &LoadedAssets)
{
    if (!Actor)
    {
        return nullptr;
    }

    // Get asset loading subsystem for mesh path resolution
    UAssetLoadingSubsystem *AssetLoadingSubsystem = GetGameInstance()->GetSubsystem<UAssetLoadingSubsystem>();
    if (!AssetLoadingSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("AssetLoadingSubsystem not available"));
        return nullptr;
    }

    // Get mesh path and try to find the loaded mesh
    FSoftObjectPath MeshPath = AssetLoadingSubsystem->GetMeshPathForShape(VisualComponent.type, VisualComponent.meshPath);
    UStaticMesh *Mesh = Cast<UStaticMesh>(FindLoadedAsset(LoadedAssets, MeshPath.ToString()));

    // If not found in loaded assets, try direct loading for Engine basic shapes
    if (!Mesh && MeshPath.ToString().StartsWith(TEXT("/Engine/BasicShapes/")))
    {
        UE_LOG(LogTemp, Warning, TEXT("Basic shape not found in loaded assets, trying direct load: %s"), *MeshPath.ToString());
        Mesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *MeshPath.ToString()));
    }

    if (!Mesh)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to find loaded mesh for: %s"), *VisualComponent.name);
        return nullptr;
    }

    // Create mesh component with unique name
    FString ComponentName = GenerateUniqueComponentName(TEXT("MeshComponent"));
    UStaticMeshComponent *MeshComp = NewObject<UStaticMeshComponent>(Actor, FName(*ComponentName));
    if (!MeshComp)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create mesh component for: %s"), *VisualComponent.name);
        return nullptr;
    }

    // Set up component hierarchy
    MeshComp->AttachToComponent(Actor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
    MeshComp->SetRelativeTransform(VisualComponent.rigidTransform);
    MeshComp->RegisterComponent();
    MeshComp->SetStaticMesh(Mesh);

    // Register this mesh component for processing tracking if it's a custom imported mesh
    if (UMeshProcessingSubsystem* MeshProcessingSubsystem = GetGameInstance()->GetSubsystem<UMeshProcessingSubsystem>())
    {
        MeshProcessingSubsystem->RegisterMeshComponent(MeshComp, VisualComponent.name);
    }

    // Apply dimensions and appearance
    ApplyDimensionsToMesh(MeshComp, VisualComponent);

    // Find and apply material
    UMaterialInterface *BaseMaterial = Cast<UMaterialInterface>(FindLoadedAsset(LoadedAssets, TEXT("/Game/Materials/M_UnlitBasicShape.M_UnlitBasicShape")));


    // If not found in loaded assets, try direct loading for Engine material
    if (!BaseMaterial)
    {
        UE_LOG(LogTemp, Warning, TEXT("BasicShapeMaterial not found in loaded assets, trying direct load"));
        BaseMaterial = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Game/Materials/M_UnlitBasicShape.M_UnlitBasicShape")));
    }

    if (BaseMaterial)
    {
        ApplyAppearanceToMesh(MeshComp, VisualComponent, BaseMaterial);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to find base material for: %s"), *VisualComponent.name);
    }

    return MeshComp;
}

TArray<UStaticMeshComponent *> UActorSpawningSubsystem::CreateAssimpMeshComponents(AActor *Actor, const FFMUVisualComponent &VisualComponent, UAIScene *ImportedScene)
{
    TArray<UStaticMeshComponent *> CreatedComponents;

    if (!Actor || !ImportedScene)
    {
        return CreatedComponents;
    }

    // Get asset loading subsystem for material creation
    UAssetLoadingSubsystem *AssetLoadingSubsystem = GetGameInstance()->GetSubsystem<UAssetLoadingSubsystem>();
    if (!AssetLoadingSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("AssetLoadingSubsystem not available"));
        return CreatedComponents;
    }

    // Get base material for Assimp
    UMaterialInterface *BaseMaterial = Cast<UMaterialInterface>(
        StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Game/Materials/M_AssimpBase.M_AssimpBase")));

    if (!BaseMaterial)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load Assimp base material"));
        return CreatedComponents;
    }

    // Create materials from scene
    TArray<UMaterialInstanceDynamic *> Materials = AssetLoadingSubsystem->CreateMaterialsFromScene(ImportedScene, BaseMaterial);

    // Get scale from dimensions
    const float *Scale = VisualComponent.dimensions.Find(TEXT("scale"));
    FVector ScaleVector = Scale ? FVector(*Scale) : FVector(1.0f);

    // Create static meshes from all meshes in the scene
    TArray<UAIMesh *> AllMeshes = ImportedScene->GetAllMeshes();

    for (UAIMesh *AiMesh : AllMeshes)
    {
        if (!AiMesh)
        {
            continue;
        }

        UStaticMesh *ImportedMesh = AiMesh->GetStaticMesh();
        if (!ImportedMesh)
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to get static mesh from Assimp mesh"));
            continue;
        }

        // Create mesh component
        FString ComponentName = GenerateUniqueComponentName(TEXT("AssimpMesh"));
        UStaticMeshComponent *MeshComp = NewObject<UStaticMeshComponent>(Actor, FName(*ComponentName));
        if (!MeshComp)
        {
            continue;
        }

        MeshComp->SetupAttachment(Actor->GetRootComponent());
        MeshComp->SetRelativeTransform(VisualComponent.rigidTransform);
        MeshComp->SetStaticMesh(ImportedMesh);
        MeshComp->SetRelativeScale3D(ScaleVector);
        MeshComp->RegisterComponent();

        // Register this mesh component for processing tracking
        if (UMeshProcessingSubsystem* MeshProcessingSubsystem = GetGameInstance()->GetSubsystem<UMeshProcessingSubsystem>())
        {
            FString ComponentIdentifier = FString::Printf(TEXT("%s_%s_%d"), *VisualComponent.name, *ComponentName, CreatedComponents.Num());
            MeshProcessingSubsystem->RegisterMeshComponent(MeshComp, ComponentIdentifier);
        }

        // Apply material if available
        int32 MatIndex = AiMesh->GetMaterialIndex();
        if (Materials.IsValidIndex(MatIndex) && Materials[MatIndex])
        {
            MeshComp->SetMaterial(0, Materials[MatIndex]);
            UE_LOG(LogTemp, Verbose, TEXT("Applied material to Assimp mesh: %s"), *VisualComponent.name);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("No material found for Assimp mesh %s at index %d"), *VisualComponent.name, MatIndex);
        }

        CreatedComponents.Add(MeshComp);
    }

    UE_LOG(LogTemp, Log, TEXT("Successfully created %d mesh components from Assimp scene for: %s"), CreatedComponents.Num(), *VisualComponent.name);
    return CreatedComponents;
}

void UActorSpawningSubsystem::ApplyDimensionsToMesh(UStaticMeshComponent *MeshComponent, const FFMUVisualComponent &VisualComponent)
{
    if (!MeshComponent || VisualComponent.dimensions.Num() == 0)
    {
        return;
    }

    FVector Scale = CalculateScaleForShape(VisualComponent.type, VisualComponent.dimensions);
    MeshComponent->SetRelativeScale3D(Scale);

    UE_LOG(LogTemp, Verbose, TEXT("Applied scale to %s: %s"), *VisualComponent.name, *Scale.ToString());
}

void UActorSpawningSubsystem::ApplyAppearanceToMesh(UStaticMeshComponent *MeshComponent, const FFMUVisualComponent &VisualComponent, UMaterialInterface *BaseMaterial)
{
    if (!MeshComponent || !BaseMaterial)
    {
        return;
    }

    // Get asset loading subsystem for material creation
    UAssetLoadingSubsystem *AssetLoadingSubsystem = GetGameInstance()->GetSubsystem<UAssetLoadingSubsystem>();
    if (!AssetLoadingSubsystem)
    {
        return;
    }

    UMaterialInstanceDynamic *DynMaterial = AssetLoadingSubsystem->CreateMaterialInstance(BaseMaterial, VisualComponent.appearance.color);
    if (!DynMaterial)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to create dynamic material for: %s"), *VisualComponent.name);
        return;
    }

    MeshComponent->SetMaterial(0, DynMaterial);

    // Apply opacity if less than 1.0
    if (VisualComponent.appearance.opacity < 1.0f)
    {
        float OpacityValue;
        if (DynMaterial->GetScalarParameterDefaultValue(TEXT("Opacity"), OpacityValue))
        {
            DynMaterial->SetScalarParameterValue(TEXT("Opacity"), VisualComponent.appearance.opacity);
            MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            UE_LOG(LogTemp, Verbose, TEXT("Applied opacity to %s: %f"), *VisualComponent.name, VisualComponent.appearance.opacity);
        }
    }

    // Apply PBR material properties
    float DefaultValue;
    if (DynMaterial->GetScalarParameterDefaultValue(TEXT("Metallic"), DefaultValue))
    {
        DynMaterial->SetScalarParameterValue(TEXT("Metallic"), VisualComponent.appearance.metallic);
    }

    if (DynMaterial->GetScalarParameterDefaultValue(TEXT("Roughness"), DefaultValue))
    {
        DynMaterial->SetScalarParameterValue(TEXT("Roughness"), VisualComponent.appearance.roughness);
    }

    UE_LOG(LogTemp, Verbose, TEXT("Applied appearance to %s"), *VisualComponent.name);
}

AActor *UActorSpawningSubsystem::GetSpawnedActor(const FString &ComponentName) const
{
    return SpawnedActors.FindRef(ComponentName);
}

void UActorSpawningSubsystem::ClearSpawnedActors(UWorld *World)
{
    if (!World)
    {
        return;
    }

    int32 DestroyedCount = 0;
    for (const auto &Pair : SpawnedActors)
    {
        if (AActor *Actor = Pair.Value)
        {
            if (IsValid(Actor))
            {
                Actor->Destroy();
                DestroyedCount++;
            }
        }
    }

    SpawnedActors.Empty();
    ComponentCounter = 0;

    UE_LOG(LogTemp, Log, TEXT("Cleared %d spawned actors"), DestroyedCount);
}

void UActorSpawningSubsystem::SetupActorComponents(AActor *Actor)
{
    if (!Actor)
    {
        return;
    }

    // Create and set up a movable root component if it doesn't exist
    if (!Actor->GetRootComponent())
    {
        USceneComponent *RootComp = NewObject<USceneComponent>(Actor, TEXT("Root"));
        RootComp->SetMobility(EComponentMobility::Movable);
        RootComp->RegisterComponent();
        Actor->SetRootComponent(RootComp);
    }
}

void UActorSpawningSubsystem::AddVisualComponentToActor(AActor *Actor, const FFMUVisualComponent &VisualComponent, const TArray<UObject *> &LoadedAssets)
{
    if (!Actor)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot add visual component to null actor"));
        return;
    }

    // Handle different types of visual components
    if (VisualComponent.type.Equals(TEXT("mesh"), ESearchCase::IgnoreCase) && !VisualComponent.meshPath.IsEmpty())
    {
        // Import and create mesh from file using Assimp
        if (UAssetLoadingSubsystem *AssetLoadingSubsystem = GetGameInstance()->GetSubsystem<UAssetLoadingSubsystem>())
        {
            UAIScene *ImportedScene = AssetLoadingSubsystem->ImportMeshFromFile(VisualComponent.meshPath, Actor->GetWorld());
            if (ImportedScene)
            {
                TArray<UStaticMeshComponent *> MeshComponents = CreateAssimpMeshComponents(Actor, VisualComponent, ImportedScene);
                UE_LOG(LogTemp, Log, TEXT("Added %d mesh components from Assimp import to existing actor: %s"), MeshComponents.Num(), *VisualComponent.name);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("Failed to import mesh from: %s"), *VisualComponent.meshPath);
            }
        }
    }
    else
    {
        // Create standard mesh component for basic shapes
        UStaticMeshComponent *MeshComponent = CreateStandardMeshComponent(Actor, VisualComponent, LoadedAssets);
        if (MeshComponent)
        {
            UE_LOG(LogTemp, Log, TEXT("Added standard mesh component to existing actor: %s"), *VisualComponent.name);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to create mesh component for: %s"), *VisualComponent.name);
        }
    }
}

UObject *UActorSpawningSubsystem::FindLoadedAsset(const TArray<UObject *> &LoadedAssets, const FString &AssetPath) const
{
    for (UObject *Asset : LoadedAssets)
    {
        if (Asset && Asset->GetPathName().Equals(AssetPath, ESearchCase::IgnoreCase))
        {
            return Asset;
        }
    }
    return nullptr;
}

FString UActorSpawningSubsystem::GenerateUniqueComponentName(const FString &BaseName) const
{
    return FString::Printf(TEXT("%s_%d"), *BaseName, const_cast<UActorSpawningSubsystem *>(this)->ComponentCounter++);
}

FVector UActorSpawningSubsystem::CalculateScaleForShape(const FString &ShapeType, const TMap<FString, float> &Dimensions) const
{
    FVector Scale(1.0f);

    if (ShapeType.Equals(TEXT("sphere"), ESearchCase::IgnoreCase))
    {
        const float *Radius = Dimensions.Find(TEXT("radius"));
        Scale = Radius ? FVector((*Radius) * 2.0f) : FVector(1.0f);
    }
    else if (ShapeType.Equals(TEXT("box"), ESearchCase::IgnoreCase) || ShapeType.Equals(TEXT("cube"), ESearchCase::IgnoreCase))
    {
        const float *Length = Dimensions.Find(TEXT("length"));
        const float *Width = Dimensions.Find(TEXT("width"));
        const float *Height = Dimensions.Find(TEXT("height"));
        Scale = FVector(
            Length ? *Length : 1.0f,
            Width ? *Width : 1.0f,
            Height ? *Height : 1.0f);
    }
    else if (ShapeType.Equals(TEXT("cylinder"), ESearchCase::IgnoreCase) || ShapeType.Equals(TEXT("cone"), ESearchCase::IgnoreCase))
    {
        const float *Radius = Dimensions.Find(TEXT("radius"));
        const float *Height = Dimensions.Find(TEXT("height"));
        const float RadiusValue = Radius ? *Radius : 0.5f;
        Scale = FVector(
            RadiusValue * 2.0f,
            RadiusValue * 2.0f,
            Height ? *Height : 1.0f);
    }
    else if (ShapeType.Equals(TEXT("mesh"), ESearchCase::IgnoreCase))
    {
        const float *ScaleValue = Dimensions.Find(TEXT("scale"));
        Scale = ScaleValue ? FVector(*ScaleValue) : FVector(1.0f);
    }

    return Scale;
}
