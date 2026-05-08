#include "Subsystems/AssetLoadingSubsystem.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "AssimpFunctionLibrary.h"
#include "Engine/Texture2D.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

void UAssetLoadingSubsystem::Initialize(FSubsystemCollectionBase &Collection)
{
    Super::Initialize(Collection);

    StreamableManager = MakeShared<FStreamableManager>();
    bIsLoadingAssets = false;

    UE_LOG(LogTemp, Log, TEXT("AssetLoadingSubsystem initialized"));
}

void UAssetLoadingSubsystem::Deinitialize()
{
    if (StreamableManager.IsValid())
    {
        // Note: FStreamableManager doesn't have CancelAsyncLoading method
        // StreamableManager will be automatically cleaned up
    }

    StreamableManager.Reset();
    bIsLoadingAssets = false;
    LoadedAssetCache.Empty();

    Super::Deinitialize();
}

void UAssetLoadingSubsystem::LoadAssetsForVisualComponents(const TArray<FFMUVisualComponent> &VisualComponents, const FOnAssetsLoadedDelegate &OnComplete)
{
    if (bIsLoadingAssets)
    {
        UE_LOG(LogTemp, Warning, TEXT("Asset loading already in progress"));
        return;
    }

    TArray<FSoftObjectPath> AssetPaths;
    GatherAssetPaths(VisualComponents, AssetPaths);

    if (AssetPaths.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No assets to load for visual components"));
        OnComplete.Broadcast(TArray<UObject *>());
        return;
    }

    bIsLoadingAssets = true;

    // Request async load of all assets
    StreamableManager->RequestAsyncLoad(
        AssetPaths,
        FStreamableDelegate::CreateLambda([this, AssetPaths, OnComplete]()
                                          { this->OnAssetLoadComplete(AssetPaths, OnComplete); }));

    UE_LOG(LogTemp, Log, TEXT("Started async loading of %d assets"), AssetPaths.Num());
}

void UAssetLoadingSubsystem::LoadAssetsForVisualComponents(const TArray<FFMUVisualComponent> &VisualComponents, const FOnAssetsLoadedSingleDelegate &OnComplete)
{
    if (bIsLoadingAssets)
    {
        UE_LOG(LogTemp, Warning, TEXT("Asset loading already in progress"));
        return;
    }

    TArray<FSoftObjectPath> AssetPaths;
    GatherAssetPaths(VisualComponents, AssetPaths);

    if (AssetPaths.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No assets to load for visual components"));
        OnComplete.ExecuteIfBound(TArray<UObject *>());
        return;
    }

    bIsLoadingAssets = true;

    // Request async load of all assets
    StreamableManager->RequestAsyncLoad(
        AssetPaths,
        FStreamableDelegate::CreateLambda([this, AssetPaths, OnComplete]()
                                          { this->OnAssetLoadCompleteSingle(AssetPaths, OnComplete); }));

    UE_LOG(LogTemp, Log, TEXT("Started async loading of %d assets"), AssetPaths.Num());
}

void UAssetLoadingSubsystem::GatherAssetPaths(const TArray<FFMUVisualComponent> &VisualComponents, TArray<FSoftObjectPath> &OutAssetPaths)
{
    TSet<FSoftObjectPath> UniqueAssets;

    // Add common materials
    UniqueAssets.Add(FSoftObjectPath(TEXT("/Game/Materials/M_AssimpBase.M_AssimpBase")));

    // Add mesh paths from visual components
    for (const FFMUVisualComponent &VisualComp : VisualComponents)
    {
        FSoftObjectPath MeshPath = GetMeshPathForShape(VisualComp.type, VisualComp.meshPath);
        UniqueAssets.Add(MeshPath);
    }

    OutAssetPaths = UniqueAssets.Array();
}

void UAssetLoadingSubsystem::OnAssetLoadComplete(const TArray<FSoftObjectPath> &LoadedPaths, const FOnAssetsLoadedDelegate &OnComplete)
{
    bIsLoadingAssets = false;

    TArray<UObject *> LoadedAssets;
    for (const FSoftObjectPath &AssetPath : LoadedPaths)
    {
        if (UObject *LoadedAsset = AssetPath.TryLoad())
        {
            LoadedAssets.Add(LoadedAsset);
            // Cache the loaded asset
            LoadedAssetCache.Add(AssetPath.ToString(), LoadedAsset);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to load asset: %s"), *AssetPath.ToString());
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Asset loading completed. Loaded %d of %d assets"), LoadedAssets.Num(), LoadedPaths.Num());
    OnComplete.Broadcast(LoadedAssets);
}

void UAssetLoadingSubsystem::OnAssetLoadCompleteSingle(const TArray<FSoftObjectPath> &LoadedPaths, const FOnAssetsLoadedSingleDelegate &OnComplete)
{
    bIsLoadingAssets = false;

    TArray<UObject *> LoadedAssets;
    for (const FSoftObjectPath &AssetPath : LoadedPaths)
    {
        if (UObject *LoadedAsset = AssetPath.TryLoad())
        {
            LoadedAssets.Add(LoadedAsset);
            // Cache the loaded asset
            LoadedAssetCache.Add(AssetPath.ToString(), LoadedAsset);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to load asset: %s"), *AssetPath.ToString());
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Asset loading completed. Loaded %d of %d assets"), LoadedAssets.Num(), LoadedPaths.Num());
    OnComplete.ExecuteIfBound(LoadedAssets);
}

FSoftObjectPath UAssetLoadingSubsystem::GetMeshPathForShape(const FString &ShapeType, const FString &MeshPath) const
{
    static const FString BasePath = TEXT("/Engine/BasicShapes/");

    if (ShapeType.Equals(TEXT("mesh"), ESearchCase::IgnoreCase))
    {
        if (!MeshPath.IsEmpty())
        {
            return FSoftObjectPath(MeshPath);
        }
        UE_LOG(LogTemp, Warning, TEXT("Mesh type specified but no mesh path provided, defaulting to sphere"));
        return FSoftObjectPath(BasePath + TEXT("Sphere"));
    }
    else if (ShapeType.Equals(TEXT("sphere"), ESearchCase::IgnoreCase))
    {
        return FSoftObjectPath(BasePath + TEXT("Sphere"));
    }
    else if (ShapeType.Equals(TEXT("box"), ESearchCase::IgnoreCase) ||
             ShapeType.Equals(TEXT("cube"), ESearchCase::IgnoreCase))
    {
        return FSoftObjectPath(BasePath + TEXT("Cube"));
    }
    else if (ShapeType.Equals(TEXT("cylinder"), ESearchCase::IgnoreCase))
    {
        return FSoftObjectPath(BasePath + TEXT("Cylinder"));
    }
    else if (ShapeType.Equals(TEXT("cone"), ESearchCase::IgnoreCase))
    {
        return FSoftObjectPath(BasePath + TEXT("Cone"));
    }

    UE_LOG(LogTemp, Warning, TEXT("Unknown shape type '%s', defaulting to sphere"), *ShapeType);
    return FSoftObjectPath(BasePath + TEXT("Sphere"));
}

UMaterialInstanceDynamic *UAssetLoadingSubsystem::CreateMaterialInstance(UMaterialInterface *BaseMaterial, const FLinearColor &Color)
{
    if (!BaseMaterial)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot create material instance from null base material"));
        return nullptr;
    }

    UMaterialInstanceDynamic *DynMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, nullptr);
    if (DynMaterial)
    {
        DynMaterial->SetVectorParameterValue(TEXT("Color"), Color);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create dynamic material instance"));
    }

    return DynMaterial;
}

UAIScene *UAssetLoadingSubsystem::ImportMeshFromFile(const FString &MeshPath, UWorld *World)
{
    if (!GEngine || !GEngine->IsValidLowLevel())
    {
        UE_LOG(LogTemp, Error, TEXT("GEngine is not valid"));
        return nullptr;
    }

    UAssimpFunctionLibrary *AssimpLibrary = Cast<UAssimpFunctionLibrary>(UAssimpFunctionLibrary::StaticClass()->GetDefaultObject());
    if (!AssimpLibrary)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to get Assimp Function Library instance"));
        return nullptr;
    }

    // Set up the import flags for the mesh
    const int ImportFlags = UAssimpFunctionLibrary::PostProcess_FlipUVs() |
                            UAssimpFunctionLibrary::ProcessPreset_TargetRealtime_MaxQuality();

    // Import the scene from the file
    UAIScene *ImportedScene = AssimpLibrary->ImportScene(MeshPath, World, ImportFlags, false);
    if (!ImportedScene)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to import scene from: %s"), *MeshPath);
        return nullptr;
    }

    UE_LOG(LogTemp, Log, TEXT("Successfully imported scene from: %s"), *MeshPath);
    return ImportedScene;
}

TArray<UMaterialInstanceDynamic *> UAssetLoadingSubsystem::CreateMaterialsFromScene(UAIScene *ImportedScene, UMaterialInterface *BaseMaterial)
{
    TArray<UMaterialInstanceDynamic *> Materials;

    if (!ImportedScene || !BaseMaterial)
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid scene or base material for material creation"));
        return Materials;
    }

    TArray<UAIMaterial *> AllMaterials = ImportedScene->GetAllMaterials();

    for (UAIMaterial *Material : AllMaterials)
    {
        if (Material)
        {
            UMaterialInstanceDynamic *MaterialInstance = UMaterialInstanceDynamic::Create(BaseMaterial, nullptr);
            if (MaterialInstance)
            {
                // Set diffuse texture if available
                if (!SetMaterialTexture(Material, ImportedScene, MaterialInstance, EAiTextureType::AiTextureType_DIFFUSE, TEXT("BaseColor")))
                {
                    UE_LOG(LogTemp, Warning, TEXT("No diffuse texture found for material %s, using default color"), *Material->GetName());
                    MaterialInstance->SetVectorParameterValue("BaseColor", FLinearColor::Red);
                }

                // Set additional textures
                SetMaterialTexture(Material, ImportedScene, MaterialInstance, EAiTextureType::AiTextureType_SPECULAR, TEXT("Specular"));
                SetMaterialTexture(Material, ImportedScene, MaterialInstance, EAiTextureType::AiTextureType_NORMALS, TEXT("Normal"));
                SetMaterialTexture(Material, ImportedScene, MaterialInstance, EAiTextureType::AiTextureType_EMISSIVE, TEXT("Emissive"));
                SetMaterialTexture(Material, ImportedScene, MaterialInstance, EAiTextureType::AiTextureType_METALNESS, TEXT("Metallic"));

                Materials.Add(MaterialInstance);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Created %d materials from scene"), Materials.Num());
    return Materials;
}

bool UAssetLoadingSubsystem::SetMaterialTexture(UAIMaterial *Material, UAIScene *ImportedScene, UMaterialInstanceDynamic *MaterialInstance, EAiTextureType TextureType, FName TextureParameterName)
{
    if (!Material || !ImportedScene || !MaterialInstance)
    {
        return false;
    }

    // Get texture path
    FString TexturePath;
    FVector2D UVScale;
    Material->GetMaterialTexture(TextureType, UVScale, 0, TexturePath, EAiTextureMapping::AiTextureMapping_UV);

    if (!TexturePath.IsEmpty())
    {
        // Try to load embedded texture
        bool bIsNormalTexture = (TextureType == 5); // AiTextureType_NORMALS = 5
        UTexture2D *Texture = ImportedScene->GetEmbeddedTexture(TexturePath, bIsNormalTexture);

        if (Texture)
        {
            MaterialInstance->SetTextureParameterValue(TextureParameterName, Texture);
            UE_LOG(LogTemp, Verbose, TEXT("Set texture parameter %s on material"), *TextureParameterName.ToString());
            return true;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to get embedded texture: %s"), *TexturePath);
        }
    }

    return false;
}