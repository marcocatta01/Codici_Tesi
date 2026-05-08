#include "Sensors/IMUSensor.h"
#include "PROXSIMAGameInstance.h"
#include "Subsystems/FMUSimulationSubsystem.h"
#include "WebSocketManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFilemanager.h"
#include "Engine/World.h"

AIMUSensor::AIMUSensor()
{
    PrimaryActorTick.bCanEverTick = true;
    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);
}

void AIMUSensor::BeginPlay()
{
    Super::BeginPlay();

    // Calcola SampleInterval da RateHz (impostato in Initialize)
    SampleInterval = RateHz > 0.0f ? 1.0 / RateHz : 0.0;
    MinStreamingInterval = SampleInterval;

    // Reset storici
    countPos = 0;
    countVel = 0;
    countOmega = 0;
    PrevWorldQuat = FQuat::Identity;

    LastSampleSimTime = GetCurrentSimTime();
    LastStamp = LastSampleSimTime;
    NextSampleTime = -1.0; // verrà inizializzato alla prima OnFmuStep

    // FMU step callback
    if (UPROXSIMAGameInstance* GI = Cast<UPROXSIMAGameInstance>(GetGameInstance()))
    {
        if (GI->GetCurrentSimulationMode() == ESimulationMode::SM_FMU)
        {
            if (UFMUSimulationSubsystem* FMU = GI->GetSubsystem<UFMUSimulationSubsystem>())
            {
                const double Step = static_cast<double>(FMU->GetFixedTimeStep());
                if (Step > 0.0)
                {
                    CachedFmuStep = Step; // solo per debug/telemetria
                }

                FMU->OnFmuStep.AddUObject(this, &AIMUSensor::OnFmuStep);
                bUseFmuCallback = true;
            }
        }
    }
}

void AIMUSensor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (bUseFmuCallback)
    {
        if (UPROXSIMAGameInstance* GI = Cast<UPROXSIMAGameInstance>(GetGameInstance()))
        {
            if (UFMUSimulationSubsystem* FMU = GI->GetSubsystem<UFMUSimulationSubsystem>())
            {
                FMU->OnFmuStep.RemoveAll(this);
            }
        }
        bUseFmuCallback = false;
    }

    Super::EndPlay(EndPlayReason);
}

void AIMUSensor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    UPROXSIMAGameInstance* GI = Cast<UPROXSIMAGameInstance>(GetGameInstance());
    if (!bEnabled || (GI && !GI->GetIsSimulating()))
    {
        return;
    }

    // In FMU mode la logica è tutta nella callback degli step
    if (bUseFmuCallback)
    {
        return;
    }

    // Modalità non-FMU
    Update_Implementation(DeltaTime);
}

bool AIMUSensor::Initialize_Implementation(const FString& InSensorName, const FString& InFrameName,
    const FTransform& InRelativeTransform,
    const TMap<FString, FString>& Parameters)
{
    SensorName = InSensorName;
    FrameName = InFrameName;
    RelativeTransform = InRelativeTransform;

    if (Parameters.Contains(TEXT("rateHz"))) { RateHz = FCString::Atof(*Parameters[TEXT("rateHz")]); }
    if (RateHz <= 0.0f)
    {
        UE_LOG(LogTemp, Error, TEXT("IMU '%s': rateHz deve essere > 0 (valore letto: %f)"), *SensorName, RateHz);
        return false;
    }
    SampleInterval = 1.0 / RateHz;
    MinStreamingInterval = SampleInterval;

    if (Parameters.Contains(TEXT("includeGravity"))) { bIncludeGravity = Parameters[TEXT("includeGravity")].ToBool(); }
    if (Parameters.Contains(TEXT("accelNoiseStd"))) { AccelNoiseStd = FCString::Atof(*Parameters[TEXT("accelNoiseStd")]); }
    if (Parameters.Contains(TEXT("gyroNoiseStd"))) { GyroNoiseStd = FCString::Atof(*Parameters[TEXT("gyroNoiseStd")]); }
    if (Parameters.Contains(TEXT("slowmoFact"))) { SlowmoFactor = FCString::Atof(*Parameters[TEXT("slowmoFact")]); }

    if (Parameters.Contains(TEXT("accelBias")))
    {
        TArray<FString> P; Parameters[TEXT("accelBias")].ParseIntoArray(P, TEXT(","), true);
        if (P.Num() >= 3) AccelBias = FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]));
    }
    if (Parameters.Contains(TEXT("gyroBias")))
    {
        TArray<FString> P; Parameters[TEXT("gyroBias")].ParseIntoArray(P, TEXT(","), true);
        if (P.Num() >= 3) GyroBias = FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]));
    }

    if (Parameters.Contains(TEXT("accelBiasRW")))
    {
        TArray<FString> P; Parameters[TEXT("accelBiasRW")].ParseIntoArray(P, TEXT(","), true);
        if (P.Num() >= 3) AccelBiasRW = FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]));
    }

    if (Parameters.Contains(TEXT("gyroBiasRW")))
    {
        TArray<FString> P; Parameters[TEXT("gyroBiasRW")].ParseIntoArray(P, TEXT(","), true);
        if (P.Num() >= 3) GyroBiasRW = FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]));
    }

    if (Parameters.Contains(TEXT("requireClientConnection")))
    {
        bRequireClientConnection = Parameters[TEXT("requireClientConnection")].ToBool();
    }

    UE_LOG(LogTemp, Log, TEXT("[IMU %s] rateHz=%.3f (Ts=%.6f) slowmo=%.3f includeGravity=%s"),
        *SensorName, RateHz, SampleInterval, SlowmoFactor, bIncludeGravity ? TEXT("true") : TEXT("false"));

    return true;
}

void AIMUSensor::Update_Implementation(float DeltaTime)
{
    // Non-FMU: rispetta rateHz con accumulatore nel tempo di gioco
    if (bUseFmuCallback || SampleInterval <= 0.0)
    {
        return;
    }

    Accumulator += DeltaTime;
    while (Accumulator + 1e-12 >= SampleInterval)
    {
        Accumulator -= SampleInterval;

        const float dt = FMath::Max(1e-6f, static_cast<float>(SampleInterval));
        ComputeImuSample(dt);

        LastStamp = GetCurrentSimTime();
        LastSampleSimTime = LastStamp;

        if (bSaveCsv && !OutputDir.IsEmpty())
        {
            SaveSensorData_Implementation();
        }

        if (bStream)
        {
            StreamLatestSample();
        }
    }
}

void AIMUSensor::OnFmuStep(double SimTime, double /*StepSeconds*/)
{
    if (!bEnabled || SampleInterval <= 0.0)
        return;

    // Inizializza il primo tempo di campionamento al PRIMO multiplo utile >= SimTime
    if (NextSampleTime < 0.0)
    {
        // k = ceil(SimTime / Ts); Next = k * Ts
        const int32 k = FMath::CeilToInt(SimTime / SampleInterval);
        NextSampleTime = static_cast<double>(k) * SampleInterval;
    }

    // Rilascia uno o più campioni fino a raggiungere l'istante corrente
    while (SimTime + 1e-12 >= NextSampleTime)
    {
        const float dt = FMath::Max(1e-6f, static_cast<float>(SampleInterval));
        ComputeImuSample(dt);

        LastStamp = NextSampleTime;
        LastSampleSimTime = NextSampleTime;

        if (bSaveCsv && !OutputDir.IsEmpty())
        {
            SaveSensorData_Implementation();
        }

        if (bStream)
        {
            StreamLatestSample();
        }

        NextSampleTime += SampleInterval;
    }
}

FVector AIMUSensor::RandNormalVec3(const FVector& Std)
{
    return FVector(
        RandNormal(0.0, Std.X),
        RandNormal(0.0, Std.Y),
        RandNormal(0.0, Std.Z)
    );
}

void AIMUSensor::ComputeImuSample(float dt)
{
    const FTransform T = GetActorTransform();

    const FVector PosUEcm = T.GetLocation();
    const double Px = static_cast<double>(PosUEcm.X) / 100.0;
    const double Py = static_cast<double>(PosUEcm.Y) / 100.0;
    const double Pz = static_cast<double>(PosUEcm.Z) / 100.0;

    // Quaternione world
    FQuat Qw = T.GetRotation();
    if ((PrevWorldQuat | Qw) < 0.f)
    {
        Qw.X = -Qw.X; 
        Qw.Y = -Qw.Y; 
        Qw.Z = -Qw.Z; 
        Qw.W = -Qw.W;
    }

    // --- ACCELERAZIONE (in double) ---
    double Ax = 0.0, Ay = 0.0, Az = 0.0;

    const double dt_d = static_cast<double>(dt);

    if (countPos == 0)
    {
        Ax = Ay = Az = 0.0;
    }
    else if (countPos == 1)
    {
        Ax = Ay = Az = 0.0;
    }
    else
    {
        Ax = (Px - 2.0 * historyPos_n1_X + historyPos_n2_X) / (dt_d * dt_d);
        Ay = (Py - 2.0 * historyPos_n1_Y + historyPos_n2_Y) / (dt_d * dt_d);
        Az = (Pz - 2.0 * historyPos_n1_Z + historyPos_n2_Z) / (dt_d * dt_d);
    }

    // Aggiorna storia posizioni (double)
    historyPos_n2_X = historyPos_n1_X; historyPos_n2_Y = historyPos_n1_Y; historyPos_n2_Z = historyPos_n1_Z;
    historyPos_n1_X = Px; historyPos_n1_Y = Py; historyPos_n1_Z = Pz;
    countPos = FMath::Min(countPos + 1, 2);

    // Converti risultati in FVector (float) per uso successivo
    FVector AccWorld(static_cast<float>(Ax), static_cast<float>(Ay), static_cast<float>(Az));

    // --- VELOCITA' ANGOLARE ---
    FVector OmegaWorld = FVector::ZeroVector;

    if (countOmega > 0)
    {
        FQuat dQ = PrevWorldQuat.Inverse() * Qw;
        dQ.Normalize();

        double Angle; FVector Axis;
        dQ.ToAxisAndAngle(Axis, Angle);
        OmegaWorld = Axis * static_cast<float>(Angle / dt);
    }
    PrevWorldQuat = Qw;
    countOmega = FMath::Min(countOmega + 1, 1);

    // --- GRAVITA' ---
    const float gZ = GetWorld() ? (GetWorld()->GetGravityZ() / 100.0f) : -3.72f;
    const FVector GravityWorld(0.0f, 0.0f, gZ);

    // --- TRASFORMAZIONE WORLD -> BODY ---
    const float s = SlowmoFactor;

    // ============================================
    // VALORI nel body frame UE (LH)
    // ============================================
    const FVector AccBodyClean_UE = Qw.UnrotateVector(AccWorld) * (s * s) -
        (bIncludeGravity ? Qw.UnrotateVector(GravityWorld) : FVector::ZeroVector);

    const FVector OmegaBodyClean_UE = Qw.UnrotateVector(OmegaWorld) * s;

    // ============================================
    // CONVERSIONE LH → RH (flip Y)
    // ============================================
    FVector AccBodyClean_RH(AccBodyClean_UE.X, -AccBodyClean_UE.Y, AccBodyClean_UE.Z);
    FVector OmegaBodyClean_RH(OmegaBodyClean_UE.X, -OmegaBodyClean_UE.Y, OmegaBodyClean_UE.Z);

    // ============================================
    // BIAS RANDOM WALK (già in RH dal JSON)
    // ============================================
    const FVector stdIncrA = AccelBiasRW * FMath::Sqrt(dt);
    AccelBias += RandNormalVec3(stdIncrA);

    const FVector stdIncrG = GyroBiasRW * FMath::Sqrt(dt);
    GyroBias += RandNormalVec3(stdIncrG);

    // ============================================
    // MEASUREMENT NOISE (generato in RH)
    // ============================================
    const FVector NoiseA = RandNormalVec3(FVector(AccelNoiseStd));
    const FVector NoiseG = RandNormalVec3(FVector(GyroNoiseStd));

    // ============================================
    // OUTPUT FINALE = pulito(RH) + bias(RH) + noise(RH)
    // ============================================
    FVector NewAccelBody = AccBodyClean_RH + AccelBias + NoiseA;
    FVector NewGyroBody = OmegaBodyClean_RH + GyroBias + NoiseG;

    AccelBody = IsFinite(NewAccelBody) ? NewAccelBody : FVector::ZeroVector;
    GyroBody = IsFinite(NewGyroBody) ? NewGyroBody : FVector::ZeroVector;
}

double AIMUSensor::GetCurrentSimTime() const
{
    UPROXSIMAGameInstance* GI = Cast<UPROXSIMAGameInstance>(GetGameInstance());
    if (GI && GI->GetCurrentSimulationMode() == ESimulationMode::SM_FMU)
    {
        if (UFMUSimulationSubsystem* FMU = GI->GetSubsystem<UFMUSimulationSubsystem>())
        {
            return FMU->GetSimulationTime();
        }
    }
    return GetWorld() ? GetWorld()->TimeSeconds : 0.0;
}

double AIMUSensor::RandNormal(double Mean, double StdDev)
{
    if (StdDev <= 0.0) return Mean;
    double U1 = FMath::FRand();
    double U2 = FMath::FRand();
    U1 = FMath::Clamp(U1, SMALL_NUMBER, 1.0 - SMALL_NUMBER);
    double R = FMath::Sqrt(-2.0 * FMath::Loge(U1));
    double Theta = 2.0 * PI * U2;
    return Mean + StdDev * R * FMath::Cos(Theta);
}

void AIMUSensor::SetEnabled_Implementation(bool bInEnabled)
{
    bEnabled = bInEnabled;
    SetActorHiddenInGame(!bEnabled);
    SetActorTickEnabled(bEnabled);
}

bool AIMUSensor::IsEnabled_Implementation() const
{
    return bEnabled;
}

FString AIMUSensor::GetSensorName_Implementation() const
{
    return SensorName;
}

FString AIMUSensor::GetFrameName_Implementation() const
{
    return FrameName;
}

FTransform AIMUSensor::GetRelativeTransform_Implementation() const
{
    return RelativeTransform;
}

void AIMUSensor::SetOutputPath_Implementation(const FString& Path)
{
    if (Path.IsEmpty())
    {
        bSaveCsv = false;
        OutputDir.Empty();
        CsvFilePath.Empty();
        return;
    }

    FString Base = Path;
    if (FPaths::IsRelative(Base))
    {
        Base = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Base);
    }

    const FString Stamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S"));
    OutputDir = FPaths::Combine(Base, Stamp);
    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
    PF.CreateDirectoryTree(*OutputDir);

    CsvFilePath = FPaths::Combine(OutputDir, FString::Printf(TEXT("%s.csv"), *SensorName));
    bCsvHeaderWritten = false;
    bSaveCsv = true;
}

bool AIMUSensor::SaveSensorData_Implementation()
{
    if (!bSaveCsv || CsvFilePath.IsEmpty()) return false;
    EnsureOutputCsv();
    AppendCsvLine(LastStamp, AccelBody, GyroBody);
    return true;
}

bool AIMUSensor::HasDataToSave_Implementation() const
{
    return bSaveCsv && !CsvFilePath.IsEmpty();
}

void AIMUSensor::SetStreamingEnabled_Implementation(bool bInEnabled, int32 /*Port*/)
{
    if (bStream == bInEnabled)
        return;

    bStream = bInEnabled;

    if (bStream)
    {
        UPROXSIMAGameInstance* GameInstance = Cast<UPROXSIMAGameInstance>(GetGameInstance());
        if (GameInstance)
        {
            UWebSocketManager* WS = GameInstance->GetSubsystem<UWebSocketManager>();
            if (IsValid(WS) && WS->IsRunning())
            {
                WS->RegisterSensorEndpoint(SensorName, GetSensorType_Implementation());
                StreamingURL = WS->GetSensorWebSocketURL(SensorName);

                if (!OutputDir.IsEmpty())
                {
                    WS->SaveHTMLViewer(SensorName, GetSensorType_Implementation(), OutputDir);
                }

                UE_LOG(LogTemp, Display, TEXT("IMU %s streaming enabled at %s"), *SensorName, *StreamingURL);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("WebSocket server not running for IMU %s"), *SensorName);
                bStream = false;
            }
        }
    }
    else
    {
        StreamingURL.Empty();
        UE_LOG(LogTemp, Display, TEXT("IMU %s streaming disabled"), *SensorName);
    }
}

bool AIMUSensor::IsStreamingEnabled_Implementation() const
{
    return bStream;
}

FString AIMUSensor::GetStreamingURL_Implementation() const
{
    return StreamingURL;
}

bool AIMUSensor::RequiresClientConnection_Implementation() const
{
    return bRequireClientConnection;
}

void AIMUSensor::EnsureOutputCsv()
{
    if (bCsvHeaderWritten) return;
    const FString Header = TEXT("timestamp,ax,ay,az,gx,gy,gz\n");
    FFileHelper::SaveStringToFile(Header, *CsvFilePath);
    bCsvHeaderWritten = true;
}

void AIMUSensor::AppendCsvLine(double Timestamp, const FVector& AccRH, const FVector& GyroRH)
{
    const double t_sim = Timestamp;
    const double t_real = t_sim / SlowmoFactor;

    const FString Line = FString::Printf(
        TEXT("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n"),
        t_real,
        AccRH.X, AccRH.Y, AccRH.Z,
        GyroRH.X, GyroRH.Y, GyroRH.Z
    );

    FFileHelper::SaveStringToFile(
        Line, *CsvFilePath,
        FFileHelper::EEncodingOptions::AutoDetect,
        &IFileManager::Get(),
        FILEWRITE_Append
    );
}

void AIMUSensor::StreamLatestSample()
{
    UPROXSIMAGameInstance* GameInstance = Cast<UPROXSIMAGameInstance>(GetGameInstance());
    if (!GameInstance) return;

    UWebSocketManager* WS = GameInstance->GetSubsystem<UWebSocketManager>();
    if (!IsValid(WS) || WS->GetSensorClientCount(SensorName) == 0)
        return;

    double Sim_Time = LastStamp;
    if (MinStreamingInterval > 0.0 && LastStreamedTime >= 0.0 &&
        (Sim_Time - LastStreamedTime) + 1e-12 < MinStreamingInterval * 0.5)
    {
        return;
    }

    LastStreamedTime = Sim_Time;

    FString Json = FString::Printf(
        TEXT("{\"sensor\":\"%s\",\"type\":\"IMU\",\"t\":%.6f,\"ax\":%.6f,\"ay\":%.6f,\"az\":%.6f,\"gx\":%.6f,\"gy\":%.6f,\"gz\":%.6f}"),
        *SensorName, Sim_Time,
        AccelBody.X, AccelBody.Y, AccelBody.Z,
        GyroBody.X, GyroBody.Y, GyroBody.Z);

    TArray<uint8> Bytes;
    Bytes.Append(reinterpret_cast<const uint8*>(TCHAR_TO_UTF8(*Json)), strlen(TCHAR_TO_UTF8(*Json)));

    WS->SendSensorData(SensorName, Bytes, Sim_Time);
}