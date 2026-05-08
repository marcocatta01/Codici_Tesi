// Fill out your copyright notice in the Description page of Project Settings.

#include "PROXSIMAGameMode.h"

APROXSIMAGameMode::APROXSIMAGameMode()
{
    // Set default pawn class to be a SpectatorPawn (invisible in the renderings)
    DefaultPawnClass = ASpectatorPawn::StaticClass();
}