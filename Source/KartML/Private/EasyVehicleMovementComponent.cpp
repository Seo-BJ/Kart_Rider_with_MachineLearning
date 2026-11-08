// Fill out your copyright notice in the Description page of Project Settings.


#include "EasyVehicleMovementComponent.h"

#include "EasyVehicle.h"
#include "EngineStats.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerState.h"
#include "KartML/Public/FeatureProcessor.h"
#include "Net/UnrealNetwork.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFilemanager.h"

namespace EasyVehicleDrift
{
	// 조향 입력은 int8로 양자화해서 보낸다. 클라이언트도 같은 값을 써야 서버와 결과가 같다.
	static float QuantizeSteer(float InSteer)
	{
		const int32 Q = FMath::Clamp(FMath::RoundToInt(FMath::Clamp(InSteer, -1.f, 1.f) * 127.f), -127, 127);
		return static_cast<float>(Q) / 127.f;
	}
}

//======================================================================
// Network move data

void FEasyVehicleNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
	Super::ClientFillNetworkMoveData(ClientMove, MoveType);

	const UEasyVehicleMovementComponent::FSavedMove_EasyVehicle& EasyMove = static_cast<const UEasyVehicleMovementComponent::FSavedMove_EasyVehicle&>(ClientMove);
	SteerInput = EasyMove.Saved_SteerInput;
}

bool FEasyVehicleNetworkMoveData::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	const bool bSuperOk = Super::Serialize(CharacterMovement, Ar, PackageMap, MoveType);

	int8 QuantizedSteer = 0;
	if (Ar.IsSaving())
	{
		QuantizedSteer = static_cast<int8>(FMath::Clamp(FMath::RoundToInt(SteerInput * 127.f), -127, 127));
	}
	Ar << QuantizedSteer;
	if (Ar.IsLoading())
	{
		SteerInput = static_cast<float>(QuantizedSteer) / 127.f;
	}

	return bSuperOk && !Ar.IsError();
}

FEasyVehicleNetworkMoveDataContainer::FEasyVehicleNetworkMoveDataContainer()
{
	NewMoveData = &CustomMoveData[0];
	PendingMoveData = &CustomMoveData[1];
	OldMoveData = &CustomMoveData[2];
}

//======================================================================
// Movement component

UEasyVehicleMovementComponent::UEasyVehicleMovementComponent()
{
	bIsGroundTruthClient = false;
	GroundTruthActorPosition = FVector::ZeroVector;
	GroundTruthMeshPosition = FVector::ZeroVector;

	// 회전은 UpdateDrift가 직접 정한다
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = false;

	SetNetworkMoveDataContainer(EasyNetworkMoveDataContainer);
}

void UEasyVehicleMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	FeatureProcessor = GetOwner()->FindComponentByClass<UFeatureProcessor>();
}

void UEasyVehicleMovementComponent::DriftPressed()
{
	Safe_bWantsToDrift = true;
}

void UEasyVehicleMovementComponent::DriftReleased()
{
	Safe_bWantsToDrift = false;
}

void UEasyVehicleMovementComponent::SetSteerInput(float InSteer)
{
	Safe_SteerInput = EasyVehicleDrift::QuantizeSteer(InSteer);
}

void UEasyVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 기존 BP 변수(TurnMultiplier 등)는 디버깅/호환용으로 값만 보여 준다. 계산은 UpdateDrift에서만 한다.
	MirrorDriftStateToOwner();
}

void UEasyVehicleMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);

	// PerformMovement 안에서 호출된다: 소유 클라이언트 예측, 서버의 MoveAutonomous, 보정 후 재생.
	// 세 경우 모두 이동의 DeltaSeconds를 쓰므로 프레임레이트와 무관하고, 같은 입력이면 같은 결과가 나온다.
	if (CharacterOwner && UpdatedComponent && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		UpdateDrift(DeltaSeconds);
	}
}

void UEasyVehicleMovementComponent::UpdateDrift(float DeltaSeconds)
{
	const FVector Forward = UpdatedComponent->GetForwardVector();
	const float AccelSize = Acceleration.Size();

	// 가속 입력의 앞/뒤 부호 (BP: IA_Move Y). 방향은 클라이언트 값을 믿지 않고 아래에서 차체 방향으로 다시 만든다.
	float Throttle = 0.f;
	if (AccelSize > KINDA_SMALL_NUMBER)
	{
		const float ForwardDot = (Acceleration / AccelSize) | Forward;
		if (FMath::Abs(ForwardDot) > InputDeadZone)
		{
			Throttle = FMath::Sign(ForwardDot);
		}
	}

	const float Steer = FMath::Abs(Safe_SteerInput) > InputDeadZone ? Safe_SteerInput : 0.f;

	// 이동 방향 = (회전 전) 차체 앞 방향 * 부호 * 크기. BP는 회전 전 컨트롤 yaw의 앞 방향을 썼다.
	Acceleration = Forward * (Throttle * AccelSize);

	// BP IA_Drift Completed + 기존 C++ TickComponent 초기화
	if (!Safe_bWantsToDrift)
	{
		ResetDriftState();
	}

	// BP IA_Move Completed: 조향도 가속도 없으면 드리프트 상태 초기화
	if (Throttle == 0.f && Steer == 0.f)
	{
		ResetDriftState();
		return;
	}

	//------------------------------------------------------------------
	// then_0: 회전 속도 (BP: 프레임당 AddControllerYawInput → 여기서는 초당 값 * dt)
	float YawUnitsPerSecond = 0.f;
	if (Throttle > 0.f)
	{
		if (bDrifting)
		{
			const float Alpha = FMath::Abs(DriftAmount) / FMath::Max(MaxDriftDuration, KINDA_SMALL_NUMBER);
			const float TurnMultiplier = FMath::GetMappedRangeValueClamped(FVector2f(0.f, 1.f), FVector2f(DriftTurnMultiplierMin, DriftTurnMultiplierMax), Alpha);
			YawUnitsPerSecond = (DriftAmount > 0.f ? TurnMultiplier : -TurnMultiplier) * DriftTurnRateScale;
			DriftTurnMultiplier = TurnMultiplier;
		}
		else
		{
			YawUnitsPerSecond = Steer * SteerTurnRate;
		}
	}
	else
	{
		// 후진 또는 정지(가속 0). 기존 BP는 둘 다 반전 없이 Steer * 0.2.
		const float ReverseSign = (Throttle < 0.f && bInvertSteerWhenReversing) ? -1.f : 1.f;
		YawUnitsPerSecond = Steer * SteerTurnRate * ReverseSign;
	}

	//------------------------------------------------------------------
	// then_1: 드리프트 진입과 드리프트 크기
	if (Steer > 0.f)
	{
		if (bDrifting)
		{
			DriftAmount = FMath::Clamp(DriftAmount + DeltaSeconds, -1.f, 1.f);
		}
		else if (Safe_bWantsToDrift)
		{
			bDrifting = true;
		}
	}
	else if (Steer < 0.f)
	{
		if (bDrifting)
		{
			DriftAmount = FMath::Clamp(DriftAmount - DeltaSeconds, -1.f, 1.f);
		}
		else if (Safe_bWantsToDrift)
		{
			bDrifting = true;
		}
	}

	//------------------------------------------------------------------
	// then_2: 드리프트 취소 (TurnMultiplier가 낮은 상태가 일정 시간 이어지면)
	if (DriftTurnMultiplier < DriftCancelTurnMultiplier)
	{
		DriftCancelTime += DeltaSeconds;
		if (DriftCancelTime > DriftCancelDelay)
		{
			ResetDriftState();
		}
	}
	else
	{
		DriftCancelTime = 0.f;
	}

	//------------------------------------------------------------------
	// 회전 적용 (서버 권한: 서버도 같은 코드로 계산하고, 보정 시 회전을 보낸다)
	if (YawUnitsPerSecond != 0.f)
	{
		FRotator NewRotation = UpdatedComponent->GetComponentRotation();
		NewRotation.Yaw += YawUnitsPerSecond * LegacyYawUnitDegrees * DeltaSeconds;
		MoveUpdatedComponent(FVector::ZeroVector, NewRotation.Quaternion(), /*bSweep*/ false);
	}
}

void UEasyVehicleMovementComponent::ResetDriftState()
{
	bDrifting = false;
	DriftAmount = 0.f;
	DriftTurnMultiplier = 0.f;
	DriftCancelTime = 0.f;
}

void UEasyVehicleMovementComponent::MirrorDriftStateToOwner()
{
	if (AEasyVehicle* Owner = GetOwner<AEasyVehicle>())
	{
		Owner->TurnMultiplier = DriftTurnMultiplier;
		Owner->TurnMultiplierBeforeClamp = DriftAmount;
		Owner->DriftCancleDurtion = DriftCancelTime;
	}
}

void UEasyVehicleMovementComponent::MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel)
{
	// 서버: 이번에 처리하는 네트워크 이동(New/Pending/Old)의 조향 입력을 꺼낸다.
	// 클라이언트 재생에서는 PrepMoveFor가 저장된 값을 이미 넣어 두었다.
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_Authority)
	{
		if (const FCharacterNetworkMoveData* MoveData = GetCurrentNetworkMoveData())
		{
			Safe_SteerInput = static_cast<const FEasyVehicleNetworkMoveData*>(MoveData)->SteerInput;
		}
	}

	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
}

void UEasyVehicleMovementComponent::SmoothClientPosition(float DeltaSeconds)
{
	//SCOPE_CYCLE_COUNTER(STAT_CharacterMovementSmoothClientPosition_Interp);
	FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh();

	if (ClientData)
	{
		// Linear 모드는 MeshRotationOffset의 의미가 달라서 아래 Exponential 기준 적용 코드를 쓸 수 없다.
		if (bUseExtrapolationForPredict == false || NetworkSmoothingMode != ENetworkSmoothingMode::Exponential)
		{
			Super::SmoothClientPosition(DeltaSeconds);
		}
		else
		{
			if (!HasValidData() || NetworkSmoothingMode == ENetworkSmoothingMode::Disabled)
			{
				return;
			}

			// We shouldn't be running this on a server that is not a listen server.
			checkSlow(GetNetMode() != NM_DedicatedServer);
			checkSlow(GetNetMode() != NM_Standalone);

			// Only client proxies or remote clients on a listen server should run this code.
			const bool bIsSimulatedProxy = (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy);
			const bool bIsRemoteAutoProxy = (CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy);
			if (!ensure(bIsSimulatedProxy || bIsRemoteAutoProxy))
			{
				return;
			}

			SmoothClientPosition_Extrapolate(DeltaSeconds);
			SmoothClientPosition_UpdateVisuals_Extrapolate();
		}
	}
}

void UEasyVehicleMovementComponent::SmoothClientPosition_Extrapolate(float DeltaSeconds)
{
	if (FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character())
	{
		// 보정 오차(MeshTranslationOffset)와 회전 오프셋은 엔진 방식 그대로 줄인다.
		// 패킷을 받을 때 SmoothCorrection이 쌓은 오차를 덮어쓰면 캡슐 이동이 메시에 그대로 튄다.
		SmoothClientPosition_Interpolate(DeltaSeconds);
		const bool bCorrectionSmoothingComplete = bNetworkSmoothingComplete;

		// 예측 오프셋은 보정 오차와 별도로 매 프레임 새로 계산한다.
		PredictedMeshOffset = FVector::ZeroVector;
		if (!bUseNeuralForPredict)
		{
			PredictedMeshOffset = Velocity * NetworkLatency;
		}
		else if (FeatureProcessor)
		{
			PredictedMeshOffset = FeatureProcessor->GetEstimatePostion();
		}

		// 보정이 끝나도 움직이는 동안은 예측 오프셋을 계속 갱신해야 하므로 스무딩을 끝내지 않는다.
		bNetworkSmoothingComplete = bCorrectionSmoothingComplete && Velocity.IsNearlyZero() && PredictedMeshOffset.IsNearlyZero(1e-2f);
		if (bNetworkSmoothingComplete)
		{
			PredictedMeshOffset = FVector::ZeroVector;
		}

		// 화면에 실제로 적용되는 오프셋
		FVector LocalPos = ClientData->MeshTranslationOffset + PredictedMeshOffset;
		FQuat LocalRot = ClientData->MeshRotationOffset;

		/*
		if (GetWorld())
		{
			// DrawDebugBox(GetWorld(), CurrentMeshPos, FVector(30.0f, 30.0f, 30.0f), FQuat(LocalRot), FColor::Red, true, -1, 0, 2.0f);
		}
		UE_LOG(LogTemp, Warning, TEXT("AfterPosition = %f, %f, %f"), LocalPos.X, LocalPos.Y, LocalPos.Z);
		UE_LOG(LogTemp, Warning, TEXT("MeshTranslationOffset = %f, %f, %f"), ClientData->MeshTranslationOffset.X, ClientData->MeshTranslationOffset.Y, ClientData->MeshTranslationOffset.Z);
		*/

		// Calculate current positions
		FVector CurrentMeshPos = LocalPos + GetActorLocation();

		FString NewLine = FString::Printf(TEXT("%f,%f,%f\n"), CurrentMeshPos.X, CurrentMeshPos.Y, CurrentMeshPos.Z);

		// 파일 경로: Saved/SimulatedPositions.csv
		FString FilePath = FPaths::ProjectSavedDir() + "SimulatedPositions.csv";

		FFileHelper::SaveStringToFile(
			NewLine,
			*FilePath,
			FFileHelper::EEncodingOptions::AutoDetect,
			&IFileManager::Get(),
			FILEWRITE_Append
		);
	}
}


void UEasyVehicleMovementComponent::SmoothClientPosition_UpdateVisuals_Extrapolate()
{
	//SCOPE_CYCLE_COUNTER(STAT_CharacterMovementSmoothClientPosition_Visual);
	FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh();
	if (ClientData && Mesh && !Mesh->IsSimulatingPhysics())
	{
		const USceneComponent* MeshParent = Mesh->GetAttachParent();

		FVector MeshParentScale = MeshParent != nullptr ? MeshParent->GetComponentScale() : FVector(1.0f, 1.0f, 1.0f);

		MeshParentScale.X = FMath::IsNearlyZero(MeshParentScale.X) ? 1.0f : MeshParentScale.X;
		MeshParentScale.Y = FMath::IsNearlyZero(MeshParentScale.Y) ? 1.0f : MeshParentScale.Y;
		MeshParentScale.Z = FMath::IsNearlyZero(MeshParentScale.Z) ? 1.0f : MeshParentScale.Z;

		// Adjust extrapolated mesh location and rotation
		const FVector NewRelTranslation = (UpdatedComponent->GetComponentToWorld().InverseTransformVectorNoScale(ClientData->MeshTranslationOffset + PredictedMeshOffset) / MeshParentScale) + CharacterOwner->GetBaseTranslationOffset();
		const FQuat NewRelRotation = ClientData->MeshRotationOffset * CharacterOwner->GetBaseRotationOffset();
		Mesh->SetRelativeLocationAndRotation(NewRelTranslation, NewRelRotation, false, nullptr, GetTeleportType());
	}
}

//======================================================================
// Saved move

bool UEasyVehicleMovementComponent::FSavedMove_EasyVehicle::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	const FSavedMove_EasyVehicle* NewEasyVehicleMove = static_cast<const FSavedMove_EasyVehicle*>(NewMove.Get());

	if (Saved_bWantsToDrift != NewEasyVehicleMove->Saved_bWantsToDrift)
	{
		return false;
	}

	if (Saved_SteerInput != NewEasyVehicleMove->Saved_SteerInput)
	{
		return false;
	}

	// 드리프트 중(또는 드리프트 키를 누른 동안)은 합치지 않는다.
	// 합치면 서버가 큰 dt 한 번으로 계산해서 취소 시간 같은 문턱을 넘는 시점이 클라이언트와 달라질 수 있다.
	if (Saved_bWantsToDrift || Saved_bDrifting || NewEasyVehicleMove->Saved_bDrifting)
	{
		return false;
	}

	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

bool UEasyVehicleMovementComponent::FSavedMove_EasyVehicle::IsImportantMove(const FSavedMovePtr& LastAckedMove) const
{
	if (Super::IsImportantMove(LastAckedMove))
	{
		return true;
	}

	// 조향이 바뀐 이동은 패킷이 빠져도 OldMove로 다시 보내게 한다
	const FSavedMove_EasyVehicle* LastAcked = static_cast<const FSavedMove_EasyVehicle*>(LastAckedMove.Get());
	return LastAcked && LastAcked->Saved_SteerInput != Saved_SteerInput;
}

void UEasyVehicleMovementComponent::FSavedMove_EasyVehicle::CombineWith(const FSavedMove_Character* OldMove, ACharacter* InCharacter, APlayerController* PC, const FVector& OldStartLocation)
{
	Super::CombineWith(OldMove, InCharacter, PC, OldStartLocation);

	// 위치/속도처럼 드리프트 상태도 합쳐지는 이전 이동의 시작 상태로 되돌린다
	const FSavedMove_EasyVehicle* OldEasyMove = static_cast<const FSavedMove_EasyVehicle*>(OldMove);
	if (UEasyVehicleMovementComponent* MoveComp = Cast<UEasyVehicleMovementComponent>(InCharacter->GetCharacterMovement()))
	{
		MoveComp->bDrifting = OldEasyMove->Saved_bDrifting;
		MoveComp->DriftAmount = OldEasyMove->Saved_DriftAmount;
		MoveComp->DriftTurnMultiplier = OldEasyMove->Saved_DriftTurnMultiplier;
		MoveComp->DriftCancelTime = OldEasyMove->Saved_DriftCancelTime;
	}
}

void UEasyVehicleMovementComponent::FSavedMove_EasyVehicle::Clear()
{
	FSavedMove_Character::Clear();
	Saved_bWantsToDrift = 0;
	Saved_SteerInput = 0.f;
	Saved_bDrifting = 0;
	Saved_DriftAmount = 0.f;
	Saved_DriftTurnMultiplier = 0.f;
	Saved_DriftCancelTime = 0.f;
}

uint8 UEasyVehicleMovementComponent::FSavedMove_EasyVehicle::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();

	if (Saved_bWantsToDrift)
	{
		Result |= FLAG_Custom_0;
	}

	return Result;
}

void UEasyVehicleMovementComponent::FSavedMove_EasyVehicle::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	FSavedMove_Character::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);

	if (const UEasyVehicleMovementComponent* MoveComp = Cast<UEasyVehicleMovementComponent>(C->GetCharacterMovement()))
	{
		Saved_bWantsToDrift = MoveComp->Safe_bWantsToDrift;
		Saved_SteerInput = MoveComp->Safe_SteerInput;

		Saved_bDrifting = MoveComp->bDrifting;
		Saved_DriftAmount = MoveComp->DriftAmount;
		Saved_DriftTurnMultiplier = MoveComp->DriftTurnMultiplier;
		Saved_DriftCancelTime = MoveComp->DriftCancelTime;
	}
}

void UEasyVehicleMovementComponent::FSavedMove_EasyVehicle::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);

	if (UEasyVehicleMovementComponent* MoveComp = Cast<UEasyVehicleMovementComponent>(C->GetCharacterMovement()))
	{
		MoveComp->Safe_bWantsToDrift = Saved_bWantsToDrift;
		MoveComp->Safe_SteerInput = Saved_SteerInput;

		MoveComp->bDrifting = Saved_bDrifting;
		MoveComp->DriftAmount = Saved_DriftAmount;
		MoveComp->DriftTurnMultiplier = Saved_DriftTurnMultiplier;
		MoveComp->DriftCancelTime = Saved_DriftCancelTime;
	}
}

UEasyVehicleMovementComponent::FEasyVehicleNetworkPredictionData_Client::FEasyVehicleNetworkPredictionData_Client(const UCharacterMovementComponent& ClientMovement)
	: Super(ClientMovement)
{
}

FSavedMovePtr UEasyVehicleMovementComponent::FEasyVehicleNetworkPredictionData_Client::AllocateNewMove()
{
	return FSavedMovePtr(new FSavedMove_EasyVehicle());
}

FNetworkPredictionData_Client* UEasyVehicleMovementComponent::GetPredictionData_Client() const
{
	check(PawnOwner != nullptr)

		if (ClientPredictionData == nullptr)
		{
			UEasyVehicleMovementComponent* MutableThis = const_cast<UEasyVehicleMovementComponent*>(this);

			MutableThis->ClientPredictionData = new FEasyVehicleNetworkPredictionData_Client(*this);
			MutableThis->ClientPredictionData->MaxSmoothNetUpdateDist = 92.f;
			MutableThis->ClientPredictionData->NoSmoothNetUpdateDist = 140.f;
		}

	return ClientPredictionData;
}

void UEasyVehicleMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	Safe_bWantsToDrift = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

void UEasyVehicleMovementComponent::SavePositionToCSV(const FVector& LocalPos, bool bUseNerual, bool bSimulatedProxy)
{
	FString FilePath;
	FString Condition;

	if (bSimulatedProxy)
	{
		if (bUseNerual)
		{
			FilePath = FPaths::ProjectSavedDir() + "NeuralPositions.csv";
			Condition = TEXT("Neural");
		}
		else
		{
			FilePath = FPaths::ProjectSavedDir() + "NonNeuralPositions.csv";
			Condition = TEXT("Non-Neural");
		}
	}
	else
	{
		FilePath = FPaths::ProjectSavedDir() + "ServerPositions.csv";
		Condition = TEXT("Server");
	}

	bool bFileExists = FPaths::FileExists(FilePath);

	if (!bFileExists)
	{
		FString Header = TEXT("Time,Condition,X,Y,Z\n");
		FFileHelper::SaveStringToFile(Header, *FilePath);
	}

	double CurrentTimeInSeconds = FPlatformTime::Seconds();
	FString CurrentTime = FString::Printf(TEXT("%f"), CurrentTimeInSeconds);

	FString Line = FString::Printf(TEXT("%s,%s,%f,%f,%f\n"), *CurrentTime, *Condition, LocalPos.X, LocalPos.Y, LocalPos.Z);

	FFileHelper::SaveStringToFile(Line, *FilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);
}
