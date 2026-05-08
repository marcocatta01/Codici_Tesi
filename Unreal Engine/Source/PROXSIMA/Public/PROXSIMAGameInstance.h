#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Subsystems/SimulationConfigurationSubsystem.h"
#include "FMUConfiguration.h"
#include "Sensors/SensorManager.h"
#include "WebSocketManager.h"
#include "PROXSIMAGameInstance.generated.h"

// Forward declarations to reduce includes
class UAssetLoadingSubsystem;
class UFMUSimulationSubsystem;
class UActorSpawningSubsystem;
class USequenceManager;
class UOnDemandCaptureManager;

/**
 * Game instance class that orchestrates PROXSIMA's dual-mode simulation system
 * Acts as a high-level coordinator between subsystems for FMU and sequence modes
 *
 * Responsibilities:
 * - Coordinate initialization between subsystems
 * - Provide unified API for Blueprint and external access
 * - Manage simulation lifecycle and mode switching
 * - Maintain backward compatibility with existing interfaces
 *
 * Implementation delegated to specialized subsystems:
 * - USimulationConfigurationSubsystem: Configuration parsing and validation
 * - UAssetLoadingSubsystem: Async asset loading and 3D model importing
 * - UFMUSimulationSubsystem: FMU simulation loop and variable management
 * - UActorSpawningSubsystem: Visual component and frame actor creation
 */
UCLASS()
class PROXSIMA_API UPROXSIMAGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    UPROXSIMAGameInstance();
    virtual ~UPROXSIMAGameInstance();

    // Store the config filename chosen from UI
    UPROPERTY(BlueprintReadWrite, Category = "Config File")
    FString ConfigPathFromUI;

    // Track initialization status
    UPROPERTY(BlueprintReadWrite, Category = "Config File")
    bool bIsInitialized = false;

    // Delegate to notify UI when initialization is done
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInitializationComplete);
    UPROPERTY(BlueprintAssignable)
    FOnInitializationComplete OnInitializationComplete;

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    bool InitializeSimulation(const FString &ConfigPath);

    // Keep old function name for backward compatibility
    UFUNCTION(BlueprintCallable, Category = "FMU")
    bool InitializeFMU(const FString &ConfigPath) { return InitializeSimulation(ConfigPath); }

    UFUNCTION(BlueprintCallable, Category = "FMU")
    bool RegisterVariableObserver(const FString &VariableName, AActor *Component);

    UFUNCTION(BlueprintCallable, Category = "FMU")
    void UnregisterVariableObserver(AActor *Component);

    UFUNCTION(BlueprintCallable, Category = "FMU")
    void StartSimulation();

    UFUNCTION(BlueprintCallable, Category = "FMU")
    void PauseSimulation();

    UFUNCTION(BlueprintCallable, Category = "FMU")
    bool GetVariableValue(const FString &VariableName, double &OutValue) const;

    UFUNCTION(BlueprintCallable, Category = "FMU")
    bool SetVariableValue(const FString &VariableName, double Value);

    /** Initialize and spawn actors based on configuration */
    UFUNCTION(BlueprintCallable, Category = "Simulation")
    void InitializeSimulationActors(UWorld *World);

    /** Set the current model description */
    UFUNCTION(BlueprintCallable, Category = "FMU")
    void SetModelDescription(UFMUModelDescription *InModelDescription) { ModelDescription = InModelDescription; }

    /** Legacy update function - now delegates to subsystem */
    UFUNCTION()
    void UpdateFMU(float DeltaTime);

    /** Get the current simulation mode */
    UFUNCTION(BlueprintPure, Category = "Simulation")
    ESimulationMode GetCurrentSimulationMode() const;

    /** Get the current model description */
    UFUNCTION(BlueprintPure, Category = "FMU")
    UFMUModelDescription *GetModelDescription() const { return ModelDescription; }

    /** Get the sensor manager */
    UFUNCTION(BlueprintCallable, Category = "Sensors")
    USensorManager *GetSensorManager() const { return SensorManager; }

    /** Get a sensor by name */
    UFUNCTION(BlueprintCallable, Category = "Sensors")
    AActor *GetSensorByName(const FString &SensorName) const;

    /** Get frame actor by name */
    UFUNCTION(BlueprintCallable, Category = "FMU")
    AActor *GetFrameActor(const FString &FrameName) const;

    /** Get the WebSocket manager */
    UFUNCTION(BlueprintCallable, Category = "PROXSIMA")
    UWebSocketManager *GetWebSocketManager() const
    {
        return GetSubsystem<UWebSocketManager>();
    }

    /** Request application shutdown */
    UFUNCTION()
    void RequestShutdown();

    /** Get the global streaming port */
    UFUNCTION(BlueprintPure, Category = "WebSocket")
    int32 GetGlobalStreamingPort() const { return GlobalStreamingPort; }

    /** Get the sequence manager */
    UFUNCTION(BlueprintPure, Category = "Sequence")
    USequenceManager* GetSequenceManager() const { return SequenceManager; }

    /** Get the ondemand capture manager */
    UFUNCTION(BlueprintPure, Category = "OnDemand")
    UOnDemandCaptureManager* GetOnDemandCaptureManager() const { return OnDemandCaptureManager; }

    /** Check if simulation is running (public access for sensors) */
    UFUNCTION(BlueprintPure, Category = "Simulation")
    bool GetIsSimulating() const;

    /** Check if any input variables use WebSocket as input source */
    UFUNCTION(BlueprintPure, Category = "WebSocket")
    bool HasWebSocketInputSources() const;

protected:
    virtual void BeginDestroy() override;

private:
    /** Initialize FMU mode from configuration */
    bool InitializeFMUMode(const FFMUConfiguration& Configuration);

    /** Initialize sequence mode from configuration */
    bool InitializeSequenceMode(const FString &ConfigPath);

    /** Initialize ondemand mode from configuration */
    bool InitializeOnDemandMode(const FFMUConfiguration& Configuration);

    /** Callback when assets are loaded */
    UFUNCTION()
    void OnAssetsLoaded(UWorld* World, const TArray<UObject*>& LoadedAssets);

    /** Internal callback wrapper for asset loading */
    UFUNCTION()
    void OnAssetsLoadedDelegate(const TArray<UObject*>& LoadedAssets);

    /** Callback when mesh processing is complete and simulation can start */
    UFUNCTION()
    void OnMeshProcessingComplete();

    /** Handle WebSocket capture commands for OnDemand mode */
    UFUNCTION()
    void HandleWebSocketCaptureCommand(const FString& CommandJson);

    /** Handle WebSocket shutdown commands */
    UFUNCTION()
    void HandleWebSocketShutdownCommand(const FString& CommandJson);

    /** Check control connection readiness periodically */
    UFUNCTION()
    void CheckControlConnectionsReady();

    /** Verify that all critical subsystems are ready for simulation */
    bool VerifySubsystemsReady();

    /** Verify that sensor streaming is properly set up and ready */
    bool CheckSensorStreamingReady();

    /** Notify that initialization is complete for the current simulation mode */
    void NotifyInitializationComplete();

    /** Start FMU simulation with proper logging and UI feedback */
    void StartFMUSimulation();

private:
    /** Model description object that persists between level transitions */
    UPROPERTY()
    UFMUModelDescription *ModelDescription;

    /** Sensor manager for handling all sensors in the simulation */
    UPROPERTY()
    USensorManager *SensorManager;

    /** Property to store the streaming port */
    UPROPERTY(BlueprintReadOnly, Category = "WebSocket", meta = (AllowPrivateAccess = "true"))
    int32 GlobalStreamingPort;

    /** Sequence mode manager */
    UPROPERTY()
    USequenceManager *SequenceManager;

    /** OnDemand mode capture manager */
    UPROPERTY()
    UOnDemandCaptureManager *OnDemandCaptureManager;

    /** Temporary storage for world during asset loading */
    UPROPERTY()
    UWorld* PendingWorld;

    /** Whether we're waiting for control connections before starting simulation */
    bool bWaitingForControlConnections = false;

    /** Timer handle for checking control connection readiness */
    FTimerHandle ControlConnectionCheckTimer;

    /** Timer handle for checking subsystem readiness */
    FTimerHandle SubsystemReadinessTimer;
};
