#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FMUhandling/FMUInputDataStructures.h"
#include "FMUInputHandlingSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInputValueChanged, const FString &, VariableName, double, Value);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInputSourceAdded, const FString &, SourceId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInputSourceRemoved, const FString &, SourceId);

/**
 * Central subsystem for managing FMU input handling
 * Supports multiple input sources with priority-based resolution
 * Provides real-time input scheduling and buffering for smooth simulation
 */
UCLASS(BlueprintType)
class PROXSIMA_API UFMUInputHandlingSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // USubsystem interface
    virtual void Initialize(FSubsystemCollectionBase &Collection) override;
    virtual void Deinitialize() override;

    /**
     * Initialize the input handling subsystem with configuration
     * @param Configuration Input configuration to use
     * @return True if initialization was successful
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Input")
    bool InitializeInputHandling(const FFMUInputConfiguration &Configuration);

    /**
     * Set the current input configuration
     * @param Configuration New input configuration
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Input")
    void SetInputConfiguration(const FFMUInputConfiguration &Configuration);

    /**
     * Get the current input configuration
     * @return Current input configuration
     */
    UFUNCTION(BlueprintPure, Category = "FMU Input")
    const FFMUInputConfiguration &GetInputConfiguration() const { return InputConfiguration; }

    /**
     * Add or update an input source for a specific variable
     * @param VariableName Name of the FMU variable
     * @param InputSource Input source configuration
     * @return True if source was successfully added
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Input")
    bool AddInputSource(const FString &VariableName, const FFMUInputSource &InputSource);

    /**
     * Remove an input source by ID
     * @param SourceId ID of the input source to remove
     * @return True if source was successfully removed
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Input")
    bool RemoveInputSource(const FString &SourceId);

    /**
     * Update input value from external source (e.g., WebSocket)
     * @param SourceId ID of the input source
     * @param NewValue New value to set
     * @param CurrentTime Current simulation time
     * @return True if value was successfully updated
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Input")
    bool UpdateInputValue(const FString &SourceId, double NewValue, double CurrentTime);

    /**
     * Load input timeseries from external source
     * @param SourceId ID of the input source
     * @param Timeseries Timeseries data to load
     * @return True if timeseries was successfully loaded
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Input")
    bool LoadInputTimeseries(const FString &SourceId, const FFMUInputTimeseries &Timeseries);

    /**
     * Get current input value for a variable
     * @param VariableName Name of the FMU variable
     * @param CurrentTime Current simulation time
     * @param OutValue Output value
     * @return True if value was successfully retrieved
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Input")
    bool GetInputValue(const FString &VariableName, double CurrentTime, double &OutValue) const;

    /**
     * Get all current input values for configured variables
     * @param CurrentTime Current simulation time
     * @param OutValues Map of variable names to their current values
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Input")
    void GetAllInputValues(double CurrentTime, TMap<FString, double> &OutValues) const;

    /**
     * Process inputs for the current simulation time
     * Called by FMUSimulationSubsystem before each simulation step
     * @param CurrentTime Current simulation time
     * @param OutInputValues Map of variable names to their processed values
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Input")
    void ProcessInputs(double CurrentTime, TMap<FString, double> &OutInputValues);

    /**
     * Enable or disable input processing
     * @param bEnabled Whether input processing should be enabled
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Input")
    void SetInputProcessingEnabled(bool bEnabled);

    /**
     * Check if input processing is enabled
     * @return True if input processing is enabled
     */
    UFUNCTION(BlueprintPure, Category = "FMU Input")
    bool IsInputProcessingEnabled() const { return bInputProcessingEnabled; }

    /**
     * Get all configured input variables
     * @return Array of input variable configurations
     */
    UFUNCTION(BlueprintPure, Category = "FMU Input")
    const TArray<FFMUInputVariableConfig> &GetInputVariables() const { return InputConfiguration.inputVariables; }

    /**
     * Find input variable configuration by name
     * @param VariableName Name of the variable to find
     * @param OutVariableConfig Output variable configuration
     * @return True if variable was found
     */
    UFUNCTION(BlueprintPure, Category = "FMU Input")
    bool FindInputVariable(const FString &VariableName, FFMUInputVariableConfig &OutVariableConfig) const;

    /**
     * Get statistics about input processing
     * @param OutStats Map of statistic names to their values
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Input")
    void GetInputProcessingStats(TMap<FString, double> &OutStats) const;

    /**
     * Clear all input buffers and reset processing state
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Input")
    void ClearInputBuffers();

    /**
     * Handle WebSocket input command
     * @param CommandJson JSON string containing the input command
     * @return True if command was successfully processed
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Input")
    bool HandleWebSocketInputCommand(const FString &CommandJson);

    /**
     * Handle WebSocket input command for delegate binding (void return)
     * @param CommandJson JSON string containing the input command
     */
    UFUNCTION()
    void HandleWebSocketInputCommandDelegate(const FString &CommandJson);

    /**
     * Load input timeseries from file
     * @param FilePath Path to the timeseries file
     * @param VariableName Name of the variable to load timeseries for
     * @param SourceId ID of the input source
     * @return True if timeseries was successfully loaded
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Input")
    bool LoadInputTimeseriesFromFile(const FString &FilePath, const FString &VariableName, const FString &SourceId);

    /**
     * Load multiple input variables from a multi-variable CSV file
     * @param FilePath Path to the multi-variable CSV file
     * @param SourceIdPrefix Prefix for auto-generated source IDs (e.g., "csv_file")
     * @param OutLoadedVariables List of variable names that were successfully loaded
     * @return True if at least one variable was successfully loaded
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Input")
    bool LoadMultiVariableTimeseriesFromFile(const FString &FilePath, const FString &SourceIdPrefix, TArray<FString> &OutLoadedVariables);

    /**
     * Get available variable names from a multi-variable CSV file header
     * @param FilePath Path to the CSV file
     * @param OutVariableNames List of variable names found in the header
     * @return True if header was successfully parsed
     */
    UFUNCTION(BlueprintCallable, Category = "FMU Input")
    bool GetAvailableVariablesFromCSV(const FString &FilePath, TArray<FString> &OutVariableNames);

public:
    /** Event fired when an input value changes */
    UPROPERTY(BlueprintAssignable, Category = "FMU Input|Events")
    FOnInputValueChanged OnInputValueChanged;

    /** Event fired when an input source is added */
    UPROPERTY(BlueprintAssignable, Category = "FMU Input|Events")
    FOnInputSourceAdded OnInputSourceAdded;

    /** Event fired when an input source is removed */
    UPROPERTY(BlueprintAssignable, Category = "FMU Input|Events")
    FOnInputSourceRemoved OnInputSourceRemoved;

private:
    /** Current input configuration */
    UPROPERTY()
    FFMUInputConfiguration InputConfiguration;

    /** Whether input processing is enabled */
    UPROPERTY()
    bool bInputProcessingEnabled;

    /** Map of source IDs to their variable names for quick lookup */
    UPROPERTY()
    TMap<FString, FString> SourceIdToVariableMap;

    /** Input processing statistics */
    UPROPERTY()
    TMap<FString, double> ProcessingStats;

    /** Cached input values for performance */
    mutable TMap<FString, double> CachedInputValues;

    /** Time when cached values were last updated */
    mutable double CachedValuesTime;

    /** Cached values validity duration */
    static constexpr double CacheValidityDuration = 0.001; // 1ms

    /** Thread safety for input updates */
    mutable FCriticalSection InputUpdateLock;

    /**
     * Find input source by ID across all variables
     * @param SourceId ID of the input source to find
     * @param OutVariable Output variable that contains the source
     * @param OutSource Output source found
     * @return True if source was found
     */
    bool FindInputSourceById(const FString &SourceId, FFMUInputVariableConfig *&OutVariable, FFMUInputSource *&OutSource);

    /**
     * Find input variable configuration by name (internal use)
     * @param VariableName Name of the variable to find
     * @return Pointer to variable configuration, or nullptr if not found
     */
    const FFMUInputVariableConfig *FindInputVariableInternal(const FString &VariableName) const;

    /**
     * Load input timeseries (internal version that assumes lock is already held)
     * @param SourceId ID of the input source
     * @param Timeseries Timeseries data to load
     * @return True if timeseries was successfully loaded
     */
    bool LoadInputTimeseriesInternal(const FString &SourceId, const FFMUInputTimeseries &Timeseries);

    /**
     * Update source-to-variable mapping
     */
    void UpdateSourceIdMapping();

    /**
     * Validate input configuration
     * @param Configuration Configuration to validate
     * @return True if configuration is valid
     */
    bool ValidateInputConfiguration(const FFMUInputConfiguration &Configuration) const;

    /**
     * Parse WebSocket input command JSON
     * @param CommandJson JSON string to parse
     * @param OutCommand Output parsed command
     * @return True if JSON was successfully parsed
     */
    bool ParseWebSocketInputCommand(const FString &CommandJson, TSharedPtr<FJsonObject> &OutCommand) const;

    /**
     * Update processing statistics
     * @param StatName Name of the statistic to update
     * @param Value Value to set
     */
    void UpdateProcessingStat(const FString &StatName, double Value);

    /**
     * Load timeseries data from CSV file
     * @param FilePath Path to the CSV file
     * @param OutTimeseries Output timeseries data
     * @return True if file was successfully loaded
     */
    bool LoadTimeseriesFromCSV(const FString &FilePath, FFMUInputTimeseries &OutTimeseries) const;

    /**
     * Load timeseries data from JSON file
     * @param FilePath Path to the JSON file
     * @param OutTimeseries Output timeseries data
     * @return True if file was successfully loaded
     */
    bool LoadTimeseriesFromJSON(const FString &FilePath, FFMUInputTimeseries &OutTimeseries) const;

    /**
     * Load timeseries data for a specific variable from multi-variable CSV file
     * @param FilePath Path to the CSV file
     * @param VariableName Name of the variable to extract
     * @param OutTimeseries Output timeseries data
     * @return True if variable was found and loaded successfully
     */
    bool LoadTimeseriesFromMultiVariableCSV(const FString &FilePath, const FString &VariableName, FFMUInputTimeseries &OutTimeseries) const;

    /**
     * Parse CSV file header to get column names and indices
     * @param FilePath Path to the CSV file
     * @param OutColumnNames Map of column names to their indices
     * @param OutTimeColumnIndex Index of the time column (-1 if not found)
     * @return True if header was successfully parsed
     */
    bool ParseCSVHeader(const FString &FilePath, TMap<FString, int32> &OutColumnNames, int32 &OutTimeColumnIndex) const;

    /**
     * Load all timeseries data from multi-variable CSV file
     * @param FilePath Path to the CSV file
     * @param OutTimeseries Map of variable names to their timeseries data
     * @return True if file was successfully loaded
     */
    bool LoadAllTimeseriesFromMultiVariableCSV(const FString &FilePath, TMap<FString, FFMUInputTimeseries> &OutTimeseries) const;

    /**
     * Load timeseries data for all configured input sources
     * Called internally after configuration is set to load CSV/JSON data
     */
    void LoadTimeseriesDataForAllSources();

};