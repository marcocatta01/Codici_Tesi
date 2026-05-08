#include "FileDialogHelper.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "Framework/Application/SlateApplication.h"

FString UFileDialogHelper::OpenFileDialog(const FString &DialogTitle, const FString &FileTypes)
{
    FString SelectedFilePath = "";
    IDesktopPlatform *DesktopPlatform = FDesktopPlatformModule::Get();
    if (DesktopPlatform)
    {
        TArray<FString> OutFiles;
        const bool bFileSelected = DesktopPlatform->OpenFileDialog(
            FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
            DialogTitle,
            FPaths::ProjectDir(),   // Default directory
            "",                     // Default file
            FileTypes,              // File types filter
            EFileDialogFlags::None, // Flags
            OutFiles);

        if (bFileSelected && OutFiles.Num() > 0)
        {
            SelectedFilePath = OutFiles[0];
        }
    }
    return SelectedFilePath;
}