#include "FMUComponent.h"
#include "Misc/Paths.h"
#include "PROXSIMAGameInstance.h"

UFMUComponent::UFMUComponent()
    : accumulatedTime(0.0f), fixedTimeStep(0.001), bIsSimulating(false)
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    FMUIntegration = nullptr;
}

void UFMUComponent::BeginPlay()
{
    Super::BeginPlay();

    // Create FMUIntegration with GameInstance
    UWorld *World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("FMUComponent BeginPlay - Invalid World"));
        return;
    }

    if (UPROXSIMAGameInstance *ProxGameInst = Cast<UPROXSIMAGameInstance>(World->GetGameInstance()))
    {
        // Create and store FMUIntegration
        FMUIntegration = MakeUnique<FFMUIntegration>(ProxGameInst);
        if (FMUIntegration)
        {
            UE_LOG(LogTemp, Warning, TEXT("FMUComponent BeginPlay - Created FMUIntegration"));
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("FMUComponent BeginPlay - Invalid GameInstance type"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("FMUComponent BeginPlay - Failed to get GameInstance"));
    }
}

void UFMUComponent::InitializeFMU(const FString &configPath)
{
    // Load configuration file
    if (!UFMUConfigurationLoader::LoadConfiguration(configPath, Configuration))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load FMU configuration from: %s"), *configPath);
        return;
    }

    // Get full path for FMU
    FString fmuPath = FPaths::Combine(
        FPaths::ProjectContentDir(),
        TEXT("FMUs"),
        Configuration.fmuPath);

    if (!FPaths::FileExists(fmuPath))
    {
        UE_LOG(LogTemp, Error, TEXT("FMU file not found at: %s"), *fmuPath);
        return;
    }

    // Load and initialize FMU
    if (!FMUIntegration)
    {
        UE_LOG(LogTemp, Error, TEXT("FMUIntegration not initialized"));
        return;
    }

    if (!FMUIntegration->LoadFMU(fmuPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load FMU"));
        return;
    }

    if (!FMUIntegration->Initialize(Configuration))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to initialize FMU"));
        return;
    }

    // Set timestep from configuration if specified
    if (Configuration.timeStep > 0.0)
    {
        fixedTimeStep = Configuration.timeStep;
    }

    // Clear any existing component mappings
    MappedComponents.Empty();

    UE_LOG(LogTemp, Warning, TEXT("FMU initialized successfully"));
}

void UFMUComponent::MapComponentToVariable(const FString &variableName, USceneComponent* component)
{
    if (!component)
    {
        UE_LOG(LogTemp, Error, TEXT("Attempted to map null component to variable: %s"), *variableName);
        return;
    }

    MappedComponents.Add(variableName, component);
    UE_LOG(LogTemp, Warning, TEXT("Mapped component to variable: %s"), *variableName);
}

void UFMUComponent::StartSimulation()
{
    if (!FMUIntegration)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot start simulation: FMU not initialized"));
        return;
    }

    if (bIsSimulating)
    {
        UE_LOG(LogTemp, Warning, TEXT("Simulation is already running"));
        return;
    }

    // Get GameInstance to ensure synchronized simulation state
    if (UPROXSIMAGameInstance *GameInst = Cast<UPROXSIMAGameInstance>(GetWorld()->GetGameInstance()))
    {
        GameInst->StartSimulation();
        bIsSimulating = true;
        UE_LOG(LogTemp, Log, TEXT("FMU Simulation started"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to get GameInstance"));
    }
}

void UFMUComponent::PauseSimulation()
{
    bIsSimulating = false;
}

double UFMUComponent::GetVariableValue(const FString &variableName) const
{
    if (!FMUIntegration)
    {
        UE_LOG(LogTemp, Warning, TEXT("FMUIntegration not initialized"));
        return 0.0;
    }

    double value;
    if (FMUIntegration->GetVariable(variableName, value))
    {
        return value;
    }
    return 0.0;
}

bool UFMUComponent::SetVariableValue(const FString &variableName, double value)
{
    if (!FMUIntegration)
    {
        UE_LOG(LogTemp, Warning, TEXT("FMUIntegration not initialized"));
        return false;
    }
    return FMUIntegration->SetVariable(variableName, value);
}

void UFMUComponent::GetAllConfiguredValues(TMap<FString, double>& outValues) const
{
    if (!FMUIntegration)
    {
        UE_LOG(LogTemp, Warning, TEXT("FMUIntegration not initialized"));
        return;
    }
    FMUIntegration->GetAllConfiguredValues(outValues);
}

// DEPRECATED: Component updates are now handled by PROXSIMAGameInstance through the observer pattern.
// See PROXSIMAGameInstance::RegisterVariableObserver and UpdateObservers for the current implementation.
void UFMUComponent::ProcessSubSteps(float DeltaTime)
{
    // Disabled - FMU updates are now handled by GameInstance timer
    // See PROXSIMAGameInstance::SetupFMUUpdateTimer and UpdateFMU
}

// DEPRECATED: This function is no longer used as component updates are handled by PROXSIMAGameInstance.
// Components should be registered using PROXSIMAGameInstance::RegisterVariableObserver instead of
// UFMUComponent::MapComponentToVariable.
void UFMUComponent::UpdateComponentProperties()
{
    if (!FMUIntegration)
    {
        UE_LOG(LogTemp, Warning, TEXT("UpdateComponentProperties: FMUIntegration is null"));
        return;
    }

    if (!bIsSimulating)
    {
        UE_LOG(LogTemp, Verbose, TEXT("UpdateComponentProperties: Not currently simulating"));
        return;
    }

    if (MappedComponents.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("UpdateComponentProperties: No components mapped for updates"));
        return;
    }

    // Get all current values from FMU
    TMap<FString, double> currentValues;
    FMUIntegration->GetAllConfiguredValues(currentValues);

    int32 updatedComponents = 0;
    // Update component properties based on configuration mappings
    for (const auto& var : Configuration.variables)
    {
        if (!var.componentProperty.IsEmpty())
        {
            USceneComponent* component = MappedComponents.FindRef(var.name);
            if (!component)
            {
                UE_LOG(LogTemp, Verbose, TEXT("No component mapped for variable: %s"), *var.name);
                continue;
            }

            const double *value = currentValues.Find(var.name);
            if (!value)
            {
                UE_LOG(LogTemp, Warning, TEXT("No value found for mapped variable: %s"), *var.name);
                continue;
            }

            FVector loc = component->GetRelativeLocation();
            bool locationUpdated = true;

            // Apply value based on component property mapping
            if (var.componentProperty.Equals(TEXT("X"), ESearchCase::IgnoreCase))
            {
                loc.X = *value;
            }
            else if (var.componentProperty.Equals(TEXT("Y"), ESearchCase::IgnoreCase))
            {
                loc.Y = *value;
            }
            else if (var.componentProperty.Equals(TEXT("h"), ESearchCase::IgnoreCase))
            {
                loc.Z = *value;
                UE_LOG(LogTemp, Verbose, TEXT("Updated height for %s to: %f"), *var.name, *value);
            }
            else
            {
                locationUpdated = false;
                UE_LOG(LogTemp, Warning, TEXT("Unknown component property mapping: %s"), *var.componentProperty);
            }

            if (locationUpdated)
            {
                component->SetRelativeLocation(loc);
                updatedComponents++;
            }
        }
    }

    if (updatedComponents > 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("UpdateComponentProperties: Successfully updated %d components"), updatedComponents);
    }
}

void UFMUComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Disabled - Component updates are handled by GameInstance timer
    // if (bIsSimulating)
    // {
    //     ProcessSubSteps(DeltaTime);
    // }
}

void UFMUComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);
    // Reset will call destructor which will handle cleanup
    FMUIntegration.Reset();
}
