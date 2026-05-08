#pragma once

#include "CoreMinimal.h"
#include "IWebSocket.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WebSocketManager.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWebSocketClientEvent, const FString&, SensorName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWebSocketInputCommand, const FString&, CommandJson);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWebSocketCaptureCommand, const FString&, CommandJson);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWebSocketShutdownCommand, const FString&, CommandJson);

/**
 * Manages WebSocket connections for sensor data streaming
 */
UCLASS(BlueprintType)
class PROXSIMA_API UWebSocketManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // Initialize and Deinitialize are key subsystem lifecycle methods
    virtual void Initialize(FSubsystemCollectionBase &Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "WebSocket")
    bool InitializeWebSocketServer();

    /** Initialize the WebSocket server on the specified port */
    UFUNCTION(BlueprintCallable, Category = "WebSocket")
    bool InitializeWebSocket(int32 Port);

    /** Shut down the WebSocket server */
    UFUNCTION(BlueprintCallable, Category = "WebSocket")
    void ShutdownWebSocket();

    /** Register a new sensor endpoint */
    UFUNCTION(BlueprintCallable, Category = "WebSocket")
    FString RegisterSensorEndpoint(const FString& SensorName, const FString& SensorType);

    /** Send binary data to all clients connected to a specific sensor */
    UFUNCTION(BlueprintCallable, Category = "WebSocket")
    bool SendSensorData(const FString& SensorName, const TArray<uint8>& Data, float SimulationTime = -1.0f);


    /** Get the number of connected clients for a sensor (reported by server) */
    UFUNCTION(BlueprintCallable, Category = "WebSocket")
    int32 GetSensorClientCount(const FString& SensorName) const;

    /** Get the WebSocket URL for a specific sensor */
    UFUNCTION(BlueprintCallable, Category = "WebSocket")
    FString GetSensorWebSocketURL(const FString& SensorName) const;

    /** Generate HTML viewer for a sensor */
    UFUNCTION(BlueprintCallable, Category = "WebSocket")
    FString GenerateHTMLViewer(const FString& SensorName, const FString& SensorType) const;

    /** Generate and save HTML viewer for a sensor to the specified output path */
    UFUNCTION(BlueprintCallable, Category = "WebSocket")
    bool SaveHTMLViewer(const FString& SensorName, const FString& SensorType, const FString& OutputPath);

    /** Event triggered when a client connects to a sensor */
    UPROPERTY(BlueprintAssignable, Category = "WebSocket|Events")
    FWebSocketClientEvent OnClientConnected;

    /** Event triggered when a client disconnects from a sensor */
    UPROPERTY(BlueprintAssignable, Category = "WebSocket|Events")
    FWebSocketClientEvent OnClientDisconnected;

    /** Check if the WebSocket server is running */
    UFUNCTION(BlueprintCallable, Category = "WebSocket")
    bool IsRunning() const { return bIsRunning; }

    /** Send FMU input command to external GNC system */
    UFUNCTION(BlueprintCallable, Category = "WebSocket|Input")
    bool SendInputCommand(const FString& CommandJson);

    /** Register for input command notifications */
    UFUNCTION(BlueprintCallable, Category = "WebSocket|Input")
    void RegisterInputCommandHandler();

    /** Unregister from input command notifications */
    UFUNCTION(BlueprintCallable, Category = "WebSocket|Input")
    void UnregisterInputCommandHandler();

    /** Send acknowledgment for processed input command */
    UFUNCTION(BlueprintCallable, Category = "WebSocket|Input")
    bool SendInputCommandAck(const FString& CommandId, bool bSuccess, const FString& ErrorMessage = TEXT(""));

    /** Request input timeseries from external system */
    UFUNCTION(BlueprintCallable, Category = "WebSocket|Input")
    bool RequestInputTimeseries(const FString& VariableName, double StartTime, double EndTime);

    /** Broadcast simulation state change to all connected clients */
    UFUNCTION(BlueprintCallable, Category = "WebSocket|Simulation")
    void BroadcastSimulationStateChange(bool bIsSimulationStarting);

    /** Register for capture command notifications */
    UFUNCTION(BlueprintCallable, Category = "WebSocket|Capture")
    void RegisterCaptureCommandHandler();

    /** Unregister from capture command notifications */
    UFUNCTION(BlueprintCallable, Category = "WebSocket|Capture")
    void UnregisterCaptureCommandHandler();

    /** Send acknowledgment for processed capture command */
    UFUNCTION(BlueprintCallable, Category = "WebSocket|Capture")
    bool SendCaptureCommandAck(const FString& CommandId, const FString& CameraName, bool bSuccess, const FString& ErrorMessage = TEXT(""));

    /** Register for shutdown command notifications */
    UFUNCTION(BlueprintCallable, Category = "WebSocket|Shutdown")
    void RegisterShutdownCommandHandler();

    /** Unregister from shutdown command notifications */
    UFUNCTION(BlueprintCallable, Category = "WebSocket|Shutdown")
    void UnregisterShutdownCommandHandler();

    /** Send acknowledgment for processed shutdown command */
    UFUNCTION(BlueprintCallable, Category = "WebSocket|Shutdown")
    bool SendShutdownCommandAck(const FString& CommandId, bool bSuccess, const FString& ErrorMessage = TEXT(""));

    /** Event triggered when an input command is received */
    UPROPERTY(BlueprintAssignable, Category = "WebSocket|Events")
    FWebSocketInputCommand OnInputCommandReceived;

    /** Event triggered when a capture command is received */
    UPROPERTY(BlueprintAssignable, Category = "WebSocket|Events")
    FWebSocketCaptureCommand OnCaptureCommandReceived;

    /** Event triggered when a shutdown command is received */
    UPROPERTY(BlueprintAssignable, Category = "WebSocket|Events")
    FWebSocketShutdownCommand OnShutdownCommandReceived;

    /** Check if control clients are connected and ready */
    UFUNCTION(BlueprintCallable, Category = "WebSocket|Control")
    bool HasControlClientsConnected() const;

private:
    // Timer handle for keep-alive messages
    FTimerHandle KeepAliveTimerHandle;

    // Flag to track if the connection has been established
    bool bConnectionEstablished;

    // Track control client connection status (reported by server)
    bool bHasControlClientsConnected;

    // Track sensor client counts (reported by server)
    TMap<FString, int32> SensorClientCounts;

    // Track the last time we received a message from the server
    FDateTime LastMessageTime;

    // Track the last time we attempted to reconnect
    FDateTime LastReconnectTime;

    // Methods for connection management
    void StartKeepAliveTimer();
    void SendKeepAlive();

    void AttemptConnection(int32 Port, int32 RetryCount, const int32 MaxRetries);

    /** Whether the server is currently running */
    bool bIsRunning;

    /** Whether input command handling is registered */
    bool bInputCommandHandlerRegistered;

    /** Whether capture command handling is registered */
    bool bCaptureCommandHandlerRegistered;

    /** Whether shutdown command handling is registered */
    bool bShutdownCommandHandlerRegistered;

public:
    // Add a tick function to monitor the connection
    virtual void Tick(float DeltaTime);

    /** The WebSocket connection to the external server */
    TSharedPtr<IWebSocket> ServerConnection;

    /** Map of sensor names to their endpoint paths */
    TMap<FString, FString> SensorEndpoints;

    /** The port the server is running on */
    int32 ServerPort;

    /** Process ID of the external Node.js server */
    FProcHandle NodeServerProcess;

    /** Start the external Node.js WebSocket server */
    bool StartExternalServer(int32 Port);

    /** Stop the external Node.js WebSocket server */
    void StopExternalServer();

    /** Connect to the external WebSocket server */
    bool ConnectToServer();

    /** Handle messages from the external server */
    void HandleServerMessage(const FString& Message);
};
