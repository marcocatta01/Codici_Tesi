#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "FMUInputDataStructures.generated.h"

/**
 * Represents a single time-value pair for FMU input timeseries
 */
USTRUCT(BlueprintType)
struct FFMUInputTimeValue
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    double time = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    double value = 0.0;

    FFMUInputTimeValue() = default;
    FFMUInputTimeValue(double InTime, double InValue) : time(InTime), value(InValue) {}
};

/**
 * Interpolation methods for input timeseries (per variabile)
 */
UENUM(BlueprintType)
enum class EFMUInputInterpolation : uint8
{
    None        UMETA(DisplayName = "None (Hold Last Value)"),
    Linear      UMETA(DisplayName = "Linear Interpolation"),
    Cubic       UMETA(DisplayName = "Cubic Hermite (Catmull-Rom style)")
};

/**
 * Priority levels for input sources
 */
UENUM(BlueprintType)
enum class EFMUInputPriority : uint8
{
    Low         UMETA(DisplayName = "Low Priority"),
    Medium      UMETA(DisplayName = "Medium Priority"),
    High        UMETA(DisplayName = "High Priority"),
    Critical    UMETA(DisplayName = "Critical Priority")
};

/**
 * Input source types
 */
UENUM(BlueprintType)
enum class EFMUInputSourceType : uint8
{
    Static      UMETA(DisplayName = "Static Value"),
    Timeseries  UMETA(DisplayName = "Timeseries File"),
    WebSocket   UMETA(DisplayName = "WebSocket Commands"),
    Manual      UMETA(DisplayName = "Manual API"),
    GameController UMETA(DisplayName = "Game Controller"),
    VRHeadset   UMETA(DisplayName = "VR Headset")
};

/**
 * A timeseries of input values
 */
USTRUCT(BlueprintType)
struct FFMUInputTimeseries
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    TArray<FFMUInputTimeValue> timeValues;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    bool bLooping = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    double loopDuration = 0.0;

    // Clamp cubic interpolation inside segment [v_left, v_right] to avoid overshoot
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    bool bClampCubicToSegment = true;

    /**
     * Get interpolated value at a specific time using the provided interpolation method
     * @param time Simulation time to get value for
     * @param outValue Output interpolated value
     * @param interpolationMethod Interpolation method to use (provided by the variable)
     * @return True if value was successfully interpolated
     */
    bool GetValueAtTime(double time, double& outValue, EFMUInputInterpolation interpolationMethod) const;

    void AddTimeValue(double time, double value);
    void Clear();
    void Sort();
    bool GetTimeRange(double& outStartTime, double& outEndTime) const;

private:
    double LinearInterpolate(double t, double t1, double v1, double t2, double v2) const;
    double CubicHermite(double t,
                        double t0, double v0, double m0,
                        double t1, double v1, double m1) const;
};

/**
 * Configuration for an input source
 */
USTRUCT(BlueprintType)
struct FFMUInputSource
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    FString sourceId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    EFMUInputSourceType sourceType = EFMUInputSourceType::Static;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    EFMUInputPriority priority = EFMUInputPriority::Medium;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    double staticValue = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    FString timeseriesFilePath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    FFMUInputTimeseries timeseries;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    TMap<FString, FString> parameters;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    bool bEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    double validityDuration = 0.0; // 0 means no expiration

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    double lastUpdateTime = 0.0;

    bool GetValue(double currentTime, double& outValue, EFMUInputInterpolation interpolationMethod) const;
    void UpdateValue(double currentTime, double newValue);
    bool IsValid(double currentTime) const;
};

/**
 * Configuration for an FMU input variable
 */
USTRUCT(BlueprintType)
struct FFMUInputVariableConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    FString variableName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    TArray<FFMUInputSource> inputSources;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    double defaultValue = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    double unitConversionFactor = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    double minValue = -TNumericLimits<double>::Max();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    double maxValue = TNumericLimits<double>::Max();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    bool bClampToRange = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    EFMUInputInterpolation interpolationMethod = EFMUInputInterpolation::None;

    bool GetCurrentValue(double currentTime, double& outValue) const;
    void AddInputSource(const FFMUInputSource& inputSource);
    void RemoveInputSource(const FString& sourceId);
    FFMUInputSource* FindInputSource(const FString& sourceId);

private:
    const FFMUInputSource* GetBestInputSource(double currentTime) const;
};

/**
 * Complete FMU input configuration
 */
USTRUCT(BlueprintType)
struct FFMUInputConfiguration
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    TArray<FFMUInputVariableConfig> inputVariables;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    bool bSynchronizeWithSimulation = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FMU Input")
    TMap<FString, FString> globalParameters;

    FFMUInputVariableConfig* FindInputVariable(const FString& variableName);
    const FFMUInputVariableConfig* FindInputVariable(const FString& variableName) const;
    void SetInputVariable(const FFMUInputVariableConfig& variableConfig);
    void RemoveInputVariable(const FString& variableName);
};