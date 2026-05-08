#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SensorInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI, Blueprintable)
class USensorInterface : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for all sensor types in the simulation.
 * Implementers should provide _Implementation versions of the BlueprintNativeEvent methods.
 */
class PROXSIMA_API ISensorInterface
{
    GENERATED_BODY()

public:
    // Initialize the sensor with its parameters
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    bool Initialize(const FString& SensorName,
                    const FString& FrameName,
                    const FTransform& RelativeTransform,
                    const TMap<FString, FString>& Parameters);

    // Update the sensor state (called every tick or at sensor-defined rate)
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    void Update(float DeltaTime);

    // Enable or disable the sensor
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    void SetEnabled(bool bEnabled);

    // Check if the sensor is enabled
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    bool IsEnabled() const;

    // Get the sensor name
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    FString GetSensorName() const;

    // Get the sensor type (e.g., "Camera", "IMU")
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    FString GetSensorType() const;

    // Get the frame name this sensor is attached to
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    FString GetFrameName() const;

    // Get the relative transform of the sensor (w.r.t. attached frame)
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    FTransform GetRelativeTransform() const;

    // Set the output path for sensor data (images, CSVs, etc.)
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    void SetOutputPath(const FString& Path);

    // Save sensor data (if any). Returns true if something was saved.
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    bool SaveSensorData();

    // Check if sensor has data to save (or stream)
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    bool HasDataToSave() const;

    // Methods for streaming sensor data
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    void SetStreamingEnabled(bool bEnabled, int32 Port);

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    bool IsStreamingEnabled() const;

    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    FString GetStreamingURL() const;

    // If true, the sensor expects a client connection before producing data (useful for on-demand streaming)
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sensor")
    bool RequiresClientConnection() const;
};