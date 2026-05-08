#include "Subsystems/FMUSimulationSubsystem.h"
#include "Subsystems/FMUInputHandlingSubsystem.h"
#include "Subsystems/RoverGroundingSubsystem.h" // Grounding post-process
#include "FMUIntegration.h"
#include "Engine/World.h"
#include "Kismet/KismetMathLibrary.h"
#include "PROXSIMAGameInstance.h"

void UFMUSimulationSubsystem::Initialize(FSubsystemCollectionBase &Collection)
{
    Super::Initialize(Collection);

    bIsSimulating = false;
    AccumulatedTime = 0.0f;
    FixedTimeStep = 0.001f; // Default 1ms timestep
    SimulationTime = 0.0;

    // Create FMU integration instance
    UPROXSIMAGameInstance *ProxsimaGameInstance = Cast<UPROXSIMAGameInstance>(GetGameInstance());
    if (ProxsimaGameInstance)
    {
        FMUIntegration = MakeUnique<FFMUIntegration>(ProxsimaGameInstance);
        UE_LOG(LogTemp, Log, TEXT("FMUSimulationSubsystem initialized"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to cast GameInstance to UPROXSIMAGameInstance"));
    }
}

void UFMUSimulationSubsystem::Deinitialize()
{
    StopSimulation();
    CleanupFMU();

    Super::Deinitialize();
}

void UFMUSimulationSubsystem::Tick(float DeltaTime)
{
    if (!bIsSimulating || !FMUIntegration)
    {
        static float LogTimer = 0.0f;
        LogTimer += DeltaTime;
        if (LogTimer > 5.0f)
        {
            UE_LOG(LogTemp, Warning, TEXT("FMU Tick not running: bIsSimulating=%s, FMUIntegration=%s"),
                bIsSimulating ? TEXT("true") : TEXT("false"),
                FMUIntegration.IsValid() ? TEXT("valid") : TEXT("null"));
            LogTimer = 0.0f;
        }
        return;
    }

    AccumulatedTime += DeltaTime;
    const float Step = FMath::Max(1e-6f, FixedTimeStep);

    while (AccumulatedTime + KINDA_SMALL_NUMBER >= Step)
    {
        AccumulatedTime -= Step;

        // Avanza l'FMU di uno step (gestisce anche gli input e SimulationTime)
        const bool bStepOk = PerformSimulationStep();
        if (!bStepOk)
        {
            bIsSimulating = false;
            OnSimulationStateChanged.Broadcast(false);
            UE_LOG(LogTemp, Error, TEXT("FMU step failed; stopping simulation"));
            break;
        }

        // Applica lo stato (frame) agli attori
        UpdateObservers();

        // Notifica i sensori subito dopo lo stato aggiornato
        OnFmuStep.Broadcast(SimulationTime, Step);
    }
}

bool UFMUSimulationSubsystem::IsTickable() const
{
    return bIsSimulating && FMUIntegration.IsValid();
}

TStatId UFMUSimulationSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UFMUSimulationSubsystem, STATGROUP_Tickables);
}

bool UFMUSimulationSubsystem::InitializeFMU(const FFMUConfiguration &InConfiguration)
{
    if (!FMUIntegration)
    {
        UE_LOG(LogTemp, Error, TEXT("FMU integration not available"));
        return false;
    }

    Configuration = InConfiguration;

    if (Configuration.timeStep > 0.0)
    {
        FixedTimeStep = Configuration.timeStep;
    }

    if (!FMUIntegration->Initialize(Configuration))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to initialize FMU with configuration"));
        return false;
    }

    if (Configuration.inputConfiguration.inputVariables.Num() > 0)
    {
        if (!InitializeInputHandling(Configuration.inputConfiguration))
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to initialize input handling, continuing without inputs"));
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("Input handling initialized with %d input variables"),
                   Configuration.inputConfiguration.inputVariables.Num());
        }
    }

    if (URoverGroundingSubsystem* Grounding = GetGameInstance()->GetSubsystem<URoverGroundingSubsystem>())
    {
        Grounding->ConfigureFromConfiguration(Configuration);
        UE_LOG(LogTemp, Log, TEXT("RoverGroundingSubsystem configured"));
    }

    UE_LOG(LogTemp, Log, TEXT("FMU initialized successfully with timestep: %f"), FixedTimeStep);
    return true;
}

bool UFMUSimulationSubsystem::LoadFMU(const FString &FMUPath)
{
    if (!FMUIntegration)
    {
        UE_LOG(LogTemp, Error, TEXT("FMU integration not available"));
        return false;
    }

    if (!FMUIntegration->LoadFMU(FMUPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load FMU from: %s"), *FMUPath);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("FMU loaded successfully from: %s"), *FMUPath);
    return true;
}

void UFMUSimulationSubsystem::StartSimulation()
{
    if (!FMUIntegration || !IsFMULoaded())
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot start simulation: FMU not loaded"));
        return;
    }

    bIsSimulating = true;
    AccumulatedTime = 0.0f;
    SimulationTime = 0.0;

    OnSimulationStateChanged.Broadcast(true);
    UE_LOG(LogTemp, Log, TEXT("FMU simulation started"));
}

void UFMUSimulationSubsystem::PauseSimulation()
{
    bIsSimulating = false;
    OnSimulationStateChanged.Broadcast(false);
    UE_LOG(LogTemp, Log, TEXT("FMU simulation paused"));
}

void UFMUSimulationSubsystem::StopSimulation()
{
    bIsSimulating = false;
    AccumulatedTime = 0.0f;
    SimulationTime = 0.0;

    OnSimulationStateChanged.Broadcast(false);
    UE_LOG(LogTemp, Log, TEXT("FMU simulation stopped"));
}

void UFMUSimulationSubsystem::ApplyInitialConditions()
{
    if (!FMUIntegration)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot apply initial conditions: FMU not loaded"));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("Applying FMU initial conditions to frame actors..."));

    UpdateObservers();

    if (FMUIntegration->DoStep(0.001))
    {
        SimulationTime += 0.001;
        UpdateObservers();
        UE_LOG(LogTemp, Log, TEXT("Applied 1ms integration step for initial conditions"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Initial condition integration step failed, but continuing"));
    }

    UE_LOG(LogTemp, Log, TEXT("Initial conditions applied successfully - actors should be positioned correctly"));
}

bool UFMUSimulationSubsystem::RegisterVariableObserver(const FString &VariableName, AActor *Actor)
{
    if (!Actor)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot register null actor for variable: %s"), *VariableName);
        return false;
    }

    if (VariableObservers.Contains(VariableName))
    {
        UE_LOG(LogTemp, Warning, TEXT("Variable %s already has an observer, replacing"), *VariableName);
    }

    VariableObservers.Add(VariableName, Actor);
    UE_LOG(LogTemp, Verbose, TEXT("Registered observer for variable: %s"), *VariableName);
    return true;
}

void UFMUSimulationSubsystem::UnregisterVariableObserver(AActor *Actor)
{
    if (!Actor)
    {
        return;
    }

    TArray<FString> KeysToRemove;
    for (const auto &Pair : VariableObservers)
    {
        if (Pair.Value == Actor)
        {
            KeysToRemove.Add(Pair.Key);
        }
    }

    for (const FString &Key : KeysToRemove)
    {
        VariableObservers.Remove(Key);
        UE_LOG(LogTemp, Verbose, TEXT("Unregistered observer for variable: %s"), *Key);
    }
}

bool UFMUSimulationSubsystem::GetVariableValue(const FString &VariableName, double &OutValue) const
{
    if (!FMUIntegration)
    {
        return false;
    }
    return FMUIntegration->GetVariable(VariableName, OutValue);
}

bool UFMUSimulationSubsystem::SetVariableValue(const FString &VariableName, double Value)
{
    if (!FMUIntegration)
    {
        return false;
    }

    bool bSuccess = FMUIntegration->SetVariable(VariableName, Value);
    if (bSuccess)
    {
        OnVariableChanged.Broadcast(VariableName, Value);
    }
    return bSuccess;
}

AActor *UFMUSimulationSubsystem::GetFrameActor(const FString &FrameName) const
{
    return VariableObservers.FindRef(FrameName);
}

bool UFMUSimulationSubsystem::IsFMULoaded() const
{
    return FMUIntegration.IsValid();
}

double UFMUSimulationSubsystem::GetSimulationTime() const
{
    return SimulationTime;
}

void UFMUSimulationSubsystem::SetFixedTimeStep(float TimeStep)
{
    if (TimeStep > 0.0f)
    {
        FixedTimeStep = TimeStep;
        UE_LOG(LogTemp, Log, TEXT("Fixed timestep set to: %f"), FixedTimeStep);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Invalid timestep value: %f"), TimeStep);
    }
}

bool UFMUSimulationSubsystem::PerformSimulationStep()
{
    if (!FMUIntegration)
    {
        return false;
    }

    // Process inputs before simulation step
    ProcessInputsForSimulationStep();

    if (FMUIntegration->DoStep(FixedTimeStep))
    {
        SimulationTime = FMUIntegration->GetCurrentTime();
        return true;
    }
    return false;
}

void UFMUSimulationSubsystem::UpdateObservers()
{
    if (!FMUIntegration)
    {
        return;
    }

    UE_LOG(LogTemp, Verbose, TEXT("UpdateObservers called with %d registered observers"), VariableObservers.Num());

    TMap<FString, FTransform> PendingUpdates;

    for (const auto &Pair : VariableObservers)
    {
        const FString &FrameName = Pair.Key;

        FTransform FrameTransform;
        if (GetFrameTransform(FrameName, FrameTransform))
        {
            PendingUpdates.Add(FrameName, FrameTransform);
        }
    }

    for (const auto &Pair : PendingUpdates)
    {
        AActor *Actor = VariableObservers.FindRef(Pair.Key);
        if (Actor && IsValid(Actor))
        {
            Actor->SetActorTransform(Pair.Value, false, nullptr, ETeleportType::None);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Invalid or null actor for frame: %s"), *Pair.Key);
        }
    }
}

FVector UFMUSimulationSubsystem::ConvertCoordinates(const FVector &RightHandedVector) const
{
    return FVector(RightHandedVector.X, -RightHandedVector.Y, RightHandedVector.Z);
}

FQuat UFMUSimulationSubsystem::ConvertRotation(const FMatrix &RotationMatrix) const
{
    FMatrix ConvertedMatrix = RotationMatrix;
    ConvertedMatrix.M[0][1] *= -1.0f;
    ConvertedMatrix.M[1][0] *= -1.0f;
    ConvertedMatrix.M[1][2] *= -1.0f;
    ConvertedMatrix.M[2][1] *= -1.0f;

    return FQuat(ConvertedMatrix);
}

bool UFMUSimulationSubsystem::GetFrameTransform(const FString& FrameName, FTransform& OutTransform) const
{
    if (!FMUIntegration)
    {
        return false;
    }

    FVector Location = FVector::ZeroVector;
    for (int i = 0; i < 3; i++)
    {
        FString VarName = FString::Printf(TEXT("%s.r_0[%d]"), *FrameName, i + 1);
        double Value;
        if (!FMUIntegration->GetVariable(VarName, Value))
        {
            UE_LOG(LogTemp, Verbose, TEXT("Failed to get position variable: %s"), *VarName);
            continue;
        }
        Location[i] = static_cast<float>(Value * MetersToCentimeters);
    }

    Location = ConvertCoordinates(Location);
    OutTransform.SetLocation(Location);

    double q[4] = { 0, 0, 0, 1 };
    for (int k = 0; k < 4; ++k) {
        FString VarName = FString::Printf(TEXT("%s.R.q[%d]"), *FrameName, k + 1);
        double Value;
        if (!FMUIntegration->GetVariable(VarName, Value)) {
            UE_LOG(LogTemp, Warning, TEXT("Failed to get quaternion component: %s"), *VarName);
            OutTransform.SetRotation(FQuat::Identity);
            OutTransform.SetScale3D(FVector::OneVector);
            return true;
        }
        q[k] = Value;
    }

    const double norm = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (norm <= 0.0) {
        UE_LOG(LogTemp, Warning, TEXT("Quaternion norm is zero for frame %s"), *FrameName);
        OutTransform.SetRotation(FQuat::Identity);
        OutTransform.SetScale3D(FVector::OneVector);
        return true;
    }

    const FQuat FMUQuat(
        static_cast<float>(q[0] / norm),
        static_cast<float>(q[1] / norm),
        static_cast<float>(q[2] / norm),
        static_cast<float>(q[3] / norm)
    );

    const FMatrix RHMatrix = FQuatRotationMatrix(FMUQuat);
    const FQuat Rotation = ConvertRotation(RHMatrix);
    OutTransform.SetRotation(Rotation);

    OutTransform.SetScale3D(FVector::OneVector);
    return true;
}

void UFMUSimulationSubsystem::CleanupFMU()
{
    bIsSimulating = false;
    AccumulatedTime = 0.0f;
    SimulationTime = 0.0;
    VariableObservers.Empty();

    if (FMUIntegration)
    {
        FMUIntegration->CleanUp();
    }

    UE_LOG(LogTemp, Log, TEXT("FMU simulation cleaned up"));
}

bool UFMUSimulationSubsystem::InitializeInputHandling(const FFMUInputConfiguration &InputConfiguration)
{
    UFMUInputHandlingSubsystem *InputSubsystem = GetGameInstance()->GetSubsystem<UFMUInputHandlingSubsystem>();
    if (!InputSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("FMUInputHandlingSubsystem not available"));
        return false;
    }

    bool bSuccess = InputSubsystem->InitializeInputHandling(InputConfiguration);
    if (bSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("FMU input handling initialized successfully"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to initialize FMU input handling"));
    }

    return bSuccess;
}

void UFMUSimulationSubsystem::SetInputConfiguration(const FFMUInputConfiguration &InputConfiguration)
{
    UFMUInputHandlingSubsystem *InputSubsystem = GetGameInstance()->GetSubsystem<UFMUInputHandlingSubsystem>();
    if (InputSubsystem)
    {
        InputSubsystem->SetInputConfiguration(InputConfiguration);
        UE_LOG(LogTemp, Log, TEXT("FMU input configuration updated"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("FMUInputHandlingSubsystem not available for configuration update"));
    }
}

const FFMUInputConfiguration &UFMUSimulationSubsystem::GetInputConfiguration() const
{
    UFMUInputHandlingSubsystem *InputSubsystem = GetGameInstance()->GetSubsystem<UFMUInputHandlingSubsystem>();
    if (InputSubsystem)
    {
        return InputSubsystem->GetInputConfiguration();
    }
    else
    {
        static FFMUInputConfiguration EmptyConfig;
        UE_LOG(LogTemp, Warning, TEXT("FMUInputHandlingSubsystem not available for configuration retrieval"));
        return EmptyConfig;
    }
}

void UFMUSimulationSubsystem::SetInputProcessingEnabled(bool bEnabled)
{
    UFMUInputHandlingSubsystem *InputSubsystem = GetGameInstance()->GetSubsystem<UFMUInputHandlingSubsystem>();
    if (InputSubsystem)
    {
        InputSubsystem->SetInputProcessingEnabled(bEnabled);
        UE_LOG(LogTemp, Log, TEXT("FMU input processing %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("FMUInputHandlingSubsystem not available for enabling/disabling"));
    }
}

bool UFMUSimulationSubsystem::IsInputProcessingEnabled() const
{
    UFMUInputHandlingSubsystem *InputSubsystem = GetGameInstance()->GetSubsystem<UFMUInputHandlingSubsystem>();
    if (InputSubsystem)
    {
        return InputSubsystem->IsInputProcessingEnabled();
    }
    else
    {
        return false;
    }
}

void UFMUSimulationSubsystem::ProcessInputsForSimulationStep()
{
    UFMUInputHandlingSubsystem *InputSubsystem = GetGameInstance()->GetSubsystem<UFMUInputHandlingSubsystem>();
    if (!InputSubsystem)
    {
        return;
    }

    if (!InputSubsystem->IsInputProcessingEnabled())
    {
        return;
    }

    TMap<FString, double> InputValues;
    InputSubsystem->ProcessInputs(SimulationTime, InputValues);

    for (const auto &InputPair : InputValues)
    {
        const FString &VariableName = InputPair.Key;
        double Value = InputPair.Value;

        if (FMUIntegration && FMUIntegration->SetVariable(VariableName, Value))
        {
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to set FMU input '%s' to %f"), *VariableName, Value);
        }
    }

    static double LastStatsLogTime = 0.0;
    if (SimulationTime - LastStatsLogTime > 5.0)
    {
        TMap<FString, double> Stats;
        InputSubsystem->GetInputProcessingStats(Stats);

        double TotalProcessed = Stats.FindRef(TEXT("TotalInputsProcessed"));
        double AverageTime = Stats.FindRef(TEXT("AverageProcessingTime"));
        double ActiveSources = Stats.FindRef(TEXT("ActiveInputSources"));

        UE_LOG(LogTemp, Log, TEXT("Input Stats: Processed=%d, AvgTime=%.3fms, ActiveSources=%d"),
               FMath::RoundToInt(TotalProcessed), AverageTime * 1000.0, FMath::RoundToInt(ActiveSources));

        LastStatsLogTime = SimulationTime;
    }
}