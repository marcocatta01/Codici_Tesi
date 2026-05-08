#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "XmlFile.h"
#include "FMUModelDescription.generated.h"

USTRUCT(BlueprintType)
struct FFMUVariable
{
    GENERATED_BODY()

    UPROPERTY()
    FString name;

    UPROPERTY()
    FString description;

    UPROPERTY()
    FString causality;

    UPROPERTY()
    FString variability;

    UPROPERTY()
    FString initial;

    UPROPERTY()
    uint32 valueReference = MAX_uint32;

    UPROPERTY()
    FString type;

    // Optional bounds
    UPROPERTY()
    TOptional<double> start;

    UPROPERTY()
    TOptional<double> min;

    UPROPERTY()
    TOptional<double> max;

    UPROPERTY()
    TOptional<FString> unit;

    bool IsValid() const
    {
        return !name.IsEmpty() && valueReference != MAX_uint32;
    }
};

UCLASS(BlueprintType)
class PROXSIMA_API UFMUModelDescription : public UObject
{
    GENERATED_BODY()

public:
    UFMUModelDescription();

    // Initialize from an already extracted FMU directory
    UFUNCTION(BlueprintCallable, Category = "FMU")
    bool InitializeFromExtractedFMU(const FString &extractedFMUPath);

    // Getters
    UFUNCTION(BlueprintPure, Category = "FMU")
    const FString &GetGUID() const { return ModelGUID; }

    UFUNCTION(BlueprintPure, Category = "FMU")
    const FString &GetModelName() const { return ModelName; }

    UFUNCTION(BlueprintPure, Category = "FMU")
    const FString &GetModelIdentifier() const { return ModelIdentifier; }

    UFUNCTION(BlueprintPure, Category = "FMU")
    const TMap<FString, FFMUVariable> &GetVariables() const { return Variables; }

    // Validation
    UFUNCTION(BlueprintCallable, Category = "FMU")
    bool ValidateValue(const FString &varName, double value) const;

    UFUNCTION(Category = "FMU")
    bool GetValueReference(const FString &varName, uint32 &outValueRef) const;

    UFUNCTION(BlueprintPure, Category = "FMU")
    double GetDefaultStepSize() const { return DefaultStepSize; }

private:
    bool ParseModelDescriptionXML(const FString &xmlPath);
    bool ParseScalarVariable(const FXmlNode *varNode, FFMUVariable &outVar);

    UPROPERTY()
    FString ModelName;

    UPROPERTY()
    FString ModelIdentifier;

    UPROPERTY()
    FString ModelGUID;

    UPROPERTY()
    double DefaultStepSize;

    UPROPERTY()
    TMap<FString, FFMUVariable> Variables;

    // Critical section for thread-safe access to Variables
    mutable FCriticalSection VariablesCriticalSection;
};