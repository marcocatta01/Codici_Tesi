#include "Sensors/SensorManager.h"
#include "Sensors/CameraSensor.h"
#include "Sensors/IMUSensor.h"            
#include "Engine/World.h"
#include "PROXSIMAGameInstance.h"

USensorManager::USensorManager()
{
}

bool USensorManager::InitializeSensors(UWorld* World, const TArray<FFMUSensorConfig>& SensorConfigs)
{
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot initialize sensors: Invalid World"));
        return false;
    }

    // Removes existent sensors
    CleanupSensors();

    bool bAllSensorsInitialized = true;
    UPROXSIMAGameInstance* GameInstance = Cast<UPROXSIMAGameInstance>(World->GetGameInstance());

    // Creates and initializes each sensor
    for (const FFMUSensorConfig& SensorConfig : SensorConfigs)
    {
        AActor* SensorActor = CreateSensor(World, SensorConfig);
        if (!SensorActor)
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to create sensor: %s of type %s"),
                *SensorConfig.name, *SensorConfig.type);
            bAllSensorsInitialized = false;
            continue;
        }

        Sensors.Add(SensorConfig.name, SensorActor);

        if (!SensorActor->Implements<USensorInterface>())
        {
            UE_LOG(LogTemp, Error, TEXT("Sensor actor %s does not implement USensorInterface"), *SensorConfig.name);
            bAllSensorsInitialized = false;
            continue;
        }

        // Output path
        FString OutputPath;
        if (SensorConfig.parameters.Contains(TEXT("outputPath")))
        {
            OutputPath = SensorConfig.parameters[TEXT("outputPath")];
        }

        // Initialization with interface
        const bool bInitialized = ISensorInterface::Execute_Initialize(
            SensorActor,
            SensorConfig.name,
            SensorConfig.frameName,
            SensorConfig.rigidTransform,
            SensorConfig.parameters);

        if (!bInitialized)
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to initialize sensor: %s"), *SensorConfig.name);
            bAllSensorsInitialized = false;
            continue;
        }

        // Set output path
        if (!OutputPath.IsEmpty())
        {
            ISensorInterface::Execute_SetOutputPath(SensorActor, OutputPath);
        }

        // Attach frame
        if (GameInstance && !SensorConfig.frameName.IsEmpty())
        {
            AActor* FrameActor = GameInstance->GetFrameActor(SensorConfig.frameName);
            if (FrameActor)
            {
                SensorActor->AttachToActor(FrameActor, FAttachmentTransformRules::KeepRelativeTransform);
                SensorActor->SetActorRelativeTransform(
                    SensorConfig.rigidTransform, false, nullptr, ETeleportType::None);

                UE_LOG(LogTemp, Display, TEXT("Attached sensor %s to frame %s"),
                    *SensorConfig.name, *SensorConfig.frameName);
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("Frame actor not found for sensor %s: %s"),
                    *SensorConfig.name, *SensorConfig.frameName);
            }
        }

        // Enable streaming (if streaming port > 0)
        if (bInitialized && GameInstance && GameInstance->GetGlobalStreamingPort() > 0)
        {
            ISensorInterface::Execute_SetStreamingEnabled(SensorActor, true, 0);
        }
    }

    return bAllSensorsInitialized;
}

void USensorManager::UpdateSensors(float DeltaTime)
{
    // Update sensor using interface
    for (auto& Pair : Sensors)
    {
        AActor* SensorActor = Pair.Value;
        if (SensorActor && SensorActor->Implements<USensorInterface>())
        {
            ISensorInterface::Execute_Update(SensorActor, DeltaTime);
        }
    }
}

AActor* USensorManager::GetSensorByName(const FString& SensorName) const
{
    return Sensors.FindRef(SensorName);
}

TArray<AActor*> USensorManager::GetAllSensors() const
{
    TArray<AActor*> SensorActors;
    Sensors.GenerateValueArray(SensorActors);
    return SensorActors;
}

void USensorManager::CleanupSensors()
{
    for (auto& Pair : Sensors)
    {
        if (AActor* SensorActor = Pair.Value)
        {
            SensorActor->Destroy();
        }
    }
    Sensors.Empty();
}

AActor* USensorManager::CreateSensor(UWorld* World, const FFMUSensorConfig& SensorConfig)
{
    if (!World)
    {
        return nullptr;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.Name = FName(*SensorConfig.name);

    // Creation according to sensor type
    if (SensorConfig.type.Equals(TEXT("Camera"), ESearchCase::IgnoreCase))
    {
        return World->SpawnActor<ACameraSensor>(
            ACameraSensor::StaticClass(), FTransform::Identity, SpawnParams);
    }
    else if (SensorConfig.type.Equals(TEXT("IMU"), ESearchCase::IgnoreCase))
    {
        return World->SpawnActor<AIMUSensor>(
            AIMUSensor::StaticClass(), FTransform::Identity, SpawnParams);
    }

    UE_LOG(LogTemp, Error, TEXT("Unknown sensor type: %s"), *SensorConfig.type);
    return nullptr;
}