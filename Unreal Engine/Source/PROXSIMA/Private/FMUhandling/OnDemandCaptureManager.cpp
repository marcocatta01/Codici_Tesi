/**
 * @file OnDemandCaptureManager.cpp
 * Implementation of on-demand image capture functionality triggered by WebSocket commands
 */

#include "FMUhandling/OnDemandCaptureManager.h"
#include "Sensors/SensorManager.h"
#include "Sensors/CameraSensor.h"
#include "Engine/World.h"

UOnDemandCaptureManager::UOnDemandCaptureManager()
    : SensorManager(nullptr), bIsInitialized(false), TotalCapturesProcessed(0), SuccessfulCaptures(0), FailedCaptures(0)
{
    // Initialize with default state
}

bool UOnDemandCaptureManager::Initialize(USensorManager* InSensorManager)
{
    if (!InSensorManager)
    {
        UE_LOG(LogTemp, Error, TEXT("OnDemandCaptureManager: Cannot initialize with null SensorManager"));
        return false;
    }

    SensorManager = InSensorManager;
    bIsInitialized = true;
    
    // Reset statistics
    ResetStatistics();

    UE_LOG(LogTemp, Log, TEXT("OnDemandCaptureManager initialized successfully"));
    return true;
}

bool UOnDemandCaptureManager::ProcessCaptureCommand(const FString& CameraName, const FSerializedCameraPose& Pose)
{
    if (!IsReady())
    {
        UE_LOG(LogTemp, Warning, TEXT("OnDemandCaptureManager: Not initialized, cannot process capture command for camera '%s'"), *CameraName);
        return false;
    }

    if (CameraName.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("OnDemandCaptureManager: Cannot process capture command with empty camera name"));
        FailedCaptures++;
        TotalCapturesProcessed++;
        return false;
    }

    // Find the camera sensor
    ACameraSensor* CameraSensor = FindCameraSensorByName(CameraName);
    if (!CameraSensor)
    {
        UE_LOG(LogTemp, Error, TEXT("OnDemandCaptureManager: Camera sensor '%s' not found"), *CameraName);
        FailedCaptures++;
        TotalCapturesProcessed++;
        OnCaptureProcessed.Broadcast(CameraName, false);
        return false;
    }

    // Check if camera is enabled
    if (!CameraSensor->IsEnabled_Implementation())
    {
        UE_LOG(LogTemp, Warning, TEXT("OnDemandCaptureManager: Camera sensor '%s' is disabled"), *CameraName);
        FailedCaptures++;
        TotalCapturesProcessed++;
        OnCaptureProcessed.Broadcast(CameraName, false);
        return false;
    }

    // Bind to capture completion callback - AddDynamic is safe to call multiple times
    CameraSensor->OnCaptureCompleted.AddDynamic(this, &UOnDemandCaptureManager::OnCaptureCompleted);

    // Trigger the image capture with the specified pose
    CameraSensor->CaptureImageWithPose(Pose);
    
    TotalCapturesProcessed++;
    
    UE_LOG(LogTemp, Log, TEXT("OnDemandCaptureManager: Triggered capture for camera '%s' at location (%.2f, %.2f, %.2f)"), 
           *CameraName, 
           Pose.Transform.GetLocation().X, 
           Pose.Transform.GetLocation().Y, 
           Pose.Transform.GetLocation().Z);

    return true;
}

void UOnDemandCaptureManager::GetCaptureStatistics(int32& OutTotalProcessed, int32& OutSuccessful, int32& OutFailed) const
{
    OutTotalProcessed = TotalCapturesProcessed;
    OutSuccessful = SuccessfulCaptures;
    OutFailed = FailedCaptures;
}

void UOnDemandCaptureManager::ResetStatistics()
{
    TotalCapturesProcessed = 0;
    SuccessfulCaptures = 0;
    FailedCaptures = 0;
    
    UE_LOG(LogTemp, Log, TEXT("OnDemandCaptureManager: Statistics reset"));
}

ACameraSensor* UOnDemandCaptureManager::FindCameraSensorByName(const FString& CameraName) const
{
    if (!SensorManager)
    {
        return nullptr;
    }

    // Use the sensor manager's GetSensorByName method
    AActor* SensorActor = SensorManager->GetSensorByName(CameraName);
    if (!SensorActor)
    {
        return nullptr;
    }

    // Cast to camera sensor
    ACameraSensor* CameraSensor = Cast<ACameraSensor>(SensorActor);
    return CameraSensor;
}

void UOnDemandCaptureManager::OnCaptureCompleted(bool bSuccess, const FString& CameraName)
{
    if (bSuccess)
    {
        SuccessfulCaptures++;
        UE_LOG(LogTemp, Log, TEXT("OnDemandCaptureManager: Capture completed successfully for camera '%s'"), *CameraName);
    }
    else
    {
        FailedCaptures++;
        UE_LOG(LogTemp, Warning, TEXT("OnDemandCaptureManager: Capture failed for camera '%s'"), *CameraName);
    }

    // Broadcast the completion event
    OnCaptureProcessed.Broadcast(CameraName, bSuccess);
}