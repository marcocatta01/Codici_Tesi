#include "LimboLevelManager.h"
#include "Kismet/GameplayStatics.h"
#include "PROXSIMAGameInstance.h"

void ALimboLevelManager::BeginPlay()
{
    Super::BeginPlay();

    FString ConfigPath;
    UPROXSIMAGameInstance* GameInst = Cast<UPROXSIMAGameInstance>(GetGameInstance());

    if (FParse::Value(FCommandLine::Get(), TEXT("-config="), ConfigPath) && GameInst)
    {
        UE_LOG(LogTemp, Warning, TEXT("Config file path provided via CLI: %s"), *ConfigPath);
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow, FString::Printf(TEXT("Config file path provided via CLI: %s"), *ConfigPath));
            GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow, FString::Printf(TEXT("Loading Configuration...")));
        }

        if (GameInst->InitializeSimulation(ConfigPath))
        {
            UE_LOG(LogTemp, Warning, TEXT("Simulation initialized from config file: %s"), *ConfigPath);
            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green, FString::Printf(TEXT("Simulation initialized from config file: %s"), *ConfigPath));
            }
            UGameplayStatics::OpenLevel(GetWorld(), TEXT("SimulationLevel"));
            return;
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to initialize Simulation from config: %s"), *ConfigPath);
            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, FString::Printf(TEXT("Failed to initialize Simulation from config: %s"), *ConfigPath));
            }
            // Optionally, fallback to SetupLevel or show error
            UGameplayStatics::OpenLevel(GetWorld(), TEXT("SetupLevel"));
            return;
        }
    }

    // No config argument, go to SetupLevel
    UGameplayStatics::OpenLevel(GetWorld(), TEXT("SetupLevel"));
}