// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EasyVehicleMovementComponent.generated.h"

class UFeatureProcessor;

/**
 * 드리프트 모드를 커스텀 Movement Mode로 구현
 */

UCLASS()
class KARTML_API UEasyVehicleMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
	
public:
	UEasyVehicleMovementComponent();

	UFUNCTION(BlueprintCallable) void DriftPressed();
	UFUNCTION(BlueprintCallable) void DriftReleased();

protected:	
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	
	UPROPERTY(EditDefaultsOnly)
	bool bUseExtrapolationForPredict = true;

	UPROPERTY(EditDefaultsOnly)
	bool bUseNeuralForPredict = false;
	
	UPROPERTY(EditDefaultsOnly)
	float NetworkLatency = 0.1f; // Time threshold for enabling extrapolation

	virtual void SmoothClientPosition(float DeltaSeconds) override;

private:

	void SmoothClientPosition_Extrapolate(float DeltaSeconds);
	void SmoothClientPosition_UpdateVisuals_Extrapolate();

	bool Safe_bWantsToDrift;

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

		uint8 Saved_bWantsToDrift : 1;

		virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
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
