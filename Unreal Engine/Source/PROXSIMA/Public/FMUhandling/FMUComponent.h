#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FMUIntegration.h"
#include "FMUConfiguration.h"
#include "FMUComponent.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class PROXSIMA_API UFMUComponent : public UActorComponent
{
    GENERATED_BODY()

private:
    TUniquePtr<FFMUIntegration> FMUIntegration = nullptr;
    float accumulatedTime;
    double fixedTimeStep;

    UPROPERTY()
    TMap<FString, USceneComponent *> MappedComponents;

    UPROPERTY()
    FFMUConfiguration Configuration;

    bool bIsSimulating;
    void ProcessSubSteps(float DeltaTime);
    void UpdateComponentProperties();

public:
    UFMUComponent();

    UFUNCTION(BlueprintCallable, Category = "FMU")
    void InitializeFMU(const FString &configPath);

    UFUNCTION(BlueprintCallable, Category = "FMU")
    void MapComponentToVariable(const FString &variableName, USceneComponent *component);

    UFUNCTION(BlueprintCallable, Category = "FMU")
    void StartSimulation();

    UFUNCTION(BlueprintCallable, Category = "FMU")
    void PauseSimulation();

    UFUNCTION(BlueprintCallable, Category = "FMU")
    bool IsSimulating() const { return bIsSimulating; }

    UFUNCTION(BlueprintPure, Category = "FMU")
    double GetVariableValue(const FString &variableName) const;

    UFUNCTION(BlueprintCallable, Category = "FMU")
    bool SetVariableValue(const FString &variableName, double value);

    UFUNCTION(BlueprintCallable, Category = "FMU")
    void GetAllConfiguredValues(TMap<FString, double> &outValues) const;

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};