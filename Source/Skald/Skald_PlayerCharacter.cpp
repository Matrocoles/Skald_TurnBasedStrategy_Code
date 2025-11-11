#include "Skald_PlayerCharacter.h"
#include "Abilities/SkaldAbilityTypes.h"
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

	CachedDefaultArmLength = CameraBoom->TargetArmLength;
	CachedDefaultBoomRotation = CameraBoom->GetRelativeRotation();
	bCachedUsePawnControlRotation = CameraBoom->bUsePawnControlRotation;
	bCachedInheritPitch = CameraBoom->bInheritPitch;
	bCachedInheritYaw = CameraBoom->bInheritYaw;
	bCachedInheritRoll = CameraBoom->bInheritRoll;
	bCachedCameraLag = CameraBoom->bEnableCameraLag;
	CachedCameraLagSpeed = CameraBoom->CameraLagSpeed;
	bCachedDoCollisionTest = CameraBoom->bDoCollisionTest;
	DesiredOverviewZoom = CachedDefaultArmLength;
	OverviewDefaultPitch = CachedDefaultBoomRotation.Pitch;
	OverviewFocusLocation = GetActorLocation();

        DesiredBattleZoom = FMath::Clamp(DefaultBattleZoom, MinBattleZoom, MaxBattleZoom);
        const float InitialYaw = GetActorRotation().Yaw;
        const float InitialPitch = FMath::Clamp(DefaultBattlePitch, MinBattlePitch, MaxBattlePitch);
        DesiredBattleRotation = FRotator(InitialPitch, InitialYaw, 0.f);
        CurrentBattleRotation = DesiredBattleRotation;

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
                CachedDefaultArmLength = CameraBoom->TargetArmLength;
                CachedDefaultBoomRotation = CameraBoom->GetRelativeRotation();
                bCachedUsePawnControlRotation = CameraBoom->bUsePawnControlRotation;
                bCachedInheritPitch = CameraBoom->bInheritPitch;
                bCachedInheritYaw = CameraBoom->bInheritYaw;
                bCachedInheritRoll = CameraBoom->bInheritRoll;
                bCachedCameraLag = CameraBoom->bEnableCameraLag;
                CachedCameraLagSpeed = CameraBoom->CameraLagSpeed;
                bCachedDoCollisionTest = CameraBoom->bDoCollisionTest;
                DesiredOverviewZoom = CameraBoom->TargetArmLength;
                OverviewDefaultPitch = CameraBoom->GetRelativeRotation().Pitch;
        }

        OverviewFocusLocation = GetActorLocation();

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
                        HandleTerritorySelected(WorldMap->SelectedTerritory);
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
                if (bCameraTransitionActive)
                {
                        UpdateCameraTransition(DeltaTime);
                }
                else
                {
                        UpdateBattleCamera(DeltaTime);
                }
        }
        else
        {
                UpdateOverviewCamera(DeltaTime);
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
                if (bBattleCameraLocked)
                {
                        if (!FMath::IsNearlyZero(Value))
                        {
                                bHasManuallyRotatedWhileLocked = true;

                                const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
                                const float PitchDelta = Value * BattleKeyboardPitchSpeed * DeltaTime;
                                const float NewPitch = DesiredBattleRotation.Pitch + PitchDelta;
                                DesiredBattleRotation.Pitch = FMath::Clamp(NewPitch, MinBattlePitch, MaxBattlePitch);
                        }
                        return;
                }

                BattleMoveInput.X = Value;
                return;
        }

	if (FMath::IsNearlyZero(Value) || !Controller)
	{
		return;
	}

	if (bOverviewCameraLocked)
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
                if (bBattleCameraLocked)
                {
                        if (!FMath::IsNearlyZero(Value))
                        {
                                bHasManuallyRotatedWhileLocked = true;

                                const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
                                const float YawDelta = Value * BattleKeyboardYawSpeed * DeltaTime;
                                DesiredBattleRotation.Yaw = FRotator::NormalizeAxis(DesiredBattleRotation.Yaw + YawDelta);
                        }
                        return;
                }

                BattleMoveInput.Y = Value;
                return;
        }

	if (FMath::IsNearlyZero(Value) || !Controller)
	{
		return;
	}

	if (bOverviewCameraLocked)
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

	if (bOverviewCameraLocked)
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
                        if (bBattleCameraLocked)
                        {
                                bHasManuallyRotatedWhileLocked = true;
                        }

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
                        if (bBattleCameraLocked)
                        {
                                bHasManuallyRotatedWhileLocked = true;
                        }

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

	if (!bBattleCameraActive)
	{
		if (Territory)
		{
			FocusOverviewCameraOnTerritory(Territory);
		}
		else
		{
			ClearOverviewCameraFocus();
		}
	}
}

void ASkald_PlayerCharacter::AbilityOne()
{
        if (ASkaldPlayerController* PlayerController = Cast<ASkaldPlayerController>(Controller))
        {
                PlayerController->HandleAbilityInput(ESkaldAbilitySlot::Ability1);
        }
}

void ASkald_PlayerCharacter::AbilityTwo()
{
        if (ASkaldPlayerController* PlayerController = Cast<ASkaldPlayerController>(Controller))
        {
                PlayerController->HandleAbilityInput(ESkaldAbilitySlot::Ability2);
        }
}

void ASkald_PlayerCharacter::AbilityThree()
{
        if (ASkaldPlayerController* PlayerController = Cast<ASkaldPlayerController>(Controller))
        {
                PlayerController->HandleAbilityInput(ESkaldAbilitySlot::Ability3);
        }
}

void ASkald_PlayerCharacter::AdjustZoom(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}

	if (bBattleCameraActive)
{
		const float TargetZoom = DesiredBattleZoom - (Value * BattleZoomStep);
		DesiredBattleZoom = FMath::Clamp(TargetZoom, MinBattleZoom, MaxBattleZoom);
	}
	else
{
		const float TargetZoom = DesiredOverviewZoom - (Value * OverviewZoomStep);
		DesiredOverviewZoom = FMath::Clamp(TargetZoom, OverviewMinZoom, OverviewMaxZoom);
	}
}

void ASkald_PlayerCharacter::FocusOverviewCameraOnTerritory(ATerritory* Territory)
{
	if (!Territory)
	{
		ClearOverviewCameraFocus();
		return;
	}

	LockedOverviewTerritory = Territory;
	bOverviewCameraLocked = true;
	OverviewFocusLocation = Territory->GetActorLocation() + (FVector::UpVector * OverviewFocusHeight);

	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->StopMovementImmediately();
	}
}

void ASkald_PlayerCharacter::ClearOverviewCameraFocus()
{
	bOverviewCameraLocked = false;
	LockedOverviewTerritory.Reset();
	OverviewFocusLocation = GetActorLocation();

	if (Controller)
	{
		FRotator ControlRotation = Controller->GetControlRotation();
		ControlRotation.Pitch = OverviewDefaultPitch;
		Controller->SetControlRotation(ControlRotation);
	}
}

void ASkald_PlayerCharacter::UpdateOverviewCamera(float DeltaTime)
{
	if (!CameraBoom)
	{
		return;
	}

	const float CurrentArmLength = CameraBoom->TargetArmLength;
	const float InterpZoom = FMath::FInterpTo(CurrentArmLength, DesiredOverviewZoom, DeltaTime, OverviewZoomInterpSpeed);
	CameraBoom->TargetArmLength = FMath::Clamp(InterpZoom, OverviewMinZoom, OverviewMaxZoom);

	if (bOverviewCameraLocked)
	{
		FVector DesiredLocation = OverviewFocusLocation;
		if (ATerritory* Territory = LockedOverviewTerritory.Get())
		{
			DesiredLocation = Territory->GetActorLocation() + (FVector::UpVector * OverviewFocusHeight);
		}
		else
		{
			ClearOverviewCameraFocus();
			return;
		}

		OverviewFocusLocation = DesiredLocation;

		const FVector NewLocation = FMath::VInterpTo(GetActorLocation(), DesiredLocation, DeltaTime, OverviewPanInterpSpeed);
		SetActorLocation(NewLocation);

		if (Controller)
		{
			FRotator ControlRotation = Controller->GetControlRotation();
			const float TargetPitch = OverviewLockedPitch;
			const float InterpPitch = FMath::FInterpTo(ControlRotation.Pitch, TargetPitch, DeltaTime, OverviewRotationInterpSpeed);
			ControlRotation.Pitch = InterpPitch;
			Controller->SetControlRotation(ControlRotation);
		}
	}
	else
	{
		OverviewFocusLocation = GetActorLocation();

		if (Controller)
		{
			FRotator ControlRotation = Controller->GetControlRotation();
			const float InterpPitch = FMath::FInterpTo(ControlRotation.Pitch, OverviewDefaultPitch, DeltaTime, OverviewRotationInterpSpeed);
			ControlRotation.Pitch = InterpPitch;
			Controller->SetControlRotation(ControlRotation);
		}
	}
}

void ASkald_PlayerCharacter::SetBattleCameraActive(bool bActive)
{
        if (bBattleCameraActive == bActive)
        {
                if (!bActive)
                {
                        bCameraTransitionActive = false;
                        ClearBattleCameraLock();
                }
                return;
        }

        if (!CameraBoom)
        {
                bCameraTransitionActive = false;
                bBattleCameraActive = bActive;
                return;
        }

        if (bActive)
        {
                ClearOverviewCameraFocus();

                CachedDefaultArmLength = CameraBoom->TargetArmLength;
                CachedDefaultBoomRotation = CameraBoom->GetRelativeRotation();
                bCachedUsePawnControlRotation = CameraBoom->bUsePawnControlRotation;
                bCachedInheritPitch = CameraBoom->bInheritPitch;
                bCachedInheritYaw = CameraBoom->bInheritYaw;
                bCachedInheritRoll = CameraBoom->bInheritRoll;
                bCachedCameraLag = CameraBoom->bEnableCameraLag;
                CachedCameraLagSpeed = CameraBoom->CameraLagSpeed;
                bCachedDoCollisionTest = CameraBoom->bDoCollisionTest;

                bUseControllerRotationYaw = false;
                bUseControllerRotationPitch = false;

                CameraBoom->bUsePawnControlRotation = false;
                CameraBoom->bInheritPitch = false;
                CameraBoom->bInheritYaw = false;
                CameraBoom->bInheritRoll = false;
                CameraBoom->SetUsingAbsoluteRotation(true);
                CameraBoom->bEnableCameraLag = true;
                CameraBoom->CameraLagSpeed = BattleCameraLagSpeed;
                CameraBoom->bDoCollisionTest = false;

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
                bCameraTransitionActive = false;
                ClearBattleCameraLock();

                CameraBoom->SetUsingAbsoluteRotation(false);
                CameraBoom->bUsePawnControlRotation = bCachedUsePawnControlRotation;
                CameraBoom->bInheritPitch = bCachedInheritPitch;
                CameraBoom->bInheritYaw = bCachedInheritYaw;
                CameraBoom->bInheritRoll = bCachedInheritRoll;
                CameraBoom->bEnableCameraLag = bCachedCameraLag;
                CameraBoom->CameraLagSpeed = CachedCameraLagSpeed;
                CameraBoom->bDoCollisionTest = bCachedDoCollisionTest;
                CameraBoom->SetRelativeRotation(CachedDefaultBoomRotation);
                CameraBoom->TargetArmLength = CachedDefaultArmLength;
                DesiredOverviewZoom = CameraBoom->TargetArmLength;
                OverviewDefaultPitch = CameraBoom->GetRelativeRotation().Pitch;

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
        bHasManuallyRotatedWhileLocked = false;
        BattleCameraVelocity = FVector::ZeroVector;

        DesiredBattleZoom = FMath::Clamp(LockedBattleZoom, MinBattleZoom, MaxBattleZoom);

        if (bAutoFaceLockTarget)
        {
                const FRotator TargetRotation = FocusActor->GetActorRotation();
                DesiredBattleRotation.Yaw = TargetRotation.Yaw + LockedBattleYawOffset;
        }

        const float ClampedPitch = FMath::Clamp(LockedBattlePitch, MinBattlePitch, MaxBattlePitch);
        DesiredBattleRotation.Pitch = ClampedPitch;
        DesiredBattleRotation.Roll = 0.f;
}

void ASkald_PlayerCharacter::ClearCameraFocus()
{
        ClearBattleCameraLock();
}

float ASkald_PlayerCharacter::GetCurrentBattleCameraZoom() const
{
        if (CameraBoom)
        {
                return CameraBoom->TargetArmLength;
        }

        return DesiredBattleZoom;
}

void ASkald_PlayerCharacter::StartCameraTransition(const FVector& TargetLocation, const FRotator& TargetRotation, float TargetZoom, float Duration)
{
        const float SanitisedDuration = FMath::Max(Duration, KINDA_SMALL_NUMBER);
        CameraTransitionStartLocation = GetActorLocation();
        CameraTransitionTargetLocation = TargetLocation;
        CameraTransitionStartRotation = CurrentBattleRotation;
        CameraTransitionTargetRotation = TargetRotation;
        CameraTransitionStartZoom = GetCurrentBattleCameraZoom();
        CameraTransitionTargetZoom = FMath::Clamp(TargetZoom, MinBattleZoom, MaxBattleZoom);
        CameraTransitionElapsed = 0.f;
        CameraTransitionDuration = SanitisedDuration;
        bCameraTransitionActive = true;

        BattleCameraVelocity = FVector::ZeroVector;
        BattleMoveInput = FVector2D::ZeroVector;
}

void ASkald_PlayerCharacter::UpdateCameraTransition(float DeltaTime)
{
        if (!bCameraTransitionActive)
        {
                return;
        }

        CameraTransitionElapsed += DeltaTime;
        const float Duration = FMath::Max(CameraTransitionDuration, KINDA_SMALL_NUMBER);
        const float Alpha = FMath::Clamp(CameraTransitionElapsed / Duration, 0.f, 1.f);

        const FVector NewLocation = FMath::Lerp(CameraTransitionStartLocation, CameraTransitionTargetLocation, Alpha);
        SetActorLocation(NewLocation);

        const FQuat StartQuat = CameraTransitionStartRotation.Quaternion();
        const FQuat TargetQuat = CameraTransitionTargetRotation.Quaternion();
        const FQuat BlendedQuat = FQuat::Slerp(StartQuat, TargetQuat, Alpha).GetNormalized();
        const FRotator NewRotation = BlendedQuat.Rotator();

        DesiredBattleRotation = NewRotation;
        CurrentBattleRotation = NewRotation;

        const float NewZoom = FMath::Lerp(CameraTransitionStartZoom, CameraTransitionTargetZoom, Alpha);
        DesiredBattleZoom = NewZoom;

        if (CameraBoom)
        {
                CameraBoom->SetWorldRotation(NewRotation);
                CameraBoom->TargetArmLength = NewZoom;
        }

        if (Alpha >= 1.f - KINDA_SMALL_NUMBER)
        {
                bCameraTransitionActive = false;
                CameraTransitionElapsed = CameraTransitionDuration;
                DesiredBattleRotation = CameraTransitionTargetRotation;
                CurrentBattleRotation = CameraTransitionTargetRotation;

                if (CameraBoom)
                {
                        CameraBoom->SetWorldRotation(CameraTransitionTargetRotation);
                        CameraBoom->TargetArmLength = CameraTransitionTargetZoom;
                }
        }
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
                        const FVector TargetLocation = FocusActor->GetActorLocation() + GetBattleLockWorldOffset(FocusActor);
                        const FVector NewLocation = FMath::VInterpTo(GetActorLocation(), TargetLocation, DeltaTime, BattleLockInterpSpeed);
                        SetActorLocation(NewLocation);

                        if (bAutoFaceLockTarget && !bHasManuallyRotatedWhileLocked)
                        {
                                DesiredBattleRotation.Yaw = FocusActor->GetActorRotation().Yaw + LockedBattleYawOffset;
                        }
                }
                else
                {
                        ClearBattleCameraLock();
                }

                BattleCameraVelocity = FVector::ZeroVector;
                return;
        }

        const FRotator YawRotation(0.f, CurrentBattleRotation.Yaw, 0.f);
        const FVector ForwardVector = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
        const FVector RightVector = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
        FVector DesiredVelocity = (ForwardVector * BattleMoveInput.X + RightVector * BattleMoveInput.Y) * BattlePanSpeed;

        BattleCameraVelocity = FMath::VInterpTo(BattleCameraVelocity, DesiredVelocity, DeltaTime, BattlePanSmoothing);

        if (!BattleCameraVelocity.IsNearlyZero(1.f))
        {
                const FVector NewLocation = GetActorLocation() + BattleCameraVelocity * DeltaTime;
                SetActorLocation(NewLocation);
        }
	else
        {
                BattleCameraVelocity = FVector::ZeroVector;
        }
}

void ASkald_PlayerCharacter::ClearBattleCameraLock()
{
        bBattleCameraLocked = false;
        LockedBattleActor = nullptr;
        BattleCameraVelocity = FVector::ZeroVector;
        bHasManuallyRotatedWhileLocked = false;

        DesiredBattleZoom = FMath::Clamp(DefaultBattleZoom, MinBattleZoom, MaxBattleZoom);
        const float ClampedPitch = FMath::Clamp(DefaultBattlePitch, MinBattlePitch, MaxBattlePitch);
        DesiredBattleRotation.Pitch = ClampedPitch;
        DesiredBattleRotation.Roll = 0.f;
}

FVector ASkald_PlayerCharacter::GetBattleLockWorldOffset(const AActor* FocusActor) const
{
        if (!FocusActor)
        {
                return FVector::ZeroVector;
        }

        const FVector Up = FVector::UpVector;

        const FVector Forward = FocusActor->GetActorForwardVector();
        const FVector Right = FocusActor->GetActorRightVector();

        FVector Offset = Up * BattleLockRelativeOffset.Z;

        if (!FMath::IsNearlyZero(BattleLockRelativeOffset.X))
        {
                // Positive X offsets should position the camera behind the focus actor
                // so that the camera looks in the same direction as the fighter.
                Offset -= Forward * BattleLockRelativeOffset.X;
        }

        if (!FMath::IsNearlyZero(BattleLockRelativeOffset.Y))
        {
                Offset += Right * BattleLockRelativeOffset.Y;
        }

        return Offset;
}

