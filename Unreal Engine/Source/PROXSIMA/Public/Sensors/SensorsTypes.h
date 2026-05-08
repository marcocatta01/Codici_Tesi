#pragma once

#include "CoreMinimal.h"
#include "SensorsTypes.generated.h"

/**
 * Unified structure for camera pose data used across sequence management and camera sensors
 */
USTRUCT(BlueprintType)
struct PROXSIMA_API FSerializedCameraPose
{
    GENERATED_USTRUCT_BODY()

public:
    /** Name of the camera this pose belongs to */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    FString CameraName;

    /** Transform containing location (cm) and rotation (quaternion) in UE world space */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    FTransform Transform;

    /** Timestamp of the pose in seconds */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    float Timestamp;

    /** Additional metadata key-value pairs */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    TMap<FString, FString> Metadata;

    /** Optional description of the pose */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    FString Description;

    FSerializedCameraPose()
        : Transform(FTransform::Identity), Timestamp(0.0f)
    {
    }

    FSerializedCameraPose(const FTransform& InTransform, float InTimestamp = 0.0f)
        : Transform(InTransform), Timestamp(InTimestamp)
    {
    }

    bool operator<(const FSerializedCameraPose& Other) const
    {
        return Timestamp < Other.Timestamp;
    }
};

/**
 * IMU sample structure representing a single accelerometer + gyroscope measurement.
 * Units:
 *  - Acceleration (Accel): m/s^2 in the sensor/body frame
 *  - Angular velocity (Gyro): rad/s in the sensor/body frame
 *  - Timestamp: seconds (simulation time if available, otherwise world time)
 */
USTRUCT(BlueprintType)
struct PROXSIMA_API FIMUSample
{
    GENERATED_USTRUCT_BODY()

public:
    /** Name of the sensor that produced this sample */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IMU")
    FString SensorName;

    /** Timestamp in seconds */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IMU")
    float Timestamp;

    /** Linear acceleration [m/s^2] in body frame (optionally includes gravity depending on sensor config) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IMU")
    FVector Accel;

    /** Angular velocity [rad/s] in body frame */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IMU")
    FVector Gyro;

    /** Additional metadata (e.g., includeGravity=true/false, noises, etc.) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IMU")
    TMap<FString, FString> Metadata;

    FIMUSample()
        : Timestamp(0.0f), Accel(FVector::ZeroVector), Gyro(FVector::ZeroVector)
    {
    }

    FIMUSample(const FString& InSensorName, float InTimestamp, const FVector& InAccel, const FVector& InGyro)
        : SensorName(InSensorName), Timestamp(InTimestamp), Accel(InAccel), Gyro(InGyro)
    {
    }

    bool operator<(const FIMUSample& Other) const
    {
        return Timestamp < Other.Timestamp;
    }
};