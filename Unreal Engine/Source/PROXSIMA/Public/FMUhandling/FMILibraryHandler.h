// FMILibraryHandler.h
#pragma once
#include "CoreMinimal.h"

// Platform detection
#if PLATFORM_WINDOWS
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// Include necessary FMI headers
#include "fmi2Functions.h"
#include "fmi2FunctionTypes.h"
#include "fmi2TypesPlatform.h"

// Define the function pointer types
typedef fmi2Component(*fmi2InstantiatePtr)(fmi2String, fmi2Type, fmi2String, fmi2String, const fmi2CallbackFunctions*, fmi2Boolean, fmi2Boolean);
typedef fmi2Status(*fmi2SetupExperimentPtr)(fmi2Component, fmi2Boolean, fmi2Real, fmi2Real, fmi2Boolean, fmi2Real);
typedef fmi2Status(*fmi2EnterInitializationModePtr)(fmi2Component);
typedef fmi2Status(*fmi2ExitInitializationModePtr)(fmi2Component);
typedef fmi2Status(*fmi2DoStepPtr)(fmi2Component, fmi2Real, fmi2Real, fmi2Boolean);
typedef fmi2Status(*fmi2GetRealPtr)(fmi2Component, const fmi2ValueReference*, size_t, fmi2Real*);
typedef fmi2Status(*fmi2SetRealPtr)(fmi2Component, const fmi2ValueReference*, size_t, const fmi2Real*);
typedef fmi2Status(*fmi2TerminatePtr)(fmi2Component);
typedef void (*fmi2FreeInstancePtr)(fmi2Component);


namespace PROXSIMA {  // Inside your project's namespace

    class FMIDynamicLibrary {
    public:
        FMIDynamicLibrary() : handle(nullptr) {
            // Initialize function pointers to nullptr
            fmi2Instantiate = nullptr;
            fmi2DoStep = nullptr;
            fmi2SetupExperiment = nullptr;
            fmi2EnterInitializationMode = nullptr;
            fmi2ExitInitializationMode = nullptr;
            fmi2Terminate = nullptr;
            fmi2FreeInstance = nullptr;
        }

        bool Load(const char *fmuBinariesPath, const char *mainLibraryName)
        {
#if PLATFORM_WINDOWS
            // Save the original DLL directory and set the search path to include FMU binaries
            char originalPath[MAX_PATH];
            GetDllDirectoryA(MAX_PATH, originalPath);
            SetDllDirectoryA(fmuBinariesPath);

            // Now load the main library - dependencies will be resolved automatically
            handle = LoadLibraryA(mainLibraryName);

            // Restore the original search path
            SetDllDirectoryA(originalPath);
#else
            // Similar approach for Linux
            std::string libraryPath = std::string(fmuBinariesPath) + "/" + mainLibraryName;

            // Set LD_LIBRARY_PATH temporarily (in-process only)
            std::string oldLdLibPath = "";
            char *oldEnv = getenv("LD_LIBRARY_PATH");
            if (oldEnv)
                oldLdLibPath = oldEnv;

            setenv("LD_LIBRARY_PATH", fmuBinariesPath, 1);

            handle = dlopen(libraryPath.c_str(), RTLD_NOW);

            // Restore original environment
            if (!oldLdLibPath.empty())
                setenv("LD_LIBRARY_PATH", oldLdLibPath.c_str(), 1);
            else
                unsetenv("LD_LIBRARY_PATH");
#endif

            if (!handle) return false;

            // Load all FMI functions
            bool success = LoadFMIFunctions();
            if (!success) {
                Unload();
                return false;
            }

            return true;
        }

        template<typename T>
        T GetSymbol(const char* symbolName) {
            if (!handle) return nullptr;
#if PLATFORM_WINDOWS
            return reinterpret_cast<T>(reinterpret_cast<void *>(::GetProcAddress((HMODULE)handle, symbolName)));
#else
            return (T)dlsym(handle, symbolName);
#endif
        }

        bool IsLoaded() const {
            return handle != nullptr;  // Return true if the library is loaded
        }

        bool Unload() {
            if (!handle) return false;
#if PLATFORM_WINDOWS
            bool result = FreeLibrary((HMODULE)handle) != 0;
#else
            bool result = dlclose(handle) == 0;
#endif
            if (result) {
                handle = nullptr;
                // Reset all function pointers to nullptr
                fmi2Instantiate = nullptr;
                fmi2SetupExperiment = nullptr;
                fmi2EnterInitializationMode = nullptr;
                fmi2ExitInitializationMode = nullptr;
                fmi2DoStep = nullptr;
                fmi2GetReal = nullptr;
                fmi2SetReal = nullptr;
                fmi2Terminate = nullptr;
                fmi2FreeInstance = nullptr;
            }
            return result;
        }


        const char* GetError() {
#if PLATFORM_WINDOWS
            DWORD error = GetLastError();
            static char buffer[256];
            FormatMessageA(
                FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL,
                error,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                buffer,
                sizeof(buffer),
                NULL
            );
            return buffer;
#else
            return dlerror();
#endif
        }

        ~FMIDynamicLibrary() {
            Unload();
        }

        // FMI function pointers - public for easy access
        fmi2InstantiatePtr fmi2Instantiate;
        fmi2SetupExperimentPtr fmi2SetupExperiment;
        fmi2EnterInitializationModePtr fmi2EnterInitializationMode;
        fmi2ExitInitializationModePtr fmi2ExitInitializationMode;
        fmi2DoStepPtr fmi2DoStep;
        fmi2GetRealPtr fmi2GetReal;
        fmi2SetRealPtr fmi2SetReal;
        fmi2TerminatePtr fmi2Terminate;
        fmi2FreeInstancePtr fmi2FreeInstance;


    private:
        void* handle;

        bool LoadFMIFunctions() {
            // Load all function pointers using the GetSymbol method
            fmi2Instantiate = GetSymbol<fmi2InstantiatePtr>("fmi2Instantiate");
            fmi2DoStep = GetSymbol<fmi2DoStepPtr>("fmi2DoStep");
            fmi2SetupExperiment = GetSymbol<fmi2SetupExperimentPtr>("fmi2SetupExperiment");
            fmi2EnterInitializationMode = GetSymbol<fmi2EnterInitializationModePtr>("fmi2EnterInitializationMode");
            fmi2ExitInitializationMode = GetSymbol<fmi2ExitInitializationModePtr>("fmi2ExitInitializationMode");
            fmi2GetReal = GetSymbol<fmi2GetRealPtr>("fmi2GetReal");
            fmi2SetReal = GetSymbol<fmi2SetRealPtr>("fmi2SetReal");
            fmi2Terminate = GetSymbol<fmi2TerminatePtr>("fmi2Terminate");
            fmi2FreeInstance = GetSymbol<fmi2FreeInstancePtr>("fmi2FreeInstance");

            // Check if any function failed to load
            return fmi2Instantiate && fmi2DoStep && fmi2SetupExperiment &&
                fmi2EnterInitializationMode && fmi2ExitInitializationMode &&
                fmi2GetReal && fmi2SetReal && fmi2Terminate && fmi2FreeInstance;
        };
    };

} // namespace PROXSIMA
