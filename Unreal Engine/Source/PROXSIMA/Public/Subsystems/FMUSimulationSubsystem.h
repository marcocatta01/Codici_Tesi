#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/World.h"
#include "FMUIntegration.h"
#include "FMUConfiguration.h"
#include "FMUhandling/FMUInputDataStructures.h"
#include "FMUSimulationSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSimulationStateChanged, bool, bIsRunning);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVariableChanged, const FString&, VariableName, double, Value);

// Delegate C++: notificato dopo ogni step di integrazione dell'FMU.
// Parametri: SimTime (s) tempo di simulazione corrente, Step (s) durata dello step
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnFmuStep, double /*SimTime*/, double /*Step*/);

/**
 * Manages FMU simulation lifecycle, variable observers, and simulation updates
 * Handles the fixed timestep simulation loop and coordinate system conversions
 */
UCLASS(BlueprintType)
class PROXSIMA_API UFMUSimulationSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()

public:
    // USubsystem interface
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // FTickableGameObject interface
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;

    /**
     * Initialize FMU simulation with configuration
     * @param Configuration FMU configuration to use
     * @return True if initialization was successful
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Simulation")
    bool InitializeFMU(const FFMUConfiguration& Configuration);

    /**
     * Load FMU from file path
     * @param FMUPath Path to the FMU file
     * @return True if loading was successful
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Simulation")
    bool LoadFMU(const FString& FMUPath);

    /**
     * Start the simulation
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Simulation")
    void StartSimulation();

    /**
     * Pause the simulation
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Simulation")
    void PauseSimulation();

    /**
     * Stop the simulation and reset state
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Simulation")
    void StopSimulation();

    /**
     * Apply initial conditions to all registered frame actors
     * This should be called after FMU initialization but before starting simulation
     * to ensure actors are positioned correctly when sensors are attached
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Simulation")
    void ApplyInitialConditions();

    /**
     * Register an actor as an observer for a specific variable
     * @param VariableName Name of the FMU variable to observe
     * @param Actor Actor to receive updates when the variable changes
     * @return True if registration was successful
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Simulation")
    bool RegisterVariableObserver(const FString& VariableName, AActor* Actor);

    /**
     * Unregister an actor from all variable observations
     * @param Actor Actor to unregister
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Simulation")
    void UnregisterVariableObserver(AActor* Actor);

    /**
     * Get the current value of an FMU variable
     * @param VariableName Name of the variable
     * @param OutValue Output value
     * @return True if the variable was found and retrieved
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Simulation")
    bool GetVariableValue(const FString& VariableName, double& OutValue) const;

    /**
     * Set the value of an FMU variable
     * @param VariableName Name of the variable
     * @param Value New value to set
     * @return True if the variable was found and set
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Simulation")
    bool SetVariableValue(const FString& VariableName, double Value);

    /**
     * Get actor associated with a frame name
     * @param FrameName Name of the frame
     * @return Actor associated with the frame, or nullptr if not found
     */
    UFUNCTION(BlueprintPure, Category = "FMU Simulation")
    AActor* GetFrameActor(const FString& FrameName) const;

    /**
     * Check if the simulation is currently running
     * @return True if simulation is running
     */
    UFUNCTION(BlueprintPure, Category = "FMU Simulation")
    bool IsSimulating() const { return bIsSimulating; }

    /**
     * Check if FMU is loaded and ready
     * @return True if FMU is loaded
     */
    UFUNCTION(BlueprintPure, Category = "FMU Simulation")
    bool IsFMULoaded() const;

    /**
     * Get the current simulation time
     * @return Current simulation time in seconds
     */
    UFUNCTION(BlueprintPure, Category = "FMU Simulation")
    double GetSimulationTime() const;

    /**
     * Set the fixed timestep for FMU updates
     * @param TimeStep New timestep in seconds
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Simulation")
    void SetFixedTimeStep(float TimeStep);

    /**
     * Get the current fixed timestep
     * @return Current timestep in seconds
     */
    UFUNCTION(BlueprintPure, Category = "FMU Simulation")
    float GetFixedTimeStep() const { return FixedTimeStep; }

    /**
     * Initialize input handling for the FMU simulation
     * @param InputConfiguration Input configuration to use
     * @return True if input handling was successfully initialized
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Simulation")
    bool InitializeInputHandling(const FFMUInputConfiguration& InputConfiguration);

    /**
     * Set the input configuration for the simulation
     * @param InputConfiguration New input configuration
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Simulation")
    void SetInputConfiguration(const FFMUInputConfiguration& InputConfiguration);

    /**
     * Get the current input configuration
     * @return Current input configuration
     */
    UFUNCTION(BlueprintPure, Category = "FMU Simulation")
    const FFMUInputConfiguration& GetInputConfiguration() const;

    /**
     * Enable or disable input processing for the simulation
     * @param bEnabled Whether input processing should be enabled
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Simulation")
    void SetInputProcessingEnabled(bool bEnabled);

    /**
     * Check if input processing is enabled
     * @return True if input processing is enabled
     */
    UFUNCTION(BlueprintPure, Category = "FMU Simulation")
    bool IsInputProcessingEnabled() const;

public:
    /** Event fired when simulation state changes */
    UPROPERTY(BlueprintAssignable, Category = "FMU Simulation|Events")
    FOnSimulationStateChanged OnSimulationStateChanged;

    /** Event fired when a variable value changes */
    UPROPERTY(BlueprintAssignable, Category = "FMU Simulation|Events")
    FOnVariableChanged OnVariableChanged;

    /** Evento C++ chiamato dopo ogni step di integrazione dell'FMU (non esposto a BP) */
    FOnFmuStep OnFmuStep;

private:
    /** Update variable observers with current FMU state */
    void UpdateObservers();

    /** Perform a single simulation step */
    bool PerformSimulationStep();

    /** Process inputs for the current simulation step */
    void ProcessInputsForSimulationStep();

    /** Convert coordinates from FMU (right-handed) to Unreal (left-handed) */
    FVector ConvertCoordinates(const FVector& RightHandedVector) const;

    /** Convert rotation matrix from FMU to Unreal coordinate system */
    FQuat ConvertRotation(const FMatrix& RotationMatrix) const;

    /** Get transform for a specific frame from FMU variables */
    bool GetFrameTransform(const FString& FrameName, FTransform& OutTransform) const;

    /** Clean up FMU resources */
    void CleanupFMU();

private:
    /** The FMU integration instance */
    TUniquePtr<FFMUIntegration> FMUIntegration;

    /** Map of variable names to their observer actors */
    UPROPERTY()
    TMap<FString, AActor*> VariableObservers;

    /** Current simulation configuration */
    UPROPERTY()
    FFMUConfiguration Configuration;

    /** Whether the simulation is currently running */
    UPROPERTY()
    bool bIsSimulating;

    /** Accumulated time for fixed timestep updates */
    float AccumulatedTime;

    /** Fixed timestep for FMU updates */
    float FixedTimeStep;

    /** Current simulation time */
    double SimulationTime;

    /** Conversion factor from meters to centimeters */
    static constexpr float MetersToCentimeters = 100.0f;
};