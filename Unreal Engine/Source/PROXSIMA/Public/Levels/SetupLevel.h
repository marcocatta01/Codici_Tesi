// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/LevelScriptActor.h"
#include "Components/Button.h"
#include "SetupLevel.generated.h"

UCLASS()
class PROXSIMA_API ASetupLevel : public ALevelScriptActor
{
    GENERATED_BODY()

private:
    UPROPERTY(EditAnywhere, Category="UI")
    UButton* StartButton;

    UFUNCTION()
    void OnStartSimulation();

protected:
    virtual void BeginPlay() override;
};