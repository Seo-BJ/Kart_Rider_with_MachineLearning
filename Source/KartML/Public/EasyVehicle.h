// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "EasyVehicle.generated.h"

UCLASS()
class KARTML_API AEasyVehicle : public ACharacter
{
	GENERATED_BODY()

	friend class FSavedMove_EasyVehicle;

public:

	AEasyVehicle(const FObjectInitializer& ObejctInitializer);

protected:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Movement)
	class UEasyVehicleMovementComponent* VehicleMovementComponent;
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const;

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
