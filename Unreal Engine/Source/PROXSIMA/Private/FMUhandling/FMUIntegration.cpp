#include "FMUIntegration.h"
#include "PROXSIMAGameInstance.h"
#include "ZipHandlingUtility.h"
#include <sys/stat.h>

// Constructor
FFMUIntegration::FFMUIntegration(UPROXSIMAGameInstance *InGameInstance)
    : GameInstance(InGameInstance), libraryHandle(nullptr), fmuInstance(nullptr), currentTime(0.0)
{
    check(GameInstance);
}

FFMUIntegration::~FFMUIntegration()
{
    // Terminate and free all resources
    CleanUp();

    // Clear configuration under lock
    {
        FScopeLock Lock(&ConfigurationLock);
        Configuration = FFMUConfiguration();
    }
}

bool FFMUIntegration::CreateDirectory(const FString &path)
{
    IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    return PlatformFile.CreateDirectoryTree(*path);
}

bool FFMUIntegration::LoadFMU(const FString &fmuPath)
{
    // Clean up any existing state first
    CleanUp();

    // Create a temporary directory for extraction
    FString ExtractPath = FPaths::Combine(
        FPaths::ProjectIntermediateDir(),
        TEXT("FMU"),
        FPaths::GetBaseFilename(fmuPath));

    // Extract FMU
    if (!FZipHandlingUtility::UnzipFile(fmuPath, ExtractPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to extract the FMU: %hs"), fmiLib.GetError());
        return false;
    }

    // Create and load model description, owned by GameInstance
    UPROXSIMAGameInstance *ProxGameInstance = Cast<UPROXSIMAGameInstance>(GameInstance);
    if (!ProxGameInstance)
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid GameInstance type"));
        CleanUp();
        return false;
    }

    ModelDescription = NewObject<UFMUModelDescription>(GameInstance);
    if (!ModelDescription)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create model description object"));
        CleanUp();
        return false;
    }

    if (!ModelDescription->InitializeFromExtractedFMU(ExtractPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to parse model description"));
        ModelDescription = nullptr;
        CleanUp();
        return false;
    }

    // Store ModelDescription in GameInstance for persistence between levels
    ProxGameInstance->SetModelDescription(ModelDescription);

    // Load platform-specific library
#if PLATFORM_WINDOWS
    FString BinaryPath = ExtractPath / TEXT("binaries/win64");
    FString LibExt = TEXT(".dll");
#elif PLATFORM_LINUX
    FString BinaryPath = ExtractPath / TEXT("binaries/linux64");
    FString LibExt = TEXT(".so");
#endif

    FString libName = FPaths::GetBaseFilename(fmuPath) + LibExt;

    if (!fmiLib.Load(TCHAR_TO_UTF8(*BinaryPath), TCHAR_TO_UTF8(*libName)))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load FMI library: %s"), UTF8_TO_TCHAR(fmiLib.GetError()));
        ModelDescription = nullptr;
        CleanUp();
        return false;
    }

    UE_LOG(LogTemp, Warning, TEXT("FMU Loaded successfully"));
    return true;
}

bool FFMUIntegration::Initialize(const FFMUConfiguration &config)
{
    UE_LOG(LogTemp, Warning, TEXT("Starting FMU initialization"));

    if (!ModelDescription || !IsValid(ModelDescription.Get()))
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot initialize - ModelDescription is invalid"));
        return false;
    }

    // Thread-safe configuration update
    {
        FScopeLock Lock(&ConfigurationLock);
        Configuration = config;
    }

    // Set up callbacks
    static fmi2CallbackFunctions callbacks = {
        .logger = fmiLogger,
        .allocateMemory = calloc,
        .freeMemory = free,
        .stepFinished = nullptr,
        .componentEnvironment = nullptr};

    // Get resource path from extracted FMU location
    FString ExtractPath = FPaths::Combine(
        FPaths::ProjectIntermediateDir(),
        TEXT("FMU"),
        FPaths::GetBaseFilename(config.fmuPath));
    FString resourceLocation = FPaths::Combine(ExtractPath, TEXT("resources"));

    // Convert resourceLocation to URI as required by FMI standard
    FString resourceURI = TEXT("file:///") + resourceLocation.Replace(TEXT("\\"), TEXT("/"));

    // Keep a strong reference during initialization
    UFMUModelDescription *ModelDesc = ModelDescription.Get();

    // Log all arguments before instantiation for debugging
    UE_LOG(LogTemp, Warning, TEXT("Instantiating FMU with:"));
    UE_LOG(LogTemp, Warning, TEXT("  ModelIdentifier: %s"), *ModelDesc->GetModelIdentifier());
    UE_LOG(LogTemp, Warning, TEXT("  GUID: %s"), *ModelDesc->GetGUID());
    UE_LOG(LogTemp, Warning, TEXT("  Resource URI: %s"), *resourceURI);

    // Instantiate FMU using model description info
    fmuInstance = fmiLib.fmi2Instantiate(
        StringCast<ANSICHAR>(*ModelDesc->GetModelIdentifier()).Get(),
        fmi2CoSimulation,
        StringCast<ANSICHAR>(*ModelDesc->GetGUID()).Get(),
        StringCast<ANSICHAR>(*resourceURI).Get(),
        &callbacks,
        fmi2False,
        fmi2True);

    if (!fmuInstance)
    {
        UE_LOG(LogTemp, Error, TEXT("FMU instantiation failed"));
        return false;
    }

    // Setup experiment with configuration parameters
    double stepSize = config.timeStep > 0.0 ? config.timeStep : ModelDesc->GetDefaultStepSize();
    if (fmiLib.fmi2SetupExperiment(
            fmuInstance,
            fmi2False,
            config.startTime,
            config.stopTime,
            fmi2False,
            stepSize) != fmi2OK)
    {
        UE_LOG(LogTemp, Error, TEXT("Setup experiment failed"));
        return false;
    }

    // Enter initialization mode
    if (fmiLib.fmi2EnterInitializationMode(fmuInstance) != fmi2OK)
    {
        UE_LOG(LogTemp, Error, TEXT("Enter initialization mode failed"));
        return false;
    }

    // Get a thread-safe copy of configuration for initialization
    FFMUConfiguration localConfig;
    {
        FScopeLock Lock(&ConfigurationLock);
        localConfig = Configuration;
    }

    // Set initial values from configuration
    for (const auto &var : localConfig.variables)
    {
        if (!SetVariable(var.name, var.value))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to set initial value for %s"), *var.name);
            return false;
        }
    }

    // Exit initialization mode
    if (fmiLib.fmi2ExitInitializationMode(fmuInstance) != fmi2OK)
    {
        UE_LOG(LogTemp, Error, TEXT("Exit initialization mode failed"));
        return false;
    }

    currentTime = config.startTime;
    return true;
}

bool FFMUIntegration::DoStep(double stepSize)
{
    if (!fmuInstance)
    {
        UE_LOG(LogTemp, Error, TEXT("DoStep called with null FMU instance"));
        return false;
    }

    fmi2Status status = fmiLib.fmi2DoStep(fmuInstance, currentTime, stepSize, fmi2True);
    if (status != fmi2OK)
    {
        UE_LOG(LogTemp, Error, TEXT("DoStep failed with status: %d"), (int)status);
        return false;
    }

    currentTime += stepSize;
    return true;
}

bool FFMUIntegration::GetVariable(const FString &varName, double &outValue) const
{
    if (!fmuInstance || !ModelDescription || !IsValid(ModelDescription.Get()))
    {
        UE_LOG(LogTemp, Warning, TEXT("GetVariable: Invalid state - FMU instance or model description is null"));
        return false;
    }

    UFMUModelDescription *ModelDesc = ModelDescription.Get();
    uint32 valueRef;
    if (!ModelDesc->GetValueReference(varName, valueRef))
    {
        // GetValueReference already logs the error
        return false;
    }

    // Get the raw value
    fmi2Status status = fmiLib.fmi2GetReal(fmuInstance, &valueRef, 1, &outValue);
    if (status != fmi2OK)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to get value for '%s' (valueRef: %u), status: %d"),
               *varName, valueRef, static_cast<int32>(status));
        return false;
    }
    return true;
}

bool FFMUIntegration::SetVariable(const FString &varName, double value)
{
    if (!fmuInstance || !ModelDescription || !IsValid(ModelDescription.Get()))
    {
        UE_LOG(LogTemp, Warning, TEXT("SetVariable: Invalid state - FMU instance or model description is null"));
        return false;
    }

    UFMUModelDescription *ModelDesc = ModelDescription.Get();

    // First validate the value against model description
    if (!ModelDesc->ValidateValue(varName, value))
    {
        UE_LOG(LogTemp, Warning, TEXT("SetVariable: Value validation failed for '%s'"), *varName);
        return false;
    }

    uint32 valueRef;
    if (!ModelDesc->GetValueReference(varName, valueRef))
    {
        // GetValueReference already logs the error
        return false;
    }

    fmi2Status status = fmiLib.fmi2SetReal(fmuInstance, &valueRef, 1, &value);
    if (status != fmi2OK)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to set value for '%s' (valueRef: %u), status: %d"),
               *varName, valueRef, static_cast<int32>(status));
        return false;
    }
    return true;
}

void FFMUIntegration::GetAllConfiguredValues(TMap<FString, double> &outValues) const
{
    outValues.Empty();

    if (!fmuInstance || !ModelDescription || !IsValid(ModelDescription.Get()))
    {
        UE_LOG(LogTemp, Warning, TEXT("GetAllConfiguredValues: Invalid state - FMU instance or model description is null"));
        return;
    }

    // Make a thread-safe copy of the configuration
    FFMUConfiguration localConfig;
    {
        FScopeLock Lock(&ConfigurationLock);
        localConfig = Configuration;
    }

    // Get values for all configured variables
    for (const auto &var : localConfig.variables)
    {
        double value;
        if (GetVariable(var.name, value))
        {
            outValues.Add(var.name, value);
        }
    }
}

void FFMUIntegration::fmiLogger(fmi2ComponentEnvironment componentEnvironment,
                                fmi2String instanceName,
                                fmi2Status status,
                                fmi2String category,
                                fmi2String message,
                                ...)
{
    // Only proceed for warnings and errors
    if (status != fmi2Warning && status != fmi2Error && status != fmi2Fatal)
        return;

    char buf[1024];
    va_list args;
    va_start(args, message);
    vsnprintf(buf, sizeof(buf), message, args);
    va_end(args);

    // Log based on severity
    if (status == fmi2Warning)
    {
        UE_LOG(LogTemp, Warning, TEXT("FMU [%s][%s]: %hs"),
               StringCast<TCHAR>(instanceName).Get(),
               StringCast<TCHAR>(category).Get(),
               buf);
    }
    else // Error or Fatal
    {
        UE_LOG(LogTemp, Error, TEXT("FMU [%s][%s]: %hs"),
               StringCast<TCHAR>(instanceName).Get(),
               StringCast<TCHAR>(category).Get(),
               buf);
    }
}

void FFMUIntegration::CleanUp()
{
    // First terminate and free the FMU instance
    if (fmuInstance)
    {
        // Log cleanup for debugging
        UE_LOG(LogTemp, Warning, TEXT("Cleaning up FMU instance"));

        // Terminate simulation if active
        fmiLib.fmi2Terminate(fmuInstance);

        // Free the instance
        fmiLib.fmi2FreeInstance(fmuInstance);
        fmuInstance = nullptr;
    }

    // Then unload the FMI library
    if (fmiLib.IsLoaded())
    {
        UE_LOG(LogTemp, Warning, TEXT("Unloading FMI library"));
        fmiLib.Unload();
    }

    // Reset all handles and state
    libraryHandle = nullptr;
    currentTime = 0.0;

    // Log completion
    UE_LOG(LogTemp, Warning, TEXT("FMU cleanup completed"));
}