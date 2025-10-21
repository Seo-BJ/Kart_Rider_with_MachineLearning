// Fill out your copyright notice in the Description page of Project Settings.


#include "NeuralHandler.h"
#include "FeatureProcessor.h"

// Sets default values
ANeuralHandler::ANeuralHandler()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void ANeuralHandler::BeginPlay()
{
	Super::BeginPlay();

	TWeakInterfacePtr<INNERuntimeCPU> Runtime = UE::NNE::GetRuntime<INNERuntimeCPU>("NNERuntimeORTCpu");
	if (Runtime.IsValid())
	{
		ModelHelper = MakeShared<FNeuralModelHelper>();

		TSharedPtr<UE::NNE::IModelCPU> Model = Runtime->CreateModelCPU(PreloadedModelData);
		if (Model.IsValid())
		{
			ModelHelper->ModelInstance = Model->CreateModelInstanceCPU();
			if (ModelHelper->ModelInstance.IsValid())
			{ 
				// Query, test and convert tensor shapes 

				// Input
				TConstArrayView<UE::NNE::FTensorDesc> InputTensorDesc = ModelHelper->ModelInstance->GetInputTensorDescs(); // Descriptors are only used to run some checks if the provided model can be used.
				checkf(InputTensorDesc.Num() == 1, TEXT("The current code supports only models with a single input tensor"));
				UE::NNE::FSymbolicTensorShape SymbolicInputTensorShape = InputTensorDesc[0].GetShape(); // Symbolic shapes can have individual dimensions be set to -1. > Means model accepts any size for this dimension.
				checkf(SymbolicInputTensorShape.IsConcrete(), TEXT("The current code supports only models without variable input tensor dimensions"))
				TArray< UE::NNE::FTensorShape > InputTensorShapes = { UE::NNE::FTensorShape::MakeFromSymbolic(SymbolicInputTensorShape) }; // In case NN only supports static shapes. Create a concrete tensor shape from the symbolic shape

				ModelHelper->ModelInstance->SetInputTensorShapes(InputTensorShapes);

				// Output
				TConstArrayView<UE::NNE::FTensorDesc> OutputTensorDesc = ModelHelper->ModelInstance->GetOutputTensorDescs();
				checkf(OutputTensorDesc.Num() == 1, TEXT("The current code supports only models with a single output tensor"));
				UE::NNE::FSymbolicTensorShape SymbolicOutputTensorShape = OutputTensorDesc[0].GetShape();
				checkf(SymbolicInputTensorShape.IsConcrete(), TEXT("The current code supports only models without variable input tensor dimensions"))
				TArray<UE::NNE::FTensorShape> OutputTensorShapes = { UE::NNE::FTensorShape::MakeFromSymbolic(SymbolicOutputTensorShape) };

				// Create In- and output
				ModelHelper->InputData.SetNumZeroed(InputTensorShapes[0].Volume());
				ModelHelper->InputBindings.SetNumZeroed(1);
				ModelHelper->InputBindings[0].Data = ModelHelper->InputData.GetData();
				ModelHelper->InputBindings[0].SizeInBytes = ModelHelper->InputData.Num() * sizeof(float);

				ModelHelper->OutputData.SetNumZeroed(OutputTensorShapes[0].Volume());
				ModelHelper->OutputBindings.SetNumZeroed(1);
				ModelHelper->OutputBindings[0].Data = ModelHelper->OutputData.GetData();
				ModelHelper->OutputBindings[0].SizeInBytes = ModelHelper->OutputData.Num() * sizeof(float);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to create the model instance"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to create the model"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot find runtime NNERuntimeORTCpu, please enable the corresponding plugin"))
	}

	UE_LOG(LogTemp, Warning, TEXT("Input Tensor size: %d // Output tensor size: %d"), ModelHelper->InputData.Num(), ModelHelper->OutputData.Num());
}

// Called every frame
void ANeuralHandler::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
}

void ANeuralHandler::Inference(TFunction<void(float, float, float)> Callback)
{
	if (ModelHelper.IsValid())
	{
		// Example for async inference
		if (!ModelHelper->bIsRunning)
		{
			// Process ModelHelper->OutputData from the previous run here
			// Fill in new data into ModelHelper->InputData here

			ModelHelper->bIsRunning = true;
			TSharedPtr<FNeuralModelHelper> ModelHelperPtr = ModelHelper;

			AsyncTask(ENamedThreads::AnyNormalThreadNormalTask, [ModelHelperPtr, Callback]() // For synchronization
				{
					if (ModelHelperPtr->ModelInstance->RunSync(ModelHelperPtr->InputBindings, ModelHelperPtr->OutputBindings) == UE::NNE::IModelInstanceCPU::ERunSyncStatus::Fail)
					{
						UE_LOG(LogTemp, Error, TEXT("Failed to run the model"));
					}
					
					const float GTruthPosDiffX = ModelHelperPtr->OutputData[0];
					const float GTruthPosDiffY = ModelHelperPtr->OutputData[1];
					const float GTruthRotDiffZ = ModelHelperPtr->OutputData[2]; 

					AsyncTask(ENamedThreads::GameThread, [ModelHelperPtr, GTruthPosDiffX, GTruthPosDiffY, GTruthRotDiffZ, Callback]() // Result
						{
							ModelHelperPtr->bIsRunning = false;

							//const float GTruthPosDiffX = ModelHelperPtr->OutputData[0];
							//const float GTruthPosDiffY = ModelHelperPtr->OutputData[1];
							//const float GTruthRotDiffZ = ModelHelperPtr->OutputData[2];
							//UE_LOG(LogTemp, Warning, TEXT("Inference Result: %f, %f, %f"), GTruthPosDiffX, GTruthPosDiffY, GTruthRotDiffZ);
						
							if (Callback)
							{
								Callback(GTruthPosDiffX, GTruthPosDiffY, GTruthRotDiffZ);
							}
						});
				});
		}
	}
}
