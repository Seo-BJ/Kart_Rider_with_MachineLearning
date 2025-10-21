// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FeatureProcessor.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFileWrite);

USTRUCT(BlueprintType)
struct FLearningData
{
	GENERATED_BODY()

	FVector LinearVel;
	FVector LinearAcc;
	FVector AngularVel;
	FVector AngularAcc;
	FVector VelmulDelta; // vel * delta t 
	FVector AngvelmulDelta; // angvel * delta t 
	FVector GTruthPosDiff;
	FVector GTruthRotDiff;
	//bool bDriftStatus;

	// Infeasible properties
	// Velocity >> z, Angular >> x,y
};

struct FOutputFeatures
{
	FVector VelmulDelta; // velocity * delta t 
	FVector Angdiff; // AngularVel * delta t 
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class KARTML_API UFeatureProcessor : public UActorComponent
{
	GENERATED_BODY()

private:
	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	AActor* FeatureActor;

	/** Need to be true if you wanna  */
	bool bSaveFeatures;

	/** Need to be true if you wanna write features to csv file */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = "OnOff Property")
	bool bWriteFeaturesToFile;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true), Category = "OnOff Property")
	bool bEnableInference;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true))
	FLearningData Features;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
	FString FeatureFilePath;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
	FString FeatureFile;

	FVector PrevPos;
	FVector PrevLinearVel;

	//FVector PrevRot;
	FQuat PrevQuat;
	FVector PrevAngularVel;

	int32 FileIndex;

	FTimerHandle FeatureWriteTimer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = true))
	float FeatureUpdateDelay;

	UPROPERTY(BlueprintAssignable, meta = (AllowPrivateAccess = true))
	FOnFileWrite OnFileWrite;

	UPROPERTY(VisibleAnywhere, meta = (AllowPrivateAccess = true))
	int32 DataIteration = -1;

	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	class ANeuralHandler* NeuralHandler;

	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	float EstPosDiffX;

	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	float EstPosDiffY;

	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	float EstRotDiffZ;

public:	
	// Sets default values for this component's properties
	UFeatureProcessor();

	FVector GetEstimatePostion();


protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	//UFUNCTION(BlueprintCallable)
	//void SwitchSaveFeatureBool() { bSaveFeatures = !bSaveFeatures; UE_LOG(LogTemp, Warning, TEXT("bSaveFeatures: %d"), bSaveFeatures); }

	//UFUNCTION(BlueprintCallable)
	//void WriteFeatures(float DeltaTime);

	UFUNCTION(BlueprintCallable)
	void UpdateFeatures(float DeltaTime);

	UFUNCTION(BlueprintCallable)
	void SaveDataToCSV(const FString& FilePath, const FString& DataToWrite);
	
	UFUNCTION(BlueprintCallable)
	FString FormatFeaturesToCSV();

	UFUNCTION()
	void OnFileWriteHandler();

	UFUNCTION(BlueprintCallable)
	void SwitchSaveFeatureMode();

public:
	bool SendInputFeature2Model(TArray<float>& InputData);
};
