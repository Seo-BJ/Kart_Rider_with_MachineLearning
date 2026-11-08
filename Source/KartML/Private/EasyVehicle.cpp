#include "EasyVehicle.h"
#include "Net/UnrealNetwork.h"
#include "EasyVehicleMovementComponent.h"
#include "DrawDebugHelpers.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "UObject/ConstructorHelpers.h"

AEasyVehicle::AEasyVehicle(const FObjectInitializer& ObejctInitializer)
    : Super(ObejctInitializer.SetDefaultSubobjectClass<UEasyVehicleMovementComponent>(ACharacter::CharacterMovementComponentName))
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;

    // 차체 회전은 UEasyVehicleMovementComponent::UpdateDrift가 정한다 (컨트롤 회전을 따라가지 않음)
    bUseControllerRotationYaw = false;

    VehicleMovementComponent = Cast<UEasyVehicleMovementComponent>(GetCharacterMovement());

    static ConstructorHelpers::FObjectFinder<UInputAction> MoveActionFinder(TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
    if (MoveActionFinder.Succeeded())
    {
        MoveAction = MoveActionFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UInputAction> DriftActionFinder(TEXT("/Game/Input/Actions/IA_Drift.IA_Drift"));
    if (DriftActionFinder.Succeeded())
    {
        DriftAction = DriftActionFinder.Object;
    }
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

    if (!bBindDriveInputInCpp)
    {
        return;
    }

    UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    if (!EnhancedInput)
    {
        UE_LOG(LogTemp, Error, TEXT("AEasyVehicle: EnhancedInputComponent가 아니라서 주행 입력을 바인딩하지 못했습니다."));
        return;
    }

    if (MoveAction)
    {
        EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AEasyVehicle::OnMove);
        EnhancedInput->BindAction(MoveAction, ETriggerEvent::Completed, this, &AEasyVehicle::OnMoveCompleted);
    }

    if (DriftAction)
    {
        // Down 동작이면 Started/Completed가 짝이다. 트리거(Hold 등)를 추가해도 키가 남지 않게 Canceled도 묶는다.
        EnhancedInput->BindAction(DriftAction, ETriggerEvent::Started, this, &AEasyVehicle::OnDriftStarted);
        EnhancedInput->BindAction(DriftAction, ETriggerEvent::Completed, this, &AEasyVehicle::OnDriftCompleted);
        EnhancedInput->BindAction(DriftAction, ETriggerEvent::Canceled, this, &AEasyVehicle::OnDriftCompleted);
    }
}

void AEasyVehicle::OnMove(const FInputActionValue& Value)
{
    const FVector2D Axis = Value.Get<FVector2D>();

    if (VehicleMovementComponent)
    {
        VehicleMovementComponent->SetSteerInput(static_cast<float>(Axis.X));
    }

    // 가속은 크기와 앞/뒤만 의미가 있다. 방향은 이동 컴포넌트가 차체 방향으로 다시 만든다.
    if (!FMath::IsNearlyZero(Axis.Y))
    {
        AddMovementInput(GetActorForwardVector(), static_cast<float>(Axis.Y));
    }
}

void AEasyVehicle::OnMoveCompleted(const FInputActionValue& Value)
{
    if (VehicleMovementComponent)
    {
        VehicleMovementComponent->SetSteerInput(0.f);
    }
}

void AEasyVehicle::OnDriftStarted(const FInputActionValue& Value)
{
    if (VehicleMovementComponent)
    {
        VehicleMovementComponent->DriftPressed();
    }
}

void AEasyVehicle::OnDriftCompleted(const FInputActionValue& Value)
{
    if (VehicleMovementComponent)
    {
        VehicleMovementComponent->DriftReleased();
    }
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
