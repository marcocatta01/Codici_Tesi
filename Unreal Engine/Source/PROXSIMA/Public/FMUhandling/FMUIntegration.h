#pragma once

class UPROXSIMAGameInstance;

#include "CoreMinimal.h"
#include "fmi2Functions.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "FMUModelDescription.h"
#include "FMUConfiguration.h"
#include "FMILibraryHandler.h"

struct zip;
struct zip_stat;
struct zip_file;

class PROXSIMA_API FFMUIntegration
{
private:
    class UPROXSIMAGameInstance* GameInstance;
    void *libraryHandle;
    fmi2Component fmuInstance;
    double currentTime;

    // Model description data (raw pointer since we're not a UObject)
    TObjectPtr<UFMUModelDescription> ModelDescription;
    FFMUConfiguration Configuration;

    // Thread safety
    mutable FCriticalSection ConfigurationLock;

    // FMI function pointers
    typedef fmi2Component (*fmi2InstantiateTYPE)(fmi2String, fmi2Type, fmi2String,
                                                 fmi2String, const fmi2CallbackFunctions *, fmi2Boolean, fmi2Boolean);
    typedef fmi2Status (*fmi2SetupExperimentTYPE)(fmi2Component, fmi2Boolean,
                                                  fmi2Real, fmi2Real, fmi2Boolean, fmi2Real);
    typedef fmi2Status (*fmi2EnterInitializationModeTYPE)(fmi2Component);
    typedef fmi2Status (*fmi2ExitInitializationModeTYPE)(fmi2Component);
    typedef fmi2Status (*fmi2DoStepTYPE)(fmi2Component, fmi2Real, fmi2Real, fmi2Boolean);
    typedef fmi2Status (*fmi2GetRealTYPE)(fmi2Component, const fmi2ValueReference *,
                                          size_t, fmi2Real *);
    typedef fmi2Status (*fmi2SetRealTYPE)(fmi2Component, const fmi2ValueReference *,
                                          size_t, const fmi2Real *);
    typedef fmi2Status (*fmi2TerminateTYPE)(fmi2Component);
    typedef void (*fmi2FreeInstanceTYPE)(fmi2Component);

    // Function pointers
    fmi2InstantiateTYPE fmi2Instantiate;
    fmi2SetupExperimentTYPE fmi2SetupExperiment;
    fmi2EnterInitializationModeTYPE fmi2EnterInitializationMode;
    fmi2ExitInitializationModeTYPE fmi2ExitInitializationMode;
    fmi2DoStepTYPE fmi2DoStep;
    fmi2GetRealTYPE fmi2GetReal;
    fmi2SetRealTYPE fmi2SetReal;
    fmi2TerminateTYPE fmi2Terminate;
    fmi2FreeInstanceTYPE fmi2FreeInstance;

public:
    explicit FFMUIntegration(class UPROXSIMAGameInstance *InGameInstance);
    ~FFMUIntegration();

    bool LoadFMU(const FString &fmuPath);
    bool Initialize(const FFMUConfiguration &config);
    bool DoStep(double stepSize);

    // Generic variable access
    bool GetVariable(const FString &varName, double &outValue) const;
    bool SetVariable(const FString &varName, double value);

    // Get all configured variables
    void GetAllConfiguredValues(TMap<FString, double> &outValues) const;

	// Get current integration time
	float GetCurrentTime() const {return currentTime;};

    void CleanUp();

private:
    bool ExtractFMU(const FString& fmuPath, const FString& extractPath);
    bool CreateDirectory(const FString& path);

    static void fmiLogger(fmi2ComponentEnvironment componentEnvironment,
                          fmi2String instanceName,
                          fmi2Status status,
                          fmi2String category,
                          fmi2String message,
                          ...);

    PROXSIMA::FMIDynamicLibrary fmiLib;
};
