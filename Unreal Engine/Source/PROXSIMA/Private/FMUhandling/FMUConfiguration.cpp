#include "FMUConfiguration.h"
#include "Json.h"
#include "JsonObjectConverter.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "FMUModelDescription.h"
#include "JsonSchemaValidator.h"
#include "Kismet/KismetStringLibrary.h"

bool UFMUConfigurationLoader::LoadConfiguration(const FString &path, FFMUConfiguration &outConfig)
{
    // Load JSON file content
    FString jsonString;
    if (!FFileHelper::LoadFileToString(jsonString, *path))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load FMU configuration from: %s"), *path);
        return false;
    }

    // Load JSON schema
    FString SchemaPath = FPaths::Combine(
        FPaths::GameSourceDir(),
        TEXT("PROXSIMA/Private/InitTools/fmu_config_schema.json"));
    FString SchemaString;
    if (!FFileHelper::LoadFileToString(SchemaString, *SchemaPath))
    {
        return false;
    }

    // Proceed with validation
    FString Error;
    if (UJsonSchemaValidator::ValidateJsonAgainstSchema(jsonString, SchemaString, Error))
    {
        UE_LOG(LogTemp, Log, TEXT("JSON validation successful"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Config JSON validation failed: %s"), *Error);
        return false;
    }

    // Parse JSON
    TSharedPtr<FJsonObject> jsonObject;
    TSharedRef<TJsonReader<>> reader = TJsonReaderFactory<>::Create(jsonString);
    if (!FJsonSerializer::Deserialize(reader, jsonObject) || !jsonObject.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to parse FMU configuration JSON"));
        return false;
    }

    // Parse simulation parameters from nested object
    const TSharedPtr<FJsonObject> SimulationObject = jsonObject->GetObjectField(TEXT("simulation"));
    SimulationObject->TryGetStringField(TEXT("fmuPath"), outConfig.fmuPath);
    SimulationObject->TryGetNumberField(TEXT("timeStep"), outConfig.timeStep);
    SimulationObject->TryGetNumberField(TEXT("startTime"), outConfig.startTime);
    SimulationObject->TryGetNumberField(TEXT("stopTime"), outConfig.stopTime);
    // Parse the streaming port
    int32 streamingPort = 0;
    if (SimulationObject->TryGetNumberField(TEXT("streamingPort"), streamingPort))
    {
        outConfig.streamingPort = streamingPort;
    }

    // Parse variables array
    const TArray<TSharedPtr<FJsonValue>> *variablesArray;
    if (jsonObject->TryGetArrayField(TEXT("parameters"), variablesArray))
    {
        for (const auto &varValue : *variablesArray)
        {
            const TSharedPtr<FJsonObject> &varObj = varValue->AsObject();
            FFMUVariableConfig varConfig;

            if (!varObj->TryGetStringField(TEXT("name"), varConfig.name))
            {
                UE_LOG(LogTemp, Error, TEXT("Missing 'name' in variable config"));
                return false;
            }

            if (!varObj->TryGetNumberField(TEXT("value"), varConfig.value))
            {
                UE_LOG(LogTemp, Error, TEXT("Missing 'value' in variable config"));
                return false;
            }

            varObj->TryGetStringField(TEXT("componentProperty"), varConfig.componentProperty);
            varObj->TryGetNumberField(TEXT("unitConversionFactor"), varConfig.unitConversionFactor);

            outConfig.variables.Add(varConfig);
        }
    }

    const TArray<TSharedPtr<FJsonValue>> *sensorsArray;
    if (jsonObject->TryGetArrayField(TEXT("sensors"), sensorsArray))
    {
        for (const auto &sensorValue : *sensorsArray)
        {
            const TSharedPtr<FJsonObject> &sensorObj = sensorValue->AsObject();
            if (!sensorObj.IsValid())
                continue;

            FFMUSensorConfig sensorConfig;
            sensorObj->TryGetStringField(TEXT("name"), sensorConfig.name);
            sensorObj->TryGetStringField(TEXT("type"), sensorConfig.type);
            sensorObj->TryGetStringField(TEXT("frameName"), sensorConfig.frameName);

            // Parse transform if present
            const TSharedPtr<FJsonObject> *transformObj;
            if (sensorObj->TryGetObjectField(TEXT("rigidTransform"), transformObj))
            {
                // Parse translation
                const TArray<TSharedPtr<FJsonValue>> *translationArray;
                if ((*transformObj)->TryGetArrayField(TEXT("translation"), translationArray) && translationArray->Num() >= 3)
                {
                    FVector translation(
                        (*translationArray)[0]->AsNumber(),
                        (*translationArray)[1]->AsNumber(),
                        (*translationArray)[2]->AsNumber());
                    sensorConfig.rigidTransform.SetLocation(translation);
                }

                // Parse rotation
                const TArray<TSharedPtr<FJsonValue>> *rotationArray;
                if ((*transformObj)->TryGetArrayField(TEXT("rotation"), rotationArray))
                {
                    if (rotationArray->Num() >= 3)
                    {
                        // Euler angles (roll, pitch, yaw) in degrees
                        FRotator rotator(
                            (*rotationArray)[1]->AsNumber(), // Pitch
                            (*rotationArray)[2]->AsNumber(), // Yaw
                            (*rotationArray)[0]->AsNumber()  // Roll
                        );
                        sensorConfig.rigidTransform.SetRotation(rotator.Quaternion());
                    }
                    else if (rotationArray->Num() >= 4)
                    {
                        // Quaternion (x, y, z, w)
                        FQuat quaternion(
                            (*rotationArray)[0]->AsNumber(),
                            (*rotationArray)[1]->AsNumber(),
                            (*rotationArray)[2]->AsNumber(),
                            (*rotationArray)[3]->AsNumber());
                        sensorConfig.rigidTransform.SetRotation(quaternion);
                    }
                }
            }

            // Parse sensor parameters
            const TSharedPtr<FJsonObject> *paramsObj;
            if (sensorObj->TryGetObjectField(TEXT("parameters"), paramsObj))
            {
                for (const auto &param : (*paramsObj)->Values)
                {
                    // Convert all parameter values to strings
                    if (param.Value->Type == EJson::String)
                    {
                        sensorConfig.parameters.Add(param.Key, param.Value->AsString());
                    }
                    else if (param.Value->Type == EJson::Number)
                    {
                        sensorConfig.parameters.Add(param.Key, FString::SanitizeFloat(param.Value->AsNumber()));
                    }
                    else if (param.Value->Type == EJson::Boolean)
                    {
                        sensorConfig.parameters.Add(param.Key, param.Value->AsBool() ? TEXT("true") : TEXT("false"));
                    }
                    else if (param.Value->Type == EJson::Array)
                    {
                        // Handle array parameters (like resolution)
                        FString arrayStr;
                        const TArray<TSharedPtr<FJsonValue>> *arrayValues = nullptr;
                        if (param.Value->TryGetArray(arrayValues))
                        {
                            TArray<FString> stringValues;
                            for (const auto &arrayValue : *arrayValues)
                            {
                                if (arrayValue->Type == EJson::Number)
                                {
                                    stringValues.Add(FString::SanitizeFloat(arrayValue->AsNumber()));
                                }
                                else if (arrayValue->Type == EJson::String)
                                {
                                    stringValues.Add(arrayValue->AsString());
                                }
                            }
                            arrayStr = FString::Join(stringValues, TEXT(","));
                            sensorConfig.parameters.Add(param.Key, arrayStr);
                        }
                    }
                }
            }

            outConfig.sensors.Add(sensorConfig);
        }
    }

    return true;
}

bool UFMUConfigurationLoader::SaveConfiguration(const FString &path, const FFMUConfiguration &config)
{
    // Create main JSON object
    TSharedRef<FJsonObject> jsonObject = MakeShared<FJsonObject>();

    // Create simulation object
    TSharedRef<FJsonObject> simulationObject = MakeShared<FJsonObject>();
    simulationObject->SetStringField(TEXT("fmuPath"), config.fmuPath);
    simulationObject->SetNumberField(TEXT("timeStep"), config.timeStep);
    simulationObject->SetNumberField(TEXT("startTime"), config.startTime);
    simulationObject->SetNumberField(TEXT("stopTime"), config.stopTime);
    simulationObject->SetNumberField(TEXT("streamingPort"), config.streamingPort);
    jsonObject->SetObjectField(TEXT("simulation"), simulationObject);

    // Create parameters array
    TArray<TSharedPtr<FJsonValue>> parametersArray;
    for (const FFMUVariableConfig &var : config.variables)
    {
        TSharedRef<FJsonObject> varObject = MakeShared<FJsonObject>();
        varObject->SetStringField(TEXT("name"), var.name);
        varObject->SetNumberField(TEXT("value"), var.value);
        if (!var.componentProperty.IsEmpty())
        {
            varObject->SetStringField(TEXT("componentProperty"), var.componentProperty);
        }
        if (var.unitConversionFactor != 1.0)
        {
            varObject->SetNumberField(TEXT("unitConversionFactor"), var.unitConversionFactor);
        }
        parametersArray.Add(MakeShared<FJsonValueObject>(varObject));
    }
    jsonObject->SetArrayField(TEXT("parameters"), parametersArray);

    // Create visual components array
    TArray<TSharedPtr<FJsonValue>> visualArray;
    for (const FFMUVisualComponent &comp : config.visualComponents)
    {
        TSharedRef<FJsonObject> compObject = MakeShared<FJsonObject>();
        compObject->SetStringField(TEXT("name"), comp.name);
        compObject->SetStringField(TEXT("type"), comp.type);

        // Add frameName if it's different from name (for backwards compatibility)
        if (!comp.frameName.IsEmpty() && comp.frameName != comp.name)
        {
            compObject->SetStringField(TEXT("frameName"), comp.frameName);
        }

        // Add meshPath if it exists (for 'mesh' type)
        if (!comp.meshPath.IsEmpty())
        {
            compObject->SetStringField(TEXT("meshPath"), comp.meshPath);
        }

        // Add dimensions
        if (comp.dimensions.Num() > 0)
        {
            TSharedRef<FJsonObject> dimensionsObject = MakeShared<FJsonObject>();
            for (const auto &dim : comp.dimensions)
            {
                dimensionsObject->SetNumberField(dim.Key, dim.Value);
            }
            compObject->SetObjectField(TEXT("dimensions"), dimensionsObject);
        }

        // Add transform
        TSharedRef<FJsonObject> transformObject = MakeShared<FJsonObject>();
        const FVector &loc = comp.rigidTransform.GetLocation();
        TArray<TSharedPtr<FJsonValue>> translation = {
            MakeShared<FJsonValueNumber>(loc.X),
            MakeShared<FJsonValueNumber>(loc.Y),
            MakeShared<FJsonValueNumber>(loc.Z)};
        transformObject->SetArrayField(TEXT("translation"), translation);

        const FQuat &rot = comp.rigidTransform.GetRotation();
        TArray<TSharedPtr<FJsonValue>> rotation = {
            MakeShared<FJsonValueNumber>(rot.X),
            MakeShared<FJsonValueNumber>(rot.Y),
            MakeShared<FJsonValueNumber>(rot.Z),
            MakeShared<FJsonValueNumber>(rot.W)};
        transformObject->SetArrayField(TEXT("rotation"), rotation);
        compObject->SetObjectField(TEXT("rigidTransform"), transformObject);

        // Add appearance
        TSharedRef<FJsonObject> appearanceObject = MakeShared<FJsonObject>();
        if (!comp.appearance.colorName.IsEmpty())
        {
            appearanceObject->SetStringField(TEXT("color"), comp.appearance.colorName);
        }
        else
        {
            TArray<TSharedPtr<FJsonValue>> color = {
                MakeShared<FJsonValueNumber>(comp.appearance.color.R),
                MakeShared<FJsonValueNumber>(comp.appearance.color.G),
                MakeShared<FJsonValueNumber>(comp.appearance.color.B),
                MakeShared<FJsonValueNumber>(comp.appearance.color.A)};
            appearanceObject->SetArrayField(TEXT("color"), color);
        }
        appearanceObject->SetNumberField(TEXT("opacity"), comp.appearance.opacity);
        appearanceObject->SetNumberField(TEXT("metallic"), comp.appearance.metallic);
        appearanceObject->SetNumberField(TEXT("roughness"), comp.appearance.roughness);
        compObject->SetObjectField(TEXT("appearance"), appearanceObject);

        visualArray.Add(MakeShared<FJsonValueObject>(compObject));
    }
    jsonObject->SetArrayField(TEXT("visualComponents"), visualArray);

    // Create sensors array
    TArray<TSharedPtr<FJsonValue>> sensorsArray;
    for (const FFMUSensorConfig &sensor : config.sensors)
    {
        TSharedRef<FJsonObject> sensorObject = MakeShared<FJsonObject>();
        sensorObject->SetStringField(TEXT("name"), sensor.name);
        sensorObject->SetStringField(TEXT("type"), sensor.type);
        sensorObject->SetStringField(TEXT("frameName"), sensor.frameName);

        // Add transform
        TSharedRef<FJsonObject> transformObject = MakeShared<FJsonObject>();
        const FVector &loc = sensor.rigidTransform.GetLocation();
        TArray<TSharedPtr<FJsonValue>> translation = {
            MakeShared<FJsonValueNumber>(loc.X),
            MakeShared<FJsonValueNumber>(loc.Y),
            MakeShared<FJsonValueNumber>(loc.Z)};
        transformObject->SetArrayField(TEXT("translation"), translation);

        const FQuat &rot = sensor.rigidTransform.GetRotation();
        FRotator rotator = rot.Rotator();
        TArray<TSharedPtr<FJsonValue>> rotation = {
            MakeShared<FJsonValueNumber>(rotator.Roll),
            MakeShared<FJsonValueNumber>(rotator.Pitch),
            MakeShared<FJsonValueNumber>(rotator.Yaw)};
        transformObject->SetArrayField(TEXT("rotation"), rotation);

        sensorObject->SetObjectField(TEXT("rigidTransform"), transformObject);

        // Add parameters
        if (sensor.parameters.Num() > 0)
        {
            TSharedRef<FJsonObject> paramsObject = MakeShared<FJsonObject>();
            for (const auto &param : sensor.parameters)
            {
                // Try to parse as number first
                double numValue = FCString::Atof(*param.Value);
                if (numValue)
                {
                    paramsObject->SetNumberField(param.Key, numValue);
                }
                else if (param.Value.ToLower() == TEXT("true"))
                {
                    paramsObject->SetBoolField(param.Key, true);
                }
                else if (param.Value.ToLower() == TEXT("false"))
                {
                    paramsObject->SetBoolField(param.Key, false);
                }
                else if (param.Value.Contains(TEXT(",")))
                {
                    // Handle comma-separated arrays
                    TArray<FString> arrayValues;
                    param.Value.ParseIntoArray(arrayValues, TEXT(","), true);

                    TArray<TSharedPtr<FJsonValue>> jsonArray;
                    for (const FString &arrayValue : arrayValues)
                    {
                        double arrayNumValue = FCString::Atof(*arrayValue);
                        if (arrayNumValue)
                        {
                            jsonArray.Add(MakeShared<FJsonValueNumber>(arrayNumValue));
                        }
                        else
                        {
                            jsonArray.Add(MakeShared<FJsonValueString>(arrayValue));
                        }
                    }

                    paramsObject->SetArrayField(param.Key, jsonArray);
                }
                else
                {
                    paramsObject->SetStringField(param.Key, param.Value);
                }
            }
            sensorObject->SetObjectField(TEXT("parameters"), paramsObject);
        }

        sensorsArray.Add(MakeShared<FJsonValueObject>(sensorObject));
    }
    jsonObject->SetArrayField(TEXT("sensors"), sensorsArray);

    // Convert to string with pretty formatting
    FString jsonString;
    TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> writer =
        TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&jsonString);
    if (!FJsonSerializer::Serialize(jsonObject, writer))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to serialize FMU configuration JSON"));
        return false;
    }

    // Save to file
    if (!FFileHelper::SaveStringToFile(jsonString, *path))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to save FMU configuration to: %s"), *path);
        return false;
    }

    return true;
}

bool UFMUConfigurationLoader::ValidateConfiguration(const FFMUConfiguration &config, UFMUModelDescription *modelDesc)
{
    if (!modelDesc)
    {
        UE_LOG(LogTemp, Error, TEXT("Model description is null"));
        return false;
    }

    // Validate timeStep if specified
    if (config.timeStep > 0.0)
    {
        // Could add more validation here if needed
        UE_LOG(LogTemp, Display, TEXT("Using custom time step: %f"), config.timeStep);
    }
    else
    {
        // Will use default from model description
        UE_LOG(LogTemp, Display, TEXT("Using default time step from model description: %f"),
            modelDesc->GetDefaultStepSize());
    }

    // Validate all variables
    bool allValid = true;
    for (const FFMUVariableConfig &var : config.variables)
    {
        uint32 valueRef;
        if (!modelDesc->GetValueReference(var.name, valueRef))
        {
            UE_LOG(LogTemp, Error, TEXT("Variable not found in model description: %s"), *var.name);
            allValid = false;
            continue;
        }

        if (!modelDesc->ValidateValue(var.name, var.value))
        {
            UE_LOG(LogTemp, Error, TEXT("Invalid value for variable %s: %f"), *var.name, var.value);
            allValid = false;
            continue;
        }

        UE_LOG(LogTemp, Verbose, TEXT("Variable %s validated successfully"), *var.name);
    }

    return allValid;
}