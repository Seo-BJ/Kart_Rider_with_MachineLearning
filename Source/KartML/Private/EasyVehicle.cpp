#include "EasyVehicle.h"
#include "Net/UnrealNetwork.h"
#include "EasyVehicleMovementComponent.h"
#include "DrawDebugHelpers.h"

AEasyVehicle::AEasyVehicle(const FObjectInitializer& ObejctInitializer)
    : Super(ObejctInitializer.SetDefaultSubobjectClass<UEasyVehicleMovementComponent>(ACharacter::CharacterMovementComponentName))
{   
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;

    VehicleMovementComponent = Cast<UEasyVehicleMovementComponent>(GetCharacterMovement());
}


// Called when the game starts or when spawned
void AEasyVehicle::BeginPlay()
{
    Super::BeginPlay();
}

// Replication properties
void AEasyVehicle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

// Called every frame
void AEasyVehicle::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Early return if debug is disabled
    if (!bEnableDebug)
    {
        return;
    }

    // Update local positions and rotations
    TimeSinceLastUpdate += DeltaTime;
    if (TimeSinceLastUpdate >= PositionUpdateInterval)
    {
        if (!HasAuthority() && GetLocalRole() == ROLE_AutonomousProxy)
        {
            // Save local position and rotation (once only, no duplication)
            LocalPositions.Add(GetActorLocation());
            LocalRotations.Add(GetActorRotation());

            // Limit array size to prevent memory leak
            if (LocalPositions.Num() > MaxHistorySize)
            {
                LocalPositions.RemoveAt(0);
                LocalRotations.RemoveAt(0);
            }

            FString NewLine = FString::Printf(TEXT("%f,%f,%f\n"), GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z);

            // 파일 경로: Saved/SimulatedPositions.csv
            FString FilePath = FPaths::ProjectSavedDir() + "LocalPositions.csv";

            FFileHelper::SaveStringToFile(
                NewLine,
                *FilePath,
                FFileHelper::EEncodingOptions::AutoDetect,
                &IFileManager::Get(),
                FILEWRITE_Append
            );
        }

        if (!HasAuthority() && GetLocalRole() == ROLE_SimulatedProxy)
        {
            // Save server position and rotation (replicated)
            SimulatedPositions.Add(GetActorLocation());
            SimulatedRotations.Add(GetActorRotation());

            // Limit server array size
            if (SimulatedPositions.Num() > MaxHistorySize)
            {
                SimulatedPositions.RemoveAt(0);
                SimulatedRotations.RemoveAt(0);
            }
        }
        // Reset update time
        TimeSinceLastUpdate = 0.0f;
    }

    // DrawDebugPositions();
}

// Called to bind functionality to input
void AEasyVehicle::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
}

void AEasyVehicle::DrawDebugPositions()
{
    // Calculate start index to only draw the most recent MaxDrawCount positions
    const int32 LocalStartIdx = FMath::Max(0, LocalPositions.Num() - MaxDrawCount);

    // Draw local positions and rotations in green
    for (int32 i = LocalStartIdx; i < LocalPositions.Num(); ++i)
    {
        const FVector& LocalPos = LocalPositions[i];
        const FRotator& LocalRot = LocalRotations[i];
        DrawDebugBox(GetWorld(), LocalPos, FVector(30.0f, 30.0f, 30.0f), FQuat(LocalRot), FColor::Green, false, -1, 0, 2.0f);
    }
    
    // Calculate start index for server positions
    const int32 ServerStartIdx = FMath::Max(0, SimulatedPositions.Num() - MaxDrawCount);

    // Draw server positions and rotations in red
    for (int32 i = ServerStartIdx; i < SimulatedPositions.Num(); ++i)
    {
        const FVector& ServerPos = SimulatedPositions[i];
        const FRotator& ServerRot = SimulatedRotations[i];
        DrawDebugBox(GetWorld(), ServerPos, FVector(30.0f, 30.0f, 30.0f), FQuat(ServerRot), FColor::Red, false, -1, 0, 2.0f);
    }
}
