#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "FMUConfiguration.h"
#include "Sensors/SensorInterface.h"
#include "SensorManager.generated.h"

/**
 * Manages all sensors in the simulation
 */
UCLASS(BlueprintType)
class PROXSIMA_API USensorManager : public UObject
{
    GENERATED_BODY()

public:
    USensorManager();

    // Initialize sensors from configuration
    UFUNCTION(BlueprintCallable, Category = "Sensors")
    bool InitializeSensors(UWorld *World, const TArray<FFMUSensorConfig> &SensorConfigs);

    // Update all sensors
    UFUNCTION(BlueprintCallable, Category = "Sensors")
    void UpdateSensors(float DeltaTime);

    // Get a sensor by name
    UFUNCTION(BlueprintCallable, Category = "Sensors")
    AActor *GetSensorByName(const FString &SensorName) const;

    // Get all sensors
    UFUNCTION(BlueprintCallable, Category = "Sensors")
    TArray<AActor *> GetAllSensors() const;

    // Clean up all sensors
    UFUNCTION(BlueprintCallable, Category = "Sensors")
    void CleanupSensors();

private:
    // Map of sensor name to sensor actor
    UPROPERTY()
    TMap<FString, AActor *> Sensors;

    // Create a sensor of the specified type
    AActor *CreateSensor(UWorld *World, const FFMUSensorConfig &SensorConfig);
};