#include "Subsystems/FMUInputHandlingSubsystem.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"

void UFMUInputHandlingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    bInputProcessingEnabled = true;
    CachedValuesTime = 0.0;

    // Initialize processing statistics
    ProcessingStats.Add(TEXT("TotalInputsProcessed"), 0.0);
    ProcessingStats.Add(TEXT("AverageProcessingTime"), 0.0);
    ProcessingStats.Add(TEXT("ActiveInputSources"), 0.0);
    ProcessingStats.Add(TEXT("LastProcessingTime"), 0.0);

    UE_LOG(LogTemp, Log, TEXT("FMUInputHandlingSubsystem initialized"));
}

void UFMUInputHandlingSubsystem::Deinitialize()
{
    ClearInputBuffers();
    Super::Deinitialize();

    UE_LOG(LogTemp, Log, TEXT("FMUInputHandlingSubsystem deinitialized"));
}

bool UFMUInputHandlingSubsystem::InitializeInputHandling(const FFMUInputConfiguration& Configuration)
{

    if (!ValidateInputConfiguration(Configuration))
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid input configuration provided"));
        return false;
    }

    FScopeLock Lock(&InputUpdateLock);

    InputConfiguration = Configuration;

    UpdateSourceIdMapping();
    ClearInputBuffers();

    // Load CSV timeseries data for all timeseries sources
    LoadTimeseriesDataForAllSources();

    UE_LOG(LogTemp, Log, TEXT("Input handling initialized with %d variables"),
        InputConfiguration.inputVariables.Num());

    return true;
}

void UFMUInputHandlingSubsystem::SetInputConfiguration(const FFMUInputConfiguration& Configuration)
{
    InitializeInputHandling(Configuration);
}

bool UFMUInputHandlingSubsystem::AddInputSource(const FString& VariableName, const FFMUInputSource& InputSource)
{
    if (VariableName.IsEmpty() || InputSource.sourceId.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid variable name or source ID"));
        return false;
    }

    FScopeLock Lock(&InputUpdateLock);

    // Find or create variable configuration
    FFMUInputVariableConfig* VariableConfig = InputConfiguration.FindInputVariable(VariableName);
    if (!VariableConfig)
    {
        // Create new variable configuration
        FFMUInputVariableConfig NewConfig;
        NewConfig.variableName = VariableName;
        NewConfig.defaultValue = 0.0;
        InputConfiguration.inputVariables.Add(NewConfig);
        VariableConfig = &InputConfiguration.inputVariables.Last();
    }

    // Add input source
    VariableConfig->AddInputSource(InputSource);

    // Update mapping
    SourceIdToVariableMap.Add(InputSource.sourceId, VariableName);

    // Update statistics
    UpdateProcessingStat(TEXT("ActiveInputSources"),
        static_cast<double>(SourceIdToVariableMap.Num()));

    OnInputSourceAdded.Broadcast(InputSource.sourceId);

    UE_LOG(LogTemp, Log, TEXT("Added input source '%s' for variable '%s'"),
        *InputSource.sourceId, *VariableName);

    return true;
}

bool UFMUInputHandlingSubsystem::RemoveInputSource(const FString& SourceId)
{
    if (SourceId.IsEmpty())
    {
        return false;
    }

    FScopeLock Lock(&InputUpdateLock);

    // Find and remove source
    FFMUInputVariableConfig* VariableConfig = nullptr;
    FFMUInputSource* Source = nullptr;

    if (!FindInputSourceById(SourceId, VariableConfig, Source))
    {
        UE_LOG(LogTemp, Warning, TEXT("Input source '%s' not found"), *SourceId);
        return false;
    }

    // Remove from variable config
    VariableConfig->RemoveInputSource(SourceId);

    // Remove from mapping
    SourceIdToVariableMap.Remove(SourceId);

    // Update statistics
    UpdateProcessingStat(TEXT("ActiveInputSources"),
        static_cast<double>(SourceIdToVariableMap.Num()));

    OnInputSourceRemoved.Broadcast(SourceId);

    UE_LOG(LogTemp, Log, TEXT("Removed input source '%s'"), *SourceId);

    return true;
}

bool UFMUInputHandlingSubsystem::UpdateInputValue(const FString& SourceId, double NewValue, double CurrentTime)
{
    if (SourceId.IsEmpty())
    {
        return false;
    }

    FScopeLock Lock(&InputUpdateLock);

    // Find source
    FFMUInputVariableConfig* VariableConfig = nullptr;
    FFMUInputSource* Source = nullptr;

    if (!FindInputSourceById(SourceId, VariableConfig, Source))
    {
        UE_LOG(LogTemp, Warning, TEXT("Input source '%s' not found for value update"), *SourceId);
        return false;
    }

    // Update value
    Source->UpdateValue(CurrentTime, NewValue);

    // Invalidate cached values
    CachedValuesTime = 0.0;

    // Broadcast change event
    OnInputValueChanged.Broadcast(VariableConfig->variableName, NewValue);

    return true;
}

bool UFMUInputHandlingSubsystem::LoadInputTimeseries(const FString& SourceId, const FFMUInputTimeseries& Timeseries)
{
    if (SourceId.IsEmpty())
    {
        return false;
    }

    FScopeLock Lock(&InputUpdateLock);
    return LoadInputTimeseriesInternal(SourceId, Timeseries);
}

bool UFMUInputHandlingSubsystem::LoadInputTimeseriesInternal(const FString& SourceId, const FFMUInputTimeseries& Timeseries)
{
    // This method assumes the InputUpdateLock is already held by the caller

    if (SourceId.IsEmpty())
    {
        return false;
    }

    // Find source
    FFMUInputVariableConfig* VariableConfig = nullptr;
    FFMUInputSource* Source = nullptr;

    if (!FindInputSourceById(SourceId, VariableConfig, Source))
    {
        UE_LOG(LogTemp, Warning, TEXT("Input source '%s' not found for timeseries load"), *SourceId);
        return false;
    }

    // Update timeseries
    Source->timeseries = Timeseries;
    Source->sourceType = EFMUInputSourceType::Timeseries;

    // Ensure WebSocket source is immediately active and valid
    Source->bEnabled = true;
    Source->lastUpdateTime = 0.0; // Mark as valid from simulation start

    // Invalidate cached values
    CachedValuesTime = 0.0;

    return true;
}

bool UFMUInputHandlingSubsystem::GetInputValue(const FString& VariableName, double CurrentTime, double& OutValue) const
{
    if (VariableName.IsEmpty())
    {
        return false;
    }

    FScopeLock Lock(&InputUpdateLock);

    const FFMUInputVariableConfig* VariableConfig = FindInputVariableInternal(VariableName);
    if (!VariableConfig)
    {
        UE_LOG(LogTemp, Verbose, TEXT("Variable '%s' not found in input configuration"), *VariableName);
        return false;
    }

    return VariableConfig->GetCurrentValue(CurrentTime, OutValue);
}

void UFMUInputHandlingSubsystem::GetAllInputValues(double CurrentTime, TMap<FString, double>& OutValues) const
{
    FScopeLock Lock(&InputUpdateLock);

    OutValues.Empty();

    for (const FFMUInputVariableConfig& VariableConfig : InputConfiguration.inputVariables)
    {
        double Value;
        if (VariableConfig.GetCurrentValue(CurrentTime, Value))
        {
            OutValues.Add(VariableConfig.variableName, Value);
        }
    }
}

void UFMUInputHandlingSubsystem::ProcessInputs(double CurrentTime, TMap<FString, double>& OutInputValues)
{
    if (!bInputProcessingEnabled)
    {
        OutInputValues.Empty();
        return;
    }

    double ProcessingStartTime = FPlatformTime::Seconds();

    // Check if we can use cached values
    if (FMath::Abs(CurrentTime - CachedValuesTime) < CacheValidityDuration &&
        CachedInputValues.Num() > 0)
    {
        OutInputValues = CachedInputValues;
        return;
    }

    OutInputValues.Empty();
    CachedInputValues.Empty();

    // Process all input variables
    for (const FFMUInputVariableConfig& VariableConfig : InputConfiguration.inputVariables)
    {
        double Value;
        if (VariableConfig.GetCurrentValue(CurrentTime, Value))
        {
            OutInputValues.Add(VariableConfig.variableName, Value);
            CachedInputValues.Add(VariableConfig.variableName, Value);
        }
    }

    // Update cache time
    CachedValuesTime = CurrentTime;

    // Update processing statistics
    double ProcessingTime = FPlatformTime::Seconds() - ProcessingStartTime;
    UpdateProcessingStat(TEXT("LastProcessingTime"), ProcessingTime);

    double TotalProcessed = ProcessingStats.FindRef(TEXT("TotalInputsProcessed")) + 1.0;
    UpdateProcessingStat(TEXT("TotalInputsProcessed"), TotalProcessed);

    double AverageTime = ProcessingStats.FindRef(TEXT("AverageProcessingTime"));
    AverageTime = (AverageTime * (TotalProcessed - 1.0) + ProcessingTime) / TotalProcessed;
    UpdateProcessingStat(TEXT("AverageProcessingTime"), AverageTime);
}

void UFMUInputHandlingSubsystem::SetInputProcessingEnabled(bool bEnabled)
{
    bInputProcessingEnabled = bEnabled;

    if (!bEnabled)
    {
        ClearInputBuffers();
    }

    UE_LOG(LogTemp, Log, TEXT("Input processing %s"),
        bEnabled ? TEXT("enabled") : TEXT("disabled"));
}

bool UFMUInputHandlingSubsystem::FindInputVariable(const FString& VariableName, FFMUInputVariableConfig& OutVariableConfig) const
{
    FScopeLock Lock(&InputUpdateLock);
    const FFMUInputVariableConfig* FoundConfig = InputConfiguration.FindInputVariable(VariableName);
    if (FoundConfig)
    {
        OutVariableConfig = *FoundConfig;
        return true;
    }
    return false;
}

const FFMUInputVariableConfig* UFMUInputHandlingSubsystem::FindInputVariableInternal(const FString& VariableName) const
{
    return InputConfiguration.FindInputVariable(VariableName);
}

void UFMUInputHandlingSubsystem::GetInputProcessingStats(TMap<FString, double>& OutStats) const
{
    FScopeLock Lock(&InputUpdateLock);
    OutStats = ProcessingStats;
}

void UFMUInputHandlingSubsystem::ClearInputBuffers()
{
    FScopeLock Lock(&InputUpdateLock);

    CachedInputValues.Empty();
    CachedValuesTime = 0.0;

    UE_LOG(LogTemp, Log, TEXT("Input buffers cleared"));
}

bool UFMUInputHandlingSubsystem::HandleWebSocketInputCommand(const FString& CommandJson)
{
    if (CommandJson.IsEmpty())
    {
        return false;
    }

    TSharedPtr<FJsonObject> Command;
    if (!ParseWebSocketInputCommand(CommandJson, Command))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to parse WebSocket input command: %s"), *CommandJson);
        return false;
    }

    FString CommandType;
    if (!Command->TryGetStringField(TEXT("type"), CommandType))
    {
        UE_LOG(LogTemp, Error, TEXT("Missing 'type' field in WebSocket input command"));
        return false;
    }

    if (CommandType == TEXT("set_input"))
    {
        FString SourceId;
        double Value;
        double Timestamp;

        if (Command->TryGetStringField(TEXT("sourceId"), SourceId) &&
            Command->TryGetNumberField(TEXT("value"), Value) &&
            Command->TryGetNumberField(TEXT("timestamp"), Timestamp))
        {
            return UpdateInputValue(SourceId, Value, Timestamp);
        }
    }
    else if (CommandType == TEXT("load_timeseries"))
    {
        UE_LOG(LogTemp, Verbose, TEXT("Processing load_timeseries command"));
        FString SourceId;
        if (Command->TryGetStringField(TEXT("sourceId"), SourceId))
        {
            const TArray<TSharedPtr<FJsonValue>>* TimeValuesArray;
            if (Command->TryGetArrayField(TEXT("timeValues"), TimeValuesArray))
            {
                FFMUInputTimeseries Timeseries;

                for (const TSharedPtr<FJsonValue>& TimeValueJson : *TimeValuesArray)
                {
                    const TSharedPtr<FJsonObject>& TimeValueObj = TimeValueJson->AsObject();
                    if (TimeValueObj.IsValid())
                    {
                        double Time, Value;
                        if (TimeValueObj->TryGetNumberField(TEXT("time"), Time) &&
                            TimeValueObj->TryGetNumberField(TEXT("value"), Value))
                        {
                            Timeseries.AddTimeValue(Time, Value);
                        }
                    }
                }

                Timeseries.Sort();
                return LoadInputTimeseries(SourceId, Timeseries);
            }
        }
    }
    else if (CommandType == TEXT("load_timeseries_batch"))
    {
        const TArray<TSharedPtr<FJsonValue>>* VariablesArray;
        if (Command->TryGetArrayField(TEXT("variables"), VariablesArray))
        {
            int32 SuccessCount = 0;
            int32 TotalCount = VariablesArray->Num();

            // Process all variables in the batch atomically - acquire lock once for entire batch
            FScopeLock Lock(&InputUpdateLock);

            for (const TSharedPtr<FJsonValue>& VariableJson : *VariablesArray)
            {
                const TSharedPtr<FJsonObject>& VariableObj = VariableJson->AsObject();
                if (VariableObj.IsValid())
                {
                    FString SourceId;
                    if (VariableObj->TryGetStringField(TEXT("sourceId"), SourceId))
                    {
                        const TArray<TSharedPtr<FJsonValue>>* TimeValuesArray;
                        if (VariableObj->TryGetArrayField(TEXT("timeValues"), TimeValuesArray))
                        {
                            FFMUInputTimeseries Timeseries;

                            for (const TSharedPtr<FJsonValue>& TimeValueJson : *TimeValuesArray)
                            {
                                const TSharedPtr<FJsonObject>& TimeValueObj = TimeValueJson->AsObject();
                                if (TimeValueObj.IsValid())
                                {
                                    double Time, Value;
                                    if (TimeValueObj->TryGetNumberField(TEXT("time"), Time) &&
                                        TimeValueObj->TryGetNumberField(TEXT("value"), Value))
                                    {
                                        Timeseries.AddTimeValue(Time, Value);
                                    }
                                }
                            }

                            Timeseries.Sort();

                            // Use internal method since we already hold the lock
                            if (LoadInputTimeseriesInternal(SourceId, Timeseries))
                            {
                                SuccessCount++;
                            }
                            else
                            {
                                UE_LOG(LogTemp, Error, TEXT("Failed to load timeseries for sourceId: %s in batch"), *SourceId);
                            }
                        }
                    }
                }
            }

            // Lock will be released automatically when exiting scope

            return SuccessCount == TotalCount; // Return true only if all variables were loaded successfully
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("Unknown or malformed WebSocket input command: %s"), *CommandType);
    UE_LOG(LogTemp, Verbose, TEXT("Full command JSON: %s"), *CommandJson);
    return false;
}

void UFMUInputHandlingSubsystem::HandleWebSocketInputCommandDelegate(const FString& CommandJson)
{
    HandleWebSocketInputCommand(CommandJson);
}

bool UFMUInputHandlingSubsystem::LoadInputTimeseriesFromFile(const FString& FilePath, const FString& VariableName, const FString& SourceId)
{
    if (FilePath.IsEmpty() || VariableName.IsEmpty() || SourceId.IsEmpty())
    {
        return false;
    }

    if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("Timeseries file not found: %s"), *FilePath);
        return false;
    }

    FFMUInputTimeseries Timeseries;

    FString Extension = FPaths::GetExtension(FilePath).ToLower();
    bool bLoadSuccess = false;

    if (Extension == TEXT("csv"))
    {
        // First try to load as single-variable CSV (2 columns: time, value)
        bLoadSuccess = LoadTimeseriesFromCSV(FilePath, Timeseries);

        // If that fails, try to load as multi-variable CSV
        if (!bLoadSuccess)
        {
            bLoadSuccess = LoadTimeseriesFromMultiVariableCSV(FilePath, VariableName, Timeseries);
        }
    }
    else if (Extension == TEXT("json"))
    {
        bLoadSuccess = LoadTimeseriesFromJSON(FilePath, Timeseries);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Unsupported timeseries file format: %s"), *Extension);
        return false;
    }

    if (!bLoadSuccess)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load timeseries from file: %s"), *FilePath);
        return false;
    }

    // Create input source
    FFMUInputSource InputSource;
    InputSource.sourceId = SourceId;
    InputSource.sourceType = EFMUInputSourceType::Timeseries;
    InputSource.timeseries = Timeseries;
    InputSource.timeseriesFilePath = FilePath;

    return AddInputSource(VariableName, InputSource);
}

bool UFMUInputHandlingSubsystem::FindInputSourceById(const FString& SourceId, FFMUInputVariableConfig*& OutVariable, FFMUInputSource*& OutSource)
{
    FString* VariableName = SourceIdToVariableMap.Find(SourceId);
    if (!VariableName)
    {
        return false;
    }

    OutVariable = InputConfiguration.FindInputVariable(*VariableName);
    if (!OutVariable)
    {
        return false;
    }

    OutSource = OutVariable->FindInputSource(SourceId);
    return OutSource != nullptr;
}

void UFMUInputHandlingSubsystem::UpdateSourceIdMapping()
{
    SourceIdToVariableMap.Empty();

    for (const FFMUInputVariableConfig& VariableConfig : InputConfiguration.inputVariables)
    {
        for (const FFMUInputSource& Source : VariableConfig.inputSources)
        {
            SourceIdToVariableMap.Add(Source.sourceId, VariableConfig.variableName);
        }
    }
}

bool UFMUInputHandlingSubsystem::ValidateInputConfiguration(const FFMUInputConfiguration& Configuration) const
{
    // Check for duplicate variable names
    TSet<FString> VariableNames;
    for (const FFMUInputVariableConfig& VariableConfig : Configuration.inputVariables)
    {
        if (VariableConfig.variableName.IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("Empty variable name in input configuration"));
            return false;
        }

        if (VariableNames.Contains(VariableConfig.variableName))
        {
            UE_LOG(LogTemp, Error, TEXT("Duplicate variable name: %s"), *VariableConfig.variableName);
            return false;
        }

        VariableNames.Add(VariableConfig.variableName);

        // Check for duplicate source IDs
        TSet<FString> SourceIds;
        for (const FFMUInputSource& Source : VariableConfig.inputSources)
        {
            if (Source.sourceId.IsEmpty())
            {
                UE_LOG(LogTemp, Error, TEXT("Empty source ID in variable: %s"), *VariableConfig.variableName);
                return false;
            }

            if (SourceIds.Contains(Source.sourceId))
            {
                UE_LOG(LogTemp, Error, TEXT("Duplicate source ID '%s' in variable: %s"),
                    *Source.sourceId, *VariableConfig.variableName);
                return false;
            }

            SourceIds.Add(Source.sourceId);
        }
    }

    return true;
}

bool UFMUInputHandlingSubsystem::ParseWebSocketInputCommand(const FString& CommandJson, TSharedPtr<FJsonObject>& OutCommand) const
{
    TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(CommandJson);
    return FJsonSerializer::Deserialize(JsonReader, OutCommand) && OutCommand.IsValid();
}

void UFMUInputHandlingSubsystem::UpdateProcessingStat(const FString& StatName, double Value)
{
    ProcessingStats.Add(StatName, Value);
}

bool UFMUInputHandlingSubsystem::LoadTimeseriesFromCSV(const FString& FilePath, FFMUInputTimeseries& OutTimeseries) const
{
    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
    {
        return false;
    }

    TArray<FString> Lines;
    FileContent.ParseIntoArray(Lines, TEXT("\n"), true);

    OutTimeseries.Clear();

    for (int32 i = 0; i < Lines.Num(); ++i)
    {
        FString Line = Lines[i].TrimStartAndEnd();
        if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
        {
            continue; // Skip empty lines and comments
        }

        TArray<FString> Values;
        Line.ParseIntoArray(Values, TEXT(","), true);

        if (Values.Num() >= 2)
        {
            double Time = FCString::Atod(*Values[0].TrimStartAndEnd());
            double Value = FCString::Atod(*Values[1].TrimStartAndEnd());
            OutTimeseries.AddTimeValue(Time, Value);
        }
    }

    OutTimeseries.Sort();
    return OutTimeseries.timeValues.Num() > 0;
}

bool UFMUInputHandlingSubsystem::LoadTimeseriesFromJSON(const FString &FilePath, FFMUInputTimeseries &OutTimeseries) const
{
    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(FileContent);

    if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>> *TimeValuesArray;
    if (!JsonObject->TryGetArrayField(TEXT("timeValues"), TimeValuesArray))
    {
        return false;
    }

    OutTimeseries.Clear();

    for (const TSharedPtr<FJsonValue> &TimeValueJson : *TimeValuesArray)
    {
        const TSharedPtr<FJsonObject> &TimeValueObj = TimeValueJson->AsObject();
        if (TimeValueObj.IsValid())
        {
            double Time, Value;
            if (TimeValueObj->TryGetNumberField(TEXT("time"), Time) &&
                TimeValueObj->TryGetNumberField(TEXT("value"), Value))
            {
                OutTimeseries.AddTimeValue(Time, Value);
            }
        }
    }

    // NON impostiamo più l'interpolazione nella timeseries qui.
    // L'interpolazione è proprietà della variabile (FFMUInputVariableConfig).

    // Load looping configuration
    JsonObject->TryGetBoolField(TEXT("looping"), OutTimeseries.bLooping);
    JsonObject->TryGetNumberField(TEXT("loopDuration"), OutTimeseries.loopDuration);

    OutTimeseries.Sort();
    return OutTimeseries.timeValues.Num() > 0;
}

bool UFMUInputHandlingSubsystem::LoadMultiVariableTimeseriesFromFile(const FString& FilePath, const FString& SourceIdPrefix, TArray<FString>& OutLoadedVariables)
{
    if (FilePath.IsEmpty() || SourceIdPrefix.IsEmpty())
    {
        return false;
    }

    if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("Multi-variable CSV file not found: %s"), *FilePath);
        return false;
    }

    FString Extension = FPaths::GetExtension(FilePath).ToLower();
    if (Extension != TEXT("csv"))
    {
        UE_LOG(LogTemp, Error, TEXT("Multi-variable loading only supports CSV files: %s"), *FilePath);
        return false;
    }

    // Load all timeseries from the file
    TMap<FString, FFMUInputTimeseries> AllTimeseries;
    if (!LoadAllTimeseriesFromMultiVariableCSV(FilePath, AllTimeseries))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load multi-variable CSV file: %s"), *FilePath);
        return false;
    }

    OutLoadedVariables.Empty();

    // Create input sources for each variable
    for (const auto& TimeseriesPair : AllTimeseries)
    {
        const FString& VariableName = TimeseriesPair.Key;
        const FFMUInputTimeseries& Timeseries = TimeseriesPair.Value;

        // Generate source ID
        FString SourceId = FString::Printf(TEXT("%s_%s"), *SourceIdPrefix, *VariableName);

        // Create input source
        FFMUInputSource InputSource;
        InputSource.sourceId = SourceId;
        InputSource.sourceType = EFMUInputSourceType::Timeseries;
        InputSource.timeseries = Timeseries;
        InputSource.timeseriesFilePath = FilePath;
        InputSource.priority = EFMUInputPriority::Medium;
        InputSource.bEnabled = true;

        // Add the input source (this will automatically create the variable if it doesn't exist)
        if (AddInputSource(VariableName, InputSource))
        {
            OutLoadedVariables.Add(VariableName);
            UE_LOG(LogTemp, Log, TEXT("Loaded timeseries for variable '%s' from multi-variable CSV"), *VariableName);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to add input source for variable '%s'"), *VariableName);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Loaded %d variables from multi-variable CSV file: %s"),
        OutLoadedVariables.Num(), *FilePath);

    return OutLoadedVariables.Num() > 0;
}

bool UFMUInputHandlingSubsystem::GetAvailableVariablesFromCSV(const FString& FilePath, TArray<FString>& OutVariableNames)
{
    if (FilePath.IsEmpty())
    {
        return false;
    }

    if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("CSV file not found: %s"), *FilePath);
        return false;
    }

    TMap<FString, int32> ColumnNames;
    int32 TimeColumnIndex;

    if (!ParseCSVHeader(FilePath, ColumnNames, TimeColumnIndex))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to parse CSV header: %s"), *FilePath);
        return false;
    }

    OutVariableNames.Empty();

    // Add all column names except time column
    for (const auto& ColumnPair : ColumnNames)
    {
        if (ColumnPair.Value != TimeColumnIndex)
        {
            OutVariableNames.Add(ColumnPair.Key);
        }
    }

    OutVariableNames.Sort();
    return OutVariableNames.Num() > 0;
}

bool UFMUInputHandlingSubsystem::LoadTimeseriesFromMultiVariableCSV(const FString& FilePath, const FString& VariableName, FFMUInputTimeseries& OutTimeseries) const
{
    if (FilePath.IsEmpty() || VariableName.IsEmpty())
    {
        return false;
    }

    // Parse header to find column indices
    TMap<FString, int32> ColumnNames;
    int32 TimeColumnIndex;

    if (!ParseCSVHeader(FilePath, ColumnNames, TimeColumnIndex))
    {
        return false;
    }

    if (TimeColumnIndex == -1)
    {
        UE_LOG(LogTemp, Error, TEXT("No time column found in CSV file: %s"), *FilePath);
        return false;
    }

    const int32* VariableColumnIndex = ColumnNames.Find(VariableName);
    if (!VariableColumnIndex)
    {
        UE_LOG(LogTemp, Error, TEXT("Variable '%s' not found in CSV file: %s"), *VariableName, *FilePath);
        return false;
    }

    // Load file content
    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
    {
        return false;
    }

    TArray<FString> Lines;
    FileContent.ParseIntoArray(Lines, TEXT("\n"), true);

    OutTimeseries.Clear();

    bool bSkippedHeader = false;

    for (int32 i = 0; i < Lines.Num(); ++i)
    {
        FString Line = Lines[i].TrimStartAndEnd();
        if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
        {
            continue; // Skip empty lines and comments
        }

        // Skip header line
        if (!bSkippedHeader)
        {
            bSkippedHeader = true;
            continue;
        }

        TArray<FString> Values;
        Line.ParseIntoArray(Values, TEXT(","), true);

        if (Values.Num() > FMath::Max(TimeColumnIndex, *VariableColumnIndex))
        {
            double Time = FCString::Atod(*Values[TimeColumnIndex].TrimStartAndEnd());

            FString ValueStr = Values[*VariableColumnIndex].TrimStartAndEnd();

            // Handle boolean values
            double Value;
            if (ValueStr.Equals(TEXT("True"), ESearchCase::IgnoreCase) || ValueStr.Equals(TEXT("true"), ESearchCase::IgnoreCase))
            {
                Value = 1.0;
            }
            else if (ValueStr.Equals(TEXT("False"), ESearchCase::IgnoreCase) || ValueStr.Equals(TEXT("false"), ESearchCase::IgnoreCase))
            {
                Value = 0.0;
            }
            else
            {
                Value = FCString::Atod(*ValueStr);
            }

            OutTimeseries.AddTimeValue(Time, Value);
        }
    }

    OutTimeseries.Sort();
    return OutTimeseries.timeValues.Num() > 0;
}

bool UFMUInputHandlingSubsystem::ParseCSVHeader(const FString& FilePath, TMap<FString, int32>& OutColumnNames, int32& OutTimeColumnIndex) const
{
    if (FilePath.IsEmpty())
    {
        return false;
    }

    // Load only the first few lines to get the header
    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
    {
        return false;
    }

    TArray<FString> Lines;
    FileContent.ParseIntoArray(Lines, TEXT("\n"), true);

    if (Lines.Num() == 0)
    {
        return false;
    }

    // Find the first non-comment, non-empty line as header
    FString HeaderLine;
    for (const FString& Line : Lines)
    {
        FString TrimmedLine = Line.TrimStartAndEnd();
        if (!TrimmedLine.IsEmpty() && !TrimmedLine.StartsWith(TEXT("#")))
        {
            HeaderLine = TrimmedLine;
            break;
        }
    }

    if (HeaderLine.IsEmpty())
    {
        return false;
    }

    // Parse header columns
    TArray<FString> ColumnHeaders;
    HeaderLine.ParseIntoArray(ColumnHeaders, TEXT(","), true);

    OutColumnNames.Empty();
    OutTimeColumnIndex = -1;

    for (int32 i = 0; i < ColumnHeaders.Num(); ++i)
    {
        FString ColumnName = ColumnHeaders[i].TrimStartAndEnd();

        // Check if this is the time column
        if (ColumnName.Equals(TEXT("time"), ESearchCase::IgnoreCase) ||
            ColumnName.Equals(TEXT("t"), ESearchCase::IgnoreCase) ||
            ColumnName.Equals(TEXT("timestamp"), ESearchCase::IgnoreCase))
        {
            OutTimeColumnIndex = i;
        }

        OutColumnNames.Add(ColumnName, i);
    }

    return OutColumnNames.Num() > 0 && OutTimeColumnIndex != -1;
}

bool UFMUInputHandlingSubsystem::LoadAllTimeseriesFromMultiVariableCSV(const FString& FilePath, TMap<FString, FFMUInputTimeseries>& OutTimeseries) const
{
    if (FilePath.IsEmpty())
    {
        return false;
    }

    // Parse header to find column indices
    TMap<FString, int32> ColumnNames;
    int32 TimeColumnIndex;

    if (!ParseCSVHeader(FilePath, ColumnNames, TimeColumnIndex))
    {
        return false;
    }

    if (TimeColumnIndex == -1)
    {
        UE_LOG(LogTemp, Error, TEXT("No time column found in CSV file: %s"), *FilePath);
        return false;
    }

    // Load file content
    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
    {
        return false;
    }

    TArray<FString> Lines;
    FileContent.ParseIntoArray(Lines, TEXT("\n"), true);

    // Initialize timeseries for each variable column
    OutTimeseries.Empty();
    for (const auto& ColumnPair : ColumnNames)
    {
        if (ColumnPair.Value != TimeColumnIndex) // Skip time column
        {
            OutTimeseries.Add(ColumnPair.Key, FFMUInputTimeseries());
        }
    }

    bool bSkippedHeader = false;

    for (int32 i = 0; i < Lines.Num(); ++i)
    {
        FString Line = Lines[i].TrimStartAndEnd();
        if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
        {
            continue; // Skip empty lines and comments
        }

        // Skip header line
        if (!bSkippedHeader)
        {
            bSkippedHeader = true;
            continue;
        }

        TArray<FString> Values;
        Line.ParseIntoArray(Values, TEXT(","), true);

        if (Values.Num() > TimeColumnIndex)
        {
            double Time = FCString::Atod(*Values[TimeColumnIndex].TrimStartAndEnd());

            // Process each variable column
            for (const auto& ColumnPair : ColumnNames)
            {
                if (ColumnPair.Value != TimeColumnIndex && ColumnPair.Value < Values.Num())
                {
                    FString ValueStr = Values[ColumnPair.Value].TrimStartAndEnd();

                    // Handle boolean values
                    double Value;
                    if (ValueStr.Equals(TEXT("True"), ESearchCase::IgnoreCase) || ValueStr.Equals(TEXT("true"), ESearchCase::IgnoreCase))
                    {
                        Value = 1.0;
                    }
                    else if (ValueStr.Equals(TEXT("False"), ESearchCase::IgnoreCase) || ValueStr.Equals(TEXT("false"), ESearchCase::IgnoreCase))
                    {
                        Value = 0.0;
                    }
                    else
                    {
                        Value = FCString::Atod(*ValueStr);
                    }

                    FFMUInputTimeseries* Timeseries = OutTimeseries.Find(ColumnPair.Key);
                    if (Timeseries)
                    {
                        Timeseries->AddTimeValue(Time, Value);
                    }
                }
            }
        }
    }

    // Sort all timeseries
    for (auto& TimeseriesPair : OutTimeseries)
    {
        TimeseriesPair.Value.Sort();
    }

    UE_LOG(LogTemp, Log, TEXT("Loaded %d variables from multi-variable CSV: %s"),
        OutTimeseries.Num(), *FilePath);

    return OutTimeseries.Num() > 0;
}

void UFMUInputHandlingSubsystem::LoadTimeseriesDataForAllSources()
{
    for (int32 i = 0; i < InputConfiguration.inputVariables.Num(); ++i)
    {
        FFMUInputVariableConfig& VarConfig = InputConfiguration.inputVariables[i];

        for (int32 j = 0; j < VarConfig.inputSources.Num(); ++j)
        {
            FFMUInputSource& Source = VarConfig.inputSources[j];

            // Only load timeseries data for timeseries sources that have a file path
            if (Source.sourceType == EFMUInputSourceType::Timeseries && !Source.timeseriesFilePath.IsEmpty())
            {
                // Check if file exists
                if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*Source.timeseriesFilePath))
                {
                    UE_LOG(LogTemp, Error, TEXT("Timeseries file not found: %s"), *Source.timeseriesFilePath);
                    continue;
                }

                // Determine file type and load accordingly
                FString Extension = FPaths::GetExtension(Source.timeseriesFilePath).ToLower();
                FFMUInputTimeseries LoadedTimeseries;
                bool bLoadSuccess = false;

                if (Extension == TEXT("csv"))
                {
                    // Try to load as multi-variable CSV (this is the most common case for auto-detected variables)
                    bLoadSuccess = LoadTimeseriesFromMultiVariableCSV(Source.timeseriesFilePath, VarConfig.variableName, LoadedTimeseries);

                    if (!bLoadSuccess)
                    {
                        // Fall back to single-variable CSV
                        bLoadSuccess = LoadTimeseriesFromCSV(Source.timeseriesFilePath, LoadedTimeseries);
                    }
                }
                else if (Extension == TEXT("json"))
                {
                    bLoadSuccess = LoadTimeseriesFromJSON(Source.timeseriesFilePath, LoadedTimeseries);
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("Unsupported timeseries file format: %s"), *Extension);
                    continue;
                }

                if (bLoadSuccess)
                {
                    Source.timeseries = LoadedTimeseries;
                    UE_LOG(LogTemp, Log, TEXT("Loaded timeseries for variable '%s' (%d points)"),
                        *VarConfig.variableName, LoadedTimeseries.timeValues.Num());
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("Failed to load timeseries for variable '%s' from '%s'"),
                        *VarConfig.variableName, *Source.sourceId);
                }
            }
        }
    }
}