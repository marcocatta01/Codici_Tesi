// Fill out your copyright notice in the Description page of Project Settings.

#include "PROXSIMAGameInstance.h"
#include "Subsystems/SimulationConfigurationSubsystem.h"
#include "Subsystems/AssetLoadingSubsystem.h"
#include "Subsystems/FMUSimulationSubsystem.h"
#include "Subsystems/ActorSpawningSubsystem.h"
#include "Subsystems/FMUInputHandlingSubsystem.h"
#include "Subsystems/MeshProcessingSubsystem.h"
#include "Subsystems/RoverGroundingSubsystem.h"
#include "FMUConfiguration.h"
#include "FMUModelDescription.h"
#include "FMUhandling/SequenceManager.h"
#include "FMUhandling/OnDemandCaptureManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Sensors/SensorManager.h"
#include "WebSocketManager.h"

UPROXSIMAGameInstance::UPROXSIMAGameInstance()
{
    GlobalStreamingPort = 0;

    // Create sensor manager
    SensorManager = CreateDefaultSubobject<USensorManager>(TEXT("SensorManager"));

    // Initialize pointers
    SequenceManager = nullptr;
    OnDemandCaptureManager = nullptr;
    ModelDescription = nullptr;
    PendingWorld = nullptr;
}

UPROXSIMAGameInstance::~UPROXSIMAGameInstance()
{
    // Cleanup is now handled by subsystems in their Deinitialize methods
}

bool UPROXSIMAGameInstance::InitializeSimulation(const FString &ConfigPath)
{
    // Store config path for UI
    ConfigPathFromUI = ConfigPath;

    // Get configuration subsystem and load configuration
    USimulationConfigurationSubsystem *ConfigSubsystem = GetSubsystem<USimulationConfigurationSubsystem>();
    if (!ConfigSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("SimulationConfigurationSubsystem not available"));
        return false;
    }

    FFMUConfiguration Configuration;
    ESimulationMode SimulationMode;
    if (!ConfigSubsystem->LoadConfiguration(ConfigPath, Configuration, SimulationMode))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load configuration from: %s"), *ConfigPath);
        return false;
    }

    // Store streaming port from configuration
    GlobalStreamingPort = Configuration.streamingPort;

    // Initialize WebSocket server if needed
    if (GlobalStreamingPort > 0)
    {
        if (UWebSocketManager *WebSocketManager = GetSubsystem<UWebSocketManager>())
        {
            WebSocketManager->InitializeWebSocket(GlobalStreamingPort);

            // Register shutdown command handler (available for all simulation modes)
            WebSocketManager->RegisterShutdownCommandHandler();
            WebSocketManager->OnShutdownCommandReceived.AddDynamic(this, &UPROXSIMAGameInstance::HandleWebSocketShutdownCommand);
            UE_LOG(LogTemp, Log, TEXT("Shutdown command handling integration with WebSocket established"));

            // Set up input handling integration for FMU mode
            if (SimulationMode == ESimulationMode::SM_FMU)
            {
                UFMUInputHandlingSubsystem *InputSubsystem = GetSubsystem<UFMUInputHandlingSubsystem>();
                if (InputSubsystem)
                {
                    // Register WebSocket input command handler
                    WebSocketManager->RegisterInputCommandHandler();

                    // Bind WebSocket input commands to input handling subsystem
                    WebSocketManager->OnInputCommandReceived.AddDynamic(InputSubsystem, &UFMUInputHandlingSubsystem::HandleWebSocketInputCommandDelegate);

                    UE_LOG(LogTemp, Log, TEXT("Input handling integration with WebSocket established"));
                }
            }
        }
    }

    // Initialize based on simulation mode
    bool bSuccess = false;
    if (SimulationMode == ESimulationMode::SM_FMU)
    {
        bSuccess = InitializeFMUMode(Configuration);
    }
    else if (SimulationMode == ESimulationMode::SM_Sequence)
    {
        bSuccess = InitializeSequenceMode(ConfigPath);
    }
    else if (SimulationMode == ESimulationMode::SM_OnDemand)
    {
        bSuccess = InitializeOnDemandMode(Configuration);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid simulation mode"));
        return false;
    }

    if (bSuccess)
    {
        bIsInitialized = true;
        OnInitializationComplete.Broadcast();
        UE_LOG(LogTemp, Log, TEXT("Simulation initialized successfully"));
    }

    return bSuccess;
}

bool UPROXSIMAGameInstance::InitializeFMUMode(const FFMUConfiguration &Configuration)
{
    // Get FMU simulation subsystem
    UFMUSimulationSubsystem *FMUSubsystem = GetSubsystem<UFMUSimulationSubsystem>();
    if (!FMUSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("FMUSimulationSubsystem not available"));
        return false;
    }

    // Load and initialize FMU
    if (!FMUSubsystem->LoadFMU(Configuration.fmuPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load FMU from: %s"), *Configuration.fmuPath);
        return false;
    }

    if (!FMUSubsystem->InitializeFMU(Configuration))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to initialize FMU"));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("FMU mode initialized successfully"));
    return true;
}

bool UPROXSIMAGameInstance::InitializeSequenceMode(const FString &ConfigPath)
{
    if (!SequenceManager)
    {
        SequenceManager = NewObject<USequenceManager>(this);
        if (SequenceManager)
        {
            SequenceManager->OnSequenceComplete.AddDynamic(this, &UPROXSIMAGameInstance::RequestShutdown);
        }
    }

    // Let the configuration subsystem handle sequence-specific parsing
    USimulationConfigurationSubsystem *ConfigSubsystem = GetSubsystem<USimulationConfigurationSubsystem>();
    if (!ConfigSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("SimulationConfigurationSubsystem not available"));
        return false;
    }

    // The sequence file path should be stored in the configuration
    const FFMUConfiguration &Configuration = ConfigSubsystem->GetCurrentConfiguration();
    if (!Configuration.fmuPath.IsEmpty() && SequenceManager)
    {
        // Extract valid camera names from sensor configuration for validation
        TArray<FString> ValidCameraNames;
        for (const FFMUSensorConfig& SensorConfig : Configuration.sensors)
        {
            if (SensorConfig.type.Equals(TEXT("Camera"), ESearchCase::IgnoreCase))
            {
                ValidCameraNames.Add(SensorConfig.name);
            }
        }

        if (!SequenceManager->LoadSequence(Configuration.fmuPath, ValidCameraNames))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to load camera sequence from: %s"), *Configuration.fmuPath);
            return false;
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Sequence mode initialized successfully"));
    return true;
}

bool UPROXSIMAGameInstance::InitializeOnDemandMode(const FFMUConfiguration& Configuration)
{
    if (!OnDemandCaptureManager)
    {
        OnDemandCaptureManager = NewObject<UOnDemandCaptureManager>(this);
    }

    if (!OnDemandCaptureManager)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create OnDemandCaptureManager"));
        return false;
    }

    // Initialize the capture manager with the sensor manager
    if (!OnDemandCaptureManager->Initialize(SensorManager))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to initialize OnDemandCaptureManager"));
        return false;
    }

    // Set up WebSocket capture command integration
    if (UWebSocketManager* WebSocketManager = GetSubsystem<UWebSocketManager>())
    {
        // Register capture command handler
        WebSocketManager->RegisterCaptureCommandHandler();
        
        // Bind WebSocket capture commands to on-demand capture processing
        WebSocketManager->OnCaptureCommandReceived.AddDynamic(this, &UPROXSIMAGameInstance::HandleWebSocketCaptureCommand);
        
        UE_LOG(LogTemp, Log, TEXT("WebSocket capture command integration established"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("WebSocketManager not available for OnDemand mode"));
    }

    UE_LOG(LogTemp, Log, TEXT("OnDemand mode initialized successfully"));
    return true;
}

// Delegate methods to subsystems for backward compatibility
bool UPROXSIMAGameInstance::RegisterVariableObserver(const FString &VariableName, AActor *Component)
{
    UFMUSimulationSubsystem *FMUSubsystem = GetSubsystem<UFMUSimulationSubsystem>();
    return FMUSubsystem ? FMUSubsystem->RegisterVariableObserver(VariableName, Component) : false;
}

void UPROXSIMAGameInstance::UnregisterVariableObserver(AActor *Component)
{
    UFMUSimulationSubsystem *FMUSubsystem = GetSubsystem<UFMUSimulationSubsystem>();
    if (FMUSubsystem)
    {
        FMUSubsystem->UnregisterVariableObserver(Component);
    }
}

void UPROXSIMAGameInstance::StartSimulation()
{
    UFMUSimulationSubsystem *FMUSubsystem = GetSubsystem<UFMUSimulationSubsystem>();
    if (FMUSubsystem)
    {
        FMUSubsystem->StartSimulation();
    }
}

void UPROXSIMAGameInstance::PauseSimulation()
{
    UFMUSimulationSubsystem *FMUSubsystem = GetSubsystem<UFMUSimulationSubsystem>();
    if (FMUSubsystem)
    {
        FMUSubsystem->PauseSimulation();
    }
}

bool UPROXSIMAGameInstance::GetVariableValue(const FString &VariableName, double &OutValue) const
{
    UFMUSimulationSubsystem *FMUSubsystem = GetSubsystem<UFMUSimulationSubsystem>();
    return FMUSubsystem ? FMUSubsystem->GetVariableValue(VariableName, OutValue) : false;
}

bool UPROXSIMAGameInstance::SetVariableValue(const FString &VariableName, double Value)
{
    UFMUSimulationSubsystem *FMUSubsystem = GetSubsystem<UFMUSimulationSubsystem>();
    return FMUSubsystem ? FMUSubsystem->SetVariableValue(VariableName, Value) : false;
}

ESimulationMode UPROXSIMAGameInstance::GetCurrentSimulationMode() const
{
    USimulationConfigurationSubsystem *ConfigSubsystem = GetSubsystem<USimulationConfigurationSubsystem>();
    return ConfigSubsystem ? ConfigSubsystem->GetCurrentMode() : ESimulationMode::SM_None;
}

AActor *UPROXSIMAGameInstance::GetFrameActor(const FString &FrameName) const
{
    UFMUSimulationSubsystem *FMUSubsystem = GetSubsystem<UFMUSimulationSubsystem>();
    return FMUSubsystem ? FMUSubsystem->GetFrameActor(FrameName) : nullptr;
}

AActor *UPROXSIMAGameInstance::GetSensorByName(const FString &SensorName) const
{
    return SensorManager ? SensorManager->GetSensorByName(SensorName) : nullptr;
}

void UPROXSIMAGameInstance::InitializeSimulationActors(UWorld *World)
{
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot initialize simulation actors: Invalid world"));
        return;
    }

    // Get current configuration
    USimulationConfigurationSubsystem *ConfigSubsystem = GetSubsystem<USimulationConfigurationSubsystem>();
    if (!ConfigSubsystem || !ConfigSubsystem->IsConfigurationLoaded())
    {
        UE_LOG(LogTemp, Error, TEXT("No configuration loaded"));
        return;
    }

    const FFMUConfiguration &Configuration = ConfigSubsystem->GetCurrentConfiguration();

    // Load assets for visual components
    UAssetLoadingSubsystem *AssetLoadingSubsystem = GetSubsystem<UAssetLoadingSubsystem>();
    if (!AssetLoadingSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("AssetLoadingSubsystem not available"));
        return;
    }

    // Store world for the callback
    PendingWorld = World;

    // Bind the callback and start asset loading
    FOnAssetsLoadedSingleDelegate OnAssetsLoaded;
    OnAssetsLoaded.BindDynamic(this, &UPROXSIMAGameInstance::OnAssetsLoadedDelegate);
    AssetLoadingSubsystem->LoadAssetsForVisualComponents(Configuration.visualComponents, OnAssetsLoaded);
}

void UPROXSIMAGameInstance::OnAssetsLoaded(UWorld *World, const TArray<UObject *> &LoadedAssets)
{
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid world in OnAssetsLoaded"));
        return;
    }

    // Get subsystems
    USimulationConfigurationSubsystem *ConfigSubsystem = GetSubsystem<USimulationConfigurationSubsystem>();
    UActorSpawningSubsystem *ActorSpawningSubsystem = GetSubsystem<UActorSpawningSubsystem>();
    UFMUSimulationSubsystem *FMUSubsystem = GetSubsystem<UFMUSimulationSubsystem>();

    if (!ConfigSubsystem || !ActorSpawningSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("Required subsystems not available"));
        return;
    }

    const FFMUConfiguration &Configuration = ConfigSubsystem->GetCurrentConfiguration();

    // Spawn visual component actors
    ActorSpawningSubsystem->SpawnSimulationActors(World, Configuration.visualComponents, LoadedAssets);

    // Create frame actors for sensors
    ActorSpawningSubsystem->CreateFrameActorsForSensors(World, Configuration.sensors);

    // Start mesh processing tracking instead of immediately starting simulation
    UMeshProcessingSubsystem* MeshProcessingSubsystem = GetSubsystem<UMeshProcessingSubsystem>();
    if (MeshProcessingSubsystem)
    {
        // Bind to mesh processing completion event
        MeshProcessingSubsystem->OnMeshProcessingComplete.AddDynamic(this, &UPROXSIMAGameInstance::OnMeshProcessingComplete);
        
        // Start tracking mesh processing
        MeshProcessingSubsystem->StartMeshProcessingTracking();
        UE_LOG(LogTemp, Log, TEXT("Started mesh processing tracking - simulation will begin when meshes are ready"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("MeshProcessingSubsystem not available - starting simulation immediately"));
        // Fallback: start simulation immediately if subsystem is not available
        OnMeshProcessingComplete();
    }
}

void UPROXSIMAGameInstance::OnMeshProcessingComplete()
{
    UE_LOG(LogTemp, Log, TEXT("Mesh processing completed - registering frame actors, initializing sensors, then checking control connections"));

    // Add onscreen debug message
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green, TEXT("Mesh processing completed"));
    }

    // Get subsystems
    USimulationConfigurationSubsystem* ConfigSubsystem = GetSubsystem<USimulationConfigurationSubsystem>();
    UActorSpawningSubsystem* ActorSpawningSubsystem = GetSubsystem<UActorSpawningSubsystem>();
    UFMUSimulationSubsystem* FMUSubsystem = GetSubsystem<UFMUSimulationSubsystem>();

    // Grounding Subsystem
    if (URoverGroundingSubsystem* Grounding = GetSubsystem<URoverGroundingSubsystem>())
    {
        if (Grounding->HasValidSetup())
        {
            bool bPreOk = Grounding->PreprocessInputTimeseries(GetWorld());
            UE_LOG(LogTemp, Log, TEXT("[Grounding] PreprocessInputTimeseries %s"), bPreOk ? TEXT("OK") : TEXT("FAILED"));
        }
    }

    if (!ConfigSubsystem || !ActorSpawningSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("Required subsystems not available in OnMeshProcessingComplete"));
        return;
    }

    const FFMUConfiguration& Configuration = ConfigSubsystem->GetCurrentConfiguration();

    // FIRST: Register frame actors with FMU simulation subsystem
    // This must happen before sensor initialization so sensors can find frame actors
    if (FMUSubsystem && ConfigSubsystem->GetCurrentMode() == ESimulationMode::SM_FMU)
    {
        const auto& AllSpawnedActors = ActorSpawningSubsystem->GetAllSpawnedActors();
        UE_LOG(LogTemp, Warning, TEXT("Registering %d spawned actors as FMU variable observers"), AllSpawnedActors.Num());

        for (const auto& Pair : AllSpawnedActors)
        {
            UE_LOG(LogTemp, Warning, TEXT("Registering observer: %s -> %p"), *Pair.Key, Pair.Value);
            FMUSubsystem->RegisterVariableObserver(Pair.Key, Pair.Value);
        }
        UE_LOG(LogTemp, Log, TEXT("Frame actors registered successfully"));

        // Apply initial conditions immediately after registration
        // This positions all frame actors correctly before sensors are attached
        FMUSubsystem->ApplyInitialConditions();
    }

    // SECOND: Initialize sensors after frame actors are registered
    // This ensures sensors can find and attach to the registered frame actors
    if (SensorManager && Configuration.sensors.Num() > 0)
    {
        UE_LOG(LogTemp, Log, TEXT("Initializing %d sensors from configuration"), Configuration.sensors.Num());
        SensorManager->InitializeSensors(GetWorld(), Configuration.sensors);
        UE_LOG(LogTemp, Log, TEXT("Sensors initialized and attached to frame actors successfully"));
    }

    // Check if WebSocket control sources are configured before starting simulation
    if (HasWebSocketInputSources())
    {
        UE_LOG(LogTemp, Warning, TEXT("WebSocket input sources detected - waiting for control connections before starting simulation"));
        
        // Add onscreen debug message
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(1100, 30.0f, FColor::Yellow, TEXT("Waiting for WebSocket control connections..."));
        }

        // Set waiting state and start monitoring
        bWaitingForControlConnections = true;
        
        // Start timer to check control connections every 1 second
        GetWorld()->GetTimerManager().SetTimer(
            ControlConnectionCheckTimer,
            this,
            &UPROXSIMAGameInstance::CheckControlConnectionsReady,
            1.0f, // Check every second
            true  // Loop
        );
        
        // Do NOT start simulation yet - wait for connections
        return;
    }

    // No WebSocket control sources - start simulation immediately
    UE_LOG(LogTemp, Log, TEXT("No WebSocket input sources detected - starting simulation immediately"));
    NotifyInitializationComplete();

    UE_LOG(LogTemp, Log, TEXT("Complete initialization sequence finished successfully"));
}

void UPROXSIMAGameInstance::UpdateFMU(float DeltaTime)
{
    // Legacy method - FMU updates are now handled automatically by UFMUSimulationSubsystem's Tick
    // This method is kept for backward compatibility but no longer needs implementation
}

void UPROXSIMAGameInstance::RequestShutdown()
{
    UE_LOG(LogTemp, Log, TEXT("Sequence complete - requesting application exit"));
    UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
}

void UPROXSIMAGameInstance::OnAssetsLoadedDelegate(const TArray<UObject *> &LoadedAssets)
{
    OnAssetsLoaded(PendingWorld, LoadedAssets);
    PendingWorld = nullptr; // Clear the reference
}

bool UPROXSIMAGameInstance::GetIsSimulating() const
{
    USimulationConfigurationSubsystem *ConfigSubsystem = GetSubsystem<USimulationConfigurationSubsystem>();
    if (!ConfigSubsystem)
    {
        return false;
    }

    ESimulationMode CurrentMode = ConfigSubsystem->GetCurrentMode();

    if (CurrentMode == ESimulationMode::SM_FMU)
    {
        UFMUSimulationSubsystem *FMUSubsystem = GetSubsystem<UFMUSimulationSubsystem>();
        return FMUSubsystem ? FMUSubsystem->IsSimulating() : false;
    }
    else if (CurrentMode == ESimulationMode::SM_Sequence)
    {
        // In sequence mode, we're always "simulating" once initialized
        return bIsInitialized && SequenceManager != nullptr;
    }
    else if (CurrentMode == ESimulationMode::SM_OnDemand)
    {
        // In ondemand mode, we're always "simulating" (ready to receive commands) once initialized
        return bIsInitialized && OnDemandCaptureManager != nullptr && OnDemandCaptureManager->IsReady();
    }

    return false;
}

void UPROXSIMAGameInstance::HandleWebSocketCaptureCommand(const FString& CommandJson)
{
    if (!OnDemandCaptureManager || !OnDemandCaptureManager->IsReady())
    {
        UE_LOG(LogTemp, Warning, TEXT("OnDemandCaptureManager not ready, ignoring capture command"));
        return;
    }

    // Parse the JSON command
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CommandJson);
    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("HandleWebSocketCaptureCommand: Failed to parse JSON: %s"), *CommandJson);
        return;
    }

    // Extract command ID for acknowledgment
    FString CommandId;
    JsonObject->TryGetStringField(TEXT("commandId"), CommandId);

    // Extract camera name
    FString CameraName;
    if (!JsonObject->TryGetStringField(TEXT("camera"), CameraName) || CameraName.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("HandleWebSocketCaptureCommand: Missing or empty camera name"));
        
        // Send failure acknowledgment
        if (UWebSocketManager* WebSocketManager = GetSubsystem<UWebSocketManager>())
        {
            WebSocketManager->SendCaptureCommandAck(CommandId, TEXT(""), false, TEXT("Missing camera name"));
        }
        return;
    }

    // Extract pose data
    const TSharedPtr<FJsonObject>* PoseObject;
    if (!JsonObject->TryGetObjectField(TEXT("pose"), PoseObject) || !(*PoseObject).IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("HandleWebSocketCaptureCommand: Missing pose data"));
        
        // Send failure acknowledgment
        if (UWebSocketManager* WebSocketManager = GetSubsystem<UWebSocketManager>())
        {
            WebSocketManager->SendCaptureCommandAck(CommandId, CameraName, false, TEXT("Missing pose data"));
        }
        return;
    }

    // Parse pose data into FSerializedCameraPose
    FSerializedCameraPose Pose;
    Pose.CameraName = CameraName;

    // Parse location
    const TSharedPtr<FJsonObject>* LocationObject;
    if ((*PoseObject)->TryGetObjectField(TEXT("location"), LocationObject))
    {
        double X = 0, Y = 0, Z = 0;
        (*LocationObject)->TryGetNumberField(TEXT("x"), X);
        (*LocationObject)->TryGetNumberField(TEXT("y"), Y);
        (*LocationObject)->TryGetNumberField(TEXT("z"), Z);
        Pose.Transform.SetLocation(FVector(X, Y, Z) * 100.0f); // Convert to centimeters
    }

    // Parse rotation (quaternion)
    const TSharedPtr<FJsonObject>* RotationObject;
    if ((*PoseObject)->TryGetObjectField(TEXT("rotation"), RotationObject))
    {
        double X = 0, Y = 0, Z = 0, W = 1;
        (*RotationObject)->TryGetNumberField(TEXT("x"), X);
        (*RotationObject)->TryGetNumberField(TEXT("y"), Y);
        (*RotationObject)->TryGetNumberField(TEXT("z"), Z);
        (*RotationObject)->TryGetNumberField(TEXT("w"), W);
        Pose.Transform.SetRotation(FQuat(X, Y, Z, W));
    }

    // Parse timestamp
    if ((*PoseObject)->HasField(TEXT("timestamp")))
    {
        const TSharedPtr<FJsonValue>& TimestampValue = (*PoseObject)->TryGetField(TEXT("timestamp"));
        if (TimestampValue->Type == EJson::String)
        {
            Pose.Timestamp = FCString::Atof(*TimestampValue->AsString());
        }
        else if (TimestampValue->Type == EJson::Number)
        {
            Pose.Timestamp = TimestampValue->AsNumber();
        }
    }

    // Parse metadata
    const TSharedPtr<FJsonObject>* MetadataObject;
    if ((*PoseObject)->TryGetObjectField(TEXT("metadata"), MetadataObject))
    {
        for (const auto& Pair : (*MetadataObject)->Values)
        {
            if (Pair.Key == TEXT("description"))
            {
                Pose.Description = Pair.Value->AsString();
            }
            else
            {
                Pose.Metadata.Add(Pair.Key, Pair.Value->AsString());
            }
        }
    }

    // Process the capture command
    bool bSuccess = OnDemandCaptureManager->ProcessCaptureCommand(CameraName, Pose);
    
    // Send acknowledgment
    if (UWebSocketManager* WebSocketManager = GetSubsystem<UWebSocketManager>())
    {
        FString ErrorMessage = bSuccess ? TEXT("") : TEXT("Failed to process capture command");
        WebSocketManager->SendCaptureCommandAck(CommandId, CameraName, bSuccess, ErrorMessage);
    }

    UE_LOG(LogTemp, Log, TEXT("HandleWebSocketCaptureCommand: Processed capture command for camera '%s', success: %s"), 
           *CameraName, bSuccess ? TEXT("true") : TEXT("false"));
}

void UPROXSIMAGameInstance::HandleWebSocketShutdownCommand(const FString& CommandJson)
{
    UE_LOG(LogTemp, Log, TEXT("HandleWebSocketShutdownCommand: Received shutdown command"));

    // Parse the JSON command
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CommandJson);
    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("HandleWebSocketShutdownCommand: Failed to parse JSON: %s"), *CommandJson);
        return;
    }

    // Extract command ID for acknowledgment
    FString CommandId;
    JsonObject->TryGetStringField(TEXT("commandId"), CommandId);

    // Send acknowledgment before shutdown
    if (UWebSocketManager* WebSocketManager = GetSubsystem<UWebSocketManager>())
    {
        WebSocketManager->SendShutdownCommandAck(CommandId, true, TEXT(""));
        UE_LOG(LogTemp, Log, TEXT("Sent shutdown command acknowledgment for command '%s'"), *CommandId);
    }

    // Small delay to ensure acknowledgment is sent before shutdown
    FTimerHandle ShutdownDelayTimer;
    GetWorld()->GetTimerManager().SetTimer(
        ShutdownDelayTimer,
        this,
        &UPROXSIMAGameInstance::RequestShutdown,
        2.0f, // 2s delay
        false // Don't loop
    );

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.0, FColor::Green, TEXT("Shutdown requested - proceeding in 2s..."));
    }
    UE_LOG(LogTemp, Log, TEXT("Shutdown requested via WebSocket command"));
}

bool UPROXSIMAGameInstance::HasWebSocketInputSources() const
{
    // Get the current configuration
    USimulationConfigurationSubsystem* ConfigSubsystem = GetSubsystem<USimulationConfigurationSubsystem>();
    if (!ConfigSubsystem || !ConfigSubsystem->IsConfigurationLoaded())
    {
        return false;
    }

    const FFMUConfiguration& Configuration = ConfigSubsystem->GetCurrentConfiguration();
    
    // Check if any input variables use WebSocket as source type
    for (const FFMUInputVariableConfig& InputVariable : Configuration.inputConfiguration.inputVariables)
    {
        for (const FFMUInputSource& InputSource : InputVariable.inputSources)
        {
            if (InputSource.sourceType == EFMUInputSourceType::WebSocket && InputSource.bEnabled)
            {
                UE_LOG(LogTemp, Log, TEXT("Found WebSocket input source for variable '%s'"), *InputVariable.variableName);
                return true;
            }
        }
    }
    
    return false;
}

void UPROXSIMAGameInstance::CheckControlConnectionsReady()
{
    if (!bWaitingForControlConnections)
    {
        // Clear timer if we're no longer waiting
        if (ControlConnectionCheckTimer.IsValid())
        {
            GetWorld()->GetTimerManager().ClearTimer(ControlConnectionCheckTimer);
        }
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("Checking control connection readiness..."));

    // Check if WebSocket control clients are connected
    UWebSocketManager* WebSocketManager = GetSubsystem<UWebSocketManager>();
    if (!WebSocketManager)
    {
        UE_LOG(LogTemp, Error, TEXT("WebSocketManager not available for control connection check"));
        return;
    }

    if (WebSocketManager->HasControlClientsConnected())
    {
        UE_LOG(LogTemp, Warning, TEXT("✅ WebSocket control connections established - starting simulation"));
        
        // Add onscreen debug message
        if (GEngine)
        {
            GEngine->RemoveOnScreenDebugMessage(1100);
            GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green, TEXT("WebSocket control connections established!"));
        }

        // Clear waiting state and timer
        bWaitingForControlConnections = false;
        if (ControlConnectionCheckTimer.IsValid())
        {
            GetWorld()->GetTimerManager().ClearTimer(ControlConnectionCheckTimer);
        }

        // Now start the simulation
        NotifyInitializationComplete();
    }
    else
    {
        // Still waiting - check for timeout (30 seconds)
        static float WaitTime = 0.0f;
        WaitTime += 1.0f; // Timer runs every 1 second
        
        if (WaitTime >= 30.0f)
        {
            UE_LOG(LogTemp, Warning, TEXT("⏰ Timeout waiting for control connections - starting simulation anyway"));
            
            // Add onscreen debug message
            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Orange, TEXT("Control connection timeout - starting simulation anyway"));
            }

            // Clear waiting state and timer
            bWaitingForControlConnections = false;
            WaitTime = 0.0f;
            if (ControlConnectionCheckTimer.IsValid())
            {
                GetWorld()->GetTimerManager().ClearTimer(ControlConnectionCheckTimer);
            }

            // Start simulation without control connections
            NotifyInitializationComplete();
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("Still waiting for control connections... (%.1fs elapsed)"), WaitTime);
        }
    }
}

bool UPROXSIMAGameInstance::CheckSensorStreamingReady()
{
    double CurrentTime = FPlatformTime::Seconds();
    UE_LOG(LogTemp, Warning, TEXT("[SENSOR-CHECK] t=%.3fs: Starting sensor streaming readiness verification"), CurrentTime);

    // Check if streaming is enabled (global streaming port > 0)
    if (GetGlobalStreamingPort() <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[SENSOR-CHECK] Global streaming disabled - skipping sensor checks"));
        return true; // Streaming is disabled, no need to wait
    }

    // Get WebSocketManager
    UWebSocketManager* WebSocketMgr = GetSubsystem<UWebSocketManager>();
    if (!WebSocketMgr)
    {
        UE_LOG(LogTemp, Error, TEXT("[SENSOR-CHECK] WebSocketManager not available"));
        return false;
    }

    if (!WebSocketMgr->IsRunning())
    {
        UE_LOG(LogTemp, Error, TEXT("[SENSOR-CHECK] WebSocketManager not running"));
        return false;
    }

    // Get all sensors that should be streaming
    if (!SensorManager)
    {
        UE_LOG(LogTemp, Error, TEXT("[SENSOR-CHECK] SensorManager not available"));
        return false;
    }

    TArray<AActor*> AllSensors = SensorManager->GetAllSensors();
    if (AllSensors.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[SENSOR-CHECK] No sensors found in simulation"));
        return true; // No sensors to check
    }

    TArray<FString> StreamingEnabledSensors;
    TArray<FString> ClientConnectionRequiredSensors;

    // Check each sensor to see if streaming is enabled
    for (AActor* SensorActor : AllSensors)
    {
        if (SensorActor && SensorActor->Implements<USensorInterface>())
        {
            // Check if this sensor has streaming enabled
            bool bStreamingEnabled = ISensorInterface::Execute_IsStreamingEnabled(SensorActor);
            if (bStreamingEnabled)
            {
                FString SensorName = ISensorInterface::Execute_GetSensorName(SensorActor);
                StreamingEnabledSensors.Add(SensorName);

                // Check if sensor is registered with WebSocket server
                if (!WebSocketMgr->SensorEndpoints.Contains(SensorName))
                {
                    UE_LOG(LogTemp, Error, TEXT("[SENSOR-CHECK] Streaming sensor '%s' not registered with WebSocket server yet"), *SensorName);
                    return false;
                }

                // Check if this sensor requires client connections
                if (ISensorInterface::Execute_RequiresClientConnection(SensorActor))
                {
                    ClientConnectionRequiredSensors.Add(SensorName);
                }
            }
        }
    }

    if (StreamingEnabledSensors.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[SENSOR-CHECK] No sensors have streaming enabled"));
        return true; // No streaming sensors to check
    }

    UE_LOG(LogTemp, Warning, TEXT("[SENSOR-CHECK] ✅ All streaming sensors (%d) are registered with WebSocket server"), StreamingEnabledSensors.Num());
    
    if (ClientConnectionRequiredSensors.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[SENSOR-CHECK] ✅ No sensors require client connections - proceeding"));
        return true; // No sensors require client connections
    }

    UE_LOG(LogTemp, Warning, TEXT("[SENSOR-CHECK] Checking client connections for %d sensors that require them"), ClientConnectionRequiredSensors.Num());

    // Check if sensors requiring client connections have clients connected (reported by server)
    bool bAllRequiredSensorsHaveClients = true;
    for (const FString& SensorName : ClientConnectionRequiredSensors)
    {
        int32 ClientCount = WebSocketMgr->GetSensorClientCount(SensorName);
        bool bHasClients = ClientCount > 0;

        UE_LOG(LogTemp, Warning, TEXT("[SENSOR-CHECK] Sensor '%s' (requires connection) - Clients: %d (Connected: %s)"),
               *SensorName, ClientCount, bHasClients ? TEXT("Yes") : TEXT("No"));

        if (!bHasClients)
        {
            bAllRequiredSensorsHaveClients = false;
        }
    }

    if (!bAllRequiredSensorsHaveClients)
    {
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(1200, 5.0f, FColor::Yellow, TEXT("Waiting for WebSocket streaming connections..."));
        }
        UE_LOG(LogTemp, Warning, TEXT("[SENSOR-CHECK] ⚠️ Not all required sensors have clients connected - waiting..."));
        return false;
    }

    if (GEngine)
    {
        GEngine->RemoveOnScreenDebugMessage(1200);
        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green, TEXT("WebSocket streaming connections established!"));
    }
    UE_LOG(LogTemp, Warning, TEXT("[SENSOR-CHECK] ✅ All sensors requiring client connections have clients connected"));
    return true;
}

bool UPROXSIMAGameInstance::VerifySubsystemsReady()
{
    // Check FMUInputHandlingSubsystem readiness
    UFMUInputHandlingSubsystem* InputSubsystem = GetSubsystem<UFMUInputHandlingSubsystem>();
    if (!InputSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("[SUBSYSTEM-CHECK] FMUInputHandlingSubsystem not available"));
        return false;
    }
    
    if (!InputSubsystem->IsInputProcessingEnabled())
    {
        UE_LOG(LogTemp, Error, TEXT("[SUBSYSTEM-CHECK] FMUInputHandlingSubsystem processing disabled"));
        return false;
    }

    // Check FMUSimulationSubsystem readiness
    UFMUSimulationSubsystem* FMUSubsystem = GetSubsystem<UFMUSimulationSubsystem>();
    if (!FMUSubsystem || !FMUSubsystem->IsFMULoaded())
    {
        UE_LOG(LogTemp, Error, TEXT("[SUBSYSTEM-CHECK] FMUSimulationSubsystem not ready"));
        return false;
    }

    // Check other critical subsystems
    USimulationConfigurationSubsystem* ConfigSubsystem = GetSubsystem<USimulationConfigurationSubsystem>();
    if (!ConfigSubsystem || !ConfigSubsystem->IsConfigurationLoaded())
    {
        UE_LOG(LogTemp, Error, TEXT("[SUBSYSTEM-CHECK] SimulationConfigurationSubsystem not ready"));
        return false;
    }

    // Check sensor streaming readiness
    if (!CheckSensorStreamingReady())
    {
        return false;
    }

    UE_LOG(LogTemp, Warning, TEXT("[SUBSYSTEM-CHECK] ✅ All critical subsystems verified ready"));
    return true;
}

void UPROXSIMAGameInstance::NotifyInitializationComplete()
{
    USimulationConfigurationSubsystem* ConfigSubsystem = GetSubsystem<USimulationConfigurationSubsystem>();
    if (!ConfigSubsystem)
    {
        return;
    }

    ESimulationMode CurrentMode = ConfigSubsystem->GetCurrentMode();

    // Verify all critical subsystems are ready before declaring initialization complete
    if (CurrentMode == ESimulationMode::SM_FMU)
    {
        if (!VerifySubsystemsReady())
        {
            UE_LOG(LogTemp, Warning, TEXT("⚠️ Critical subsystems not ready - delaying initialization complete"));
            
            // Retry in 0.5 seconds
            GetWorld()->GetTimerManager().SetTimer(
                SubsystemReadinessTimer,
                this,
                &UPROXSIMAGameInstance::NotifyInitializationComplete,
                0.5f, // Check every 0.5 seconds
                false  // Don't loop - just retry once
            );
            return;
        }
    }

    // Mode-specific initialization complete actions
    if (CurrentMode == ESimulationMode::SM_FMU)
    {
        StartFMUSimulation();
    }
    else if (CurrentMode == ESimulationMode::SM_OnDemand)
    {
        // For OnDemand mode, show ready message after sensors are verified ready
        const FFMUConfiguration& Configuration = ConfigSubsystem->GetCurrentConfiguration();
        
        if (GEngine)
        {
            FString ReadyMessage = FString::Printf(
                TEXT("✅ OnDemand mode ready - %d sensors initialized and ready for capture commands"), 
                Configuration.sensors.Num()
            );
            GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Green, ReadyMessage);
        }
        
        UE_LOG(LogTemp, Warning, TEXT("✅ OnDemand mode: All sensors and subsystems verified ready - ready for capture commands"));
        
        // Notify WebSocket clients that OnDemand mode is ready
        if (UWebSocketManager* WebSocketManager = GetSubsystem<UWebSocketManager>())
        {
            WebSocketManager->BroadcastSimulationStateChange(true);
            UE_LOG(LogTemp, Log, TEXT("📡 WebSocket clients notified: OnDemand mode ready"));
        }
    }
    else if (CurrentMode == ESimulationMode::SM_Sequence)
    {
        // For Sequence mode, just log that it's ready
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green, TEXT("✅ Sequence mode ready"));
        }
        UE_LOG(LogTemp, Warning, TEXT("✅ Sequence mode: Initialization complete"));
    }
}

void UPROXSIMAGameInstance::StartFMUSimulation()
{
    UFMUSimulationSubsystem* FMUSubsystem = GetSubsystem<UFMUSimulationSubsystem>();
    
    if (FMUSubsystem)
    {
        FMUSubsystem->StartSimulation();
        UE_LOG(LogTemp, Warning, TEXT("✅ FMU simulation started - all assets loaded and subsystems verified ready"));
        
        // Notify WebSocket clients that simulation has started
        if (UWebSocketManager* WebSocketManager = GetSubsystem<UWebSocketManager>())
        {
            WebSocketManager->BroadcastSimulationStateChange(true);
            UE_LOG(LogTemp, Log, TEXT("📡 WebSocket clients notified: simulation started"));
        }
        
        // Add onscreen debug message
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green, TEXT("FMU Simulation started!"));
        }
    }
}

void UPROXSIMAGameInstance::BeginDestroy()
{
    // Clear control connection timer if active
    if (ControlConnectionCheckTimer.IsValid())
    {
        GetWorld()->GetTimerManager().ClearTimer(ControlConnectionCheckTimer);
    }
    
    // Cleanup is now handled by subsystems
    Super::BeginDestroy();
}
