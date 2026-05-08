#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Sensors/SensorsTypes.h"
#include "SequenceManager.generated.h"

/**
 * Manages camera sequence playback and interpolation
 */
UCLASS()
class PROXSIMA_API USequenceManager : public UObject
{
    GENERATED_BODY()

public:
    /** Delegate fired when all sequence images are captured */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSequenceComplete);

    /** Event triggered when sequence is complete */
    UPROPERTY(BlueprintAssignable, Category = "Camera Sequence")
    FOnSequenceComplete OnSequenceComplete;

    USequenceManager();

    /**
     * Load a sequence of camera poses from a JSON file
     * @param SequencePath Path to the camera poses JSON file
     * @param ValidCameraNames List of valid camera names for validation
     */
    UFUNCTION(BlueprintCallable, Category = "Camera Sequence")
    bool LoadSequence(const FString &SequencePath, const TArray<FString>& ValidCameraNames);

    /**
     * Get all poses in the sequence
     */
    UFUNCTION(BlueprintCallable, Category = "Camera Sequence")
    const TArray<FSerializedCameraPose> &GetAllPoses() const { return AllPoses; }

    /**
     * Get poses for a specific camera
     */
    UFUNCTION(BlueprintCallable, Category = "Camera Sequence")
    TArray<FSerializedCameraPose> GetPosesForCamera(const FString &CameraName) const;

    /**
     * Reset the sequence manager
     */
    UFUNCTION(BlueprintCallable, Category = "Camera Sequence")
    void Reset() { CurrentTime = 0.0f; }

    /** Report when an image has been captured */
    UFUNCTION()
    void ReportImageCaptured();

private:
    /** All camera poses in the sequence */
    UPROPERTY()
    TArray<FSerializedCameraPose> AllPoses;

    /** Current time in the sequence */
    UPROPERTY()
    float CurrentTime;

    /** Total number of poses in sequence */
    UPROPERTY()
    int32 TotalPoses;

    /** Number of images captured so far */
    UPROPERTY()
    int32 CapturedCount;
};
