#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sensors/SensorInterface.h"
#include "IMUSensor.generated.h"

UCLASS()
class PROXSIMA_API AIMUSensor : public AActor, public ISensorInterface
{
    GENERATED_BODY()

public:
    AIMUSensor();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaTime) override;

    // ISensorInterface
    virtual bool Initialize_Implementation(const FString& InSensorName, const FString& InFrameName,
        const FTransform& InRelativeTransform,
        const TMap<FString, FString>& Parameters) override;
    virtual void Update_Implementation(float DeltaTime) override;
    virtual void SetEnabled_Implementation(bool bInEnabled) override;
    virtual bool IsEnabled_Implementation() const override;
    virtual FString GetSensorName_Implementation() const override;
    virtual FString GetSensorType_Implementation() const override { return TEXT("IMU"); }
    virtual FString GetFrameName_Implementation() const override;
    virtual FTransform GetRelativeTransform_Implementation() const override;
    virtual void SetOutputPath_Implementation(const FString& Path) override;
    virtual bool SaveSensorData_Implementation() override;
    virtual bool HasDataToSave_Implementation() const override;
    virtual void SetStreamingEnabled_Implementation(bool bEnabled, int32 Port) override;
    virtual bool IsStreamingEnabled_Implementation() const override;
    virtual FString GetStreamingURL_Implementation() const override;
    virtual bool RequiresClientConnection_Implementation() const override;

private:
    // Config
    FString SensorName;
    FString FrameName;
    FTransform RelativeTransform;

    bool bEnabled = true;
    bool bIncludeGravity = true;
    bool bSaveCsv = false;
    bool bStream = false;
    bool bRequireClientConnection = false;

    float RateHz = 100.0f;
    float AccelNoiseStd = 0.0f;
    float GyroNoiseStd = 0.0f;
    float SlowmoFactor = 5.0f;
    FVector AccelBias = FVector::ZeroVector;
    FVector GyroBias = FVector::ZeroVector;
    FVector AccelBiasRW = FVector::ZeroVector; // m/s^2 / sqrt(Hz)
    FVector GyroBiasRW = FVector::ZeroVector; // rad/s  / sqrt(Hz)

    // Output
    FString OutputDir;
    FString CsvFilePath;
    bool bCsvHeaderWritten = false;

    // Streaming
    FString StreamingURL;
    double LastStreamedTime = -1.0;
    double MinStreamingInterval = 0.0; // = SampleInterval

    // Sampling
    double LastSampleSimTime = -1.0;
    double Accumulator = 0.0;
    double SampleInterval = 0.01;
    double NextSampleTime = -1.0; // prossimo multiplo di SampleInterval in FMU mode

    // State
    FQuat   PrevWorldQuat = FQuat::Identity;

    // Finite-difference histories (lineare, in metri/m/s)
    int32   countPos = 0;
    int32   countVel = 0;
    int32   countOmega = 0;

    // History buffers using double for better precision with large coordinates
    double historyPos_n1_X = 0.0, historyPos_n1_Y = 0.0, historyPos_n1_Z = 0.0;
    double historyPos_n2_X = 0.0, historyPos_n2_Y = 0.0, historyPos_n2_Z = 0.0;

    FVector historyPos_n1 = FVector::ZeroVector; // x_{n-1} [m]
    FVector historyPos_n2 = FVector::ZeroVector; // x_{n-2} [m]
    FVector historyVel_n1 = FVector::ZeroVector; // v_{n-1} [m/s]
    FVector historyVel_n2 = FVector::ZeroVector; // v_{n-2} [m/s]

    // Latest measurement
    FVector AccelBody = FVector::ZeroVector;
    FVector GyroBody = FVector::ZeroVector;
    double  LastStamp = 0.0;

    // FMU step callback
    void OnFmuStep(double SimTime, double StepSeconds);
    bool   bUseFmuCallback = false;
    double CachedFmuStep = 0.0;

    // Helpers
    double GetCurrentSimTime() const;
    bool   ShouldSample(double CurrentTime, float DeltaTime);
    void   ComputeImuSample(float dt);

    // Box-Muller Gaussian helper (private)
    static double RandNormal(double Mean, double StdDev);

    // NEW: helper per vettore di gaussiane (usa RandNormal interno)
    static FVector RandNormalVec3(const FVector& Std);

    void EnsureOutputCsv();
    void AppendCsvLine(double Timestamp, const FVector& Accel, const FVector& Gyro);

    void StreamLatestSample(); // invio via WebSocket

    static FORCEINLINE bool IsFinite(const FVector& V)
    {
        return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
    }
};