// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "EasyVehicleMovementComponent.generated.h"

class UFeatureProcessor;

/**
 * 조향 입력을 이동마다 서버로 보내기 위한 네트워크 이동 데이터.
 * 압축 플래그(1비트)로는 아날로그 조향을 보낼 수 없어서 FCharacterNetworkMoveData를 확장한다.
 * (CharacterMovementReplication.h:89-98 주석의 확장 방식)
 */
struct FEasyVehicleNetworkMoveData : public FCharacterNetworkMoveData
{
	typedef FCharacterNetworkMoveData Super;

	float SteerInput = 0.f;

	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType) override;
};

struct FEasyVehicleNetworkMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
	FEasyVehicleNetworkMoveDataContainer();

	FEasyVehicleNetworkMoveData CustomMoveData[3];
};

/**
 * 드리프트를 CMC 안에서 계산하는 이동 컴포넌트.
 *
 * - 드리프트 상태(bDrifting, DriftAmount, DriftCancelTime)와 회전을 PerformMovement 안에서 이동의 dt로 계산한다.
 *   클라이언트 예측, 서버 재계산, 보정 후 재생이 모두 같은 코드와 같은 dt를 쓴다.
 * - 회전은 서버가 정한다(bUseControllerRotationYaw = false, 보정에 회전 포함).
 * - 이동 방향도 서버의 차체 방향으로 다시 만든다. 클라이언트가 보낸 가속도는 크기와 앞/뒤 부호만 쓴다.
 * - 드리프트는 지면 이동의 하위 상태다. 별도 MOVE_Custom 모드로 만들면 PhysWalking(바닥, 경사, 계단)을
 *   다시 구현해야 해서, MOVE_Walking을 유지하고 회전과 상태만 바꾼다.
 *
 * 수치는 기존 BP_EasyVehicle 이벤트 그래프(프레임당 값)를 약 110fps 기준으로 초당 값으로 바꾼 것이다.
 */
UCLASS()
class KARTML_API UEasyVehicleMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UEasyVehicleMovementComponent();

	/** 드리프트 키 (IA_Drift Started / Completed) */
	UFUNCTION(BlueprintCallable) void DriftPressed();
	UFUNCTION(BlueprintCallable) void DriftReleased();

	/** 조향 입력 -1..1 (IA_Move X). 매 프레임 입력 처리에서 갱신하고, 이동을 만들 때 저장된다. */
	UFUNCTION(BlueprintCallable) void SetSteerInput(float InSteer);

	UFUNCTION(BlueprintPure, Category = "Drift") bool IsDrifting() const { return bDrifting; }
	UFUNCTION(BlueprintPure, Category = "Drift") float GetDriftAmount() const { return DriftAmount; }
	UFUNCTION(BlueprintPure, Category = "Drift") float GetDriftTurnMultiplier() const { return DriftTurnMultiplier; }

	//------------------------------------------------------------------
	// 조정값. 기존 BP 값 / 110fps 환산값이 기본.

	/**
	 * AddControllerYawInput 1단위가 몇 도였는지.
	 * 기존 BP: DefaultInput.ini bEnableLegacyInputScales=True → APlayerController::AddYawInput이 InputYawScale_DEPRECATED를 곱함,
	 * BaseGame.ini InputYawScale=2.5 (CoreRedirects.cpp: InputYawScale → InputYawScale_DEPRECATED).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drift|Tuning")
	float LegacyYawUnitDegrees = 2.5f;

	/** 일반 조향 회전 속도 (입력 단위/초). BP: 프레임당 X * 0.2 → 0.2 * 110 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drift|Tuning")
	float SteerTurnRate = 22.f;

	/** 드리프트 회전 속도 배율 (입력 단위/초). BP: 프레임당 0.7..1.5 → 그 값 * 110 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drift|Tuning")
	float DriftTurnRateScale = 110.f;

	/** BP MapRangeClamped 출력 범위 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drift|Tuning")
	float DriftTurnMultiplierMin = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drift|Tuning")
	float DriftTurnMultiplierMax = 1.5f;

	/** BP 변수 MaxDriftDuration(BP_EasyVehicle 기본값 0.2). |DriftAmount| / MaxDriftDuration 이 0..1 로 매핑된다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drift|Tuning", meta = (ClampMin = "0.01"))
	float MaxDriftDuration = 0.2f;

	/** 드리프트 취소: TurnMultiplier가 이 값보다 작은 상태가 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drift|Tuning")
	float DriftCancelTurnMultiplier = 0.8f;

	/** 이 시간(초)보다 오래 이어지면 드리프트를 끝낸다. BP 값 0.15 (BP 주석은 0.2) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drift|Tuning")
	float DriftCancelDelay = 0.15f;

	/** 후진할 때 조향을 반대로. 기존 BP는 주석과 달리 반전하지 않았다(곱셈 값 1.0). false로 두면 기존 동작. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drift|Tuning")
	bool bInvertSteerWhenReversing = true;

	/** 조향/가속 입력이 이 값보다 작으면 0으로 본다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drift|Tuning")
	float InputDeadZone = KINDA_SMALL_NUMBER;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 드리프트 상태와 회전, 이동 방향을 이동 dt로 계산 (클라 예측 / 서버 / 재생 공통) */
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;

	/** 서버가 보정을 보낼 때 회전도 함께 보낸다 (기본 CMC는 회전을 클라이언트에 맡김) */
	virtual bool ShouldCorrectRotation() const override { return true; }

	/** 서버: 받은 네트워크 이동 데이터에서 조향 입력을 꺼낸다 */
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel) override;

	UPROPERTY(EditDefaultsOnly)
	bool bUseExtrapolationForPredict = true;

	UPROPERTY(EditDefaultsOnly)
	bool bUseNeuralForPredict = false;

	UPROPERTY(EditDefaultsOnly)
	float NetworkLatency = 0.1f; // Time threshold for enabling extrapolation

	virtual void SmoothClientPosition(float DeltaSeconds) override;

private:
	void UpdateDrift(float DeltaSeconds);
	void ResetDriftState();
	void MirrorDriftStateToOwner();

	void SmoothClientPosition_Extrapolate(float DeltaSeconds);
	void SmoothClientPosition_UpdateVisuals_Extrapolate();

	//------------------------------------------------------------------
	// 이동 입력 (FSavedMove로 저장, 서버로 전송)
	bool Safe_bWantsToDrift = false;
	float Safe_SteerInput = 0.f;

	// 드리프트 상태 (입력과 dt로부터 결정적으로 계산됨, 재생을 위해 FSavedMove에 저장)
	bool bDrifting = false;
	float DriftAmount = 0.f;          // BP: TurnMultiplierBeforeClamp, -1..1
	float DriftTurnMultiplier = 0.f;  // BP: TurnMultiplier
	float DriftCancelTime = 0.f;      // BP: DriftCancleDurtion

	FEasyVehicleNetworkMoveDataContainer EasyNetworkMoveDataContainer;

	// Simulated Proxy 메시에 더하는 예측 오프셋(등속 외삽 또는 DNN). 보정 오차인 MeshTranslationOffset과 분리해서 관리한다.
	FVector PredictedMeshOffset = FVector::ZeroVector;

	UPROPERTY()
	TObjectPtr<UFeatureProcessor> FeatureProcessor;
	void SavePositionToCSV(const FVector& LocalPos, bool bUseNerual, bool bSimulatedProxy);

	// Error measurement variables
	FVector GroundTruthActorPosition;
	FVector GroundTruthMeshPosition;
	bool bIsGroundTruthClient;

	UPROPERTY(EditDefaultsOnly, Category = "Error Measurement")
	bool bEnableErrorMeasurement = true;

public:
	class FSavedMove_EasyVehicle : public FSavedMove_Character
	{
		typedef FSavedMove_Character Super;

	public:
		// 입력
		uint8 Saved_bWantsToDrift : 1;
		float Saved_SteerInput = 0.f;

		// 이동 시작 시점의 드리프트 상태 (재생할 때 복원)
		uint8 Saved_bDrifting : 1;
		float Saved_DriftAmount = 0.f;
		float Saved_DriftTurnMultiplier = 0.f;
		float Saved_DriftCancelTime = 0.f;

		virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
		virtual bool IsImportantMove(const FSavedMovePtr& LastAckedMove) const override;
		virtual void CombineWith(const FSavedMove_Character* OldMove, ACharacter* InCharacter, APlayerController* PC, const FVector& OldStartLocation) override;
		virtual void Clear() override;
		virtual uint8 GetCompressedFlags() const override;
		virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, class FNetworkPredictionData_Client_Character& ClientData) override;
		virtual void PrepMoveFor(ACharacter* C) override;
	};

	class FEasyVehicleNetworkPredictionData_Client : public FNetworkPredictionData_Client_Character
	{
		typedef FNetworkPredictionData_Client_Character Super;
	public:
		FEasyVehicleNetworkPredictionData_Client(const UCharacterMovementComponent& ClientMovement);
		virtual FSavedMovePtr AllocateNewMove() override;
	};

	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

protected:

	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
};
