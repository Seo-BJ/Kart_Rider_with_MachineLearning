// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "NNE.h"
#include "NNERuntimeCPU.h"
#include "NNEModelData.h"


#include "NeuralHandler.generated.h"

//class UNNEModelData;

/// <summary>
/// The game could stop at any time and start freeing memory that a running inference may still be using, resulting in crash. 
/// If the data is part of an actor which is removed from the game, the situation can happen. + Avoid copying data for performance reasons.
/// Access data and model instance through this class's shared pointer to avoid data copy and life-time issue.
/// </summary>
class FNeuralModelHelper
{
public:
	TSharedPtr<UE::NNE::IModelInstanceCPU> ModelInstance;
	TArray<float> InputData; // Not only tarray, but also any other continuous data would do
	TArray<float> OutputData;
	TArray<UE::NNE::FTensorBindingCPU> InputBindings; // The memory is wrapped by ftensorbindingcpu to be passed to the model instance for inference.
	TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
	bool bIsRunning; // Data should never be accessed if bIsRunning is true.
};

UCLASS()
class KARTML_API ANeuralHandler : public AActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	TObjectPtr<UNNEModelData> PreloadedModelData;
	TSharedPtr<FNeuralModelHelper> ModelHelper;

public:
	// Sets default values for this actor's properties
	ANeuralHandler();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	void Inference(TFunction<void(float, float, float)> Callback);

};
