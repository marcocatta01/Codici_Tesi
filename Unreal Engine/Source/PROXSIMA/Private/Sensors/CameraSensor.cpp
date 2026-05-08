#include "Sensors/CameraSensor.h"
#include "Sensors/SensorsTypes.h"
#include "PROXSIMAGameInstance.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "WebSocketManager.h"
#include "FMUSimulationSubsystem.h"
#include "FMUhandling/SequenceManager.h"
#include "Async/Async.h"
#include "UObject/WeakObjectPtr.h"

ACameraSensor::ACameraSensor()
    : Resolution(1280, 720), FieldOfView(90.0f), NearClipPlane(0.01f), FarClipPlane(1000.0f),
      CaptureRate(0.0f), bIsEnabled(true), TimeSinceLastCapture(0.0f), LastCaptureIndex(-1), bSaveImages(false),
      bStreamImages(false), StreamingPort(0), bRequireClientConnection(false), CurrentPoseIndex(0)
{
    PrimaryActorTick.bCanEverTick = true;

    // Create root component
    USceneComponent *Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);

    // Create camera capture component
    CaptureComponent = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("CaptureComponent"));
    CaptureComponent->SetupAttachment(Root);
    CaptureComponent->bCaptureEveryFrame = false;
    CaptureComponent->bCaptureOnMovement = false;
    CaptureComponent->bAlwaysPersistRenderingState = true;
    CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
}

void ACameraSensor::BeginPlay()
{
    Super::BeginPlay();

    // Reset sequence pose index
    CurrentPoseIndex = 0;

    // Create render target if it doesn't exist
    if (!RenderTarget)
    {
        RenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(
            this,
            Resolution.X,
            Resolution.Y,
            RTF_RGBA8,
            FLinearColor::Black);

        if (RenderTarget)
        {
            CaptureComponent->TextureTarget = RenderTarget;
        }
    }
}

void ACameraSensor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Clean up render target
    if (RenderTarget)
    {
        UKismetRenderingLibrary::ReleaseRenderTarget2D(RenderTarget);
        RenderTarget = nullptr;
    }

    Super::EndPlay(EndPlayReason);
}

void ACameraSensor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    UPROXSIMAGameInstance *GI = Cast<UPROXSIMAGameInstance>(GetGameInstance());
    if (GI->GetIsSimulating() && bIsEnabled)
    {
        Update_Implementation(DeltaTime);
    }
}

void ACameraSensor::CaptureImageWithPose(const FSerializedCameraPose &Pose)
{
    static FString TempStr;

    if (!bIsEnabled || !RenderTarget || bIsCapturing)
    {
        TempStr = FString::FromInt(static_cast<int32>(Pose.Timestamp * 1000.0f));
        TempStr = FString::Printf(TEXT("%s.%03d"), *TempStr.Left(TempStr.Len() - 3), FCString::Atoi(*TempStr.Right(3)));
        OnCaptureCompleted.Broadcast(false, TempStr);
        return;
    }

    // Store current pose for file naming
    CurrentPose = Pose;

    // Apply the pose
    ApplyPose(Pose);

    // Start capture
    bIsCapturing = true;
    CaptureComponent->CaptureScene();

    // If we need to save the image, do it now
    if (bSaveImages && !OutputPath.IsEmpty())
    {
        SaveSensorData();
    }

    // Complete the capture
    HandleCaptureComplete(true, Pose);
}

FString ACameraSensor::GetCaptureFilename(const FSerializedCameraPose& Pose) const
{
    // Usa l'indice della pose corrente (1-based) per il nome file
    const int32 FrameNumber = CurrentPoseIndex + 1;
    return FPaths::Combine(
        OutputPath,
        FString::Printf(TEXT("%s_%06d.png"), *SensorName, FrameNumber)
    );
}

void ACameraSensor::HandleCaptureComplete(bool bSuccess, const FSerializedCameraPose &Pose)
{
    if (!bSuccess)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to capture image for camera %s at timestamp %0.3f"),
            *SensorName, Pose.Timestamp);
    }
    else
    {
        if (!Pose.Description.IsEmpty())
        {
            UE_LOG(LogTemp, Log, TEXT("Captured image for camera %s at timestamp %0.3f: %s"),
                *SensorName, Pose.Timestamp, *Pose.Description);
        }

        // Report capture to sequence manager
        UPROXSIMAGameInstance *GameInstance = Cast<UPROXSIMAGameInstance>(GetGameInstance());
        if (GameInstance && GameInstance->GetCurrentSimulationMode() == ESimulationMode::SM_Sequence)
        {
            if (USequenceManager *SequenceManager = GameInstance->GetSequenceManager())
            {
                SequenceManager->ReportImageCaptured();
            }
        }
    }

    bIsCapturing = false;
    FString StrTimestamp = FString::SanitizeFloat(Pose.Timestamp).Replace(TEXT("."), TEXT("_"));
    OnCaptureCompleted.Broadcast(bSuccess, StrTimestamp);
}

void ACameraSensor::ApplyPose(const FSerializedCameraPose &Pose)
{
    // Apply pose transform with immediate teleport - no interpolation
    SetActorTransform(Pose.Transform, false, nullptr, ETeleportType::None);
}

bool ACameraSensor::Initialize_Implementation(const FString &InSensorName, const FString& InFrameName,
                                              const FTransform &InRelativeTransform,
                                              const TMap<FString, FString> &Parameters)
{
    SensorName = InSensorName;
    FrameName = InFrameName;
    RelativeTransform = InRelativeTransform;

    // Store sensor parameters
    SensorParameters = Parameters;

    // Parse requireClientConnection (default to false)
    bRequireClientConnection = false;
    if (Parameters.Contains(TEXT("requireClientConnection")))
    {
        FString RequireConnectionStr = Parameters[TEXT("requireClientConnection")];
        bRequireClientConnection = RequireConnectionStr.ToBool();
    }

    // Parse camera-specific parameters
    if (Parameters.Contains(TEXT("resolution")))
    {
        FString ResolutionStr = Parameters[TEXT("resolution")];
        TArray<FString> Dimensions;
        ResolutionStr.ParseIntoArray(Dimensions, TEXT(","), true);

        if (Dimensions.Num() >= 2)
        {
            Resolution.X = FCString::Atoi(*Dimensions[0]);
            Resolution.Y = FCString::Atoi(*Dimensions[1]);
        }
    }

    if (Parameters.Contains(TEXT("fov")))
    {
        FieldOfView = FCString::Atof(*Parameters[TEXT("fov")]);
    }

    if (Parameters.Contains(TEXT("nearClipPlane")))
    {
        NearClipPlane = FCString::Atof(*Parameters[TEXT("nearClipPlane")]);
    }

    if (Parameters.Contains(TEXT("farClipPlane")))
    {
        FarClipPlane = FCString::Atof(*Parameters[TEXT("farClipPlane")]);
    }

    if (Parameters.Contains(TEXT("captureRate")))
    {
        CaptureRate = FCString::Atof(*Parameters[TEXT("captureRate")]);
    }

    // Configure camera properties
    CaptureComponent->FOVAngle = FieldOfView;
    CaptureComponent->OrthoWidth = Resolution.X;
    CaptureComponent->PostProcessSettings.bOverride_DepthOfFieldNearBlurSize = true;
    CaptureComponent->PostProcessSettings.DepthOfFieldNearBlurSize = NearClipPlane;
    CaptureComponent->PostProcessSettings.bOverride_DepthOfFieldFarBlurSize = true;
    CaptureComponent->PostProcessSettings.DepthOfFieldFarBlurSize = FarClipPlane;

    // Create render target with specified resolution
    if (Resolution.X > 0 && Resolution.Y > 0)
    {
        RenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(
            this,
            Resolution.X,
            Resolution.Y,
            RTF_RGBA8,
            FLinearColor::Black);

        if (RenderTarget)
        {
            CaptureComponent->TextureTarget = RenderTarget;
            return true;
        }
    }

    return false;
}

void ACameraSensor::Update_Implementation(float DeltaTime)
{
    if (!bIsEnabled)
        return;

    UPROXSIMAGameInstance *ProxSimaGameInstance = Cast<UPROXSIMAGameInstance>(GetGameInstance());
    if (!ProxSimaGameInstance)
        return;

    ESimulationMode CurrentMode = ProxSimaGameInstance->GetCurrentSimulationMode();

    if (CurrentMode == ESimulationMode::SM_Sequence)
    {
        // Handle sequence mode - get poses from sequence manager
        if (USequenceManager* SequenceManager = ProxSimaGameInstance->GetSequenceManager())
        {
            // Get poses specifically for this camera
            TArray<FSerializedCameraPose> CameraPoses = SequenceManager->GetPosesForCamera(SensorName);

            // Process remaining poses for this camera
            while (CurrentPoseIndex < CameraPoses.Num())
            {
                const FSerializedCameraPose &Pose = CameraPoses[CurrentPoseIndex];
                CaptureImageWithPose(Pose);
                CurrentPoseIndex++;
            }
        }
    }
    else if (CurrentMode == ESimulationMode::SM_OnDemand)
    {
        // OnDemand mode: Do not perform automatic captures
        // Images are only captured when explicitly requested via WebSocket commands
        // The OnDemandCaptureManager will call CaptureImageWithPose() when needed
        return;
    }
    else // FMU mode
    {
        // Handle capture rate timing using FMU simulation time
        bool bCapture = false;
        if (CaptureRate > 0.0f) // If capture rate is set
        {
            // Use FMU simulation time instead of world time
            float CurrentTime = 0.0f;
            UPROXSIMAGameInstance* GameInstance = Cast<UPROXSIMAGameInstance>(GetGameInstance());
            if (GameInstance)
            {
                UFMUSimulationSubsystem* FMUSubsystem = GameInstance->GetSubsystem<UFMUSimulationSubsystem>();
                if (FMUSubsystem)
                {
                    CurrentTime = static_cast<float>(FMUSubsystem->GetSimulationTime());
                }
            }

            float CaptureInterval = 1.0f / CaptureRate;

            // Calculate the next exact timestamp for capture
            int32 PreviousCaptureIndex = FMath::FloorToInt(CurrentTime / CaptureInterval);
            int32 NextCaptureIndex = PreviousCaptureIndex + 1;
            float NextCaptureTime = NextCaptureIndex * CaptureInterval;

            // Calculate the last capture time we performed
            float LastCaptureTime = LastCaptureIndex * CaptureInterval;

            // If current time exceeds the next capture time or if we haven't captured at this index yet
            if (CurrentTime >= NextCaptureTime || (PreviousCaptureIndex > LastCaptureIndex))
            {
                bCapture = true;
                // Store the index we're capturing at for filename and next capture determination
                LastCaptureIndex = FMath::Max(PreviousCaptureIndex, LastCaptureIndex + 1);
            }
        }
        else // No capture rate specified, capture every frame
        {
            bCapture = true;
        }

        if (bCapture)
        {
            CaptureImage();
            // Save image if output path is set and we captured one
            if (bSaveImages && !OutputPath.IsEmpty())
            {
                SaveSensorData();
            }
        }
    }
}

void ACameraSensor::SetEnabled_Implementation(bool bEnabled)
{
    bIsEnabled = bEnabled;
    CaptureComponent->SetVisibility(bEnabled);
    CaptureComponent->SetActive(bEnabled);
}

bool ACameraSensor::IsEnabled_Implementation() const
{
    return bIsEnabled;
}

FString ACameraSensor::GetSensorName_Implementation() const
{
    return SensorName;
}

FString ACameraSensor::GetSensorType_Implementation() const
{
    return TEXT("Camera");
}

FString ACameraSensor::GetFrameName_Implementation() const
{
    return FrameName;
}

FTransform ACameraSensor::GetRelativeTransform_Implementation() const
{
    return RelativeTransform;
}

void ACameraSensor::CaptureImage()
{
    if (bIsEnabled && RenderTarget)
    {
        CaptureComponent->CaptureScene();
    }
}

void ACameraSensor::SetOutputPath_Implementation(const FString &Path)
{
    if (Path.IsEmpty())
    {
        OutputPath = Path;
        bSaveImages = false;
        return;
    }

    // Check if the path is absolute or relative
    if (FPaths::IsRelative(Path))
    {
        // Convert relative path to absolute using project root
        FString ProjectDir = FPaths::ProjectDir();
        FString AbsolutePath = FPaths::ConvertRelativePathToFull(ProjectDir, Path);
        FPaths::CollapseRelativeDirectories(AbsolutePath);
        OutputPath = AbsolutePath;
    }
    else
    {
        // Path is already absolute
        OutputPath = Path;
    }

    // Create a timestamp folder name (format: YYYY-MM-DD_HH-MM-SS)
    FDateTime Now = FDateTime::Now();
    FString TimeStampFolder = Now.ToString(TEXT("%Y-%m-%d_%H-%M-%S"));

    // Combine base path with timestamp folder
    OutputPath = FPaths::Combine(OutputPath, TimeStampFolder);

    // Ensure the path ends with a separator
    if (!OutputPath.EndsWith(TEXT("/")))
    {
        OutputPath += TEXT("/");
    }

    // Create the directory if it doesn't exist
    FString Directory = FPaths::GetPath(OutputPath);
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*Directory))
    {
        PlatformFile.CreateDirectoryTree(*Directory);
    }

    bSaveImages = !OutputPath.IsEmpty();
}

bool ACameraSensor::SaveSensorData_Implementation()
{
    if (!bSaveImages || OutputPath.IsEmpty() || !RenderTarget)
    {
        return false;
    }

    // Determine the save path
    FString SavePath;
    UPROXSIMAGameInstance *ProxSimaGameInstance = Cast<UPROXSIMAGameInstance>(GetGameInstance());
    if (ProxSimaGameInstance && ProxSimaGameInstance->GetCurrentSimulationMode() == ESimulationMode::SM_FMU) // FMU mode
    {
        // Use FMU simulation time instead of world time
        float SimTime = 0.0f;

        // Get FMU simulation time from the subsystem
        UPROXSIMAGameInstance* GameInstance = Cast<UPROXSIMAGameInstance>(GetGameInstance());
        if (GameInstance)
        {
            UFMUSimulationSubsystem* FMUSubsystem = GameInstance->GetSubsystem<UFMUSimulationSubsystem>();
            if (FMUSubsystem)
            {
                SimTime = static_cast<float>(FMUSubsystem->GetSimulationTime());
            }
        }

        // Fallback to interval calculation if FMU time is not available
        if (SimTime == 0.0f && CaptureRate > 0.0f)
        {
            float CaptureInterval = 1.0f / CaptureRate;
            SimTime = LastCaptureIndex * CaptureInterval;
        }

        // Se non usiamo captureRate, LastCaptureIndex non viene aggiornato in Tick --> incrementiamolo qui
        if (CaptureRate <= 0.0f)
        {
            LastCaptureIndex++; // -1 -> 0 al primo salvataggio, poi 1,2,...
        }

        SavePath = FPaths::Combine(
            OutputPath,
            FString::Printf(TEXT("%s_%06d.png"), *SensorName, FMath::Max(LastCaptureIndex, 0) + 1));
    }
    else // Sequence or OnDemand mode
    {
        // Usa l'indice della posa corrente (Sequence)
        SavePath = FPaths::Combine(
            OutputPath,
            FString::Printf(TEXT("%s_%06d.png"), *SensorName, CurrentPoseIndex + 1));
    }

    // Read the render target data (do this only once)
    TArray<FColor> RawData;
    FReadSurfaceDataFlags ReadFlags;
    ReadFlags.SetLinearToGamma(false);
    bool bSuccess = false;

    if (RenderTarget->GameThread_GetRenderTargetResource()->ReadPixels(RawData, ReadFlags))
    {
        // Create shared copies of the necessary data to pass to the async tasks
        TSharedPtr<TArray<FColor>> SharedRawData = MakeShared<TArray<FColor>>(RawData);
        FIntPoint ResolutionCopy = Resolution;
        FString SavePathCopy = SavePath;
        FString SensorNameCopy = SensorName;

        // Launch an async task to save the image
        Async(EAsyncExecution::ThreadPool, [ResolutionCopy, SharedRawData, SavePathCopy]()
            {
                // Compress to PNG (in background thread)
                TArray<uint8, FDefaultAllocator64> CompressedPNG;
                FImageUtils::PNGCompressImageArray(ResolutionCopy.X, ResolutionCopy.Y, *SharedRawData, CompressedPNG);

                // Save the file (in background thread)
                FFileHelper::SaveArrayToFile(CompressedPNG, *SavePathCopy);
            });

        bSuccess = true;

        // Stream via WebSocket — invia solo JSON con timestamp + path
        if (bStreamImages)
        {
            UPROXSIMAGameInstance* GameInstance = Cast<UPROXSIMAGameInstance>(GetGameInstance());
            if (GameInstance)
            {
                UWebSocketManager* WebSocketMgr = GameInstance->GetSubsystem<UWebSocketManager>();

                if (IsValid(WebSocketMgr) && WebSocketMgr->GetSensorClientCount(SensorName) > 0)
                {
                    // Leggi il tempo FMU qui
                    float StreamingSimTime = 0.0f;
                    UFMUSimulationSubsystem* FMUSubsystem = GameInstance->GetSubsystem<UFMUSimulationSubsystem>();
                    if (FMUSubsystem)
                    {
                        StreamingSimTime = static_cast<float>(FMUSubsystem->GetSimulationTime());
                    }

                    // JSON leggero: ~100 byte, nessun pixel
                    FString SavePathForward = SavePath.Replace(TEXT("\\"), TEXT("/"));
                    FString Json = FString::Printf(
                        TEXT("{\"sensor\":\"%s\",\"type\":\"Camera\",\"t\":%.6f,\"frame_path\":\"%s\"}"),
                        *SensorName,
                        StreamingSimTime,
                        *SavePathForward);

                    TArray<uint8> Bytes;
                    FTCHARToUTF8 Utf8(*Json);
                    Bytes.Append((const uint8*)Utf8.Get(), Utf8.Length());
                    WebSocketMgr->SendSensorData(SensorName, Bytes, StreamingSimTime);
                }
            }
        }
    }

    return bSuccess;

}

bool ACameraSensor::HasDataToSave_Implementation() const
{
    return (bSaveImages && !OutputPath.IsEmpty() && RenderTarget != nullptr) ||
        (bStreamImages && RenderTarget != nullptr);
}

void ACameraSensor::SetStreamingEnabled_Implementation(bool bEnabled, int32 Port)
{
    if (bEnabled == bStreamImages)
    {
        return; // No change needed
    }

    bStreamImages = bEnabled;

    if (bStreamImages)
    {
        // Get the game instance
        UPROXSIMAGameInstance *GameInstance = Cast<UPROXSIMAGameInstance>(GetGameInstance());
        if (GameInstance && GameInstance->GetWebSocketManager())
        {
            UWebSocketManager *WebSocketMgr = GameInstance->GetSubsystem<UWebSocketManager>();
            ;
            if (IsValid(WebSocketMgr) && WebSocketMgr->IsRunning())
            {
                // Register this camera as a streaming endpoint
                WebSocketMgr->RegisterSensorEndpoint(SensorName, GetSensorType_Implementation());
                StreamingURL = WebSocketMgr->GetSensorWebSocketURL(SensorName);
                UE_LOG(LogTemp, Display, TEXT("Camera %s streaming enabled at %s"), *SensorName, *StreamingURL);

                // Generate and save HTML viewer if we have an output path
                if (!OutputPath.IsEmpty())
                {
                    WebSocketMgr->SaveHTMLViewer(SensorName, GetSensorType_Implementation(), OutputPath);
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("WebSocket server not running for camera %s"), *SensorName);
                bStreamImages = false;
            }
        }
    }
    else
    {
        StreamingURL = FString();
        UE_LOG(LogTemp, Display, TEXT("Camera %s streaming disabled"), *SensorName);
    }
}

bool ACameraSensor::IsStreamingEnabled_Implementation() const
{
    return bStreamImages;
}

FString ACameraSensor::GetStreamingURL_Implementation() const
{
    return StreamingURL;
}

bool ACameraSensor::RequiresClientConnection_Implementation() const
{
    return bRequireClientConnection;
}