#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FileDialogHelper.generated.h"

UCLASS()
class PROXSIMA_API UFileDialogHelper : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "File Dialog")
    static FString OpenFileDialog(const FString &DialogTitle, const FString &FileTypes);
};