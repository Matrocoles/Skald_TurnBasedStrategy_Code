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
#include "InputCoreTypes.h"
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
        CameraBoom->bDoCollisionTest = false;

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
	DesiredOverviewZoom = FMath::Clamp(OverviewSpawnZoom, OverviewMinZoom, OverviewMaxZoom);
        OverviewDefaultPitch = OverviewTopDownPitch;
        OverviewFocusLocation = GetActorLocation();
        OverviewWorldPivot = OverviewFocusLocation;
        DesiredOverviewRotation = FRotator(OverviewDefaultPitch, GetActorRotation().Yaw, 0.f);
        CurrentOverviewRotation = DesiredOverviewRotation;

        if (CameraBoom)
        {
                CameraBoom->TargetArmLength = DesiredOverviewZoom;
        }

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
                DesiredOverviewZoom = FMath::Clamp(OverviewSpawnZoom, OverviewMinZoom, OverviewMaxZoom);
                CameraBoom->TargetArmLength = DesiredOverviewZoom;
                OverviewDefaultPitch = OverviewTopDownPitch;
                DesiredOverviewRotation = FRotator(OverviewDefaultPitch, GetActorRotation().Yaw, 0.f);
                DesiredOverviewRotation.Pitch = FMath::Clamp(DesiredOverviewRotation.Pitch, OverviewMinPitch, OverviewMaxPitch);
                DesiredOverviewRotation.Yaw = FMath::Clamp(DesiredOverviewRotation.Yaw, OverviewMinYaw, OverviewMaxYaw);
                DesiredOverviewRotation.Roll = 0.f;
                CurrentOverviewRotation = DesiredOverviewRotation;
        }

        RefreshOverviewPivot();
        InitializeOverviewCamera();

        TryCacheWorldMap();

        CachedGameMode = GetWorld()->GetAuthGameMode<ASkaldGameMode>();
        CachedGameState = GetWorld()->GetGameState<ASkaldGameState>();
        CachedGameInstance = GetGameInstance<USkaldGameInstance>();
}

void ASkald_PlayerCharacter::PossessedBy(AController* NewController)
{
        Super::PossessedBy(NewController);

        InitializeOverviewCamera();
}


void ASkald_PlayerCharacter::OnRep_Controller()
{
        Super::OnRep_Controller();

        InitializeOverviewCamera();
}


void ASkald_PlayerCharacter::TryCacheWorldMap()
{
        if (!WorldMap)
        {
                if (AWorldMap* Found = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass())))
                {
                        WorldMap = Found;
                        RefreshOverviewPivot();
                        InitializeOverviewCamera();
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

FVector ASkald_PlayerCharacter::ComputeOverviewPivotLocation() const
{
        FVector PivotLocation = GetActorLocation();

        if (WorldMap)
        {
                PivotLocation = WorldMap->GetActorLocation() + OverviewPivotOffset;
        }

        return PivotLocation;
}

void ASkald_PlayerCharacter::RefreshOverviewPivot()
{
        OverviewWorldPivot = ComputeOverviewPivotLocation();

        if (!bBattleCameraActive && !bOverviewCameraLocked && !OverviewDiceCameraState.bActive)
        {
                OverviewFocusLocation = OverviewWorldPivot;
        }
}

bool ASkald_PlayerCharacter::ShouldProcessOverviewMouseInput() const
{
        if (bBattleCameraActive)
        {
                return false;
        }

        if (const APlayerController* PlayerController = Cast<APlayerController>(Controller))
        {
                return PlayerController->IsInputKeyDown(EKeys::RightMouseButton);
        }

        return false;
}

FVector2D ASkald_PlayerCharacter::GetLockedOverviewPitchRange() const
{
        const float SanitizedMin = FMath::Min(OverviewLockedMinPitch, OverviewLockedMaxPitch);
        const float SanitizedMax = FMath::Max(OverviewLockedMinPitch, OverviewLockedMaxPitch);
        const float ClampedMin = FMath::Clamp(SanitizedMin, OverviewMinPitch, OverviewMaxPitch);
        const float ClampedMax = FMath::Clamp(SanitizedMax, OverviewMinPitch, OverviewMaxPitch);
        return FVector2D(ClampedMin, ClampedMax);
}

void ASkald_PlayerCharacter::InitializeOverviewCamera()
{
        RefreshOverviewPivot();

        if (CameraBoom)
        {
                DesiredOverviewZoom = FMath::Clamp(OverviewSpawnZoom, OverviewMinZoom, OverviewMaxZoom);
                CameraBoom->TargetArmLength = DesiredOverviewZoom;
        }

        const float ClampedPitch = FMath::Clamp(OverviewTopDownPitch, OverviewMinPitch, OverviewMaxPitch);
        OverviewDefaultPitch = ClampedPitch;

        FRotator TargetRotation = FRotator(ClampedPitch, DesiredOverviewRotation.Yaw, 0.f);
        if (Controller)
        {
                FRotator ControlRotation = Controller->GetControlRotation();
                TargetRotation.Yaw = FMath::Clamp(ControlRotation.Yaw, OverviewMinYaw, OverviewMaxYaw);
                TargetRotation.Roll = 0.f;
                Controller->SetControlRotation(TargetRotation);
        }

        TargetRotation.Yaw = FMath::Clamp(TargetRotation.Yaw, OverviewMinYaw, OverviewMaxYaw);
        TargetRotation.Roll = 0.f;
        DesiredOverviewRotation = TargetRotation;
        CurrentOverviewRotation = TargetRotation;

        if (!bBattleCameraActive && !bOverviewCameraLocked && !OverviewDiceCameraState.bActive)
        {
                SetActorLocation(OverviewWorldPivot);
                OverviewFocusLocation = OverviewWorldPivot;

                if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
                {
                        MovementComponent->StopMovementImmediately();
                }
        }

        bHasInitializedOverviewCamera = true;
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

        if (FMath::IsNearlyZero(Value))
        {
                return;
        }

        const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
        if (DeltaTime <= 0.f)
        {
                return;
        }

        const float PitchDelta = Value * OverviewKeyboardPitchSpeed * DeltaTime;
        const float NewPitch = FMath::Clamp(DesiredOverviewRotation.Pitch + PitchDelta, OverviewMinPitch, OverviewMaxPitch);
        DesiredOverviewRotation.Pitch = NewPitch;

        if (!bOverviewCameraLocked)
        {
                OverviewDefaultPitch = NewPitch;
        }
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

        if (FMath::IsNearlyZero(Value))
        {
                return;
        }

        const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
        if (DeltaTime <= 0.f)
        {
                return;
        }

        const float YawDelta = Value * OverviewKeyboardYawSpeed * DeltaTime;
        const float NewYaw = FMath::Clamp(DesiredOverviewRotation.Yaw + YawDelta, OverviewMinYaw, OverviewMaxYaw);
        DesiredOverviewRotation.Yaw = NewYaw;
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
                // Move strictly along world Z to ensure keyboard vertical inputs translate
                // along the global up axis regardless of character rotation
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

        if (FMath::IsNearlyZero(Value))
        {
                return;
        }

        if (!ShouldProcessOverviewMouseInput())
        {
                return;
        }

        const float YawDelta = Value * OverviewMouseYawSpeed;
        const float NewYaw = FMath::Clamp(DesiredOverviewRotation.Yaw + YawDelta, OverviewMinYaw, OverviewMaxYaw);
        DesiredOverviewRotation.Yaw = NewYaw;
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

        if (FMath::IsNearlyZero(Value))
        {
                return;
        }

        if (!ShouldProcessOverviewMouseInput())
        {
                return;
        }

        const float PitchDelta = Value * OverviewMousePitchSpeed;
        const FVector2D PitchBounds = bOverviewCameraLocked
                ? GetLockedOverviewPitchRange()
                : FVector2D(OverviewMinPitch, OverviewMaxPitch);
        const float NewPitch = FMath::Clamp(DesiredOverviewRotation.Pitch + PitchDelta, PitchBounds.X, PitchBounds.Y);
        DesiredOverviewRotation.Pitch = NewPitch;

        if (!bOverviewCameraLocked)
        {
                OverviewDefaultPitch = NewPitch;
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
}

void ASkald_PlayerCharacter::HandleTerritorySelected(ATerritory* Territory)
{
ATerritory* LocalSelection = Territory;
if (Territory && !Territory->IsSelectionVisibleToLocalPlayer())
{
LocalSelection = nullptr;
}

CurrentSelection = LocalSelection;

if (!bBattleCameraActive)
{
if (LocalSelection)
{
FocusOverviewCameraOnTerritory(LocalSelection);
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

        const FVector2D LockedPitchRange = GetLockedOverviewPitchRange();
        const float LockedPitch = FMath::Clamp(OverviewLockedPitch, LockedPitchRange.X, LockedPitchRange.Y);
        DesiredOverviewRotation.Pitch = LockedPitch;

        if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
        {
                MovementComponent->StopMovementImmediately();
        }
}


void ASkald_PlayerCharacter::ClearOverviewCameraFocus()
{
        bOverviewCameraLocked = false;
        LockedOverviewTerritory.Reset();
        RefreshOverviewPivot();
        OverviewFocusLocation = OverviewWorldPivot;
        DesiredOverviewRotation.Pitch = FMath::Clamp(OverviewDefaultPitch, OverviewMinPitch, OverviewMaxPitch);

        if (!bBattleCameraActive && !OverviewDiceCameraState.bActive)
        {
                SetActorLocation(OverviewWorldPivot);

                if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
                {
                        MovementComponent->StopMovementImmediately();
                }
        }

        if (Controller)
        {
                FRotator ControlRotation = Controller->GetControlRotation();
                ControlRotation.Pitch = DesiredOverviewRotation.Pitch;
                ControlRotation.Yaw = FMath::Clamp(ControlRotation.Yaw, OverviewMinYaw, OverviewMaxYaw);
                ControlRotation.Roll = 0.f;
                Controller->SetControlRotation(ControlRotation);

                DesiredOverviewRotation.Yaw = ControlRotation.Yaw;
                DesiredOverviewRotation.Roll = 0.f;
                CurrentOverviewRotation = DesiredOverviewRotation;
        }
}

bool ASkald_PlayerCharacter::BeginStrategicInitiativeCameraView()
{
        if (bBattleCameraActive || OverviewDiceCameraState.bActive)
        {
                return false;
        }

        OverviewDiceCameraState.bActive = true;
        OverviewDiceCameraState.bWasLocked = bOverviewCameraLocked;
        OverviewDiceCameraState.OriginalLocation = GetActorLocation();
        OverviewDiceCameraState.OriginalFocusLocation = OverviewFocusLocation;
        OverviewDiceCameraState.OriginalZoom = CameraBoom ? CameraBoom->TargetArmLength : DesiredOverviewZoom;
        OverviewDiceCameraState.OriginalControlRotation = Controller ? Controller->GetControlRotation() : FRotator::ZeroRotator;
        OverviewDiceCameraState.OriginalLockedTerritory = LockedOverviewTerritory;

        bOverviewCameraLocked = false;

        RefreshOverviewPivot();
        const FVector ArenaFocus = OverviewWorldPivot + OverviewDiceCameraOffset;
        OverviewFocusLocation = ArenaFocus;
        SetActorLocation(ArenaFocus);

        if (CameraBoom)
        {
                DesiredOverviewZoom = FMath::Clamp(OverviewDiceCameraZoom, OverviewMinZoom, OverviewMaxZoom);
                CameraBoom->TargetArmLength = DesiredOverviewZoom;
        }

        if (Controller)
        {
                FRotator ControlRotation = OverviewDiceCameraState.OriginalControlRotation;
                ControlRotation.Pitch = OverviewDiceCameraPitch;
                ControlRotation.Roll = 0.f;
                ControlRotation.Yaw = FMath::Clamp(ControlRotation.Yaw, OverviewMinYaw, OverviewMaxYaw);
                Controller->SetControlRotation(ControlRotation);

                DesiredOverviewRotation = ControlRotation;
                DesiredOverviewRotation.Pitch = FMath::Clamp(DesiredOverviewRotation.Pitch, OverviewMinPitch, OverviewMaxPitch);
                DesiredOverviewRotation.Yaw = FMath::Clamp(DesiredOverviewRotation.Yaw, OverviewMinYaw, OverviewMaxYaw);
                DesiredOverviewRotation.Roll = 0.f;
                CurrentOverviewRotation = DesiredOverviewRotation;
        }

        if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
        {
                MovementComponent->StopMovementImmediately();
        }

        return true;
}

void ASkald_PlayerCharacter::EndStrategicInitiativeCameraView()
{
        if (!OverviewDiceCameraState.bActive)
        {
                return;
        }

        const FVector OriginalLocation = OverviewDiceCameraState.OriginalLocation;
        const FVector OriginalFocus = OverviewDiceCameraState.OriginalFocusLocation;

        if (CameraBoom)
        {
                DesiredOverviewZoom = FMath::Clamp(OverviewDiceCameraState.OriginalZoom, OverviewMinZoom, OverviewMaxZoom);
                CameraBoom->TargetArmLength = DesiredOverviewZoom;
        }

        if (Controller)
        {
                FRotator ControlRotation = OverviewDiceCameraState.OriginalControlRotation;
                ControlRotation.Pitch = FMath::Clamp(ControlRotation.Pitch, OverviewMinPitch, OverviewMaxPitch);
                ControlRotation.Yaw = FMath::Clamp(ControlRotation.Yaw, OverviewMinYaw, OverviewMaxYaw);
                ControlRotation.Roll = 0.f;
                Controller->SetControlRotation(ControlRotation);

                DesiredOverviewRotation = ControlRotation;
                CurrentOverviewRotation = ControlRotation;
                OverviewDefaultPitch = ControlRotation.Pitch;
        }

        SetActorLocation(OriginalLocation);
        OverviewFocusLocation = OriginalFocus;

        if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
        {
                MovementComponent->StopMovementImmediately();
        }

        if (OverviewDiceCameraState.bWasLocked && OverviewDiceCameraState.OriginalLockedTerritory.IsValid())
        {
                FocusOverviewCameraOnTerritory(OverviewDiceCameraState.OriginalLockedTerritory.Get());
        }
        else
        {
                bOverviewCameraLocked = false;
                LockedOverviewTerritory = OverviewDiceCameraState.OriginalLockedTerritory;
        }

        OverviewDiceCameraState = FOverviewDiceCameraState();
        RefreshOverviewPivot();
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

                const FVector2D LockedPitchRange = GetLockedOverviewPitchRange();
                DesiredOverviewRotation.Pitch = FMath::Clamp(DesiredOverviewRotation.Pitch, LockedPitchRange.X, LockedPitchRange.Y);
        }
        else
        {
                OverviewFocusLocation = GetActorLocation();
                DesiredOverviewRotation.Pitch = FMath::Clamp(DesiredOverviewRotation.Pitch, OverviewMinPitch, OverviewMaxPitch);
        }

        DesiredOverviewRotation.Yaw = FMath::Clamp(DesiredOverviewRotation.Yaw, OverviewMinYaw, OverviewMaxYaw);
        DesiredOverviewRotation.Roll = 0.f;

        CurrentOverviewRotation = FMath::RInterpTo(CurrentOverviewRotation, DesiredOverviewRotation, DeltaTime, OverviewRotationInterpSpeed);

        if (Controller)
        {
                Controller->SetControlRotation(CurrentOverviewRotation);
        }

        if (!bOverviewCameraLocked)
        {
                OverviewDefaultPitch = CurrentOverviewRotation.Pitch;
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

