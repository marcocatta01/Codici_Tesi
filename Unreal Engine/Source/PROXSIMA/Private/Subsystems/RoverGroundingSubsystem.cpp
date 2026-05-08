#include "Subsystems/RoverGroundingSubsystem.h"
#include "Engine/World.h"
#include "Kismet/KismetMathLibrary.h"
#include "Subsystems/FMUSimulationSubsystem.h"
#include "Subsystems/FMUInputHandlingSubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

void URoverGroundingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void URoverGroundingSubsystem::Deinitialize()
{
	Wheels.Empty();
	Super::Deinitialize();
}

void URoverGroundingSubsystem::ConfigureFromConfiguration(const FFMUConfiguration& Config)
{
	Wheels.Empty();
	FString DetectedBaseFrame;

	// Individua ruote dai visualComponents
	for (const FFMUVisualComponent& VC : Config.visualComponents)
	{
		const bool bIsWheel = VC.name.Contains(TEXT("wheel"), ESearchCase::IgnoreCase);
		if (!bIsWheel) continue;

		float RadiusMeters = 0.0f;
		if (const float* R = VC.dimensions.Find(TEXT("radius")))
		{
			RadiusMeters = *R;
		}

		FWheelGroundingConfig W;
		W.Name = VC.name;
		// VC.rigidTransform è già in cm e asse Y flippato in ParseVisualComponentsConfiguration
		W.LocalOffsetCm = VC.rigidTransform.GetLocation();
		W.RadiusCm = RadiusMeters * 100.0f;
		Wheels.Add(W);

		if (!VC.frameName.IsEmpty() && DetectedBaseFrame.IsEmpty())
		{
			DetectedBaseFrame = VC.frameName;
		}
	}

	if (!DetectedBaseFrame.IsEmpty())
	{
		BaseFrameName = DetectedBaseFrame;
	}

	// Abilita solo se almeno 3 ruote
	bEnabled = (Wheels.Num() >= 3);
}

bool URoverGroundingSubsystem::HasValidSetup() const
{
	return bEnabled && !BaseFrameName.IsEmpty() && Wheels.Num() >= 3;
}

bool URoverGroundingSubsystem::PreprocessInputTimeseries(UWorld* World)
{
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[Grounding] World non valido"));
		return false;
	}
	if (!HasValidSetup())
	{
		UE_LOG(LogTemp, Error, TEXT("[Grounding] Setup non valido (min 3 ruote + frame base)"));
		return false;
	}

	UGameInstance* GI = World->GetGameInstance();
	if (!GI)
	{
		UE_LOG(LogTemp, Error, TEXT("[Grounding] GameInstance non disponibile"));
		return false;
	}

	UFMUInputHandlingSubsystem* InputSubsystem = GI->GetSubsystem<UFMUInputHandlingSubsystem>();
	if (!InputSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("[Grounding] FMUInputHandlingSubsystem non disponibile"));
		return false;
	}

	// Variabili attese (da adattare in base alle convenzioni usate)
	const FString RxVar = TEXT("r_rov[1]");
	const FString RyVar = TEXT("r_rov[2]");
	const FString RzVar = TEXT("r_rov[3]");
	const FString QxVar = TEXT("q_rov[1]");
	const FString QyVar = TEXT("q_rov[2]");
	const FString QzVar = TEXT("q_rov[3]");
	const FString QwVar = TEXT("q_rov[4]");

	// Ricava dominio temporale da una delle sorgenti timeseries disponibili
	FFMUInputVariableConfig VarRx, VarRy, VarRz;
	if (!InputSubsystem->FindInputVariable(RxVar, VarRx) ||
		!InputSubsystem->FindInputVariable(RyVar, VarRy) ||
		!InputSubsystem->FindInputVariable(RzVar, VarRz))
	{
		UE_LOG(LogTemp, Error, TEXT("[Grounding] Variabili r_rov[*] non trovate"));
		return false;
	}

	TArray<double> Times;
	auto ExtractTimesFromVar = [&Times](const FFMUInputVariableConfig& V) -> bool
	{
		for (const FFMUInputSource& S : V.inputSources)
		{
			if (S.sourceType == EFMUInputSourceType::Timeseries && S.timeseries.timeValues.Num() > 0)
			{
				Times.Reserve(S.timeseries.timeValues.Num());
				for (const auto& tv : S.timeseries.timeValues)
				{
					Times.Add(tv.time);
				}
				return true;
			}
		}
		return false;
	};

	if (!ExtractTimesFromVar(VarRx))
	{
		if (!ExtractTimesFromVar(VarRy) && !ExtractTimesFromVar(VarRz))
		{
			UE_LOG(LogTemp, Error, TEXT("[Grounding] Nessuna timeseries disponibile per ricavare il dominio temporale"));
			return false;
		}
	}

	// Ricostruisci posizioni (FMU: metri RH) e rotazioni (FMU: quat RH) per ogni campione
	TArray<FVector> PosMeters_FMU;
	TArray<FQuat> RotQuats_FMU;
	PosMeters_FMU.SetNum(Times.Num());
	RotQuats_FMU.SetNum(Times.Num());

	for (int32 i = 0; i < Times.Num(); ++i)
	{
		const double t = Times[i];
		double vx=0.0, vy=0.0, vz=0.0;
		double qx=0.0, qy=0.0, qz=0.0, qw=1.0;

		InputSubsystem->GetInputValue(RxVar, t, vx);
		InputSubsystem->GetInputValue(RyVar, t, vy);
		InputSubsystem->GetInputValue(RzVar, t, vz);
		InputSubsystem->GetInputValue(QxVar, t, qx);
		InputSubsystem->GetInputValue(QyVar, t, qy);
		InputSubsystem->GetInputValue(QzVar, t, qz);
		InputSubsystem->GetInputValue(QwVar, t, qw);

		PosMeters_FMU[i] = FVector(static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vz));
		RotQuats_FMU[i] = FQuat(static_cast<float>(qx), static_cast<float>(qy), static_cast<float>(qz), static_cast<float>(qw));
	}

	// Applica grounding: converte a UE, calcola, poi riconverte a FMU
	TArray<FQuat> CorrectedQuats_FMU;
	if (!ApplyGroundingToTimeseries_FMU(World, Times, PosMeters_FMU, RotQuats_FMU, CorrectedQuats_FMU))
	{
		UE_LOG(LogTemp, Error, TEXT("[Grounding] ApplyGroundingToTimeseries_FMU fallita"));
		return false;
	}

	// Ricostruisci nuove timeseries per Z (in metri FMU) e quaternion (FMU RH) e scrivile nel subsystem
	// Conserva eventualmente parametri di loop delle serie precedenti
	bool bPrevLoopZ=false; double PrevLoopDurZ=0.0;

	for (const FFMUInputSource& S : VarRz.inputSources)
	{
		if (S.sourceType == EFMUInputSourceType::Timeseries)
		{
			bPrevLoopZ = S.timeseries.bLooping;
			PrevLoopDurZ = S.timeseries.loopDuration;
			break;
		}
	}

	FFMUInputTimeseries TsZ; TsZ.bLooping = bPrevLoopZ; TsZ.loopDuration = PrevLoopDurZ;
	FFMUInputTimeseries TsQx, TsQy, TsQz, TsQw;
	for (int32 i = 0; i < Times.Num(); ++i)
	{
		TsZ.AddTimeValue(Times[i], static_cast<double>(PosMeters_FMU[i].Z)); // metri FMU
		const FQuat& Qfm = CorrectedQuats_FMU.IsValidIndex(i) ? CorrectedQuats_FMU[i] : RotQuats_FMU[i];
		TsQx.AddTimeValue(Times[i], static_cast<double>(Qfm.X));
		TsQy.AddTimeValue(Times[i], static_cast<double>(Qfm.Y));
		TsQz.AddTimeValue(Times[i], static_cast<double>(Qfm.Z));
		TsQw.AddTimeValue(Times[i], static_cast<double>(Qfm.W));
	}
	TsZ.Sort(); TsQx.Sort(); TsQy.Sort(); TsQz.Sort(); TsQw.Sort();

	// Sostituisci le sorgenti esistenti (o creane di nuove se mancanti)
	auto ReplaceOrAdd = [InputSubsystem](const FString& VarName, const FFMUInputVariableConfig& VarCfg, const FFMUInputTimeseries& NewTs, const FString& DefaultSourceId) -> bool
	{
		FString SourceId;
		for (const FFMUInputSource& S : VarCfg.inputSources)
		{
			if (S.sourceType == EFMUInputSourceType::Timeseries)
			{
				SourceId = S.sourceId;
				break;
			}
		}
		if (SourceId.IsEmpty())
		{
			SourceId = DefaultSourceId;
		}

		if (InputSubsystem->LoadInputTimeseries(SourceId, NewTs))
		{
			return true;
		}
		else
		{
			FFMUInputSource NewSrc;
			NewSrc.sourceId = SourceId;
			NewSrc.sourceType = EFMUInputSourceType::Timeseries;
			NewSrc.timeseries = NewTs;
			NewSrc.priority = EFMUInputPriority::Medium;
			NewSrc.bEnabled = true;
			return InputSubsystem->AddInputSource(VarName, NewSrc);
		}
	};

	bool bOkZ = ReplaceOrAdd(RzVar, VarRz, TsZ, FString::Printf(TEXT("grounded_%s"), *RzVar));

	FFMUInputVariableConfig VarQx, VarQy, VarQz, VarQw;
	InputSubsystem->FindInputVariable(QxVar, VarQx);
	InputSubsystem->FindInputVariable(QyVar, VarQy);
	InputSubsystem->FindInputVariable(QzVar, VarQz);
	InputSubsystem->FindInputVariable(QwVar, VarQw);

	bool bOkQx = ReplaceOrAdd(QxVar, VarQx, TsQx, FString::Printf(TEXT("grounded_%s"), *QxVar));
	bool bOkQy = ReplaceOrAdd(QyVar, VarQy, TsQy, FString::Printf(TEXT("grounded_%s"), *QyVar));
	bool bOkQz = ReplaceOrAdd(QzVar, VarQz, TsQz, FString::Printf(TEXT("grounded_%s"), *QzVar));
	bool bOkQw = ReplaceOrAdd(QwVar, VarQw, TsQw, FString::Printf(TEXT("grounded_%s"), *QwVar));

	const bool bAllOk = bOkZ && bOkQx && bOkQy && bOkQz && bOkQw;
	UE_LOG(LogTemp, Log, TEXT("[Grounding] Preprocess: Z+quaternioni aggiornati (%d campioni), esito=%s"),
		TsZ.timeValues.Num(), bAllOk ? TEXT("OK") : TEXT("PARTIAL"));

	// --------------------------------------------------------
	// Esporta traiettoria reale dopo grounding in CSV
	// --------------------------------------------------------
	{
		// Costruiamo il contenuto del CSV in una FString
		FString CsvContent;
		CsvContent.Append(TEXT("Time,r_rov[1],r_rov[2],r_rov[3],q_rov[1],q_rov[2],q_rov[3],q_rov[4]\n"));

		const int32 NumSamples = Times.Num();
		for (int32 i = 0; i < NumSamples; ++i)
		{
			const double t   = Times[i];
			const FVector& P = PosMeters_FMU[i];  // Posizione aggiornata in metri FMU (XY originali, Z grounded)
			const FQuat&  Q  = CorrectedQuats_FMU.IsValidIndex(i)
				? CorrectedQuats_FMU[i]
				: RotQuats_FMU[i];                // Quaternion FMU RH (aggiornato o originale)

			// Time, r_rov[*] a 6 cifre decimali; q_rov[*] a 9
			const FString Line = FString::Printf(
				TEXT("%.6f,%.6f,%.6f,%.6f,%.9f,%.9f,%.9f,%.9f\n"),
				t,
				static_cast<double>(P.X),
				static_cast<double>(P.Y),
				static_cast<double>(P.Z),
				static_cast<double>(Q.X),
				static_cast<double>(Q.Y),
				static_cast<double>(Q.Z),
				static_cast<double>(Q.W)
			);

			CsvContent.Append(Line);
		}

		// Path assoluto richiesto (da modificare)
		const FString CsvPath = TEXT("C:\\Users\\marco\\Desktop\\Thesis\\proxsima\\Saved\\Simulation_Marco\\real_trajectory.csv");

		if (FFileHelper::SaveStringToFile(CsvContent, *CsvPath))
		{
			UE_LOG(LogTemp, Log, TEXT("[Grounding] CSV real_trajectory.csv scritto con %d campioni in: %s"),
				Times.Num(), *CsvPath);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[Grounding] Impossibile scrivere il CSV in: %s"), *CsvPath);
		}
	}

	return bAllOk;
}

bool URoverGroundingSubsystem::ApplyGroundingToTimeseries_FMU(
	UWorld* World,
	const TArray<double>& InTimes,
	TArray<FVector>& InOutPositionsMeters_FMU,
	const TArray<FQuat>& InRotations_FMU,
	TArray<FQuat>& OutRotations_FMU
)
{
	OutRotations_FMU.SetNum(InOutPositionsMeters_FMU.Num());

	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("ApplyGroundingToTimeseries_FMU: World non valido"));
		return false;
	}
  
	if (Wheels.Num() < 3)
	{
		UE_LOG(LogTemp, Error, TEXT("ApplyGroundingToTimeseries_FMU: configurazione ruote non valida (min 3)"));
		return false;
	}
	if (InOutPositionsMeters_FMU.Num() == 0)
	{
		return true;
	}
	if (InRotations_FMU.Num() != InOutPositionsMeters_FMU.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("ApplyGroundingToTimeseries_FMU: rotazioni (%d) != posizioni (%d)"),
			InRotations_FMU.Num(), InOutPositionsMeters_FMU.Num());
	}

	// Normal del piano al passo precedente (UE) per regola D
	bool bHavePrevNormal = false;
	FVector PrevPlaneNormalUE = FVector::UpVector;

	for (int32 i = 0; i < InOutPositionsMeters_FMU.Num(); ++i)
	{
		// FMU -> UE conversion per calcolo (pos: m RH -> cm LH; rot: quat RH -> quat UE)
		const FVector PosUEcm = ConvertPos_FMU_to_UEcm(InOutPositionsMeters_FMU[i]);
		const FQuat   RotUE   = ConvertQuat_FMU_to_UE(InRotations_FMU.IsValidIndex(i) ? InRotations_FMU[i] : FQuat::Identity);

		// XY per trace
		const FVector2D XYcm(PosUEcm.X, PosUEcm.Y);

		// Estrai yaw da rotazione UE corrente
		const float YawDeg = RotUE.Rotator().Yaw;

		// Proietta ruote e costruisci candidati piani
		TArray<FVector> WheelTargetsCm; // UE world
		float PlaneZAtCM = PosUEcm.Z; // Z iniziale in cm UE, verrà rimpiazzata
		FVector PlaneNormalUE = FVector::UpVector;

		const FVector* PrevNPtr = bHavePrevNormal ? &PrevPlaneNormalUE : nullptr;
		BuildWheelTargetsAtXY(World, XYcm, YawDeg, WheelTargetsCm, PlaneZAtCM, PlaneNormalUE, PrevNPtr);

		// Rotazione del frame base coerente col piano e yaw (UE)
		const FQuat RotUE_Corrected = MakeRotationFromNormalAndYaw(PlaneNormalUE, YawDeg);

		// Altezza del baricentro UE = media sui target Z ruote meno quota local-Z ruote (corpo rigido)
		double AccumZcm = 0.0;
		int32 CountZ = 0;
		for (int32 w = 0; w < Wheels.Num(); ++w)
		{
			const float TargetZcm = WheelTargetsCm.IsValidIndex(w) ? WheelTargetsCm[w].Z : PlaneZAtCM;
			const float LocalZcm = RotUE_Corrected.RotateVector(Wheels[w].LocalOffsetCm).Z;
			AccumZcm += (TargetZcm - LocalZcm);
			CountZ++;
		}
		const float ZcmUE = CountZ > 0 ? static_cast<float>(AccumZcm / CountZ) : PlaneZAtCM;

		// UE -> FMU riconversione e scrittura nei buffer di output:
		// Posizione: aggiorna solo Z (in metri FMU), mantenendo X,Y FMU originali
		FVector NewPosUEcm(PosUEcm.X, PosUEcm.Y, ZcmUE);
		const FVector NewPosFMU = ConvertPos_UEcm_to_FMU(NewPosUEcm);
		// Mantieni X,Y dai dati FMU originali, sostituisci Z con nuova Z FMU (per evitare drift dovuto a flip multipli)
		InOutPositionsMeters_FMU[i].Z = NewPosFMU.Z;

		// Rotazione: UE -> FMU (quat RH)
		const FQuat NewRotFMU = ConvertQuat_UE_to_FMU(RotUE_Corrected);
		OutRotations_FMU[i] = NewRotFMU;

		// Aggiorna normal precedente (UE)
		PrevPlaneNormalUE = PlaneNormalUE;
		bHavePrevNormal = true;
	}

	UE_LOG(LogTemp, Log, TEXT("ApplyGroundingToTimeseries_FMU: calcolati Z+quaternioni (FMU) per %d campioni"), InOutPositionsMeters_FMU.Num());
	return true;
}

bool URoverGroundingSubsystem::BuildWheelTargetsAtXY(
	UWorld* World,
	const FVector2D& ProposedXYcm,
	float RoverYawDeg,
	TArray<FVector>& OutWheelWorldTargets,
	float& OutBestPlaneZAtCM,
	FVector& OutBestPlaneNormal,
	const FVector* PrevPlaneNormalOpt) const
{
	OutWheelWorldTargets.Reset();
	OutWheelWorldTargets.SetNum(Wheels.Num());

	const FQuat YawOnly = FRotator(0.f, RoverYawDeg, 0.f).Quaternion();

	// A) Proiezione ruote a terra (verticale) con aggiunta raggio
	for (int32 i = 0; i < Wheels.Num(); ++i)
	{
		const FVector Local = Wheels[i].LocalOffsetCm;
		const FVector LocalYawRot = YawOnly.RotateVector(Local);

		const FVector WheelXY(ProposedXYcm.X + LocalYawRot.X, ProposedXYcm.Y + LocalYawRot.Y, 0.0f);

		float GroundZ = 0.0f;
		if (!TraceGroundZ(World, WheelXY, GroundZ))
		{
			GroundZ = 0.0f; // fallback minimale
		}

		const float WheelZ = GroundZ + Wheels[i].RadiusCm;
		OutWheelWorldTargets[i] = FVector(WheelXY.X, WheelXY.Y, WheelZ);
	}

	// B) Triangoli: costruisci tutti i piani possibili
	const FVector2D CM_XY(ProposedXYcm);

	struct FCandidate
	{
		int32 I=INDEX_NONE, J=INDEX_NONE, K=INDEX_NONE;
		float Z=0.f;
		FVector N=FVector::UpVector;
		bool bContains=false;
	};
	TArray<FCandidate> Candidates;
	Candidates.Reserve((Wheels.Num() * (Wheels.Num()-1) * (Wheels.Num()-2)) / 6);

	auto EvaluateCandidate = [&](int32 I, int32 J, int32 K)
	{
		float Z; FVector N;
		if (!EvaluatePlaneAtXY(OutWheelWorldTargets[I], OutWheelWorldTargets[J], OutWheelWorldTargets[K], CM_XY, Z, N))
			return;

		const FVector2D A(OutWheelWorldTargets[I].X, OutWheelWorldTargets[I].Y);
		const FVector2D B(OutWheelWorldTargets[J].X, OutWheelWorldTargets[J].Y);
		const FVector2D C(OutWheelWorldTargets[K].X, OutWheelWorldTargets[K].Y);

		FCandidate Cnd;
		Cnd.I = I; Cnd.J = J; Cnd.K = K;
		Cnd.Z = Z;
		Cnd.N = (N.Z < 0.0f) ? (-N.GetSafeNormal()) : N.GetSafeNormal(); // normal verso +Z
		Cnd.bContains = PointInTriangle2D(CM_XY, A, B, C);
		Candidates.Add(Cnd);
	};

	const int32 Nw = OutWheelWorldTargets.Num();
	for (int32 i = 0; i < Nw; ++i)
	  for (int32 j = i+1; j < Nw; ++j)
		for (int32 k = j+1; k < Nw; ++k)
		  EvaluateCandidate(i,j,k);

	if (Candidates.Num() == 0)
	{
		OutBestPlaneZAtCM = 0.0f;
		OutBestPlaneNormal = FVector::UpVector;
		return false;
	}

	// C) Candidati: seleziona quelli che contengono il baricentro
	TArray<FCandidate*> Containing;
	Containing.Reserve(Candidates.Num());
	for (FCandidate& C : Candidates)
	{
		if (C.bContains)
			Containing.Add(&C);
	}

	auto AngleDegBetween = [](const FVector& A, const FVector& B)
	{
		const FVector An = A.GetSafeNormal();
		const FVector Bn = B.GetSafeNormal();
		const float Dot = FMath::Clamp(FVector::DotProduct(An, Bn), -1.0f, 1.0f);
		return FMath::RadiansToDegrees(FMath::Acos(Dot));
	};

	// D) Regola di selezione del candidato migliore
	FCandidate* Best = nullptr;

	if (Containing.Num() == 1)
	{
		Best = Containing[0];
	}
	else if (Containing.Num() > 1)
	{
		if (PrevPlaneNormalOpt)
		{
			const FVector PrevN = (PrevPlaneNormalOpt->Z < 0.0f) ? (-PrevPlaneNormalOpt->GetSafeNormal()) : PrevPlaneNormalOpt->GetSafeNormal();

			// Se tra i candidati c’è il piano precedente (entro tolleranza), tienilo
			FCandidate* MatchingPrev = nullptr;
			for (FCandidate* C : Containing)
			{
				const float ang = AngleDegBetween(C->N, PrevN);
				if (ang <= PlaneMatchAngleToleranceDeg)
				{
					MatchingPrev = C;
					break;
				}
			}

			if (MatchingPrev)
			{
				Best = MatchingPrev;
				Best->N = PrevN; // Mantieni esattamente la normal precedente
			}
			else
			{
				// Altrimenti, scegli il piano che richiede rotazione minima rispetto al precedente
				float BestAng = TNumericLimits<float>::Max();
				for (FCandidate* C : Containing)
				{
					const float ang = AngleDegBetween(C->N, PrevN);
					if (ang < BestAng)
					{
						BestAng = ang;
						Best = C;
					}
				}
			}
		}
		else
		{
			// Nessuna normal precedente: scegli quello “più alto” tra i contenenti
			for (FCandidate* C : Containing)
			{
				if (!Best || C->Z > Best->Z)
					Best = C;
			}
		}
	}
	else
	{
		// Nessun triangolo contiene il baricentro: fallback al piano con Z più alta
		for (FCandidate& C : Candidates)
		{
			if (!Best || C.Z > Best->Z)
				Best = &C;
		}
	}

	if (!Best)
		Best = &Candidates[0];

	// E) Output del piano selezionato (Z al baricentro e normal)
	OutBestPlaneZAtCM = Best->Z;
	OutBestPlaneNormal = (Best->N.Z < 0.0f) ? (-Best->N) : Best->N;
	return true;
}

bool URoverGroundingSubsystem::TraceGroundZ(UWorld* World, const FVector& XYcm, float& OutZcm) const
{
	if (!World) return false;

	const FVector Start(XYcm.X, XYcm.Y, TraceAboveCm);
	const FVector End  (XYcm.X, XYcm.Y, -TraceBelowCm);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RoverGrounding), /*bTraceComplex*/ false);

	// Ignora rover e attached (se noto il frame base)
	if (UGameInstance* GI = World->GetGameInstance())
	{
		if (UFMUSimulationSubsystem* FMUSim = GI->GetSubsystem<UFMUSimulationSubsystem>())
		{
			if (!BaseFrameName.IsEmpty())
			{
				if (AActor* BaseActor = FMUSim->GetFrameActor(BaseFrameName))
				{
					Params.AddIgnoredActor(BaseActor);
					TArray<AActor*> Attached;
					BaseActor->GetAttachedActors(Attached, true);
					for (AActor* A : Attached)
					{
						Params.AddIgnoredActor(A);
					}
				}
			}
		}
	}

	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECollisionChannel::ECC_Visibility, Params);
	if (bHit)
	{
		OutZcm = Hit.ImpactPoint.Z;
		return true;
	}

	return false;
}

bool URoverGroundingSubsystem::PointInTriangle2D(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C)
{
	auto Sign = [](const FVector2D& P1, const FVector2D& P2, const FVector2D& P3)
	{ return (P1.X - P3.X) * (P2.Y - P3.Y) - (P2.X - P3.X) * (P1.Y - P3.Y); };

	const float d1 = Sign(P, A, B);
	const float d2 = Sign(P, B, C);
	const float d3 = Sign(P, C, A);

	const bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
	const bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

	return !(has_neg && has_pos);
}

bool URoverGroundingSubsystem::EvaluatePlaneAtXY(const FVector& A, const FVector& B, const FVector& C, const FVector2D& XY, float& OutZ, FVector& OutNormal)
{
	const FVector AB = B - A;
	const FVector AC = C - A;
	FVector N = FVector::CrossProduct(AB, AC);

	if (N.IsNearlyZero())
	{
		OutZ = (A.Z + B.Z + C.Z) / 3.0f;
		OutNormal = FVector::UpVector;
		return true;
	}

	N.Normalize();
	OutNormal = N;

	const float denom = (FMath::Abs(N.Z) < KINDA_SMALL_NUMBER) ? 0.0f : N.Z;
	if (FMath::IsNearlyZero(denom))
	{
		OutZ = (A.Z + B.Z + C.Z) / 3.0f;
		return true;
	}

	OutZ = A.Z - (N.X * (XY.X - A.X) + N.Y * (XY.Y - A.Y)) / denom;
	return true;
}

FQuat URoverGroundingSubsystem::MakeRotationFromNormalAndYaw(const FVector& Normal, float YawDegrees)
{
	const FVector ForwardYaw = FRotationMatrix(FRotator(0.f, YawDegrees, 0.f)).GetUnitAxis(EAxis::X);
	FVector XAxis = (ForwardYaw - FVector::DotProduct(ForwardYaw, Normal) * Normal).GetSafeNormal();

	if (!XAxis.IsNearlyZero())
	{
		const FVector ZAxis = Normal.GetSafeNormal();
		const FVector YAxis = FVector::CrossProduct(ZAxis, XAxis).GetSafeNormal();
		const FVector OrthoX = FVector::CrossProduct(YAxis, ZAxis).GetSafeNormal();

		FMatrix M; M.SetAxis(0, OrthoX); M.SetAxis(1, YAxis); M.SetAxis(2, ZAxis);
		return FQuat(M);
	}
	return FRotationMatrix::MakeFromZX(Normal, ForwardYaw).ToQuat();
}

// -------------------------
// Conversion helpers (FMU<->UE)
// -------------------------

FVector URoverGroundingSubsystem::ConvertPos_FMU_to_UEcm(const FVector& PosMetersFMU)
{
	// FMUSimulationSubsystem::ConvertCoordinates: (X, -Y, Z) e metri->cm
	return FVector(PosMetersFMU.X * 100.0f, -PosMetersFMU.Y * 100.0f, PosMetersFMU.Z * 100.0f);
}

FVector URoverGroundingSubsystem::ConvertPos_UEcm_to_FMU(const FVector& PosCmUE)
{
	// Inverso: (X, -Y, Z) e cm->metri
	return FVector(PosCmUE.X / 100.0f, -(PosCmUE.Y / 100.0f), PosCmUE.Z / 100.0f);
}

FQuat URoverGroundingSubsystem::ConvertQuat_FMU_to_UE(const FQuat& QuatFMU)
{
	// FMU (RH) -> UE (LH): stessa logica di UFMUSimulationSubsystem::ConvertRotation
	// 1) Costruisci matrice RH dal quat FMU
	const FMatrix RHMatrix = FQuatRotationMatrix(QuatFMU);

	// 2) Applica i flip di elementi per passare a UE
	FMatrix ConvertedMatrix = RHMatrix;
	ConvertedMatrix.M[0][1] *= -1.0f;
	ConvertedMatrix.M[1][0] *= -1.0f;
	ConvertedMatrix.M[1][2] *= -1.0f;
	ConvertedMatrix.M[2][1] *= -1.0f;

	// 3) Ritorna quat UE
	return FQuat(ConvertedMatrix);
}

FQuat URoverGroundingSubsystem::ConvertQuat_UE_to_FMU(const FQuat& QuatUE)
{
	// UE (LH) -> FMU (RH): applichiamo gli stessi flip sugli elementi della matrice
	// (operazione involutiva: applicare i segni una seconda volta torna alla base RH)
	const FMatrix UEMatrix = FQuatRotationMatrix(QuatUE);

	FMatrix ConvertedMatrix = UEMatrix;
	ConvertedMatrix.M[0][1] *= -1.0f;
	ConvertedMatrix.M[1][0] *= -1.0f;
	ConvertedMatrix.M[1][2] *= -1.0f;
	ConvertedMatrix.M[2][1] *= -1.0f;

	return FQuat(ConvertedMatrix);
}