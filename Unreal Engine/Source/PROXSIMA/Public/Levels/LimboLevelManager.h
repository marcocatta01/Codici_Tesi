#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LimboLevelManager.generated.h"

UCLASS()
class PROXSIMA_API ALimboLevelManager : public AActor
{
    GENERATED_BODY()

public:
    virtual void BeginPlay() override;
};