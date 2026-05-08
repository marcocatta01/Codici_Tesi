#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sensors/SensorInterface.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "WebSocketManager.h"
#include "Sensors/SensorsTypes.h"
#include "CameraSensor.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCaptureCompletedSignature, bool, bSuccess, const FString &, Timestamp);

/**
 * Monocular camera sensor implementation
 */
UCLASS()
class PROXSIMA_API ACameraSensor : public AActor, public ISensorInterface
{
    GENERATED_BODY()

public:
    /** Delegate that is broadcasted when a capture is completed */
    UPROPERTY(BlueprintAssignable, Category = "Camera")
    FOnCaptureCompletedSignature OnCaptureCompleted;

    ACameraSensor();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
    virtual void Tick(float DeltaTime) override;

    // ISensorInterface implementation
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    bool Initialize(const FString &InSensorName, const FString &InFrameName, const FTransform &InRelativeTransform, const TMap<FString, FString> &Parameters);
    virtual bool Initialize_Implementation(const FString &InSensorName, const FString &InFrameName, const FTransform &InRelativeTransform, const TMap<FString, FString> &Parameters) override;

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    void Update(float DeltaTime);
    virtual void Update_Implementation(float DeltaTime) override;

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    void SetEnabled(bool bEnabled);
    virtual void SetEnabled_Implementation(bool bEnabled) override;

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    bool IsEnabled() const;
    virtual bool IsEnabled_Implementation() const override;

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    FString GetSensorName() const;
    virtual FString GetSensorName_Implementation() const override;

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    FString GetSensorType() const;
    virtual FString GetSensorType_Implementation() const override;

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    FString GetFrameName() const;
    virtual FString GetFrameName_Implementation() const override;

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    FTransform GetRelativeTransform() const;
    virtual FTransform GetRelativeTransform_Implementation() const override;

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    void SetOutputPath(const FString &Path);
    virtual void SetOutputPath_Implementation(const FString &Path) override;

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    bool SaveSensorData();
    virtual bool SaveSensorData_Implementation() override;

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    bool HasDataToSave() const;
    virtual bool HasDataToSave_Implementation() const override;

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    void SetStreamingEnabled(bool bEnabled, int32 Port);
    virtual void SetStreamingEnabled_Implementation(bool bEnabled, int32 Port) override;

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    bool IsStreamingEnabled() const;
    virtual bool IsStreamingEnabled_Implementation() const override;

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    FString GetStreamingURL() const;
    virtual FString GetStreamingURL_Implementation() const override;

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    bool RequiresClientConnection() const;
    virtual bool RequiresClientConnection_Implementation() const override;

    // Camera-specific functions
    UFUNCTION(BlueprintCallable, Category = "Camera")
    UTextureRenderTarget2D *GetRenderTarget() const { return RenderTarget; }

    /** Capture an image with the current camera pose */
    UFUNCTION(BlueprintCallable, Category = "Camera")
    void CaptureImage();

    /** Capture an image with a specific pose */
    UFUNCTION(BlueprintCallable, Category = "Camera")
    void CaptureImageWithPose(const FSerializedCameraPose &Pose);

    /** Apply a specific pose to the camera */
    UFUNCTION(BlueprintCallable, Category = "Camera")
    void ApplyPose(const FSerializedCameraPose &Pose);

    /** Check if the camera is currently processing a capture */
    UFUNCTION(BlueprintCallable, Category = "Camera")
    bool IsCapturing() const { return bIsCapturing; }

private:
    /** Get filename for the current capture */
    FString GetCaptureFilename(const FSerializedCameraPose &Pose) const;

    /** Handle completion of image capture and saving */
    void HandleCaptureComplete(bool bSuccess, const FSerializedCameraPose &Pose);

private:
    // Camera components
    UPROPERTY(VisibleAnywhere, Category = "Camera")
    USceneCaptureComponent2D *CaptureComponent;

    UPROPERTY(VisibleAnywhere, Category = "Camera")
    UTextureRenderTarget2D *RenderTarget;

    // Camera parameters
    UPROPERTY(VisibleAnywhere, Category = "Camera")
    FIntPoint Resolution;

    UPROPERTY(VisibleAnywhere, Category = "Camera")
    float FieldOfView;

    UPROPERTY(VisibleAnywhere, Category = "Camera")
    float NearClipPlane;

    UPROPERTY(VisibleAnywhere, Category = "Camera")
    float FarClipPlane;

    UPROPERTY(VisibleAnywhere, Category = "Camera")
    float CaptureRate;

    // Sensor properties
    UPROPERTY(VisibleAnywhere, Category = "Sensor")
    FString SensorName;

    UPROPERTY(VisibleAnywhere, Category = "Sensor")
    FString FrameName;

    UPROPERTY(VisibleAnywhere, Category = "Sensor")
    FTransform RelativeTransform;

    UPROPERTY(VisibleAnywhere, Category = "Sensor")
    bool bIsEnabled;

    // Timing for capture rate
    float TimeSinceLastCapture;
    
    /** Index of the last captured frame (for FMU mode) */
    int32 LastCaptureIndex;

    UPROPERTY(VisibleAnywhere, Category = "Sensor")
    FString OutputPath;

    UPROPERTY(VisibleAnywhere, Category = "Sensor")
    bool bSaveImages;

    /** Whether to stream images via WebSocket */
    UPROPERTY(VisibleAnywhere, Category = "Sensor")
    bool bStreamImages;

    /** WebSocket port for streaming */
    UPROPERTY(VisibleAnywhere, Category = "Sensor")
    int32 StreamingPort;

    /** WebSocket URL for this camera */
    UPROPERTY(VisibleAnywhere, Category = "Sensor")
    FString StreamingURL;

    /** Whether the camera is currently processing a capture */
    UPROPERTY(VisibleAnywhere, Category = "Camera")
    bool bIsCapturing;

    /** Sensor configuration parameters */
    UPROPERTY(VisibleAnywhere, Category = "Sensor")
    TMap<FString, FString> SensorParameters;

    /** Whether client connections are required for this sensor */
    UPROPERTY(VisibleAnywhere, Category = "Sensor")
    bool bRequireClientConnection;

    /** Current pose being processed */
    FSerializedCameraPose CurrentPose;

    /** Current index in sequence mode */
    UPROPERTY(VisibleAnywhere, Category = "Camera")
    int32 CurrentPoseIndex;
};
