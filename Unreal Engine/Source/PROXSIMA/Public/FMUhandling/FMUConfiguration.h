#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "FMUhandling/FMUInputDataStructures.h"
#include "FMUConfiguration.generated.h"

USTRUCT(BlueprintType)
struct FFMUVariableConfig
{
    GENERATED_BODY()

    // The name of the variable as defined in modelDescription.xml
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    FString name;

    // The initial value for the variable
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    double value;

    // Optional mapping to a component property (for visualization)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    FString componentProperty;

    // Optional unit conversion factor (e.g., 100 for m to cm)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    double unitConversionFactor = 1.0;
};

USTRUCT(BlueprintType)
struct FFMUVisualAppearance
{
    GENERATED_BODY()

    // Color of the component (can be set via string name or RGB values)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    FLinearColor color;

    // Optional color name (takes precedence over RGB values if set)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    FString colorName;

    // Opacity of the component (0.0 - 1.0)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    float opacity = 1.0f;

    // Material properties
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    float metallic = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    float roughness = 0.8f;
};

USTRUCT(BlueprintType)
struct FFMUVisualComponent
{
    GENERATED_BODY()

    // Unique identifier for the visual component
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    FString name;

    // Optional name of the FMU frame to attach to (if empty, spawns as static world-space object)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    FString frameName;

    // Type of shape to spawn
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    FString type;

    // Path to the 3D model file (when type is 'mesh')
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    FString meshPath;

    // Dimensions of the shape
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    TMap<FString, float> dimensions;

    // Optional transform offset
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    FTransform rigidTransform;

    // Visual appearance settings
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    FFMUVisualAppearance appearance;

    // Pointer to the spawned component
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    UStaticMeshComponent *MeshComponent = nullptr;
};

USTRUCT(BlueprintType)
struct FFMUSensorConfig
{
    GENERATED_BODY()

    // Unique identifier for the sensor
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    FString name;

    // Type of sensor (e.g., "Camera")
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    FString type;

    // Name of the FMU frame to attach the sensor to
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    FString frameName;

    // Optional transform offset
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    FTransform rigidTransform;

    /**
    * Sensor-specific parameters
    * Common parameters:
    * - outputPath: Path to save sensor data
    * - requireClientConnection: Whether client connections are mandatory for this sensor (true/false, default: false)
    */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    TMap<FString, FString> parameters;

    // Output path for sensor data
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    FString outputPath;

    // Pointer to the spawned sensor actor
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    AActor *SensorActor = nullptr;
};

USTRUCT(BlueprintType)
struct FFMUConfiguration
{
    GENERATED_BODY()

    // Path to the FMU file
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    FString fmuPath;

    // Fixed timestep for simulation (optional, uses DefaultExperiment from XML if not set)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    double timeStep = 0.001;

    // Start time for the simulation
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    double startTime = 0.0;

    // Stop time for the simulation (0 means no limit)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    double stopTime = 0.0;

    // Port for WebSocket streaming (0 to disable)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    int32 streamingPort = 0; // disable by default

    // Variables to configure
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    TArray<FFMUVariableConfig> variables;

    // Visual components to spawn
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    TArray<FFMUVisualComponent> visualComponents;

    // Sensors to spawn
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    TArray<FFMUSensorConfig> sensors;

    // Input configuration for real-time control
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU")
    FFMUInputConfiguration inputConfiguration;
};

// Helper class to load/save FMU configurations
UCLASS()
class PROXSIMA_API UFMUConfigurationLoader : public UObject
{
    GENERATED_BODY()

public:
    // Load configuration from JSON file
    UFUNCTION(BlueprintCallable, Category = "FMU")
    static bool LoadConfiguration(const FString &path, FFMUConfiguration &outConfig);

    // Save configuration to JSON file
    UFUNCTION(BlueprintCallable, Category = "FMU")
    static bool SaveConfiguration(const FString &path, const FFMUConfiguration &config);

    // Validate configuration against model description
    UFUNCTION(BlueprintCallable, Category = "FMU")
    static bool ValidateConfiguration(const FFMUConfiguration &config, class UFMUModelDescription *modelDesc);
};
