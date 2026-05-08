// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file SequenceManager.cpp
 * Implementation of camera sequence playback functionality
 */

#include "FMUhandling/SequenceManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

USequenceManager::USequenceManager()
    : CurrentTime(0.0f), TotalPoses(0), CapturedCount(0)
{
    // Initialize with empty sequence data
}

void USequenceManager::ReportImageCaptured()
{
    CapturedCount++;
    UE_LOG(LogTemp, Log, TEXT("Image captured (%d/%d)"), CapturedCount, TotalPoses);

    if (CapturedCount >= TotalPoses)
    {
        OnSequenceComplete.Broadcast();
    }
}

bool USequenceManager::LoadSequence(const FString &SequencePath, const TArray<FString>& ValidCameraNames)
{
    FString JsonContent;
    if (!FFileHelper::LoadFileToString(JsonContent, *SequencePath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load sequence file: %s"), *SequencePath);
        return false;
    }

    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
    if (!FJsonSerializer::Deserialize(Reader, JsonObject))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to parse sequence file"));
        return false;
    }

    AllPoses.Empty();
    const TArray<TSharedPtr<FJsonValue>> *SequencesArray;
    if (!JsonObject->TryGetArrayField(TEXT("sequences"), SequencesArray))
    {
        UE_LOG(LogTemp, Error, TEXT("No sequences found in file"));
        return false;
    }

    for (const auto &SequenceValue : *SequencesArray)
    {
        const TSharedPtr<FJsonObject> SequenceObject = SequenceValue->AsObject();
        if (!SequenceObject)
            continue;

        FString CameraName;
        if (!SequenceObject->TryGetStringField(TEXT("camera"), CameraName))
            continue;

        const TArray<TSharedPtr<FJsonValue>> *PosesArray;
        if (!SequenceObject->TryGetArrayField(TEXT("poses"), PosesArray))
            continue;

        for (const auto &PoseValue : *PosesArray)
        {
            const TSharedPtr<FJsonObject> PoseObject = PoseValue->AsObject();
            if (!PoseObject)
                continue;

            FSerializedCameraPose Pose;
            Pose.CameraName = CameraName;

            // Parse location
            const TSharedPtr<FJsonObject> *LocationObject;
            if (PoseObject->TryGetObjectField(TEXT("location"), LocationObject))
            {
                double X = 0, Y = 0, Z = 0;
                (*LocationObject)->TryGetNumberField(TEXT("x"), X);
                (*LocationObject)->TryGetNumberField(TEXT("y"), Y);
                (*LocationObject)->TryGetNumberField(TEXT("z"), Z);
                Pose.Transform.SetLocation(FVector(X, Y, Z) * 100.0f); // Convert to centimeters
            }

            // Parse rotation
            const TSharedPtr<FJsonObject> *RotationObject;
            if (PoseObject->TryGetObjectField(TEXT("rotation"), RotationObject))
            {
                double X = 0, Y = 0, Z = 0, W = 1;
                (*RotationObject)->TryGetNumberField(TEXT("x"), X);
                (*RotationObject)->TryGetNumberField(TEXT("y"), Y);
                (*RotationObject)->TryGetNumberField(TEXT("z"), Z);
                (*RotationObject)->TryGetNumberField(TEXT("w"), W);
                Pose.Transform.SetRotation(FQuat(X, Y, Z, W));
            }

            // Parse timestamp (support both string and number formats)
            if (PoseObject->HasField(TEXT("timestamp")))
            {
                const TSharedPtr<FJsonValue> &TimestampValue = PoseObject->TryGetField(TEXT("timestamp"));
                if (TimestampValue->Type == EJson::String)
                {
                    Pose.Timestamp = FCString::Atof(*TimestampValue->AsString());
                }
                else if (TimestampValue->Type == EJson::Number)
                {
                    Pose.Timestamp = TimestampValue->AsNumber();
                }
            }

            // Parse metadata
            const TSharedPtr<FJsonObject> *MetadataObject;
            if (PoseObject->TryGetObjectField(TEXT("metadata"), MetadataObject))
            {
                for (const auto &Pair : (*MetadataObject)->Values)
                {
                    if (Pair.Key == TEXT("description"))
                    {
                        Pose.Description = Pair.Value->AsString();
                    }
                    else
                    {
                        Pose.Metadata.Add(Pair.Key, Pair.Value->AsString());
                    }
                }
            }

            AllPoses.Add(Pose);
        }
    }

    // Sort poses by timestamp
    AllPoses.Sort([](const FSerializedCameraPose &A, const FSerializedCameraPose &B)
                  { return A.Timestamp < B.Timestamp; });

    // Validate camera names if validation list is provided
    if (ValidCameraNames.Num() > 0)
    {
        TSet<FString> ValidCameraSet(ValidCameraNames);
        TSet<FString> UndefinedCameras;
        TArray<FSerializedCameraPose> ValidPoses;

        for (const FSerializedCameraPose &Pose : AllPoses)
        {
            if (ValidCameraSet.Contains(Pose.CameraName))
            {
                ValidPoses.Add(Pose);
            }
            else
            {
                UndefinedCameras.Add(Pose.CameraName);
            }
        }

        // Handle undefined cameras
        if (UndefinedCameras.Num() > 0)
        {
            FString UndefinedList = FString::Join(UndefinedCameras.Array(), TEXT(", "));
            
            UE_LOG(LogTemp, Error, TEXT("SequenceManager: Found poses for undefined cameras: %s"), *UndefinedList);
            UE_LOG(LogTemp, Error, TEXT("SequenceManager: These cameras are not defined in the sensor configuration!"));
            UE_LOG(LogTemp, Error, TEXT("SequenceManager: Excluding %d poses from %d undefined cameras"), 
                   AllPoses.Num() - ValidPoses.Num(), UndefinedCameras.Num());

            // Display onscreen error message
            if (GEngine)
            {
                FString ErrorMsg = FString::Printf(TEXT("ERROR: Sequence contains poses for undefined cameras: %s\nCheck your sensor configuration!"), *UndefinedList);
                GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Red, ErrorMsg);
            }

            // Use only valid poses
            AllPoses = ValidPoses;
        }
    }

    CurrentTime = 0.0f;
    TotalPoses = AllPoses.Num();
    CapturedCount = 0;
    
    UE_LOG(LogTemp, Log, TEXT("SequenceManager: Loaded sequence with %d valid poses"), TotalPoses);
    return true;
}

TArray<FSerializedCameraPose> USequenceManager::GetPosesForCamera(const FString &CameraName) const
{
    TArray<FSerializedCameraPose> CameraPoses;
    for (const FSerializedCameraPose &Pose : AllPoses)
    {
        if (Pose.CameraName == CameraName)
        {
            CameraPoses.Add(Pose);
        }
    }
    return CameraPoses;
}
