// Fill out your copyright notice in the Description page of Project Settings.

#include "FeatureProcessor.h"
#include "Misc\FileHelper.h"
#include "HAL\PlatformFileManager.h"
#include "Misc\Paths.h"
#include "TimerManager.h"
#include "NeuralHandler.h"
#include "Kismet\GameplayStatics.h"

// Sets default values for this component's properties
UFeatureProcessor::UFeatureProcessor()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	FeatureFilePath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Features"));
	FeatureFilePath = FPaths::Combine(FeatureFilePath, TEXT("data0.csv"));

	FeatureUpdateDelay = 0.1f;
	
	bEnableInference = true;
}

FVector UFeatureProcessor::GetEstimatePostion()
{
	return FVector(EstPosDiffX, EstPosDiffY, 0);
}

// Called when the game starts
void UFeatureProcessor::BeginPlay()
{
	Super::BeginPlay();
	FeatureActor = GetOwner();

	// Set Filewrite timer
	GetWorld()->GetTimerManager().SetTimer(FeatureWriteTimer, [this]()
	{
		UpdateFeatures(FeatureUpdateDelay);
	}, FeatureUpdateDelay, true);

	if(bWriteFeaturesToFile) // Start timer immediately if false 
		GetWorld()->GetTimerManager().PauseTimer(FeatureWriteTimer);

	OnFileWrite.AddDynamic(this, &UFeatureProcessor::OnFileWriteHandler);

	NeuralHandler = Cast<ANeuralHandler>(UGameplayStatics::GetActorOfClass(GetWorld(), ANeuralHandler::StaticClass()));
	checkf(NeuralHandler, TEXT("Neural handler must be assigned"));	
}

// Called every frame
void UFeatureProcessor::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	//if (bSaveFeatures)
	//{
	//	UpdateFeatures(DeltaTime);
	//}
}

//void UFeatureProcessor::WriteFeatures(float DeltaTime)
//{
//	UpdateFeatures(DeltaTime);
//	//SaveDataToCSV(FeatureFilePath, FormatFeaturesToCSV());
//}

void UFeatureProcessor::UpdateFeatures(float DeltaTime)
{
	// Euler rotation calc
	//FRotator ActorRot = FeatureActor->GetActorRotation(); // Current rotation
	//FVector CurRot = FVector(ActorRot.Roll, ActorRot.Pitch, ActorRot.Yaw);
	//FVector CurAngularVel = (CurRot - PrevRot) / DeltaTime; // TODO ? 
	//FVector CurAngularAcc = (CurAngularVel - PrevAngularVel) / DeltaTime;

	// Quaternion rotation calc
	FQuat CurQuat = FeatureActor->GetActorQuat();
	FQuat DeltaQuat = CurQuat * PrevQuat.Inverse();
	
	FVector Axis;
	float Angle;
	DeltaQuat.ToAxisAndAngle(Axis, Angle); 

	FVector CurAngularVel = Axis * Angle / DeltaTime;
	FVector CurAngularAcc = (CurAngularVel - PrevAngularVel) / DeltaTime;

	FVector CurPos = FeatureActor->GetActorLocation();
	FVector CurLinearVel = FeatureActor->GetVelocity() * 0.01f; // to m/s

	// TODO: Ground truth i+1 frame���� ��� ��, i frame ������ �������� ���� write. 
	++DataIteration;
	if (DataIteration >= 1)
	{
		Features.GTruthPosDiff = CurPos - PrevPos;
		Features.GTruthRotDiff = Axis * Angle;

		if (bEnableInference)
		{
			// INFERENCE HERE
			SendInputFeature2Model(NeuralHandler->ModelHelper->InputData);
			NeuralHandler->Inference([this](float GTruthPosDiffX, float GTruthPosDiffY, float GTruthRotDiffZ)
				{
					// Update estimated value of this local actor using callback function of inference.
					EstPosDiffX = GTruthPosDiffX;
					EstPosDiffY = GTruthPosDiffY;
					EstRotDiffZ = GTruthRotDiffZ;

					UE_LOG(LogTemp, Warning, TEXT("Callback Received: %f, %f, %f"), EstPosDiffX, EstPosDiffY, EstRotDiffZ);
				});
		}
		if (bWriteFeaturesToFile)
			SaveDataToCSV(FeatureFilePath, FormatFeaturesToCSV());
	}

	// Linear
	Features.LinearVel = CurLinearVel;
	Features.LinearAcc = (Features.LinearVel - PrevLinearVel) / DeltaTime;

	// Rotation
	Features.AngularVel = CurAngularVel;
	Features.AngularAcc = CurAngularAcc;

	// Delta
	Features.VelmulDelta = Features.LinearVel * DeltaTime;
	Features.AngvelmulDelta = Features.AngularVel * DeltaTime;

	// Value update
	PrevPos = CurPos;
	PrevLinearVel = CurLinearVel;
	//PrevRot = CurRot;
	PrevQuat = CurQuat;
	PrevAngularVel = CurAngularVel;
}

void UFeatureProcessor::SaveDataToCSV(const FString& FilePath, const FString& DataToWrite)
{
	if (!FPaths::FileExists(FilePath))
	{
		FString Header = TEXT("LinearVel.X,LinearVel.Y,LinearAcc.X,LinearAcc.Y,AngularVel.Z,AngularAcc.Z,VelMulDelta.X,VelMulDelta.Y, Angvelmuldelta.Z,GTruthPosDiff.X,GTruthPosDiff.Y,GTruthRotDiff.Z,\n");
		//FFileHelper::SaveStringArrayToFile()
		FFileHelper::SaveStringToFile(Header, *FilePath);
	}

	bool Result = FFileHelper::SaveStringToFile(DataToWrite, *FilePath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);
	if (Result)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *DataToWrite);
	}
}

FString UFeatureProcessor::FormatFeaturesToCSV()
{
	FString DataRow = FString::Printf(TEXT("%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,\n"),
		Features.LinearVel.X,
		Features.LinearVel.Y,

		Features.LinearAcc.X,	
		Features.LinearAcc.Y,

		Features.AngularVel.Z,

		Features.AngularAcc.Z,

		Features.VelmulDelta.X,
		Features.VelmulDelta.Y,

		Features.AngvelmulDelta.Z,

		Features.GTruthPosDiff.X,
		Features.GTruthPosDiff.Y,

		Features.GTruthRotDiff.Z
		);

	//FString DataRow = FString::Printf(TEXT("%f,%f,%f\n"),
	//	Features.AngularAcc.X,
	//	Features.AngularAcc.Y,
	//	Features.AngularAcc.Z
	//);

	return DataRow;
}

void UFeatureProcessor::OnFileWriteHandler()
{
	if (!bWriteFeaturesToFile) return; // Function is not used when I don't wanna write features into file.

	bSaveFeatures = !bSaveFeatures;

	if (bSaveFeatures)
		GetWorld()->GetTimerManager().UnPauseTimer(FeatureWriteTimer);
	else
	{
		GetWorld()->GetTimerManager().PauseTimer(FeatureWriteTimer);
		DataIteration = -1;
	}

	UE_LOG(LogTemp, Warning, TEXT("FILEWRITE MODE: %d"), bSaveFeatures);
}

void UFeatureProcessor::SwitchSaveFeatureMode()
{
	if(OnFileWrite.IsBound())
		OnFileWrite.Broadcast();
}

bool UFeatureProcessor::SendInputFeature2Model(TArray<float>& InputData)
{
	if (DataIteration < 1) return false;

	InputData[0] = Features.LinearVel.X;
	InputData[1] = Features.LinearVel.Y;

	InputData[2] = Features.LinearAcc.X;
	InputData[3] = Features.LinearAcc.Y;

	InputData[4] = Features.AngularVel.Z;
	InputData[5] = Features.AngularAcc.Z;

	InputData[6] = Features.VelmulDelta.X;
	InputData[7] = Features.VelmulDelta.Y;
	InputData[8] = Features.AngvelmulDelta.Z;

	return true;
}

