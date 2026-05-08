#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FMUConfiguration.h"
#include "Dom/JsonObject.h"
#include "SimulationConfigurationSubsystem.generated.h"

/**
 * Simulation mode enum for PROXSIMA system
 */
UENUM(BlueprintType)
enum class ESimulationMode : uint8
{
    /** Traditional FMU-based simulation with dynamic updates */
    SM_FMU UMETA(DisplayName = "FMU Mode"),

    /** Pre-defined sequence playback mode for camera animations */
    SM_Sequence UMETA(DisplayName = "Sequence Mode"),

    /** On-demand image capture mode controlled via WebSocket */
    SM_OnDemand UMETA(DisplayName = "OnDemand Mode"),

    /** No active simulation */
    SM_None UMETA(DisplayName = "No Active Mode")
};

/**
 * Manages configuration parsing and validation for PROXSIMA simulations
 * Handles both FMU and sequence mode configurations
 */
UCLASS(BlueprintType)
class PROXSIMA_API USimulationConfigurationSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // USubsystem interface
    virtual void Initialize(FSubsystemCollectionBase &Collection) override;
    virtual void Deinitialize() override;

    /**
     * Load and parse configuration file supporting both FMU and sequence modes
     * @param ConfigPath Path to the configuration JSON file
     * @param OutConfig Configuration structure to populate
     * @param OutMode Detected simulation mode
     * @return True if parsing was successful
     */
    UFUNCTION(BlueprintCallable, Category = "Configuration")
    bool LoadConfiguration(const FString &ConfigPath, FFMUConfiguration &OutConfig, ESimulationMode &OutMode);

    /**
     * Parse configuration from pre-loaded JSON object
     * @param ConfigPath Original config file path (for relative path resolution)
     * @param JsonObject Pre-parsed JSON object
     * @param OutConfig Configuration structure to populate
     * @param OutMode Detected simulation mode
     * @return True if parsing was successful
     */
    // UFUNCTION(BlueprintCallable, Category = "Configuration")
    bool ParseConfiguration(const FString &ConfigPath, const TSharedPtr<FJsonObject> &JsonObject, FFMUConfiguration &OutConfig, ESimulationMode &OutMode);

    /**
     * Validate configuration for consistency and completeness
     * @param Config Configuration to validate
     * @param Mode Simulation mode to validate against
     * @return True if configuration is valid
     */
    UFUNCTION(BlueprintCallable, Category = "Configuration")
    bool ValidateConfiguration(const FFMUConfiguration &Config, ESimulationMode Mode) const;

    /**
     * Get the cached configuration for quick access
     * @return Reference to the current configuration
     */
    UFUNCTION(BlueprintPure, Category = "Configuration")
    const FFMUConfiguration &GetCurrentConfiguration() const { return CachedConfiguration; }

    /**
     * Get the current simulation mode
     * @return Current simulation mode
     */
    UFUNCTION(BlueprintPure, Category = "Configuration")
    ESimulationMode GetCurrentMode() const { return CurrentMode; }

    /**
     * Check if a configuration has been loaded
     * @return True if configuration is loaded and valid
     */
    UFUNCTION(BlueprintPure, Category = "Configuration")
    bool IsConfigurationLoaded() const { return bConfigurationLoaded; }

private:
    /** Parse FMU mode specific configuration */
    bool ParseFMUConfiguration(const FString &ConfigPath, const TSharedPtr<FJsonObject> &JsonObject, FFMUConfiguration &OutConfig);

    /** Parse sequence mode specific configuration */
    bool ParseSequenceConfiguration(const FString &ConfigPath, const TSharedPtr<FJsonObject> &JsonObject, FFMUConfiguration &OutConfig);

    /** Parse ondemand mode specific configuration */
    bool ParseOnDemandConfiguration(const FString &ConfigPath, const TSharedPtr<FJsonObject> &JsonObject, FFMUConfiguration &OutConfig);

    /** Parse sensor configuration array */
    bool ParseSensorsConfiguration(const TSharedPtr<FJsonObject> &JsonObject, FFMUConfiguration &OutConfig);

    /** Parse visual components configuration array */
    bool ParseVisualComponentsConfiguration(const FString &ConfigPath, const TSharedPtr<FJsonObject> &JsonObject, FFMUConfiguration &OutConfig);

    /** Parse rigid transform from JSON */
    bool ParseRigidTransform(const TSharedPtr<FJsonObject> &TransformObj, FTransform &OutTransform);

    /** Parse input configuration from JSON */
    bool ParseInputConfiguration(const TSharedPtr<FJsonObject> &JsonObject, FFMUInputConfiguration &OutInputConfig, const FString &ConfigPath = FString());

    /** Process auto-detection of variables from CSV file */
    bool ProcessAutoDetectionFromCSV(const FString &TimeseriesPath, const TSharedPtr<FJsonObject> &VariableObj, FFMUInputConfiguration &OutInputConfig, const FString &ConfigPath);

    /** Resolve relative paths based on config directory */
    FString ResolveRelativePath(const FString &ConfigPath, const FString &RelativePath) const;

private:
    /** Currently cached configuration */
    UPROPERTY()
    FFMUConfiguration CachedConfiguration;

    /** Current simulation mode */
    UPROPERTY()
    ESimulationMode CurrentMode;

    /** Whether a valid configuration has been loaded */
    bool bConfigurationLoaded;
};