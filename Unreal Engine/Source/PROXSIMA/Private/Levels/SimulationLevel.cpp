#include "SimulationLevel.h"
#include "PROXSIMAGameInstance.h"
#include "TimerManager.h"
#include "WebSocketManager.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"

ASimulationLevel::ASimulationLevel()
{
    // Disable tick since we use timers
    PrimaryActorTick.bCanEverTick = false;
}

void ASimulationLevel::BeginPlay()
{
    Super::BeginPlay();

    // Invece di iniziare subito gli attori della simulazione,
    // prima aspettiamo che l’ambiente del livello sia completamente pronto.
    StartEnvironmentReadinessCheck();
}

void ASimulationLevel::StartEnvironmentReadinessCheck()
{
    bEnvironmentReady = false;
    AccumulatedWaitTime = 0.0f;

    // Poll ogni 0.25s per ridurre overhead e avere feedback fluido
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().SetTimer(
            EnvironmentCheckTimerHandle,
            this,
            &ASimulationLevel::CheckEnvironmentReady,
            0.25f,
            true
        );
    }
}

bool ASimulationLevel::AreStreamingLevelsReady() const
{
    UWorld* World = GetWorld();
    if (!World) return false;

    // Se non dobbiamo attendere streaming livelli, si considerano pronti
    if (!bWaitForStreamingLevels)
    {
        return true;
    }

    // Verifica tutti i livelli streammati: Loaded & Visible
    const TArray<ULevelStreaming*>& StreamingLevels = World->GetStreamingLevels();
    if (StreamingLevels.Num() == 0)
    {
        // Nessun livello streammato presente: consideriamo pronto
        return true;
    }

    for (const ULevelStreaming* LS : StreamingLevels)
    {
        if (!LS) continue;

        // Se il livello è previsto visibile, attendi che sia effettivamente visibile e caricato
        const bool bShouldBeVisible = LS->ShouldBeVisible();
        if (bShouldBeVisible)
        {
            const bool bLoaded = LS->IsLevelLoaded();
            const bool bVisible = LS->IsLevelVisible();
            if (!bLoaded || !bVisible)
            {
                return false;
            }
        }
    }

    return true;
}

void ASimulationLevel::CheckEnvironmentReady()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        // world non valido, riprova al prossimo tick
        return;
    }

    // Condizione 1: tutti i sub-level streammati (se presenti) sono pronti
    const bool bStreamingReady = AreStreamingLevelsReady();

    // Condizione 2
    const bool bManualReady = bEnvironmentReady;

    // Timeout (se definito) per non bloccare indefinitamente
    if (StreamingLevelsTimeoutSeconds > 0.0f)
    {
        AccumulatedWaitTime += 0.25f;
    }

    const bool bTimedOut = (StreamingLevelsTimeoutSeconds > 0.0f) && (AccumulatedWaitTime >= StreamingLevelsTimeoutSeconds);

    if ((bStreamingReady && (bManualReady || !bWaitForStreamingLevels)) || bTimedOut)
    {
        // Ambiente pronto oppure timeout raggiunto: procedi
        World->GetTimerManager().ClearTimer(EnvironmentCheckTimerHandle);
        BeginSimulationInitialization();
    }
    else
    {
        // Eventuali messaggi di debug a schermo se serve
        // UE_LOG(LogTemp, Verbose, TEXT("Environment not ready yet..."));
    }
}

void ASimulationLevel::MarkEnvironmentReady()
{
    bEnvironmentReady = true;
}

void ASimulationLevel::BeginSimulationInitialization()
{
    if (UPROXSIMAGameInstance *GameInst = Cast<UPROXSIMAGameInstance>(GetGameInstance()))
    {
        GameInst->InitializeSimulationActors(GetWorld());
        UE_LOG(LogTemp, Log, TEXT("Environment ready - Simulation actors initialization started"));
        // L’avvio reale della simulazione FMU rimane gestito da UPROXSIMAGameInstance
        // dopo OnMeshProcessingComplete + VerifySubsystemsReady -> StartFMUSimulation()
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("GameInstance cast failed in BeginSimulationInitialization"));
    }
}

void ASimulationLevel::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);

    // Clear timers
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(FMUUpdateTimerHandle);
        GetWorld()->GetTimerManager().ClearTimer(EnvironmentCheckTimerHandle);
    }

    // Pause simulation in game instance
    if (UPROXSIMAGameInstance* GameInst = Cast<UPROXSIMAGameInstance>(GetGameInstance()))
    {
        GameInst->PauseSimulation();

        // Notify WebSocket clients that simulation has stopped
        if (UWebSocketManager* WebSocketManager = GameInst->GetSubsystem<UWebSocketManager>())
        {
            WebSocketManager->BroadcastSimulationStateChange(false);
            UE_LOG(LogTemp, Log, TEXT("Simulation stopped - WebSocket notification sent"));
        }
    }
}