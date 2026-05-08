#include "WebSocketManager.h" // Assuming this now includes Subsystems/GameInstanceSubsystem.h and declares the class inheriting from it
#include "WebSocketsModule.h"
#include "PROXSIMAGameInstance.h"
#include "IWebSocket.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Base64.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "JsonObjectConverter.h"
#include "Engine/GameInstance.h" // Needed for GetGameInstance()
#include "TimerManager.h"        // Needed for TimerManager
#include "Misc/FileHelper.h"

// Subsystem Initialization
void UWebSocketManager::Initialize(FSubsystemCollectionBase &Collection)
{
    Super::Initialize(Collection);
    bIsRunning = false; // Initialize state
    bConnectionEstablished = false;
    bHasControlClientsConnected = false;
    bInputCommandHandlerRegistered = false;
    bCaptureCommandHandlerRegistered = false;
    bShutdownCommandHandlerRegistered = false;
    ServerPort = 0;
    UE_LOG(LogTemp, Display, TEXT("WebSocketManager Subsystem Initialized."));
}

// Subsystem Deinitialization (Cleanup)
void UWebSocketManager::Deinitialize()
{
    UE_LOG(LogTemp, Display, TEXT("WebSocketManager Subsystem Deinitializing."));
    ShutdownWebSocket();
    Super::Deinitialize();
}

void UWebSocketManager::AttemptConnection(int32 Port, int32 RetryCount, const int32 MaxRetries)
{
    if (!StartExternalServer(Port))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to start external WebSocket server"));
        return;
    }

    if (!ConnectToServer())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to connect to external WebSocket server"));
        StopExternalServer();

        if (RetryCount < MaxRetries)
        {
            float DelayTime = 1.0f + (RetryCount * 0.5f);
            UE_LOG(LogTemp, Warning, TEXT("Retrying connection in %.1f seconds... (%d/%d)"),
                   DelayTime, RetryCount + 1, MaxRetries);

            if (UGameInstance *GI = GetGameInstance())
            {
                FTimerHandle RetryTimerHandle;
                GI->GetTimerManager().SetTimer(
                    RetryTimerHandle,
                    FTimerDelegate::CreateUObject(this, &UWebSocketManager::AttemptConnection, Port, RetryCount + 1, MaxRetries),
                    DelayTime,
                    false);
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to initialize WebSocket after %d attempts"), MaxRetries);
            ServerPort = 0;
        }
        return;
    }

    bIsRunning = true;
    UE_LOG(LogTemp, Display, TEXT("WebSocket server initialized on port %d"), Port);
}

bool UWebSocketManager::InitializeWebSocket(int32 Port)
{
    if (bIsRunning)
    {
        UE_LOG(LogTemp, Warning, TEXT("WebSocket server is already running"));
        return true;
    }

    ServerPort = Port;
    AttemptConnection(Port, 0, 3); // Start with retry count 0, max retries 3
    return true;
}

// Shutdown the WebSocket server and connection (internal cleanup)
void UWebSocketManager::ShutdownWebSocket()
{
    if (!bIsRunning)
    {
        return;
    }

    UGameInstance *GI = GetGameInstance();

    // Stop the keep-alive timer safely using GameInstance's TimerManager
    if (KeepAliveTimerHandle.IsValid())
    {
        if (IsValid(GI))
        {
            GI->GetTimerManager().ClearTimer(KeepAliveTimerHandle);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("UWebSocketManager::ShutdownWebSocket - Could not get valid GameInstance or TimerManager to clear KeepAliveTimerHandle."));
        }
        KeepAliveTimerHandle.Invalidate();
    }

    // Close connection to the server
    if (ServerConnection.IsValid())
    {
        try
        {
            ServerConnection->OnConnected().Clear();
            ServerConnection->OnConnectionError().Clear();
            ServerConnection->OnMessage().Clear();
            ServerConnection->OnClosed().Clear();

            if (ServerConnection->IsConnected())
            {
                ServerConnection->Close();
            }
        }
        catch (...)
        {
            UE_LOG(LogTemp, Warning, TEXT("Exception caught while closing WebSocket connection"));
        }
        ServerConnection.Reset();
    }

    StopExternalServer();

    SensorEndpoints.Empty();
    bIsRunning = false;
    bConnectionEstablished = false;
    ServerPort = 0;

    UE_LOG(LogTemp, Display, TEXT("WebSocket Manager Shutdown complete."));
}

// Start the external Node.js server process
bool UWebSocketManager::StartExternalServer(int32 Port)
{
    FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FString ServerDir = FPaths::Combine(ProjectDir, TEXT("WebSocketServer"));
    FString ServerScriptPath = FPaths::Combine(ServerDir, TEXT("server.js"));

    if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*ServerScriptPath))
    {
        UE_LOG(LogTemp, Error, TEXT("WebSocket server script not found at: %s"), *ServerScriptPath);
        return false;
    }

    uint32 ProcessID = 0;
    NodeServerProcess = FPlatformProcess::CreateProc(
        TEXT("node"),
        *FString::Printf(TEXT("\"%s\" %d"), *ServerScriptPath, Port),
        false, // Don't launch detached
        true,  // Launch hidden to reduce UI overhead
        true,  // Launch minimized
        &ProcessID,
        0,          // Priority
        *ServerDir, // Working directory
        nullptr,    // No stdin pipe
        nullptr     // No stdout pipe
    );

    if (!NodeServerProcess.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to start Node.js server process"));
        return false;
    }

    FPlatformProcess::Sleep(1.0f);

    if (!FPlatformProcess::IsProcRunning(NodeServerProcess))
    {
        UE_LOG(LogTemp, Error, TEXT("Server process exited immediately"));
        FPlatformProcess::CloseProc(NodeServerProcess);
        NodeServerProcess.Reset();
        return false;
    }

    UE_LOG(LogTemp, Display, TEXT("Started external WebSocket server on port %d with PID %d"), Port, ProcessID);
    return true;
}

// Stop the external Node.js server process
void UWebSocketManager::StopExternalServer()
{
    if (NodeServerProcess.IsValid())
    {
        if (FPlatformProcess::IsProcRunning(NodeServerProcess))
        {
            FPlatformProcess::TerminateProc(NodeServerProcess);
        }
        FPlatformProcess::CloseProc(NodeServerProcess);
        NodeServerProcess.Reset();
        UE_LOG(LogTemp, Display, TEXT("Stopped external WebSocket server"));
    }
}
bool UWebSocketManager::ConnectToServer()
{
    if (ServerPort <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot connect to server: Invalid port (%d)"), ServerPort);
        return false;
    }

    FWebSocketsModule &WebSocketsModule = FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets");
    FString ServerURL = FString::Printf(TEXT("ws://localhost:%d/unreal"), ServerPort);

    // Clear existing connection if any
    if (ServerConnection.IsValid())
    {
        if (ServerConnection->IsConnected())
        {
            ServerConnection->Close();
        }
        ServerConnection->OnConnected().Clear();
        ServerConnection->OnConnectionError().Clear();
        ServerConnection->OnMessage().Clear();
        ServerConnection->OnClosed().Clear();
        ServerConnection.Reset();
    }

    TArray<FString> Protocols;
    TMap<FString, FString> Headers;
    Headers.Add(TEXT("X-Client-Type"), TEXT("UnrealEngine"));

    // Create new connection
    ServerConnection = WebSocketsModule.CreateWebSocket(ServerURL, Protocols, Headers);

    if (!ServerConnection.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create WebSocket connection object"));
        return false;
    }

    // Increase message size limit to handle large batch commands (10MB limit)
    ServerConnection->SetTextMessageMemoryLimit(10 * 1024 * 1024);  // 10MB
    UE_LOG(LogTemp, Log, TEXT("WebSocket text message limit set to 10MB for large control commands"));

    bConnectionEstablished = false;

    // Store weak pointer to this for lambda captures
    TWeakObjectPtr<UWebSocketManager> WeakThis(this);

    // Set up connection handlers with safety checks
    ServerConnection->OnConnected().AddLambda([WeakThis]()
                                              {
        if (!WeakThis.IsValid()) return;

        UWebSocketManager* Manager = WeakThis.Get();
        if (!Manager->ServerConnection.IsValid()) return;

        UE_LOG(LogTemp, Display, TEXT("WebSocket Connected."));
        Manager->bConnectionEstablished = true;

        if (Manager->ServerConnection->IsConnected())
        {
            FString PingMessage = TEXT("{\"type\":\"ue_connected\",\"timestamp\":0}");
            Manager->ServerConnection->Send(PingMessage);
            Manager->LastMessageTime = FDateTime::UtcNow();
            Manager->StartKeepAliveTimer();
        } });

    ServerConnection->OnConnectionError().AddLambda([WeakThis](const FString &Error)
                                                    {
        if (!WeakThis.IsValid()) return;

        UE_LOG(LogTemp, Error, TEXT("WebSocket Connection Error: %s"), *Error);
        WeakThis->bConnectionEstablished = false; });

    ServerConnection->OnMessage().AddLambda([WeakThis](const FString &Message)
                                            {
        if (!WeakThis.IsValid()) return;

        WeakThis->HandleServerMessage(Message);
        WeakThis->LastMessageTime = FDateTime::UtcNow(); });

    ServerConnection->OnClosed().AddLambda([WeakThis](int32 StatusCode, const FString &Reason, bool bWasClean)
                                           {
        if (!WeakThis.IsValid()) return;

        UE_LOG(LogTemp, Warning, TEXT("WebSocket Closed. Code: %d, Reason: %s, Clean: %s"),
            StatusCode, *Reason, bWasClean ? TEXT("true") : TEXT("false"));

        UWebSocketManager* Manager = WeakThis.Get();
        Manager->bConnectionEstablished = false;

        if (UGameInstance* GI = Manager->GetGameInstance())
        {
            if (Manager->KeepAliveTimerHandle.IsValid())
            {
                GI->GetTimerManager().ClearTimer(Manager->KeepAliveTimerHandle);
                Manager->KeepAliveTimerHandle.Invalidate();
            }
        } });

    // Initiate connection
    ServerConnection->Connect();

    // Set up connection timeout
    if (UGameInstance *GI = GetGameInstance())
    {
        FTimerHandle ConnectionTimeoutHandle;
        GI->GetTimerManager().SetTimer(
            ConnectionTimeoutHandle,
            FTimerDelegate::CreateWeakLambda(this, [WeakThis]()
                                             {
                if (!WeakThis.IsValid()) return;

                UWebSocketManager* Manager = WeakThis.Get();
                if (!Manager->bConnectionEstablished && Manager->ServerConnection.IsValid())
                {
                    UE_LOG(LogTemp, Warning, TEXT("WebSocket connection timed out"));
                    Manager->ServerConnection->Close();
                    Manager->ServerConnection.Reset();
                } }),
            4.0f, // TimeoutSeconds
            false // no looping
        );
    }

    return true;
}

// Start a timer to send keep-alive messages
void UWebSocketManager::StartKeepAliveTimer()
{
    UGameInstance *GI = GetGameInstance();
    if (!GI)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot start keep-alive timer: GameInstance is null"));
        return;
    }
    if (!IsValid(GI))
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot start keep-alive timer: TimerManager is invalid"));
        return;
    }

    if (KeepAliveTimerHandle.IsValid())
    {
        GI->GetTimerManager().ClearTimer(KeepAliveTimerHandle);
    }

    GI->GetTimerManager().SetTimer(
        KeepAliveTimerHandle,
        this,
        &UWebSocketManager::SendKeepAlive,
        2.0f, // Interval
        true  // Loop
    );
    UE_LOG(LogTemp, Display, TEXT("Started keep-alive timer"));
}

// Send a keep-alive (ping) message
void UWebSocketManager::SendKeepAlive()
{
    if (!bIsRunning || !ServerConnection.IsValid() || !ServerConnection->IsConnected())
    {
        UGameInstance *GI = GetGameInstance();
        if (KeepAliveTimerHandle.IsValid())
        {
            if (IsValid(GI))
            {
                GI->GetTimerManager().ClearTimer(KeepAliveTimerHandle);
            }
            KeepAliveTimerHandle.Invalidate();
        }
        // Consider triggering reconnect logic here if disconnected unexpectedly
        return;
    }

    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
    JsonObject->SetStringField(TEXT("type"), TEXT("ping"));
    JsonObject->SetNumberField(TEXT("timestamp"), FDateTime::UtcNow().ToUnixTimestamp());

    FString JsonString;
    TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter);

    ServerConnection->Send(JsonString);

    // Optional: Check time since last message and trigger reconnect if needed
    FTimespan TimeSinceLastMessage = FDateTime::UtcNow() - LastMessageTime;
    if (TimeSinceLastMessage.GetTotalSeconds() > 10.0) // Example timeout
    {
        UE_LOG(LogTemp, Warning, TEXT("No messages received from server in %.1f seconds, attempting reconnect..."), TimeSinceLastMessage.GetTotalSeconds());
        ConnectToServer(); // Attempt reconnect
    }
}

void UWebSocketManager::Tick(float DeltaTime)
{
    // Check if the connection is still valid
    if (bIsRunning && (!ServerConnection.IsValid() || !ServerConnection->IsConnected()))
    {
        // Only attempt to reconnect if we haven't tried recently
        FTimespan TimeSinceLastReconnect = FDateTime::UtcNow() - LastReconnectTime;
        if (TimeSinceLastReconnect.GetTotalSeconds() > 10.0)
        {
            UE_LOG(LogTemp, Warning, TEXT("WebSocket connection lost, attempting to reconnect..."));
            ConnectToServer();
            LastReconnectTime = FDateTime::UtcNow();
        }
    }
}

void UWebSocketManager::HandleServerMessage(const FString &Message)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Message);

    if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
    {
        FString MessageType;
        if (!JsonObject->TryGetStringField(TEXT("type"), MessageType))
        {
            return;
        }
        
        if (MessageType == TEXT("register_response"))
        {
            FString SensorName = JsonObject->GetStringField(TEXT("sensorName"));
            bool Success = JsonObject->GetBoolField(TEXT("success"));
            if (Success)
            {
                UE_LOG(LogTemp, Display, TEXT("Successfully registered sensor endpoint: %s"), *SensorName);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("Failed to register sensor endpoint: %s"), *SensorName);
                SensorEndpoints.Remove(SensorName);
            }
        }
        else if (MessageType == TEXT("client_connected"))
        {
            FString SensorName = JsonObject->GetStringField(TEXT("sensorName"));
            UE_LOG(LogTemp, Display, TEXT("Client connected to sensor: %s"), *SensorName);
            OnClientConnected.Broadcast(SensorName);
        }
        else if (MessageType == TEXT("client_disconnected"))
        {
            FString SensorName = JsonObject->GetStringField(TEXT("sensorName"));
            UE_LOG(LogTemp, Display, TEXT("Client disconnected from sensor: %s"), *SensorName);
            OnClientDisconnected.Broadcast(SensorName);
        }
        else if (MessageType == TEXT("pong"))
        { /* Optional: Handle pong */
        }
        else if (MessageType == TEXT("control_client_status"))
        {
            bool HasControlClients = JsonObject->GetBoolField(TEXT("hasControlClients"));
            int32 ControlClientCount = JsonObject->GetNumberField(TEXT("controlClientCount"));
            
            bHasControlClientsConnected = HasControlClients;
            
            UE_LOG(LogTemp, Warning, TEXT("[CONTROL STATUS] Control clients connected: %s (%d clients)"), 
                   HasControlClients ? TEXT("YES") : TEXT("NO"), ControlClientCount);
        }
        else if (MessageType == TEXT("sensor_client_connected"))
        {
            FString SensorName = JsonObject->GetStringField(TEXT("sensorName"));
            int32 ClientCount = JsonObject->GetNumberField(TEXT("clientCount"));
            
            // Update sensor client count
            SensorClientCounts.Add(SensorName, ClientCount);
            
            UE_LOG(LogTemp, Warning, TEXT("[SENSOR CLIENT] Client connected to sensor '%s' (total: %d clients)"), 
                   *SensorName, ClientCount);
        }
        else if (MessageType == TEXT("sensor_client_disconnected"))
        {
            FString SensorName = JsonObject->GetStringField(TEXT("sensorName"));
            int32 ClientCount = JsonObject->GetNumberField(TEXT("clientCount"));
            
            // Update sensor client count
            SensorClientCounts.Add(SensorName, ClientCount);
            
            UE_LOG(LogTemp, Warning, TEXT("[SENSOR CLIENT] Client disconnected from sensor '%s' (remaining: %d clients)"), 
                   *SensorName, ClientCount);
        }
        // Handle input commands forwarded from control clients through the Node.js server
        else if (MessageType == TEXT("load_timeseries") || MessageType == TEXT("load_timeseries_batch") || MessageType == TEXT("set_input") || MessageType == TEXT("input_command"))
        {
            if (bInputCommandHandlerRegistered)
            {
                // Forward the entire JSON message to the input handler
                OnInputCommandReceived.Broadcast(Message);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("Received input command from control client but input handler not registered: %s"), *MessageType);
            }
        }
        else if (MessageType == TEXT("capture_image"))
        {
            if (bCaptureCommandHandlerRegistered)
            {
                // Forward the entire JSON message to the capture handler
                OnCaptureCommandReceived.Broadcast(Message);
                UE_LOG(LogTemp, Verbose, TEXT("Capture image command received and forwarded"));
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("Received capture command but capture handler not registered"));
            }
        }
        else if (MessageType == TEXT("shutdown"))
        {
            if (bShutdownCommandHandlerRegistered)
            {
                // Forward the entire JSON message to the shutdown handler
                OnShutdownCommandReceived.Broadcast(Message);
                UE_LOG(LogTemp, Log, TEXT("Shutdown command received and forwarded"));
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("Received shutdown command but shutdown handler not registered"));
            }
        }
    }
    else
        UE_LOG(LogTemp, Warning, TEXT("Failed to parse incoming WebSocket JSON message: %s"), *Message);
}

// Register a sensor endpoint with the server
FString UWebSocketManager::RegisterSensorEndpoint(const FString &SensorName, const FString &SensorType)
{
    if (!bIsRunning || !ServerConnection.IsValid() || !ServerConnection->IsConnected())
        return FString();
    if (SensorName.IsEmpty() || SensorType.IsEmpty())
        return FString();

    FString EndpointPath = FString::Printf(TEXT("/%s"), *SensorName.Replace(TEXT(" "), TEXT("_")));
    SensorEndpoints.Add(SensorName, EndpointPath); // Add optimistically

    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
    JsonObject->SetStringField(TEXT("type"), TEXT("register"));
    JsonObject->SetStringField(TEXT("sensorName"), SensorName);
    JsonObject->SetStringField(TEXT("sensorType"), SensorType);

    FString JsonString;
    TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter);

    ServerConnection->Send(JsonString);

    UE_LOG(LogTemp, Display, TEXT("Attempting to register WebSocket endpoint for sensor %s: ws://localhost:%d%s"), *SensorName, ServerPort, *EndpointPath);
    return EndpointPath;
}

// Send binary sensor data via the server
bool UWebSocketManager::SendSensorData(const FString &SensorName, const TArray<uint8> &Data, float SimulationTime)
{
    if (!bIsRunning || !ServerConnection.IsValid() || !ServerConnection->IsConnected())
        return false;
    if (!SensorEndpoints.Contains(SensorName))
        return false;
    if (Data.Num() == 0)
        return false;

    FString Base64Data = FBase64::Encode(Data);

    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
    JsonObject->SetStringField(TEXT("type"), TEXT("data"));
    JsonObject->SetStringField(TEXT("sensorName"), SensorName);
    JsonObject->SetStringField(TEXT("data"), Base64Data);
    
    // Include simulation timestamp if provided
    if (SimulationTime >= 0.0f)
    {
        JsonObject->SetNumberField(TEXT("timestamp"), SimulationTime);
    }

    FString JsonString;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonString);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter);

    ServerConnection->Send(JsonString);
    return true;
}

// Check if clients are connected
int32 UWebSocketManager::GetSensorClientCount(const FString &SensorName) const
{
    const int32* ClientCount = SensorClientCounts.Find(SensorName);
    return ClientCount ? *ClientCount : 0;
}

// Get the full WebSocket URL for a sensor
FString UWebSocketManager::GetSensorWebSocketURL(const FString &SensorName) const
{
    if (!bIsRunning || ServerPort <= 0)
        return FString();

    const FString *EndpointPath = SensorEndpoints.Find(SensorName);
    if (!EndpointPath)
        return FString();

    return FString::Printf(TEXT("ws://localhost:%d%s"), ServerPort, **EndpointPath);
}

// Generate HTML viewer content for a sensor
FString UWebSocketManager::GenerateHTMLViewer(const FString &SensorName, const FString &SensorType) const
{
    const FString *EndpointPathPtr = SensorEndpoints.Find(SensorName);
    if (!bIsRunning || ServerPort <= 0 || !EndpointPathPtr)
    {
        return FString("<html><body>Error: WebSocket Manager not running or sensor not registered.</body></html>");
    }
    FString EndpointPath = *EndpointPathPtr;

    if (SensorType.Equals(TEXT("Camera"), ESearchCase::IgnoreCase))
    {
        return FString::Printf(TEXT(
                                   "<!DOCTYPE html>\n"
                                   "<html>\n"
                                   "<head>\n"
                                   "    <title>%s - Camera Viewer</title>\n"
                                   "    <style>\n"
                                   "        body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 20px; }\n"
                                   "        #imageContainer { margin: 20px auto; max-width: 100%%; }\n"
                                   "        #cameraImage { max-width: 100%%; border: 1px solid #ccc; }\n"
                                   "        #status { color: #666; margin-bottom: 10px; }\n"
                                   "    </style>\n"
                                   "</head>\n"
                                   "<body>\n"
                                   "    <h1>%s - Camera Stream</h1>\n"
                                   "    <div id=\"status\">Connecting...</div>\n"
                                   "    <div id=\"imageContainer\">\n"
                                   "        <img id=\"cameraImage\" alt=\"Camera Stream\" />\n"
                                   "    </div>\n"
                                   "    <script>\n"
                                   "        const wsUrl = 'ws://localhost:%d%s';\n"
                                   "        const status = document.getElementById('status');\n"
                                   "        const img = document.getElementById('cameraImage');\n"
                                   "        let ws;\n\n"
                                   "        function connect() {\n"
                                   "            ws = new WebSocket(wsUrl);\n\n"
                                   "            ws.onopen = function() {\n"
                                   "                status.textContent = 'Connected';\n"
                                   "                status.style.color = 'green';\n"
                                   "            };\n\n"
                                   "            ws.onmessage = function(e) {\n"
                                   "                try {\n"
                                   "                    const data = JSON.parse(e.data);\n"
                                   "                    if (data.type === 'data' && data.data && data.timestamp !== undefined) {\n"
                                   "                        // Decode base64 image data\n"
                                   "                        const binaryString = atob(data.data);\n"
                                   "                        const bytes = new Uint8Array(binaryString.length);\n"
                                   "                        for (let i = 0; i < binaryString.length; i++) {\n"
                                   "                            bytes[i] = binaryString.charCodeAt(i);\n"
                                   "                        }\n"
                                   "                        const blob = new Blob([bytes], { type: 'image/png' });\n"
                                   "                        img.src = URL.createObjectURL(blob);\n"
                                   "                        \n"
                                   "                        // Update status with timestamp\n"
                                   "                        status.textContent = 'Connected - Sim Time: ' + data.timestamp.toFixed(3) + 's';\n"
                                   "                    }\n"
                                   "                } catch (err) {\n"
                                   "                    console.error('Error parsing WebSocket message:', err);\n"
                                   "                }\n"
                                   "            };\n\n"
                                   "            ws.onclose = function() {\n"
                                   "                status.textContent = 'Disconnected - Reconnecting...';\n"
                                   "                status.style.color = 'red';\n"
                                   "                setTimeout(connect, 2000);\n"
                                   "            };\n\n"
                                   "            ws.onerror = function(err) {\n"
                                   "                console.error('WebSocket error:', err);\n"
                                   "                ws.close();\n"
                                   "            };\n"
                                   "        }\n\n"
                                   "        connect();\n"
                                   "    </script>\n"
                                   "</body>\n"
                                   "</html>"),
                               *SensorName, *SensorName, ServerPort, *SensorEndpoints[SensorName]);
    }

    // Generic viewer for other types
    return FString::Printf(TEXT(
                               "<!DOCTYPE html>\n"
                               "<html>\n"
                               "<head>\n"
                               "    <title>%s - Sensor Viewer</title>\n"
                               "</head>\n"
                               "<body>\n"
                               "    <h1>%s - %s Data Stream</h1>\n"
                               "    <p>WebSocket URL: ws://localhost:%d%s</p>\n"
                               "</body>\n"
                               "</html>"),
                           *SensorName, *SensorName, *SensorType, ServerPort, *SensorEndpoints[SensorName]);
}

bool UWebSocketManager::SaveHTMLViewer(const FString &SensorName, const FString &SensorType, const FString &OutputPath)
{
    if (!bIsRunning || !SensorEndpoints.Contains(SensorName))
        return false;
    if (OutputPath.IsEmpty())
        return false;

    FString HtmlContent = GenerateHTMLViewer(SensorName, SensorType);
    if (HtmlContent.IsEmpty() || HtmlContent.Contains(TEXT("Error:"), ESearchCase::CaseSensitive))
        return false;

    FString HtmlFilePath = FPaths::ConvertRelativePathToFull(OutputPath);
    while (HtmlFilePath.EndsWith(TEXT("/")) || HtmlFilePath.EndsWith(TEXT("\\")))
    {
        HtmlFilePath.RemoveAt(HtmlFilePath.Len() - 1, 1); // Remove trailing slashes
    }
    FString ParentPath = FPaths::GetPath(HtmlFilePath);
    HtmlFilePath = FPaths::Combine(ParentPath, SensorName + TEXT("_viewer.html"));
    if (!IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*ParentPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create directory tree for HTML viewer at %s"), *ParentPath);
        return false;
    }

    bool bSuccess = FFileHelper::SaveStringToFile(HtmlContent, *HtmlFilePath);
    if (bSuccess)
    {
        UE_LOG(LogTemp, Display, TEXT("Saved HTML viewer for %s sensor at: %s"), *SensorName, *HtmlFilePath);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to save HTML viewer for %s sensor to %s"), *SensorName, *HtmlFilePath);
    }
    return bSuccess;
}

bool UWebSocketManager::InitializeWebSocketServer()
{
    if (bIsRunning)
    {
        UE_LOG(LogTemp, Warning, TEXT("WebSocket server is already running"));
        return true;
    }

    UPROXSIMAGameInstance *GameInstance = Cast<UPROXSIMAGameInstance>(GetGameInstance());
    if (!GameInstance)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to get GameInstance for WebSocket initialization"));
        return false;
    }
    if (GameInstance->GetGlobalStreamingPort() <= 0)
    {
        // Streaming is disabled
        UE_LOG(LogTemp, Warning, TEXT("Global streaming port is not set or invalid. WebSocket server will not be initialized."));
        return false;
    }

    if (!InitializeWebSocket(GameInstance->GetGlobalStreamingPort()))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to initialize WebSocket server on port %d"), GameInstance->GetGlobalStreamingPort());
        return false;
    }

    return true;
}

// Input handling methods implementation

bool UWebSocketManager::SendInputCommand(const FString &CommandJson)
{
    if (!bIsRunning || !ServerConnection.IsValid() || !ServerConnection->IsConnected())
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot send input command: WebSocket not connected"));
        return false;
    }

    if (CommandJson.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot send empty input command"));
        return false;
    }

    ServerConnection->Send(CommandJson);
    UE_LOG(LogTemp, Verbose, TEXT("Sent input command: %s"), *CommandJson);
    return true;
}

void UWebSocketManager::RegisterInputCommandHandler()
{
    bInputCommandHandlerRegistered = true;
    UE_LOG(LogTemp, Log, TEXT("Input command handler registered"));

    // Send registration message to external server
    if (bIsRunning && ServerConnection.IsValid() && ServerConnection->IsConnected())
    {
        FString RegistrationMessage = TEXT("{\"type\":\"register_input_handler\",\"timestamp\":0}");
        ServerConnection->Send(RegistrationMessage);
    }
}

void UWebSocketManager::UnregisterInputCommandHandler()
{
    bInputCommandHandlerRegistered = false;
    UE_LOG(LogTemp, Log, TEXT("Input command handler unregistered"));

    // Send unregistration message to external server
    if (bIsRunning && ServerConnection.IsValid() && ServerConnection->IsConnected())
    {
        FString UnregistrationMessage = TEXT("{\"type\":\"unregister_input_handler\",\"timestamp\":0}");
        ServerConnection->Send(UnregistrationMessage);
    }
}

bool UWebSocketManager::SendInputCommandAck(const FString &CommandId, bool bSuccess, const FString &ErrorMessage)
{
    if (!bIsRunning || !ServerConnection.IsValid() || !ServerConnection->IsConnected())
    {
        return false;
    }

    if (CommandId.IsEmpty())
    {
        return false;
    }

    TSharedPtr<FJsonObject> AckJson = MakeShareable(new FJsonObject);
    AckJson->SetStringField(TEXT("type"), TEXT("input_command_ack"));
    AckJson->SetStringField(TEXT("commandId"), CommandId);
    AckJson->SetBoolField(TEXT("success"), bSuccess);
    AckJson->SetNumberField(TEXT("timestamp"), FPlatformTime::Seconds());

    if (!bSuccess && !ErrorMessage.IsEmpty())
    {
        AckJson->SetStringField(TEXT("error"), ErrorMessage);
    }

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(AckJson.ToSharedRef(), Writer);

    ServerConnection->Send(OutputString);
    UE_LOG(LogTemp, Verbose, TEXT("Sent input command ack for command '%s': %s"),
           *CommandId, bSuccess ? TEXT("success") : TEXT("failure"));

    return true;
}

bool UWebSocketManager::RequestInputTimeseries(const FString &VariableName, double StartTime, double EndTime)
{
    if (!bIsRunning || !ServerConnection.IsValid() || !ServerConnection->IsConnected())
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot request input timeseries: WebSocket not connected"));
        return false;
    }

    if (VariableName.IsEmpty() || StartTime > EndTime)
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid parameters for input timeseries request"));
        return false;
    }

    TSharedPtr<FJsonObject> RequestJson = MakeShareable(new FJsonObject);
    RequestJson->SetStringField(TEXT("type"), TEXT("request_input_timeseries"));
    RequestJson->SetStringField(TEXT("variableName"), VariableName);
    RequestJson->SetNumberField(TEXT("startTime"), StartTime);
    RequestJson->SetNumberField(TEXT("endTime"), EndTime);
    RequestJson->SetNumberField(TEXT("timestamp"), FPlatformTime::Seconds());

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(RequestJson.ToSharedRef(), Writer);

    ServerConnection->Send(OutputString);
    UE_LOG(LogTemp, Log, TEXT("Requested input timeseries for variable '%s' from %f to %f"),
           *VariableName, StartTime, EndTime);

    return true;
}

void UWebSocketManager::BroadcastSimulationStateChange(bool bIsSimulationStarting)
{
    if (!bIsRunning || !ServerConnection.IsValid() || !ServerConnection->IsConnected())
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot broadcast simulation state change: WebSocket not connected"));
        return;
    }

    TSharedPtr<FJsonObject> StateJson = MakeShareable(new FJsonObject);
    StateJson->SetStringField(TEXT("type"), bIsSimulationStarting ? TEXT("simulation_started") : TEXT("simulation_stopped"));
    StateJson->SetBoolField(TEXT("isRunning"), bIsSimulationStarting);
    StateJson->SetNumberField(TEXT("timestamp"), FPlatformTime::Seconds());

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(StateJson.ToSharedRef(), Writer);

    ServerConnection->Send(OutputString);
    UE_LOG(LogTemp, Log, TEXT("Broadcast simulation state change: %s"),
           bIsSimulationStarting ? TEXT("started") : TEXT("stopped"));
}

// Capture handling methods implementation

void UWebSocketManager::RegisterCaptureCommandHandler()
{
    bCaptureCommandHandlerRegistered = true;
    UE_LOG(LogTemp, Log, TEXT("Capture command handler registered"));

    // Send registration message to external server
    if (bIsRunning && ServerConnection.IsValid() && ServerConnection->IsConnected())
    {
        FString RegistrationMessage = TEXT("{\"type\":\"register_capture_handler\",\"timestamp\":0}");
        ServerConnection->Send(RegistrationMessage);
    }
}

void UWebSocketManager::UnregisterCaptureCommandHandler()
{
    bCaptureCommandHandlerRegistered = false;
    UE_LOG(LogTemp, Log, TEXT("Capture command handler unregistered"));

    // Send unregistration message to external server
    if (bIsRunning && ServerConnection.IsValid() && ServerConnection->IsConnected())
    {
        FString UnregistrationMessage = TEXT("{\"type\":\"unregister_capture_handler\",\"timestamp\":0}");
        ServerConnection->Send(UnregistrationMessage);
    }
}

bool UWebSocketManager::SendCaptureCommandAck(const FString& CommandId, const FString& CameraName, bool bSuccess, const FString& ErrorMessage)
{
    if (!bIsRunning || !ServerConnection.IsValid() || !ServerConnection->IsConnected())
    {
        return false;
    }

    if (CommandId.IsEmpty())
    {
        return false;
    }

    TSharedPtr<FJsonObject> AckJson = MakeShareable(new FJsonObject);
    AckJson->SetStringField(TEXT("type"), TEXT("capture_command_ack"));
    AckJson->SetStringField(TEXT("commandId"), CommandId);
    AckJson->SetStringField(TEXT("cameraName"), CameraName);
    AckJson->SetBoolField(TEXT("success"), bSuccess);
    AckJson->SetNumberField(TEXT("timestamp"), FPlatformTime::Seconds());

    if (!bSuccess && !ErrorMessage.IsEmpty())
    {
        AckJson->SetStringField(TEXT("error"), ErrorMessage);
    }

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(AckJson.ToSharedRef(), Writer);

    ServerConnection->Send(OutputString);
    UE_LOG(LogTemp, Verbose, TEXT("Sent capture command ack for command '%s' camera '%s': %s"),
           *CommandId, *CameraName, bSuccess ? TEXT("success") : TEXT("failure"));

    return true;
}

// Shutdown handling methods implementation

void UWebSocketManager::RegisterShutdownCommandHandler()
{
    bShutdownCommandHandlerRegistered = true;
    UE_LOG(LogTemp, Log, TEXT("Shutdown command handler registered"));

    // Send registration message to external server
    if (bIsRunning && ServerConnection.IsValid() && ServerConnection->IsConnected())
    {
        FString RegistrationMessage = TEXT("{\"type\":\"register_shutdown_handler\",\"timestamp\":0}");
        ServerConnection->Send(RegistrationMessage);
    }
}

void UWebSocketManager::UnregisterShutdownCommandHandler()
{
    bShutdownCommandHandlerRegistered = false;
    UE_LOG(LogTemp, Log, TEXT("Shutdown command handler unregistered"));

    // Send unregistration message to external server
    if (bIsRunning && ServerConnection.IsValid() && ServerConnection->IsConnected())
    {
        FString UnregistrationMessage = TEXT("{\"type\":\"unregister_shutdown_handler\",\"timestamp\":0}");
        ServerConnection->Send(UnregistrationMessage);
    }
}

bool UWebSocketManager::SendShutdownCommandAck(const FString& CommandId, bool bSuccess, const FString& ErrorMessage)
{
    if (!bIsRunning || !ServerConnection.IsValid() || !ServerConnection->IsConnected())
    {
        return false;
    }

    if (CommandId.IsEmpty())
    {
        return false;
    }

    TSharedPtr<FJsonObject> AckJson = MakeShareable(new FJsonObject);
    AckJson->SetStringField(TEXT("type"), TEXT("shutdown_command_ack"));
    AckJson->SetStringField(TEXT("commandId"), CommandId);
    AckJson->SetBoolField(TEXT("success"), bSuccess);
    AckJson->SetNumberField(TEXT("timestamp"), FPlatformTime::Seconds());

    if (!bSuccess && !ErrorMessage.IsEmpty())
    {
        AckJson->SetStringField(TEXT("error"), ErrorMessage);
    }

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(AckJson.ToSharedRef(), Writer);

    ServerConnection->Send(OutputString);
    UE_LOG(LogTemp, Log, TEXT("Sent shutdown command ack for command '%s': %s"),
           *CommandId, bSuccess ? TEXT("success") : TEXT("failure"));

    return true;
}

bool UWebSocketManager::HasControlClientsConnected() const
{
    // Check if WebSocket server is running and connected to Node.js server
    if (!bIsRunning || !ServerConnection.IsValid() || !ServerConnection->IsConnected())
    {
        UE_LOG(LogTemp, Verbose, TEXT("HasControlClientsConnected: WebSocket not running or not connected"));
        return false;
    }

    // Check if input command handler is registered (indicates control integration is active)
    if (!bInputCommandHandlerRegistered)
    {
        UE_LOG(LogTemp, Verbose, TEXT("HasControlClientsConnected: Input command handler not registered"));
        return false;
    }

    // Check if connection has been established with Node.js server
    if (!bConnectionEstablished)
    {
        UE_LOG(LogTemp, Verbose, TEXT("HasControlClientsConnected: Connection not established"));
        return false;
    }

    // Check if actual control clients are connected to the /control endpoint
    // This information is provided by the Node.js server via control_client_status messages
    if (!bHasControlClientsConnected)
    {
        UE_LOG(LogTemp, Verbose, TEXT("HasControlClientsConnected: No control clients connected to /control endpoint"));
        return false;
    }

    UE_LOG(LogTemp, Verbose, TEXT("HasControlClientsConnected: All conditions met - control clients are ready"));
    return true;
}
