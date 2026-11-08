// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "EasyVehicle.generated.h"

class UInputAction;
struct FInputActionValue;

UCLASS()
class KARTML_API AEasyVehicle : public ACharacter
{
	GENERATED_BODY()

public:

	AEasyVehicle(const FObjectInitializer& ObejctInitializer);

protected:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Movement)
	class UEasyVehicleMovementComponent* VehicleMovementComponent;
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	/** 주행 입력(IA_Move, IA_Drift)을 C++에서 받는다. BP 이벤트 그래프의 IA_Move/IA_Drift 노드를 지운 뒤에 켜 둘 것. 둘 다 켜져 있으면 입력이 두 번 처리된다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	bool bBindDriveInputInCpp = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> DriftAction;

	void OnMove(const FInputActionValue& Value);
	void OnMoveCompleted(const FInputActionValue& Value);
	void OnDriftStarted(const FInputActionValue& Value);
	void OnDriftCompleted(const FInputActionValue& Value);

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const;

	// 아래 세 값은 UEasyVehicleMovementComponent가 매 틱 덮어쓰는 표시용 값이다. 계산에 쓰지 않는다.
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	float TurnMultiplier = 0.f;
	
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	float TurnMultiplierBeforeClamp = 0.f;
	
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	float DriftCancleDurtion = 0.f;
	
	// Debug visualization settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bEnableDebug = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "1", ClampMax = "1000"))
	int32 MaxHistorySize = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "1", ClampMax = "500"))
	int32 MaxDrawCount = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float PositionUpdateInterval = 0.01f;

	// Position and rotation history
	TArray<FVector> LocalPositions;
	TArray<FRotator> LocalRotations;
	
	TArray<FVector> SimulatedPositions;
	TArray<FRotator> SimulatedRotations;

	float TimeSinceLastUpdate = 0.0f;

	void DrawDebugPositions();

};
