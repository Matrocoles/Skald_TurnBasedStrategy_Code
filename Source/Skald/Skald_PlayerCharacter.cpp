#include "Skald_PlayerCharacter.h"
#include "Skald.h"
#include "SkaldLogging.h"
#include "WorldMap.h"
#include "Territory.h"
#include "Skald_PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "Skald_GameMode.h"
#include "Skald_GameState.h"
#include "Skald_GameInstance.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Math/RotationMatrix.h"
#include "TimerManager.h"

// Sets default values
ASkald_PlayerCharacter::ASkald_PlayerCharacter()
{
        PrimaryActorTick.bCanEverTick = true;

        bUseControllerRotationYaw = true;
        bUseControllerRotationPitch = true;

        WorldMap = nullptr;
        CurrentSelection = nullptr;

        CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
        CameraBoom->SetupAttachment(RootComponent);
        CameraBoom->bUsePawnControlRotation = true;

        FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
        FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
        FollowCamera->bUsePawnControlRotation = false;

        CachedBoomRelativeLocation = CameraBoom->GetRelativeLocation();
        bCachedAbsoluteLocation = CameraBoom->IsUsingAbsoluteLocation();
        CachedDefaultArmLength = CameraBoom->TargetArmLength;
        CachedDefaultBoomRotation = CameraBoom->GetRelativeRotation();
        bCachedUsePawnControlRotation = CameraBoom->bUsePawnControlRotation;
        bCachedInheritPitch = CameraBoom->bInheritPitch;
        bCachedInheritYaw = CameraBoom->bInheritYaw;
        bCachedInheritRoll = CameraBoom->bInheritRoll;
        bCachedCameraLag = CameraBoom->bEnableCameraLag;
        CachedCameraLagSpeed = CameraBoom->CameraLagSpeed;
        bCachedDoCollisionTest = CameraBoom->bDoCollisionTest;

        DesiredBattleZoom = FMath::Clamp(DefaultBattleZoom, MinBattleZoom, MaxBattleZoom);
        const float InitialYaw = GetActorRotation().Yaw;
        const float InitialPitch = FMath::Clamp(DefaultBattlePitch, MinBattlePitch, MaxBattlePitch);
        DesiredBattleRotation = FRotator(InitialPitch, InitialYaw, 0.f);
        CurrentBattleRotation = DesiredBattleRotation;
        BattleCameraLocation = GetActorLocation();

        if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
        {
                MovementComponent->SetMovementMode(MOVE_Flying);
                MovementComponent->bOrientRotationToMovement = false;
                MovementComponent->GravityScale = 0.0f;
                MovementComponent->BrakingDecelerationFlying = MovementComponent->BrakingDecelerationWalking;
        }
}

// Called when the game starts or when spawned
void ASkald_PlayerCharacter::BeginPlay()
{
        Super::BeginPlay();

        if (CameraBoom)
        {
                CachedBoomRelativeLocation = CameraBoom->GetRelativeLocation();
                bCachedAbsoluteLocation = CameraBoom->IsUsingAbsoluteLocation();
                CachedDefaultArmLength = CameraBoom->TargetArmLength;
                CachedDefaultBoomRotation = CameraBoom->GetRelativeRotation();
                bCachedUsePawnControlRotation = CameraBoom->bUsePawnControlRotation;
                bCachedInheritPitch = CameraBoom->bInheritPitch;
                bCachedInheritYaw = CameraBoom->bInheritYaw;
                bCachedInheritRoll = CameraBoom->bInheritRoll;
                bCachedCameraLag = CameraBoom->bEnableCameraLag;
                CachedCameraLagSpeed = CameraBoom->CameraLagSpeed;
                bCachedDoCollisionTest = CameraBoom->bDoCollisionTest;
        }

        BattleCameraLocation = GetActorLocation();

        TryCacheWorldMap();

        CachedGameMode = GetWorld()->GetAuthGameMode<ASkaldGameMode>();
        CachedGameState = GetWorld()->GetGameState<ASkaldGameState>();
        CachedGameInstance = GetGameInstance<USkaldGameInstance>();
}

void ASkald_PlayerCharacter::TryCacheWorldMap()
{
        if (!WorldMap)
        {
                if (AWorldMap* Found = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass())))
                {
                        WorldMap = Found;
                        WorldMap->OnTerritorySelected.AddUniqueDynamic(this, &ASkald_PlayerCharacter::HandleTerritorySelected);
                        GetWorldTimerManager().ClearTimer(WorldMapSearchHandle);
                }
                else
                {
                        GetWorldTimerManager().SetTimer(WorldMapSearchHandle, this, &ASkald_PlayerCharacter::TryCacheWorldMap, 0.5f, false);
                }
        }
}

// Called every frame
void ASkald_PlayerCharacter::Tick(float DeltaTime)
{
        Super::Tick(DeltaTime);

        if (bBattleCameraActive)
        {
                UpdateBattleCamera(DeltaTime);
        }

        // Example tick behavior: keep track of selection validity
        if (!IsValid(CurrentSelection))
        {
                CurrentSelection = nullptr;
        }
}

// Called to bind functionality to input
void ASkald_PlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
        Super::SetupPlayerInputComponent(PlayerInputComponent);

        check(PlayerInputComponent);

        PlayerInputComponent->BindAxis("MoveForward", this, &ASkald_PlayerCharacter::MoveForward);
        PlayerInputComponent->BindAxis("MoveRight", this, &ASkald_PlayerCharacter::MoveRight);
       PlayerInputComponent->BindAxis("MoveUp", this, &ASkald_PlayerCharacter::MoveUp);
        PlayerInputComponent->BindAxis("Turn", this, &ASkald_PlayerCharacter::Turn);
        PlayerInputComponent->BindAxis("LookUp", this, &ASkald_PlayerCharacter::LookUp);
        PlayerInputComponent->BindAxis("BattleZoom", this, &ASkald_PlayerCharacter::AdjustZoom);

        PlayerInputComponent->BindAction("Ability1", IE_Pressed, this, &ASkald_PlayerCharacter::AbilityOne);
        PlayerInputComponent->BindAction("Ability2", IE_Pressed, this, &ASkald_PlayerCharacter::AbilityTwo);
        PlayerInputComponent->BindAction("Ability3", IE_Pressed, this, &ASkald_PlayerCharacter::AbilityThree);
}

void ASkald_PlayerCharacter::MoveForward(float Value)
{
        if (bBattleCameraActive)
        {
                BattleMoveInput.X = Value;
                return;
        }

        if (FMath::IsNearlyZero(Value) || !Controller)
        {
                return;
        }

        const FRotator ControlRotation = Controller->GetControlRotation();
        const FRotationMatrix ControlRotMatrix(FRotator(0.f, ControlRotation.Yaw, 0.f));
        const FVector ForwardVector = ControlRotMatrix.GetUnitAxis(EAxis::X);
        AddMovementInput(ForwardVector, Value);
}

void ASkald_PlayerCharacter::MoveRight(float Value)
{
        if (bBattleCameraActive)
        {
                BattleMoveInput.Y = Value;
                return;
        }

        if (FMath::IsNearlyZero(Value) || !Controller)
        {
                return;
        }

        const FRotator ControlRotation = Controller->GetControlRotation();
        const FRotationMatrix ControlRotMatrix(FRotator(0.f, ControlRotation.Yaw, 0.f));
        const FVector RightVector = ControlRotMatrix.GetUnitAxis(EAxis::Y);
        AddMovementInput(RightVector, Value);
}

void ASkald_PlayerCharacter::MoveUp(float Value)
{
        if (bBattleCameraActive)
        {
                return;
        }

        if (!FMath::IsNearlyZero(Value))
        {
                // Move strictly along world Z to ensure SpaceBar and LeftControl
                // translate vertically regardless of character rotation
                AddMovementInput(FVector::UpVector, Value);
        }
}

void ASkald_PlayerCharacter::Turn(float Value)
{
        if (bBattleCameraActive)
        {
                if (!FMath::IsNearlyZero(Value))
                {
                        DesiredBattleRotation.Yaw = FRotator::NormalizeAxis(
                                DesiredBattleRotation.Yaw + (Value * BattleMouseYawSpeed));
                }
                return;
        }

        if (!FMath::IsNearlyZero(Value))
        {
                AddControllerYawInput(Value);
        }
}

void ASkald_PlayerCharacter::LookUp(float Value)
{
        if (bBattleCameraActive)
        {
                if (!FMath::IsNearlyZero(Value))
                {
                        const float NewPitch = DesiredBattleRotation.Pitch + (Value * BattleMousePitchSpeed);
                        DesiredBattleRotation.Pitch = FMath::Clamp(NewPitch, MinBattlePitch, MaxBattlePitch);
                }
                return;
        }

        if (!FMath::IsNearlyZero(Value))
        {
                AddControllerPitchInput(Value);
        }
}

void ASkald_PlayerCharacter::Select()
{
        APlayerController* PlayerController = Cast<APlayerController>(Controller);
        if (!PlayerController)
        {
                return;
        }

        FHitResult Hit;
        if (PlayerController->GetHitResultUnderCursor(ECC_Visibility, false, Hit))
        {
                if (Cast<ATerritory>(Hit.GetActor()))
                {
                        // ATerritory::HandleClicked already issues the RPC with toggle state
                        return;
                }
        }

        // Clicked empty space; request deselection
        if (ASkaldPlayerController* PC = Cast<ASkaldPlayerController>(PlayerController))
        {
                PC->ServerSelectTerritory(-1);
        }
}

void ASkald_PlayerCharacter::HandleTerritorySelected(ATerritory* Territory)
{
        CurrentSelection = Territory;
}

void ASkald_PlayerCharacter::AbilityOne()
{
        if (CurrentSelection)
        {
                UE_LOG(LogSkald, Log, TEXT("%s used Ability One on %s"), *GetName(), *CurrentSelection->GetName());
        }
}

void ASkald_PlayerCharacter::AbilityTwo()
{
        if (CurrentSelection)
        {
                UE_LOG(LogSkald, Log, TEXT("%s used Ability Two"), *GetName());
        }
}

void ASkald_PlayerCharacter::AbilityThree()
{
        UE_LOG(LogSkald, Log, TEXT("%s used Ability Three"), *GetName());
}

void ASkald_PlayerCharacter::AdjustZoom(float Value)
{
        if (!bBattleCameraActive || FMath::IsNearlyZero(Value))
        {
                return;
        }

        const float TargetZoom = DesiredBattleZoom - (Value * BattleZoomStep);
        DesiredBattleZoom = FMath::Clamp(TargetZoom, MinBattleZoom, MaxBattleZoom);
}

void ASkald_PlayerCharacter::SetBattleCameraActive(bool bActive)
{
        if (bBattleCameraActive == bActive)
        {
                if (!bActive)
                {
                        ClearBattleCameraLock();
                }
                return;
        }

        if (!CameraBoom)
        {
                bBattleCameraActive = bActive;
                return;
        }

        if (bActive)
        {
                if (!bBattleCameraDetached)
                {
                        CameraBoom->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
                        bBattleCameraDetached = true;
                }

                BattleCameraLocation = CameraBoom->GetComponentLocation();
                CachedDefaultArmLength = CameraBoom->TargetArmLength;
                CachedDefaultBoomRotation = CameraBoom->GetRelativeRotation();
                bCachedUsePawnControlRotation = CameraBoom->bUsePawnControlRotation;
                bCachedInheritPitch = CameraBoom->bInheritPitch;
                bCachedInheritYaw = CameraBoom->bInheritYaw;
                bCachedInheritRoll = CameraBoom->bInheritRoll;
                bCachedCameraLag = CameraBoom->bEnableCameraLag;
                CachedCameraLagSpeed = CameraBoom->CameraLagSpeed;
                bCachedDoCollisionTest = CameraBoom->bDoCollisionTest;
                CachedBoomRelativeLocation = CameraBoom->GetRelativeLocation();
                bCachedAbsoluteLocation = CameraBoom->IsUsingAbsoluteLocation();

                bUseControllerRotationYaw = false;
                bUseControllerRotationPitch = false;

                CameraBoom->bUsePawnControlRotation = false;
                CameraBoom->bInheritPitch = false;
                CameraBoom->bInheritYaw = false;
                CameraBoom->bInheritRoll = false;
                CameraBoom->SetUsingAbsoluteRotation(true);
                CameraBoom->SetUsingAbsoluteLocation(true);
                CameraBoom->bEnableCameraLag = true;
                CameraBoom->CameraLagSpeed = BattleCameraLagSpeed;
                CameraBoom->bDoCollisionTest = false;
                CameraBoom->SetWorldLocation(BattleCameraLocation);

                DesiredBattleZoom = FMath::Clamp(DefaultBattleZoom, MinBattleZoom, MaxBattleZoom);
                CameraBoom->TargetArmLength = DesiredBattleZoom;

                const float StartingYaw = CameraBoom->GetComponentRotation().Yaw;
                const float ClampedPitch = FMath::Clamp(DefaultBattlePitch, MinBattlePitch, MaxBattlePitch);
                DesiredBattleRotation = FRotator(ClampedPitch, StartingYaw, 0.f);
                CurrentBattleRotation = DesiredBattleRotation;
                CameraBoom->SetWorldRotation(CurrentBattleRotation);

                BattleMoveInput = FVector2D::ZeroVector;
                BattleCameraVelocity = FVector::ZeroVector;

                if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
                {
                        MovementComponent->StopMovementImmediately();
                }

                bBattleCameraActive = true;
        }
        else
        {
                ClearBattleCameraLock();

                if (bBattleCameraDetached)
                {
                        CameraBoom->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
                        bBattleCameraDetached = false;
                }

                CameraBoom->SetUsingAbsoluteRotation(false);
                CameraBoom->SetUsingAbsoluteLocation(bCachedAbsoluteLocation);
                CameraBoom->bUsePawnControlRotation = bCachedUsePawnControlRotation;
                CameraBoom->bInheritPitch = bCachedInheritPitch;
                CameraBoom->bInheritYaw = bCachedInheritYaw;
                CameraBoom->bInheritRoll = bCachedInheritRoll;
                CameraBoom->bEnableCameraLag = bCachedCameraLag;
                CameraBoom->CameraLagSpeed = CachedCameraLagSpeed;
                CameraBoom->bDoCollisionTest = bCachedDoCollisionTest;
                CameraBoom->SetRelativeRotation(CachedDefaultBoomRotation);
                CameraBoom->TargetArmLength = CachedDefaultArmLength;
                CameraBoom->SetRelativeLocation(CachedBoomRelativeLocation);

                bUseControllerRotationYaw = true;
                bUseControllerRotationPitch = true;

                BattleMoveInput = FVector2D::ZeroVector;
                BattleCameraVelocity = FVector::ZeroVector;

                bBattleCameraActive = false;
        }
}

void ASkald_PlayerCharacter::FocusCameraOnActor(AActor* FocusActor)
{
        if (!bBattleCameraActive || !FocusActor)
        {
                return;
        }

        LockedBattleActor = FocusActor;
        bBattleCameraLocked = true;
        BattleCameraVelocity = FVector::ZeroVector;

        if (bAutoFaceLockTarget)
        {
                FVector ToTarget = FocusActor->GetActorLocation() - GetActorLocation();
                ToTarget.Z = 0.f;
                if (!ToTarget.IsNearlyZero())
                {
                        DesiredBattleRotation.Yaw = ToTarget.Rotation().Yaw;
                }
        }
}

void ASkald_PlayerCharacter::ClearCameraFocus()
{
        ClearBattleCameraLock();
}

void ASkald_PlayerCharacter::UpdateBattleCamera(float DeltaTime)
{
        if (!CameraBoom)
        {
                return;
        }

        if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
        {
                MovementComponent->Velocity = FVector::ZeroVector;
        }

        CurrentBattleRotation = FMath::RInterpTo(CurrentBattleRotation, DesiredBattleRotation, DeltaTime, BattleRotationInterpSpeed);
        CameraBoom->SetWorldRotation(CurrentBattleRotation);

        const float CurrentArmLength = CameraBoom->TargetArmLength;
        const float TargetArmLength = FMath::FInterpTo(CurrentArmLength, DesiredBattleZoom, DeltaTime, BattleZoomInterpSpeed);
        CameraBoom->TargetArmLength = TargetArmLength;

        if (bBattleCameraLocked)
        {
                if (AActor* FocusActor = LockedBattleActor.Get())
                {
                        FVector TargetLocation = FocusActor->GetActorLocation() + BattleLockOffset;
                        TargetLocation.Z = BattleCameraLocation.Z + BattleLockOffset.Z;
                        BattleCameraLocation = FMath::VInterpTo(BattleCameraLocation, TargetLocation, DeltaTime, BattleLockInterpSpeed);
                }
                else
                {
                        ClearBattleCameraLock();
                }

                BattleCameraVelocity = FVector::ZeroVector;
                CameraBoom->SetWorldLocation(BattleCameraLocation);
                return;
        }

        const FRotator YawRotation(0.f, CurrentBattleRotation.Yaw, 0.f);
        const FVector ForwardVector = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
        const FVector RightVector = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
        FVector DesiredVelocity = (ForwardVector * BattleMoveInput.X + RightVector * BattleMoveInput.Y) * BattlePanSpeed;

        BattleCameraVelocity = FMath::VInterpTo(BattleCameraVelocity, DesiredVelocity, DeltaTime, BattlePanSmoothing);

        if (!BattleCameraVelocity.IsNearlyZero(1.f))
        {
                BattleCameraLocation += BattleCameraVelocity * DeltaTime;
        }
        else
        {
                BattleCameraVelocity = FVector::ZeroVector;
        }

        CameraBoom->SetWorldLocation(BattleCameraLocation);
}

void ASkald_PlayerCharacter::ClearBattleCameraLock()
{
        bBattleCameraLocked = false;
        LockedBattleActor = nullptr;
        BattleCameraVelocity = FVector::ZeroVector;
}

