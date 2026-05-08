// Fill out your copyright notice in the Description page of Project Settings.


#include "SetupLevel.h"
#include "Kismet/GameplayStatics.h"
#include "PROXSIMAGameInstance.h"

void ASetupLevel::BeginPlay() {
    Super::BeginPlay();

    UE_LOG(LogTemp, Log, TEXT("Starting FMU setup..."));

    UPROXSIMAGameInstance *GameInst = Cast<UPROXSIMAGameInstance>(GetGameInstance());
    if (GameInst && !GameInst->ConfigPathFromUI.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("Loading FMU config from: %s"), *GameInst->ConfigPathFromUI);

        const bool bSuccess = GameInst->InitializeFMU(GameInst->ConfigPathFromUI);
        if (!bSuccess)
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to initialize FMU in game instance"));
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("FMU initialized successfully"));
        }
        
        GameInst->bIsInitialized = bSuccess;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("No config file path is currently set in game instance"));
        return;
    }

    // Notify UI that initialization is done
    GameInst->OnInitializationComplete.Broadcast();

    // Setup UI
    if (StartButton) {
        StartButton->OnClicked.AddDynamic(this, &ASetupLevel::OnStartSimulation);
    }
}

void ASetupLevel::OnStartSimulation() {
    UGameplayStatics::OpenLevel(this, TEXT("SimulationLevel"));
}
