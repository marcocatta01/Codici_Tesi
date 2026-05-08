// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/SpectatorPawn.h"
#include "PROXSIMAGameMode.generated.h"

/**
 * This is the game mode class for the PROXSIMA simulation.
 * It is used so that we can set the default pawn class to be a spectator
 * and not appear in the renderings from the cameras.
 */
UCLASS()
class PROXSIMA_API APROXSIMAGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APROXSIMAGameMode();
};
