#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

// defines needed for rapidjson includes to work
#define __GLIBC_USE_ISOC23 1
#define __GLIBC_USE_C23_STRTOL 1
// #define __GLIBC_USE_IEC_60559_BFP_EXT_C23 1

#include "rapidjson/document.h"
#include "rapidjson/schema.h"

#include "JsonSchemaValidator.generated.h"

UCLASS()
class PROXSIMA_API UJsonSchemaValidator : public UObject
{
    GENERATED_BODY()

public:
    // Validates a JSON string against a schema string
    UFUNCTION(BlueprintCallable, Category = "JSON")
    static bool ValidateJsonAgainstSchema(const FString &JsonString, const FString &SchemaString, FString &OutError);

    // Validates a JSON object against a schema object
    static bool ValidateJsonObject(const TSharedPtr<FJsonObject> &JsonObject, const TSharedPtr<FJsonObject> &SchemaObject, FString &OutError);

private:
    // Normalize enum case for JSON object
    static void NormalizeEnumCase(TSharedPtr<FJsonObject> &JsonObject);

    // Convert Unreal JSON object to RapidJSON document
    static void ConvertToRapidJsonDocument(const TSharedPtr<FJsonObject> &JsonObject, rapidjson::Document &OutDocument);

    // Get a human-readable error message based on the schema keyword
    static FString GetDetailedErrorMessage(const rapidjson::SchemaValidator &Validator, const rapidjson::Value &InvalidValue);
};
