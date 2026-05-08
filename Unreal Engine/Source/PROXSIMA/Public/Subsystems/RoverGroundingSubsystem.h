#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FMUConfiguration.h"
#include "RoverGroundingSubsystem.generated.h"

/**
 * Configurazione singola ruota:
 * - Name: nome ruota (facoltativo)
 * - LocalOffsetCm: offset locale (in cm) del centro ruota rispetto al frame base (UE world, LH)
 * - RadiusCm: raggio ruota (in cm)
 */
USTRUCT()
struct FWheelGroundingConfig
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FVector LocalOffsetCm = FVector::ZeroVector;

	UPROPERTY()
	float RadiusCm = 0.0f;
};

/**
 * Subsystem per pre-processare la traiettoria del rover in funzione del terreno.
 * Opera sui dati già caricati nel FMUInputHandlingSubsystem:
 * - legge r_rov[1..3] (FMU: metri, RH) e q_rov[1..4] (FMU quaternion RH)
 * - per ogni timestamp, CONVERTE temporaneamente in UE world (cm, LH) per tracing/assetto,
 *   proietta le ruote a terra, costruisce i triangoli, seleziona il piano secondo le regole A–E
 * - calcola la Z del baricentro e l’assetto (quaternion) coerente col piano + yaw (UE world)
 * - RICONVERTE i risultati in convenzione FMU (metri RH) e riscrive le timeseries di r_rov[3] e q_rov[1..4]
 *
 * Nota: questo avviene PRIMA dell’avvio della simulazione (virtualmente), in OnMeshProcessingComplete.
 */
UCLASS()
class PROXSIMA_API URoverGroundingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Configura ruote e frame base dai visualComponents della configurazione */
	void ConfigureFromConfiguration(const FFMUConfiguration& Config);

	/** True se configurazione valida (>=3 ruote e frame base noto) */
	bool HasValidSetup() const;

	/** Nome del frame base rilevato (se disponibile) */
	FString GetBaseFrameName() const { return BaseFrameName; }

	/**
	 * Preprocessa le timeseries di input del rover:
	 * - legge r_rov[1], r_rov[2], r_rov[3], q_rov[1..4] (FMU, metri RH / quat RH)
	 * - converte in UE world (cm LH) per calcolo del grounding
	 * - applica grounding per ciascun campione
	 * - riconverte in FMU (metri RH / quat RH)
	 * - aggiorna r_rov[3] e q_rov[1..4] nel FMUInputHandlingSubsystem
	 *
	 * Va chiamata prima dell’avvio, ad esempio in OnMeshProcessingComplete.
	 */
	bool PreprocessInputTimeseries(UWorld* World);

	/**
	 * Applica grounding a una traiettoria: input in CONVENZIONE FMU (metri RH, quat RH).
	 * Internamente converte a UE (cm LH), calcola Z e rotazione coerente col piano,
	 * poi ritorna i quaternioni corretti in convenzione FMU (RH). Aggiorna le posizioni (Z) in metri FMU.
	 */
	bool ApplyGroundingToTimeseries_FMU(
		UWorld* World,
		const TArray<double>& InTimes,
		TArray<FVector>& InOutPositionsMeters_FMU,
		const TArray<FQuat>& InRotations_FMU,
		TArray<FQuat>& OutRotations_FMU
	);

private:
	// Helpers principali (necessari al pre-process) - lavorano in UE world (cm, LH)
	bool BuildWheelTargetsAtXY(
		UWorld* World,
		const FVector2D& ProposedXYcm,
		float RoverYawDeg,
		TArray<FVector>& OutWheelWorldTargetsCm,
		float& OutBestPlaneZAtCM,
		FVector& OutBestPlaneNormal,
		const FVector* PrevPlaneNormalOpt) const;

	bool TraceGroundZ(UWorld* World, const FVector& XYcm, float& OutZcm) const;

	static bool PointInTriangle2D(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C);

	static bool EvaluatePlaneAtXY(const FVector& A, const FVector& B, const FVector& C, const FVector2D& XY, float& OutZ, FVector& OutNormal);

	/** Costruisce rotazione world coerente con normal del piano e yaw (keep heading) - UE world */
	static FQuat MakeRotationFromNormalAndYaw(const FVector& Normal, float YawDegrees);

private:
	// Conversion helpers tra FMU (RH metri) e UE (LH cm)
	static FVector ConvertPos_FMU_to_UEcm(const FVector& PosMetersFMU);
	static FVector ConvertPos_UEcm_to_FMU(const FVector& PosCmUE);
	static FQuat   ConvertQuat_FMU_to_UE(const FQuat& QuatFMU);
	static FQuat   ConvertQuat_UE_to_FMU(const FQuat& QuatUE);

private:
	// Stato/configurazione
	UPROPERTY()
	TArray<FWheelGroundingConfig> Wheels;

	UPROPERTY()
	FString BaseFrameName;

	UPROPERTY()
	bool bEnabled = true;

	// Parametri tracing (cm)
	UPROPERTY(EditAnywhere, Category="RoverGrounding|Params")
	float TraceAboveCm = 10000.0f;

	UPROPERTY(EditAnywhere, Category="RoverGrounding|Params")
	float TraceBelowCm = 1000.0f;

	UPROPERTY(EditAnywhere, Category="RoverGrounding|Params")
	float SlopeProbeDistCm = 10.0f; // usato internamente per stabilità del piano

	UPROPERTY(EditAnywhere, Category="RoverGrounding|Params")
	float PlaneMatchAngleToleranceDeg = 1.0f; // tolleranza per matching piano precedente
};