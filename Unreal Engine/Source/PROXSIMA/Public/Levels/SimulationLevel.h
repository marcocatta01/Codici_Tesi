// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/StaticMesh.h"
#include "SimulationLevel.generated.h"

UCLASS()
class PROXSIMA_API ASimulationLevel : public ALevelScriptActor
{
    GENERATED_BODY()

public:
    ASimulationLevel();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
    // Blueprint callable: consente di segnalare manualmente che l’ambiente è pronto
    UFUNCTION(BlueprintCallable, Category = "Simulation Level")
    void MarkEnvironmentReady();

    // Se true, aspetta il caricamento dei sub-level streammati prima di iniziare la simulazione
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Level")
    bool bWaitForStreamingLevels = true;

    // Timeout massimo in secondi per attendere i livelli (0 = nessun timeout)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Level")
    float StreamingLevelsTimeoutSeconds = 30.0f;

private:
    /** Timer handle per la verifica readiness ambiente */
    FTimerHandle EnvironmentCheckTimerHandle;

    /** Timer handle per l’FMU (legacy clear all’EndPlay) */
    FTimerHandle FMUUpdateTimerHandle;

    /** Flag interno: l’ambiente è stato dichiarato pronto */
    bool bEnvironmentReady = false;

    /** Tempo accumulato di attesa per streaming */
    float AccumulatedWaitTime = 0.0f;

    /** Avvia polling per readiness ambiente */
    void StartEnvironmentReadinessCheck();

    /** Funzione periodica che verifica che l’ambiente UE sia pronto */
    void CheckEnvironmentReady();

    /** True se tutti gli ULevelStreaming sono Loaded & Visible (se presenti) */
    bool AreStreamingLevelsReady() const;

    /** Una volta pronto l’ambiente, parte l’inizializzazione degli attori di simulazione */
    void BeginSimulationInitialization();
};
