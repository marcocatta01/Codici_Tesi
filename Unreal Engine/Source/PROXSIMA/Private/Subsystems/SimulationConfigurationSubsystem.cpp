#include "Subsystems/SimulationConfigurationSubsystem.h"
#include "FMUConfiguration.h"
#include "Subsystems/FMUInputHandlingSubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFilemanager.h"

void USimulationConfigurationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    bConfigurationLoaded = false;
    CurrentMode = ESimulationMode::SM_None;

    UE_LOG(LogTemp, Log, TEXT("SimulationConfigurationSubsystem initialized"));
}

void USimulationConfigurationSubsystem::Deinitialize()
{
    bConfigurationLoaded = false;
    CurrentMode = ESimulationMode::SM_None;
    CachedConfiguration = FFMUConfiguration();

    Super::Deinitialize();
}

bool USimulationConfigurationSubsystem::LoadConfiguration(const FString& ConfigPath, FFMUConfiguration& OutConfig, ESimulationMode& OutMode)
{
    // Load JSON content from file
    FString JsonContent;
    if (!FFileHelper::LoadFileToString(JsonContent, *ConfigPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load configuration file: %s"), *ConfigPath);
        return false;
    }

    // Parse JSON
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
    if (!FJsonSerializer::Deserialize(Reader, JsonObject))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to parse configuration file: %s"), *ConfigPath);
        return false;
    }

    return ParseConfiguration(ConfigPath, JsonObject, OutConfig, OutMode);
}

bool USimulationConfigurationSubsystem::ParseConfiguration(const FString& ConfigPath, const TSharedPtr<FJsonObject>& JsonObject, FFMUConfiguration& OutConfig, ESimulationMode& OutMode)
{
    if (!JsonObject.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid JSON object provided"));
        return false;
    }

    // Determine simulation mode
    FString SimulationModeStr;
    if (JsonObject->TryGetStringField(TEXT("simulationMode"), SimulationModeStr))
    {
        if (SimulationModeStr.Equals(TEXT("sequence"), ESearchCase::IgnoreCase))
        {
            OutMode = ESimulationMode::SM_Sequence;
        }
        else if (SimulationModeStr.Equals(TEXT("fmu"), ESearchCase::IgnoreCase))
        {
            OutMode = ESimulationMode::SM_FMU;
        }
        else if (SimulationModeStr.Equals(TEXT("ondemand"), ESearchCase::IgnoreCase))
        {
            OutMode = ESimulationMode::SM_OnDemand;
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Unsupported simulation mode: %s"), *SimulationModeStr);
            OutMode = ESimulationMode::SM_None;
            return false;
        }
    }
    else
    {
        // Default to FMU mode if no mode is specified
        OutMode = ESimulationMode::SM_FMU;
        UE_LOG(LogTemp, Warning, TEXT("No simulation mode specified, defaulting to FMU mode"));
    }

    bool bSuccess = false;

    // Parse mode-specific configuration
    if (OutMode == ESimulationMode::SM_FMU)
    {
        bSuccess = ParseFMUConfiguration(ConfigPath, JsonObject, OutConfig);
    }
    else if (OutMode == ESimulationMode::SM_Sequence)
    {
        bSuccess = ParseSequenceConfiguration(ConfigPath, JsonObject, OutConfig);
    }
    else if (OutMode == ESimulationMode::SM_OnDemand)
    {
        bSuccess = ParseOnDemandConfiguration(ConfigPath, JsonObject, OutConfig);
    }

    // Parse common sensor configuration regardless of mode
    if (bSuccess)
    {
        ParseSensorsConfiguration(JsonObject, OutConfig);
    }

    // Parse input configuration (only relevant for FMU mode)
    if (bSuccess && OutMode == ESimulationMode::SM_FMU)
    {
        ParseInputConfiguration(JsonObject, OutConfig.inputConfiguration, ConfigPath);
    }

    // Cache successful configuration
    if (bSuccess)
    {
        CachedConfiguration = OutConfig;
        CurrentMode = OutMode;
        bConfigurationLoaded = true;

        UE_LOG(LogTemp, Log, TEXT("Configuration loaded successfully from: %s (Mode: %s)"),
            *ConfigPath,
            OutMode == ESimulationMode::SM_FMU ? TEXT("FMU") : TEXT("Sequence"));
    }

    return bSuccess;
}

bool USimulationConfigurationSubsystem::ParseFMUConfiguration(const FString& ConfigPath, const TSharedPtr<FJsonObject>& JsonObject, FFMUConfiguration& OutConfig)
{
    // Use existing FMU configuration loader for simulation settings, parameters, and sensors
    bool bSuccess = UFMUConfigurationLoader::LoadConfiguration(ConfigPath, OutConfig);

    if (bSuccess)
    {
        // Resolve relative FMU path
        if (!OutConfig.fmuPath.IsEmpty())
        {
            OutConfig.fmuPath = ResolveRelativePath(ConfigPath, OutConfig.fmuPath);
            UE_LOG(LogTemp, Log, TEXT("Resolved FMU path to: %s"), *OutConfig.fmuPath);
        }

        // Parse visual components
        ParseVisualComponentsConfiguration(ConfigPath, JsonObject, OutConfig);
    }

    return bSuccess;
}

bool USimulationConfigurationSubsystem::ParseSequenceConfiguration(const FString& ConfigPath, const TSharedPtr<FJsonObject>& JsonObject, FFMUConfiguration& OutConfig)
{
    // Parse sequence-specific configuration
    const TSharedPtr<FJsonObject>* SequenceObj;
    if (JsonObject->TryGetObjectField(TEXT("sequence"), SequenceObj))
    {
        // Set sequence-specific configuration
        OutConfig.timeStep = 0.0; // Let sequence manager handle timing

        // Extract sequence file path if needed
        FString SequenceFile;
        if ((*SequenceObj)->TryGetStringField(TEXT("sequenceFile"), SequenceFile))
        {
            // Store sequence file path for later use by sequence manager
            OutConfig.fmuPath = ResolveRelativePath(ConfigPath, SequenceFile);
        }
    }

    // Parse any global simulation parameters
    if (JsonObject->HasField(TEXT("streamingPort")))
    {
        OutConfig.streamingPort = JsonObject->GetIntegerField(TEXT("streamingPort"));
    }

    // Parse visual components for sequence mode (static objects)
    ParseVisualComponentsConfiguration(ConfigPath, JsonObject, OutConfig);

    return true;
}

bool USimulationConfigurationSubsystem::ParseOnDemandConfiguration(const FString& ConfigPath, const TSharedPtr<FJsonObject>& JsonObject, FFMUConfiguration& OutConfig)
{
    // OnDemand mode configuration is minimal since it operates via WebSocket commands
    // No timing control needed - captures happen on-demand
    OutConfig.timeStep = 0.0;

    // Clear any FMU path since we don't use FMU files in ondemand mode
    OutConfig.fmuPath.Empty();

    // Parse global simulation parameters
    if (JsonObject->HasField(TEXT("streamingPort")))
    {
        OutConfig.streamingPort = JsonObject->GetIntegerField(TEXT("streamingPort"));
    }
    else
    {
        // OnDemand mode requires WebSocket communication, so we need a streaming port
        UE_LOG(LogTemp, Warning, TEXT("OnDemand mode requires a streamingPort for WebSocket communication"));
        OutConfig.streamingPort = 8080; // Default port
    }

    // Parse any ondemand-specific configuration if present
    const TSharedPtr<FJsonObject>* OnDemandObj;
    if (JsonObject->TryGetObjectField(TEXT("ondemand"), OnDemandObj))
    {
        // Future ondemand-specific settings can be parsed here
        // For now, the configuration is minimal
        UE_LOG(LogTemp, Log, TEXT("Found ondemand-specific configuration section"));
    }

    // Parse visual components for ondemand mode (static objects)
    ParseVisualComponentsConfiguration(ConfigPath, JsonObject, OutConfig);

    UE_LOG(LogTemp, Log, TEXT("OnDemand mode configuration parsed successfully with streaming port %d"), OutConfig.streamingPort);
    return true;
}

bool USimulationConfigurationSubsystem::ParseSensorsConfiguration(const TSharedPtr<FJsonObject>& JsonObject, FFMUConfiguration& OutConfig)
{
    const TArray<TSharedPtr<FJsonValue>>* SensorsArray;
    if (!JsonObject->TryGetArrayField(TEXT("sensors"), SensorsArray))
    {
        // No sensors configured, which is valid
        return true;
    }

    OutConfig.sensors.Empty();

    for (const TSharedPtr<FJsonValue>& SensorValue : *SensorsArray)
    {
        const TSharedPtr<FJsonObject> SensorObj = SensorValue->AsObject();
        if (!SensorObj.IsValid())
        {
            UE_LOG(LogTemp, Warning, TEXT("Invalid sensor object in configuration"));
            continue;
        }

        FFMUSensorConfig SensorConfig;

        // Parse basic sensor properties
        SensorConfig.name = SensorObj->GetStringField(TEXT("name"));
        SensorConfig.type = SensorObj->GetStringField(TEXT("type"));
        if (SensorObj->HasField(TEXT("frameName")))
        {
            SensorConfig.frameName = SensorObj->GetStringField(TEXT("frameName"));
        }

        // Parse rigid transform if present
        const TSharedPtr<FJsonObject>* TransformObj;
        if (SensorObj->TryGetObjectField(TEXT("rigidTransform"), TransformObj))
        {
            if (!ParseRigidTransform(*TransformObj, SensorConfig.rigidTransform))
            {
                UE_LOG(LogTemp, Warning, TEXT("Failed to parse rigidTransform for sensor %s"), *SensorConfig.name);
            }
        }

        // Parse sensor parameters
        const TSharedPtr<FJsonObject>* ParametersObj;
        if (SensorObj->TryGetObjectField(TEXT("parameters"), ParametersObj))
        {
            for (const auto& Param : (*ParametersObj)->Values)
            {
                if (Param.Value->Type == EJson::String)
                {
                    SensorConfig.parameters.Add(Param.Key, Param.Value->AsString());
                }
                else if (Param.Value->Type == EJson::Number)
                {
                    SensorConfig.parameters.Add(Param.Key, FString::SanitizeFloat(Param.Value->AsNumber()));
                }
                else if (Param.Value->Type == EJson::Boolean)
                {
                    SensorConfig.parameters.Add(Param.Key, Param.Value->AsBool() ? TEXT("true") : TEXT("false"));
                }
                else if (Param.Value->Type == EJson::Array)
                {
                    // Handle array parameters (like resolution) - original logic
                    FString ArrayStr;
                    const TArray<TSharedPtr<FJsonValue>>* ArrayValues = nullptr;
                    if (Param.Value->TryGetArray(ArrayValues))
                    {
                        TArray<FString> StringValues;
                        for (const auto& ArrayValue : *ArrayValues)
                        {
                            if (ArrayValue->Type == EJson::Number)
                            {
                                StringValues.Add(FString::SanitizeFloat(ArrayValue->AsNumber()));
                            }
                            else if (ArrayValue->Type == EJson::String)
                            {
                                StringValues.Add(ArrayValue->AsString());
                            }
                        }
                        ArrayStr = FString::Join(StringValues, TEXT(","));
                        SensorConfig.parameters.Add(Param.Key, ArrayStr);
                    }
                }
            }
        }

        OutConfig.sensors.Add(SensorConfig);
    }

    UE_LOG(LogTemp, Log, TEXT("Loaded %d sensors from configuration"), OutConfig.sensors.Num());
    return true;
}

bool USimulationConfigurationSubsystem::ParseVisualComponentsConfiguration(const FString& ConfigPath, const TSharedPtr<FJsonObject>& JsonObject, FFMUConfiguration& OutConfig)
{
    const TArray<TSharedPtr<FJsonValue>>* VisualComponentsArray;
    if (!JsonObject->TryGetArrayField(TEXT("visualComponents"), VisualComponentsArray))
    {
        // No visual components specified, which is valid
        return true;
    }

    for (const auto& CompValue : *VisualComponentsArray)
    {
        const TSharedPtr<FJsonObject>& CompObj = CompValue->AsObject();
        FFMUVisualComponent VisualComp;

        // Required fields
        if (!CompObj->TryGetStringField(TEXT("name"), VisualComp.name))
        {
            UE_LOG(LogTemp, Error, TEXT("Missing required 'name' in visual component"));
            return false;
        }

        FString TypeStr;
        if (!CompObj->TryGetStringField(TEXT("type"), TypeStr))
        {
            UE_LOG(LogTemp, Error, TEXT("Missing required 'type' in visual component"));
            return false;
        }
        VisualComp.type = TypeStr;

        // Optional frameName field - if not specified, component will be static (world-space)
        CompObj->TryGetStringField(TEXT("frameName"), VisualComp.frameName);

        // Parse meshPath (optional, only required for 'mesh' type)
        FString MeshPathStr;
        if (CompObj->TryGetStringField(TEXT("meshPath"), MeshPathStr))
        {
            VisualComp.meshPath = ResolveRelativePath(ConfigPath, MeshPathStr);
        }

        // Dimensions
        const TSharedPtr<FJsonObject>* DimensionsObj;
        if (CompObj->TryGetObjectField(TEXT("dimensions"), DimensionsObj))
        {
            for (auto&& Dim : (*DimensionsObj)->Values)
            {
                VisualComp.dimensions.Add(Dim.Key, Dim.Value->AsNumber());
            }
        }

        // Rigid transform (optional)
        const TSharedPtr<FJsonObject>* TransformObj;
        if (CompObj->TryGetObjectField(TEXT("rigidTransform"), TransformObj))
        {
            if (!ParseRigidTransform(*TransformObj, VisualComp.rigidTransform))
            {
                UE_LOG(LogTemp, Warning, TEXT("Failed to parse rigidTransform for VisualComponent %s"), *VisualComp.name);
            }
        }

        // Parse appearance settings
        const TSharedPtr<FJsonObject>* AppearanceObj;
        if (CompObj->TryGetObjectField(TEXT("appearance"), AppearanceObj))
        {
            // Handle color (supports both string and array formats)
            const TSharedPtr<FJsonValue> ColorValue = (*AppearanceObj)->TryGetField(TEXT("color"));
            if (ColorValue.IsValid())
            {
                if (ColorValue->Type == EJson::String)
                {
                    VisualComp.appearance.colorName = ColorValue->AsString();

                    // Convert common color names to FLinearColor
                    if (VisualComp.appearance.colorName.Equals(TEXT("red"), ESearchCase::IgnoreCase))
                        VisualComp.appearance.color = FLinearColor::Red;
                    else if (VisualComp.appearance.colorName.Equals(TEXT("blue"), ESearchCase::IgnoreCase))
                        VisualComp.appearance.color = FLinearColor::Blue;
                    else if (VisualComp.appearance.colorName.Equals(TEXT("green"), ESearchCase::IgnoreCase))
                        VisualComp.appearance.color = FLinearColor::Green;
                    else if (VisualComp.appearance.colorName.Equals(TEXT("yellow"), ESearchCase::IgnoreCase))
                        VisualComp.appearance.color = FLinearColor::Yellow;
                    else if (VisualComp.appearance.colorName.Equals(TEXT("black"), ESearchCase::IgnoreCase))
                        VisualComp.appearance.color = FLinearColor::Black;
                    else if (VisualComp.appearance.colorName.Equals(TEXT("white"), ESearchCase::IgnoreCase))
                        VisualComp.appearance.color = FLinearColor::White;
                    else
                        VisualComp.appearance.color = FLinearColor::White; // Default to white if unknown
                }
                else if (ColorValue->Type == EJson::Array)
                {
                    const TArray<TSharedPtr<FJsonValue>>* ColorArray;
                    if ((*AppearanceObj)->TryGetArrayField(TEXT("color"), ColorArray) && ColorArray->Num() >= 3)
                    {
                        VisualComp.appearance.color = FLinearColor(
                            (*ColorArray)[0]->AsNumber(),
                            (*ColorArray)[1]->AsNumber(),
                            (*ColorArray)[2]->AsNumber(),
                            ColorArray->Num() > 3 ? (*ColorArray)[3]->AsNumber() : 1.0f
                        );
                    }
                }
            }

            (*AppearanceObj)->TryGetNumberField(TEXT("opacity"), VisualComp.appearance.opacity);
            (*AppearanceObj)->TryGetNumberField(TEXT("metallic"), VisualComp.appearance.metallic);
            (*AppearanceObj)->TryGetNumberField(TEXT("roughness"), VisualComp.appearance.roughness);
        }
        else
        {
            // Set default appearance if not specified
            VisualComp.appearance.color = FLinearColor::White;
            VisualComp.appearance.opacity = 1.0f;
            VisualComp.appearance.metallic = 0.0f;
            VisualComp.appearance.roughness = 0.8f;
        }

        OutConfig.visualComponents.Add(VisualComp);
    }

    UE_LOG(LogTemp, Log, TEXT("Loaded %d visual components from configuration"), OutConfig.visualComponents.Num());
    return true;
}

bool USimulationConfigurationSubsystem::ParseRigidTransform(const TSharedPtr<FJsonObject>& TransformObj, FTransform& OutTransform)
{
    if (!TransformObj.IsValid())
    {
        return false;
    }

    // Parse translation
    const TArray<TSharedPtr<FJsonValue>>* TranslationArray;
    if (TransformObj->TryGetArrayField(TEXT("translation"), TranslationArray) && TranslationArray->Num() == 3)
    {
        FVector Translation(
            (*TranslationArray)[0]->AsNumber(),
            -(*TranslationArray)[1]->AsNumber(),  // Flip Y coordinate for right-handed to left-handed conversion
            (*TranslationArray)[2]->AsNumber());
        OutTransform.SetLocation(Translation * 100.0f);  // Convert from meters to centimeters
    }
    else
    {
        return false;
    }

    // Parse rotation (quaternion (scalar last) or Euler angles (pitch-yaw-roll))
    const TArray<TSharedPtr<FJsonValue>>* RotationArray;
    if (TransformObj->TryGetArrayField(TEXT("rotation"), RotationArray))
    {
        if (RotationArray->Num() == 4)
        {
            FQuat Rotation(
                (*RotationArray)[0]->AsNumber(),
                (*RotationArray)[1]->AsNumber(),
                (*RotationArray)[2]->AsNumber(),
                (*RotationArray)[3]->AsNumber());
            OutTransform.SetRotation(Rotation);
        }
        else if (RotationArray->Num() == 3)
        {
            FRotator Rotation(
                (*RotationArray)[0]->AsNumber(),
                (*RotationArray)[1]->AsNumber(),
                (*RotationArray)[2]->AsNumber());
            OutTransform.SetRotation(Rotation.Quaternion());
        }
        else
        {
            return false;
        }
    }

    return true;
}

FString USimulationConfigurationSubsystem::ResolveRelativePath(const FString& ConfigPath, const FString& RelativePath) const
{
    if (FPaths::IsRelative(RelativePath))
    {
        const FString ConfigDir = FPaths::GetPath(ConfigPath);
        return FPaths::ConvertRelativePathToFull(FPaths::Combine(ConfigDir, RelativePath));
    }
    return RelativePath;
}

bool USimulationConfigurationSubsystem::ValidateConfiguration(const FFMUConfiguration& Config, ESimulationMode Mode) const
{
    switch (Mode)
    {
    case ESimulationMode::SM_FMU:
    {
        // Validate FMU-specific requirements
        if (Config.fmuPath.IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("FMU mode requires a valid FMU path"));
            return false;
        }

        if (!FPaths::FileExists(Config.fmuPath))
        {
            UE_LOG(LogTemp, Error, TEXT("FMU file not found: %s"), *Config.fmuPath);
            return false;
        }

        if (Config.timeStep <= 0.0)
        {
            UE_LOG(LogTemp, Error, TEXT("FMU mode requires a positive time step"));
            return false;
        }
        break;
    }

    case ESimulationMode::SM_Sequence:
    {
        // Sequence mode validation
        // The fmuPath field is repurposed to store the sequence file path in sequence mode
        if (Config.fmuPath.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("No sequence file specified for sequence mode"));
        }
        break;
    }

    case ESimulationMode::SM_OnDemand:
    {
        // OnDemand mode validation
        if (Config.streamingPort <= 0)
        {
            UE_LOG(LogTemp, Error, TEXT("OnDemand mode requires a valid streaming port for WebSocket communication"));
            return false;
        }

        // OnDemand mode should have at least one camera sensor configured
        if (Config.sensors.Num() == 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("OnDemand mode has no sensors configured - no captures will be possible"));
        }
        else
        {
            // Check if at least one camera sensor is configured
            bool bHasCameraSensor = false;
            for (const FFMUSensorConfig& SensorConfig : Config.sensors)
            {
                if (SensorConfig.type.Equals(TEXT("Camera"), ESearchCase::IgnoreCase))
                {
                    bHasCameraSensor = true;
                    break;
                }
            }

            if (!bHasCameraSensor)
            {
                UE_LOG(LogTemp, Warning, TEXT("OnDemand mode has no camera sensors configured - no image captures will be possible"));
            }
        }
        break;
    }

    case ESimulationMode::SM_None:
    default:
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid simulation mode"));
        return false;
    }
    }

    // Validate sensors
    for (const FFMUSensorConfig& SensorConfig : Config.sensors)
    {
        if (SensorConfig.name.IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("Sensor missing required name field"));
            return false;
        }

        if (SensorConfig.type.IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("Sensor '%s' missing required type field"), *SensorConfig.name);
            return false;
        }
    }

    return true;
}

bool USimulationConfigurationSubsystem::ParseInputConfiguration(const TSharedPtr<FJsonObject> &JsonObject, FFMUInputConfiguration &OutInputConfig, const FString &ConfigPath)
{
    if (!JsonObject.IsValid())
    {
        return false;
    }

    const TSharedPtr<FJsonObject> *InputConfigObj;
    if (!JsonObject->TryGetObjectField(TEXT("inputConfiguration"), InputConfigObj))
    {
        OutInputConfig = FFMUInputConfiguration();
        return true;
    }

    if ((*InputConfigObj)->HasField(TEXT("bSynchronizeWithSimulation")))
    {
        OutInputConfig.bSynchronizeWithSimulation = (*InputConfigObj)->GetBoolField(TEXT("bSynchronizeWithSimulation"));
    }
    else
    {
        OutInputConfig.bSynchronizeWithSimulation = true;
    }

    const TArray<TSharedPtr<FJsonValue>> *InputVariablesArray;
    if ((*InputConfigObj)->TryGetArrayField(TEXT("inputVariables"), InputVariablesArray))
    {
        OutInputConfig.inputVariables.Empty();

        for (const TSharedPtr<FJsonValue> &VariableValue : *InputVariablesArray)
        {
            const TSharedPtr<FJsonObject> VariableObj = VariableValue->AsObject();
            if (!VariableObj.IsValid())
            {
                UE_LOG(LogTemp, Warning, TEXT("Invalid input variable object in configuration"));
                continue;
            }

            FFMUInputVariableConfig VariableConfig;

            if (!VariableObj->TryGetStringField(TEXT("variableName"), VariableConfig.variableName))
            {
                const TArray<TSharedPtr<FJsonValue>> *InputSourcesArray;
                if (VariableObj->TryGetArrayField(TEXT("inputSources"), InputSourcesArray) && InputSourcesArray->Num() > 0)
                {
                    const TSharedPtr<FJsonObject> FirstSourceObj = (*InputSourcesArray)[0]->AsObject();
                    if (FirstSourceObj.IsValid())
                    {
                        FString TimeseriesPath;
                        FString SourceType;
                        if (FirstSourceObj->TryGetStringField(TEXT("timeseriesFilePath"), TimeseriesPath) &&
                            FirstSourceObj->TryGetStringField(TEXT("sourceType"), SourceType) &&
                            SourceType.Equals(TEXT("Timeseries"), ESearchCase::IgnoreCase))
                        {
                            if (ProcessAutoDetectionFromCSV(TimeseriesPath, VariableObj, OutInputConfig, ConfigPath))
                            {
                                UE_LOG(LogTemp, Log, TEXT("Auto-detected variables from CSV: %s"), *TimeseriesPath);
                                continue;
                            }
                            else
                            {
                                UE_LOG(LogTemp, Error, TEXT("Auto-detection failed for %s"), *TimeseriesPath);
                            }
                        }
                    }
                }

                UE_LOG(LogTemp, Warning, TEXT("Input variable missing required variableName field and auto-detection failed"));
                continue;
            }

            if (VariableObj->HasField(TEXT("defaultValue")))
            {
                VariableConfig.defaultValue = VariableObj->GetNumberField(TEXT("defaultValue"));
            }

            if (VariableObj->HasField(TEXT("unitConversionFactor")))
            {
                VariableConfig.unitConversionFactor = VariableObj->GetNumberField(TEXT("unitConversionFactor"));
            }

            if (VariableObj->HasField(TEXT("bClampToRange")))
            {
                VariableConfig.bClampToRange = VariableObj->GetBoolField(TEXT("bClampToRange"));
            }

            if (VariableObj->HasField(TEXT("minValue")))
            {
                VariableConfig.minValue = VariableObj->GetNumberField(TEXT("minValue"));
            }

            if (VariableObj->HasField(TEXT("maxValue")))
            {
                VariableConfig.maxValue = VariableObj->GetNumberField(TEXT("maxValue"));
            }

            // Interpolazione per variabile (opzionale nel JSON)
            if (VariableObj->HasField(TEXT("interpolationMethod")))
            {
                const FString MethodStr = VariableObj->GetStringField(TEXT("interpolationMethod"));
                if (MethodStr.Equals(TEXT("linear"), ESearchCase::IgnoreCase))
                {
                    VariableConfig.interpolationMethod = EFMUInputInterpolation::Linear;
                }
                else if (MethodStr.Equals(TEXT("cubic"), ESearchCase::IgnoreCase))
                {
                    VariableConfig.interpolationMethod = EFMUInputInterpolation::Cubic;
                }
                else
                {
                    VariableConfig.interpolationMethod = EFMUInputInterpolation::None;
                }
            }

            const TArray<TSharedPtr<FJsonValue>> *InputSourcesArray;
            if (VariableObj->TryGetArrayField(TEXT("inputSources"), InputSourcesArray))
            {
                VariableConfig.inputSources.Empty();

                for (const TSharedPtr<FJsonValue> &SourceValue : *InputSourcesArray)
                {
                    const TSharedPtr<FJsonObject> SourceObj = SourceValue->AsObject();
                    if (!SourceObj.IsValid())
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Invalid input source object for variable %s"), *VariableConfig.variableName);
                        continue;
                    }

                    FFMUInputSource InputSource;

                    if (!SourceObj->TryGetStringField(TEXT("sourceId"), InputSource.sourceId))
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Input source missing required sourceId field for variable %s"), *VariableConfig.variableName);
                        continue;
                    }

                    FString SourceTypeStr;
                    if (SourceObj->TryGetStringField(TEXT("sourceType"), SourceTypeStr))
                    {
                        if (SourceTypeStr.Equals(TEXT("Static"), ESearchCase::IgnoreCase))
                        {
                            InputSource.sourceType = EFMUInputSourceType::Static;
                        }
                        else if (SourceTypeStr.Equals(TEXT("Timeseries"), ESearchCase::IgnoreCase))
                        {
                            InputSource.sourceType = EFMUInputSourceType::Timeseries;
                        }
                        else if (SourceTypeStr.Equals(TEXT("WebSocket"), ESearchCase::IgnoreCase))
                        {
                            InputSource.sourceType = EFMUInputSourceType::WebSocket;
                        }
                        else if (SourceTypeStr.Equals(TEXT("Manual"), ESearchCase::IgnoreCase))
                        {
                            InputSource.sourceType = EFMUInputSourceType::Manual;
                        }
                        else if (SourceTypeStr.Equals(TEXT("GameController"), ESearchCase::IgnoreCase))
                        {
                            InputSource.sourceType = EFMUInputSourceType::GameController;
                        }
                        else if (SourceTypeStr.Equals(TEXT("VRHeadset"), ESearchCase::IgnoreCase))
                        {
                            InputSource.sourceType = EFMUInputSourceType::VRHeadset;
                        }
                        else
                        {
                            UE_LOG(LogTemp, Warning, TEXT("Unknown source type '%s' for variable %s"), *SourceTypeStr, *VariableConfig.variableName);
                            InputSource.sourceType = EFMUInputSourceType::Static;
                        }
                    }
                    else
                    {
                        InputSource.sourceType = EFMUInputSourceType::Static;
                    }

                    FString PriorityStr;
                    if (SourceObj->TryGetStringField(TEXT("priority"), PriorityStr))
                    {
                        if (PriorityStr.Equals(TEXT("Critical"), ESearchCase::IgnoreCase))
                        {
                            InputSource.priority = EFMUInputPriority::Critical;
                        }
                        else if (PriorityStr.Equals(TEXT("High"), ESearchCase::IgnoreCase))
                        {
                            InputSource.priority = EFMUInputPriority::High;
                        }
                        else if (PriorityStr.Equals(TEXT("Medium"), ESearchCase::IgnoreCase))
                        {
                            InputSource.priority = EFMUInputPriority::Medium;
                        }
                        else if (PriorityStr.Equals(TEXT("Low"), ESearchCase::IgnoreCase))
                        {
                            InputSource.priority = EFMUInputPriority::Low;
                        }
                        else
                        {
                            UE_LOG(LogTemp, Warning, TEXT("Unknown priority '%s' for source %s"), *PriorityStr, *InputSource.sourceId);
                            InputSource.priority = EFMUInputPriority::Medium;
                        }
                    }
                    else
                    {
                        InputSource.priority = EFMUInputPriority::Medium;
                    }

                    if (SourceObj->HasField(TEXT("staticValue")))
                    {
                        InputSource.staticValue = SourceObj->GetNumberField(TEXT("staticValue"));
                    }

                    if (SourceObj->HasField(TEXT("timeseriesFilePath")))
                    {
                        FString TimeseriesPath = SourceObj->GetStringField(TEXT("timeseriesFilePath"));
                        InputSource.timeseriesFilePath = TimeseriesPath;
                    }

                    if (SourceObj->HasField(TEXT("validityDuration")))
                    {
                        InputSource.validityDuration = SourceObj->GetNumberField(TEXT("validityDuration"));
                    }

                    if (SourceObj->HasField(TEXT("bEnabled")))
                    {
                        InputSource.bEnabled = SourceObj->GetBoolField(TEXT("bEnabled"));
                    }
                    else
                    {
                        InputSource.bEnabled = true;
                    }

                    const TSharedPtr<FJsonObject> *ParametersObj;
                    if (SourceObj->TryGetObjectField(TEXT("parameters"), ParametersObj))
                    {
                        for (const auto &Param : (*ParametersObj)->Values)
                        {
                            if (Param.Value->Type == EJson::String)
                            {
                                InputSource.parameters.Add(Param.Key, Param.Value->AsString());
                            }
                            else if (Param.Value->Type == EJson::Number)
                            {
                                InputSource.parameters.Add(Param.Key, FString::SanitizeFloat(Param.Value->AsNumber()));
                            }
                            else if (Param.Value->Type == EJson::Boolean)
                            {
                                InputSource.parameters.Add(Param.Key, Param.Value->AsBool() ? TEXT("true") : TEXT("false"));
                            }
                        }
                    }

                    VariableConfig.inputSources.Add(InputSource);
                }
            }

            OutInputConfig.inputVariables.Add(VariableConfig);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Loaded input configuration with %d variables"), OutInputConfig.inputVariables.Num());
    return true;
}

bool USimulationConfigurationSubsystem::ProcessAutoDetectionFromCSV(const FString &TimeseriesPath, const TSharedPtr<FJsonObject> &VariableObj, FFMUInputConfiguration &OutInputConfig, const FString &ConfigPath)
{
    if (TimeseriesPath.IsEmpty() || !VariableObj.IsValid())
    {
        return false;
    }

    UFMUInputHandlingSubsystem *InputSubsystem = GetGameInstance()->GetSubsystem<UFMUInputHandlingSubsystem>();
    if (!InputSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("FMUInputHandlingSubsystem not available for auto-detection"));
        return false;
    }

    FString FullPath = ResolveRelativePath(ConfigPath, TimeseriesPath);

    TArray<FString> AvailableVariables;
    if (!InputSubsystem->GetAvailableVariablesFromCSV(FullPath, AvailableVariables))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to get available variables from CSV file: %s"), *FullPath);
        return false;
    }

    if (AvailableVariables.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No variables found in CSV file: %s"), *FullPath);
        return false;
    }

    FFMUInputVariableConfig TemplateConfig;
    if (VariableObj->HasField(TEXT("defaultValue")))
    {
        TemplateConfig.defaultValue = VariableObj->GetNumberField(TEXT("defaultValue"));
    }
    if (VariableObj->HasField(TEXT("unitConversionFactor")))
    {
        TemplateConfig.unitConversionFactor = VariableObj->GetNumberField(TEXT("unitConversionFactor"));
    }
    if (VariableObj->HasField(TEXT("bClampToRange")))
    {
        TemplateConfig.bClampToRange = VariableObj->GetBoolField(TEXT("bClampToRange"));
    }
    if (VariableObj->HasField(TEXT("minValue")))
    {
        TemplateConfig.minValue = VariableObj->GetNumberField(TEXT("minValue"));
    }
    if (VariableObj->HasField(TEXT("maxValue")))
    {
        TemplateConfig.maxValue = VariableObj->GetNumberField(TEXT("maxValue"));
    }

    const TArray<TSharedPtr<FJsonValue>> *InputSourcesArray;
    if (!VariableObj->TryGetArrayField(TEXT("inputSources"), InputSourcesArray) || InputSourcesArray->Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("No input sources found in auto-detection template"));
        return false;
    }

    const bool bApply7Rule = (AvailableVariables.Num() == 7);

    for (const FString &VarName : AvailableVariables)
    {
        FFMUInputVariableConfig VariableConfig = TemplateConfig;
        VariableConfig.variableName = VarName;
        VariableConfig.inputSources.Empty();

        if (bApply7Rule)
        {
            if (VarName.StartsWith(TEXT("r_rov["), ESearchCase::IgnoreCase))
            {
                VariableConfig.interpolationMethod = EFMUInputInterpolation::Cubic;
            }
            else
            {
                VariableConfig.interpolationMethod = EFMUInputInterpolation::Linear;
            }
        }
        else
        {
            VariableConfig.interpolationMethod = EFMUInputInterpolation::None;
        }

        for (const TSharedPtr<FJsonValue> &SourceValue : *InputSourcesArray)
        {
            const TSharedPtr<FJsonObject> SourceObj = SourceValue->AsObject();
            if (!SourceObj.IsValid())
            {
                UE_LOG(LogTemp, Warning, TEXT("Invalid input source object for auto-detected variable %s"), *VarName);
                continue;
            }

            FFMUInputSource InputSource;

            FString SourceIdTemplate;
            if (SourceObj->TryGetStringField(TEXT("sourceId"), SourceIdTemplate))
            {
                InputSource.sourceId = FString::Printf(TEXT("%s_%s"), *SourceIdTemplate, *VarName);
            }
            else
            {
                InputSource.sourceId = FString::Printf(TEXT("auto_%s"), *VarName);
            }

            FString SourceTypeStr;
            if (SourceObj->TryGetStringField(TEXT("sourceType"), SourceTypeStr))
            {
                if (SourceTypeStr.Equals(TEXT("Static"), ESearchCase::IgnoreCase))
                {
                    InputSource.sourceType = EFMUInputSourceType::Static;
                }
                else if (SourceTypeStr.Equals(TEXT("Timeseries"), ESearchCase::IgnoreCase))
                {
                    InputSource.sourceType = EFMUInputSourceType::Timeseries;
                }
                else if (SourceTypeStr.Equals(TEXT("WebSocket"), ESearchCase::IgnoreCase))
                {
                    InputSource.sourceType = EFMUInputSourceType::WebSocket;
                }
                else if (SourceTypeStr.Equals(TEXT("Manual"), ESearchCase::IgnoreCase))
                {
                    InputSource.sourceType = EFMUInputSourceType::Manual;
                }
                else if (SourceTypeStr.Equals(TEXT("GameController"), ESearchCase::IgnoreCase))
                {
                    InputSource.sourceType = EFMUInputSourceType::GameController;
                }
                else if (SourceTypeStr.Equals(TEXT("VRHeadset"), ESearchCase::IgnoreCase))
                {
                    InputSource.sourceType = EFMUInputSourceType::VRHeadset;
                }
                else
                {
                    InputSource.sourceType = EFMUInputSourceType::Static;
                }
            }
            else
            {
                InputSource.sourceType = EFMUInputSourceType::Static;
            }

            FString PriorityStr;
            if (SourceObj->TryGetStringField(TEXT("priority"), PriorityStr))
            {
                if (PriorityStr.Equals(TEXT("Critical"), ESearchCase::IgnoreCase))
                {
                    InputSource.priority = EFMUInputPriority::Critical;
                }
                else if (PriorityStr.Equals(TEXT("High"), ESearchCase::IgnoreCase))
                {
                    InputSource.priority = EFMUInputPriority::High;
                }
                else if (PriorityStr.Equals(TEXT("Medium"), ESearchCase::IgnoreCase))
                {
                    InputSource.priority = EFMUInputPriority::Medium;
                }
                else if (PriorityStr.Equals(TEXT("Low"), ESearchCase::IgnoreCase))
                {
                    InputSource.priority = EFMUInputPriority::Low;
                }
                else
                {
                    InputSource.priority = EFMUInputPriority::Medium;
                }
            }
            else
            {
                InputSource.priority = EFMUInputPriority::Medium;
            }

            if (SourceObj->HasField(TEXT("staticValue")))
            {
                InputSource.staticValue = SourceObj->GetNumberField(TEXT("staticValue"));
            }

            if (SourceObj->HasField(TEXT("timeseriesFilePath")))
            {
                InputSource.timeseriesFilePath = FullPath; // usa path risolto
            }

            if (SourceObj->HasField(TEXT("validityDuration")))
            {
                InputSource.validityDuration = SourceObj->GetNumberField(TEXT("validityDuration"));
            }

            if (SourceObj->HasField(TEXT("bEnabled")))
            {
                InputSource.bEnabled = SourceObj->GetBoolField(TEXT("bEnabled"));
            }
            else
            {
                InputSource.bEnabled = true;
            }

            const TSharedPtr<FJsonObject> *ParametersObj;
            if (SourceObj->TryGetObjectField(TEXT("parameters"), ParametersObj))
            {
                for (const auto &Param : (*ParametersObj)->Values)
                {
                    if (Param.Value->Type == EJson::String)
                    {
                        InputSource.parameters.Add(Param.Key, Param.Value->AsString());
                    }
                    else if (Param.Value->Type == EJson::Number)
                    {
                        InputSource.parameters.Add(Param.Key, FString::SanitizeFloat(Param.Value->AsNumber()));
                    }
                    else if (Param.Value->Type == EJson::Boolean)
                    {
                        InputSource.parameters.Add(Param.Key, Param.Value->AsBool() ? TEXT("true") : TEXT("false"));
                    }
                }
            }

            VariableConfig.inputSources.Add(InputSource);
        }

        OutInputConfig.inputVariables.Add(VariableConfig);
    }

    UE_LOG(LogTemp, Log, TEXT("Auto-detected %d variables from CSV file (7-rule applied: %s)"),
           AvailableVariables.Num(), bApply7Rule ? TEXT("YES") : TEXT("NO"));

    return true;
}