#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Sensors/SensorsTypes.h"
#include "OnDemandCaptureManager.generated.h"

// Forward declarations
class USensorManager;
class ACameraSensor;

/**
 * Manages on-demand image capture triggered by WebSocket commands
 * Provides persistent mode that listens for capture requests and processes them individually
 */
UCLASS()
class PROXSIMA_API UOnDemandCaptureManager : public UObject
{
    GENERATED_BODY()

public:
    /** Delegate fired when a capture command is successfully processed */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCaptureProcessed, const FString&, CameraName, bool, bSuccess);

    /** Event triggered when a capture is processed */
    UPROPERTY(BlueprintAssignable, Category = "OnDemand Capture")
    FOnCaptureProcessed OnCaptureProcessed;

    UOnDemandCaptureManager();

    /**
     * Initialize the on-demand capture manager with sensor manager reference
     * @param InSensorManager Reference to the sensor manager containing cameras
     * @return True if initialization was successful
     */
    UFUNCTION(BlueprintCallable, Category = "OnDemand Capture")
    bool Initialize(USensorManager* InSensorManager);

    /**
     * Process a single capture command received via WebSocket
     * @param CameraName Name of the camera to capture with
     * @param Pose Camera pose data containing location, rotation, and metadata
     * @return True if the capture command was accepted and queued
     */
    UFUNCTION(BlueprintCallable, Category = "OnDemand Capture")
    bool ProcessCaptureCommand(const FString& CameraName, const FSerializedCameraPose& Pose);

    /**
     * Check if the manager is ready to process capture commands
     * @return True if initialized and ready
     */
    UFUNCTION(BlueprintPure, Category = "OnDemand Capture")
    bool IsReady() const { return bIsInitialized && SensorManager != nullptr; }

    /**
     * Get statistics about processed captures
     * @param OutTotalProcessed Total number of captures processed since initialization
     * @param OutSuccessful Number of successful captures
     * @param OutFailed Number of failed captures
     */
    UFUNCTION(BlueprintPure, Category = "OnDemand Capture")
    void GetCaptureStatistics(int32& OutTotalProcessed, int32& OutSuccessful, int32& OutFailed) const;

    /**
     * Reset capture statistics
     */
    UFUNCTION(BlueprintCallable, Category = "OnDemand Capture")
    void ResetStatistics();

private:
    /** Reference to the sensor manager containing all cameras */
    UPROPERTY()
    USensorManager* SensorManager;

    /** Whether the manager has been properly initialized */
    bool bIsInitialized;

    /** Statistics tracking */
    int32 TotalCapturesProcessed;
    int32 SuccessfulCaptures;
    int32 FailedCaptures;

    /**
     * Find a camera sensor by name
     * @param CameraName Name of the camera to find
     * @return Pointer to the camera sensor or nullptr if not found
     */
    ACameraSensor* FindCameraSensorByName(const FString& CameraName) const;

    /**
     * Callback when a capture operation completes
     * @param bSuccess Whether the capture was successful
     * @param CameraName Name of the camera that performed the capture
     */
    UFUNCTION()
    void OnCaptureCompleted(bool bSuccess, const FString& CameraName);
};