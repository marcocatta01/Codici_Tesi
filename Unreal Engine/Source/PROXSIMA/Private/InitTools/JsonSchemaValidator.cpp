#include "JsonSchemaValidator.h"
#include "JsonObjectConverter.h"
#include "Dom/JsonValue.h"

bool UJsonSchemaValidator::ValidateJsonAgainstSchema(const FString &JsonString, const FString &SchemaString, FString &OutError)
{
    // Parse JSON string
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(JsonReader, JsonObject))
    {
        OutError = TEXT("Failed to parse JSON string");
        return false;
    }

    // Parse Schema string
    TSharedPtr<FJsonObject> SchemaObject;
    TSharedRef<TJsonReader<>> SchemaReader = TJsonReaderFactory<>::Create(SchemaString);
    if (!FJsonSerializer::Deserialize(SchemaReader, SchemaObject))
    {
        OutError = TEXT("Failed to parse Schema string");
        return false;
    }

    // Normalize case for enum values before validation
    NormalizeEnumCase(JsonObject);

    return ValidateJsonObject(JsonObject, SchemaObject, OutError);
}

void UJsonSchemaValidator::NormalizeEnumCase(TSharedPtr<FJsonObject> &JsonObject)
{
    for (auto &Pair : JsonObject->Values)
    {
        if (Pair.Value->Type == EJson::String)
        {
            FString Value = Pair.Value->AsString();
            // Convert to lowercase for comparison
            FString LowerValue = Value.ToLower();

            // Check if this is a shape enum value and normalize it
            if (LowerValue == TEXT("sphere") ||
                LowerValue == TEXT("cube") ||
                LowerValue == TEXT("cylinder") ||
                LowerValue == TEXT("cone") ||
                LowerValue == TEXT("box"))
            {
                // Convert first letter to uppercase, rest to lowercase
                Value = LowerValue;
                if (Value.Len() > 0)
                {
                    Value[0] = FChar::ToUpper(Value[0]);
                }

                // Special case for Box/Cube
                if (Value == TEXT("Cube"))
                {
                    Value = TEXT("Box");
                }

                // Update the value in the JSON object
                Pair.Value = MakeShared<FJsonValueString>(Value);
            }
        }
        // Recursively process nested objects
        else if (Pair.Value->Type == EJson::Object)
        {
            TSharedPtr<FJsonObject> ChildObject = Pair.Value->AsObject();
            NormalizeEnumCase(ChildObject);
        }
        // Process arrays as well
        else if (Pair.Value->Type == EJson::Array)
        {
            TArray<TSharedPtr<FJsonValue>> Array = Pair.Value->AsArray();
            for (auto &Element : Array)
            {
                if (Element->Type == EJson::Object)
                {
                    TSharedPtr<FJsonObject> ChildObject = Element->AsObject();
                    NormalizeEnumCase(ChildObject);
                }
            }
        }
    }
}

bool UJsonSchemaValidator::ValidateJsonObject(const TSharedPtr<FJsonObject> &JsonObject,
                                              const TSharedPtr<FJsonObject> &SchemaObject, FString &OutError)
{
    rapidjson::Document JsonDoc;
    rapidjson::Document SchemaDoc;

    // Convert both JSON and Schema to RapidJSON format
    ConvertToRapidJsonDocument(JsonObject, JsonDoc);
    ConvertToRapidJsonDocument(SchemaObject, SchemaDoc);

    // Create Schema validator
    rapidjson::SchemaDocument schema(SchemaDoc);
    rapidjson::SchemaValidator validator(schema);

    // Perform validation
    if (!JsonDoc.Accept(validator))
    {
        // Get the invalid value
        rapidjson::Value *invalidValue = rapidjson::Pointer(validator.GetInvalidDocumentPointer()).Get(JsonDoc);

        // Get the location of the error
        rapidjson::StringBuffer sb;
        validator.GetInvalidDocumentPointer().StringifyUriFragment(sb);

        // Get detailed error message
        FString detailedError = GetDetailedErrorMessage(validator, invalidValue ? *invalidValue : JsonDoc);

        OutError = FString::Printf(TEXT("Validation failed at: %s. %s"),
                                   UTF8_TO_TCHAR(sb.GetString()),
                                   *detailedError);

        return false;
    }

    return true;
}

FString UJsonSchemaValidator::GetDetailedErrorMessage(const rapidjson::SchemaValidator &Validator, const rapidjson::Value &InvalidValue)
{
    const char *keyword = Validator.GetInvalidSchemaKeyword();
    FString valueStr;

    // Convert the invalid value to string for the error message
    if (InvalidValue.IsString())
        valueStr = UTF8_TO_TCHAR(InvalidValue.GetString());
    else if (InvalidValue.IsNumber())
        valueStr = FString::Printf(TEXT("%f"), InvalidValue.GetDouble());
    else if (InvalidValue.IsBool())
        valueStr = InvalidValue.GetBool() ? TEXT("true") : TEXT("false");
    else if (InvalidValue.IsNull())
        valueStr = TEXT("null");
    else if (InvalidValue.IsObject())
        valueStr = TEXT("[object]");
    else if (InvalidValue.IsArray())
        valueStr = TEXT("[array]");

    // Match the keyword to provide specific error messages
    if (strcmp(keyword, "type") == 0)
    {
        return FString::Printf(TEXT("Invalid type for value: %s"), *valueStr);
    }
    else if (strcmp(keyword, "minimum") == 0)
    {
        return FString::Printf(TEXT("Value %s is less than the minimum allowed"), *valueStr);
    }
    else if (strcmp(keyword, "maximum") == 0)
    {
        return FString::Printf(TEXT("Value %s is greater than the maximum allowed"), *valueStr);
    }
    else if (strcmp(keyword, "minLength") == 0)
    {
        return FString::Printf(TEXT("String '%s' is shorter than minimum length"), *valueStr);
    }
    else if (strcmp(keyword, "maxLength") == 0)
    {
        return FString::Printf(TEXT("String '%s' is longer than maximum length"), *valueStr);
    }
    else if (strcmp(keyword, "pattern") == 0)
    {
        return FString::Printf(TEXT("String '%s' does not match the required pattern"), *valueStr);
    }
    else if (strcmp(keyword, "format") == 0)
    {
        return FString::Printf(TEXT("Value '%s' does not match the required format"), *valueStr);
    }
    else if (strcmp(keyword, "required") == 0)
    {
        return TEXT("Missing required property");
    }
    else if (strcmp(keyword, "enum") == 0)
    {
        return FString::Printf(TEXT("Value '%s' is not one of the allowed enum values"), *valueStr);
    }
    else if (strcmp(keyword, "oneOf") == 0)
    {
        return FString::Printf(TEXT("Value '%s' does not match exactly one of the required schemas"), *valueStr);
    }
    else if (strcmp(keyword, "allOf") == 0)
    {
        return FString::Printf(TEXT("Value '%s' does not match all of the required schemas"), *valueStr);
    }
    else if (strcmp(keyword, "anyOf") == 0)
    {
        return FString::Printf(TEXT("Value '%s' does not match any of the required schemas"), *valueStr);
    }

    // Default case
    return FString::Printf(TEXT("Failed validation for keyword '%s' with value: %s"),
                           UTF8_TO_TCHAR(keyword), *valueStr);
}

void UJsonSchemaValidator::ConvertToRapidJsonDocument(const TSharedPtr<FJsonObject> &JsonObject,
                                                      rapidjson::Document &OutDocument)
{
    // Convert to string first
    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

    // Parse into RapidJSON document
    OutDocument.Parse(TCHAR_TO_UTF8(*JsonString));
}