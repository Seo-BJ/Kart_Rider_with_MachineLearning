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

UEasyVehicleMovementComponent::UEasyVehicleMovementComponent()
{
	bIsGroundTruthClient = false;
	GroundTruthActorPosition = FVector::ZeroVector;
	GroundTruthMeshPosition = FVector::ZeroVector;
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

void UEasyVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Safe_bWantsToDrift)
	{
		// 지면 마찰 감소
		AEasyVehicle* Owner = GetOwner<AEasyVehicle>();
		if (Owner)
		{
			Owner->TurnMultiplier = 0.f;
			Owner->TurnMultiplierBeforeClamp = 0.f;
			Owner->DriftCancleDurtion = 0.f;
		}
	}
}

void UEasyVehicleMovementComponent::SmoothClientPosition(float DeltaSeconds)
{
	//SCOPE_CYCLE_COUNTER(STAT_CharacterMovementSmoothClientPosition_Interp);
	FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh();

	if (ClientData)
	{
		if (bUseExtrapolationForPredict == false)
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
		float ActualLatency = NetworkLatency;  // 기본값
		
		FVector ExtrapolatedTranslation;
		if (!bUseNeuralForPredict)
		{
			ExtrapolatedTranslation = (Velocity * ActualLatency);
		}
		else
		{
			if (FeatureProcessor) ExtrapolatedTranslation = FeatureProcessor->GetEstimatePostion();
		}

		// ExtrapolatedTranslation.Z = ClientData->MeshTranslationOffset.Z;

		const float SmoothLocationTime = Velocity.IsZero() ? 0.5f * ClientData->SmoothNetUpdateTime : ClientData->SmoothNetUpdateTime;
		if (DeltaSeconds < SmoothLocationTime)
		{
			// Slowly decay translation offset
			ClientData->MeshTranslationOffset = ExtrapolatedTranslation;
		}
		else
		{
			ClientData->MeshTranslationOffset = FVector::ZeroVector;
		}

		// Smooth rotation
		const FQuat MeshRotationTarget = ClientData->MeshRotationTarget;
		if (DeltaSeconds < ClientData->SmoothNetUpdateRotationTime)
		{
			// Slowly decay rotation offset
			ClientData->MeshRotationOffset = FQuat::FastLerp(ClientData->MeshRotationOffset, MeshRotationTarget, DeltaSeconds / ClientData->SmoothNetUpdateRotationTime).GetNormalized();
		}
		else
		{
			ClientData->MeshRotationOffset = MeshRotationTarget;
		}

		// Check if lerp is complete
		if (Velocity.IsNearlyZero() && ClientData->MeshTranslationOffset.IsNearlyZero(1e-2f))
		{
			bNetworkSmoothingComplete = true;
			ClientData->MeshTranslationOffset = FVector::ZeroVector;
			ClientData->MeshRotationOffset = MeshRotationTarget;
		}


		FVector LocalPos = ClientData->MeshTranslationOffset;
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
		const FVector NewRelTranslation = (UpdatedComponent->GetComponentToWorld().InverseTransformVectorNoScale(ClientData->MeshTranslationOffset) / MeshParentScale) + CharacterOwner->GetBaseTranslationOffset();
		const FQuat NewRelRotation = ClientData->MeshRotationOffset * CharacterOwner->GetBaseRotationOffset();
		Mesh->SetRelativeLocationAndRotation(NewRelTranslation, NewRelRotation, false, nullptr, GetTeleportType());	
	}
}



bool UEasyVehicleMovementComponent::FSavedMove_EasyVehicle::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	FSavedMove_EasyVehicle* NewEasyVehicleMove = static_cast<FSavedMove_EasyVehicle*>(NewMove.Get());

	if (Saved_bWantsToDrift != NewEasyVehicleMove->Saved_bWantsToDrift)
	{
		return false;
	}

	return FSavedMove_Character::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void UEasyVehicleMovementComponent::FSavedMove_EasyVehicle::Clear()
{
	FSavedMove_Character::Clear();
	Saved_bWantsToDrift = 0;
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

	UEasyVehicleMovementComponent* CharacterMovement = Cast<UEasyVehicleMovementComponent>(C->GetCharacterMovement());
	Saved_bWantsToDrift = CharacterMovement->Safe_bWantsToDrift;
}

void UEasyVehicleMovementComponent::FSavedMove_EasyVehicle::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);
	UEasyVehicleMovementComponent* CharacterMovement = Cast<UEasyVehicleMovementComponent>(C->GetCharacterMovement());
	if (CharacterMovement)
	{
		CharacterMovement->Safe_bWantsToDrift = Saved_bWantsToDrift;
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
