#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "UObject/WeakObjectPtr.h"

class AWorldMap;
class ATerritory;
class ASkaldGameMode;
class ASkaldGameState;
class USkaldGameInstance;
class UCameraComponent;
class USpringArmComponent;
class AActor;

#include "Skald_PlayerCharacter.generated.h"

UCLASS(Blueprintable, BlueprintType)
class SKALD_API ASkald_PlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
        /** Sets default values for this character's properties */
        ASkald_PlayerCharacter();

        /** Enable or disable the tactical battle camera behaviour. */
        void SetBattleCameraActive(bool bActive);

        /** True if the dynamic battle camera is currently active. */
        bool IsBattleCameraActive() const { return bBattleCameraActive; }

        /** Lock the camera onto the supplied actor (used for active fighters). */
        void FocusCameraOnActor(AActor* FocusActor);

        /** Release any active camera lock, returning manual control to the player. */
        void ClearCameraFocus();

        /** True if the battle camera currently has an automatic lock target. */
        bool IsBattleCameraLocked() const { return bBattleCameraLocked; }

        /** Actor currently locked by the battle camera, if any. */
        AActor* GetCurrentBattleCameraLockTarget() const { return LockedBattleActor.Get(); }

        /** Current battle camera boom rotation. */
        FRotator GetCurrentBattleCameraRotation() const { return CurrentBattleRotation; }

        /** Current boom length used by the battle camera. */
        float GetCurrentBattleCameraZoom() const;

        /** Smoothly interpolate the battle camera toward the supplied transform. */
        void StartCameraTransition(const FVector& TargetLocation, const FRotator& TargetRotation, float TargetZoom, float Duration);

        /** True while an automated camera transition is running. */
        bool IsCameraTransitionActive() const { return bCameraTransitionActive; }

protected:
        /** Called when the game starts or when spawned */
        virtual void BeginPlay() override;
        virtual void PossessedBy(AController* NewController) override;
        virtual void OnRep_Controller() override;

        /** Reference to the world map actor for selection and movement */
        UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Selection", meta=(ExposeOnSpawn=true))
        AWorldMap* WorldMap;

        /** Currently selected territory, if any */
        UPROPERTY(BlueprintReadOnly, Category="Selection")
        ATerritory* CurrentSelection;

        /** Cached references to global game objects for blueprint use */
        UPROPERTY(BlueprintReadOnly, Category="Game")
        ASkaldGameMode* CachedGameMode;

        UPROPERTY(BlueprintReadOnly, Category="Game")
        ASkaldGameState* CachedGameState;

        UPROPERTY(BlueprintReadOnly, Category="Game")
        USkaldGameInstance* CachedGameInstance;

        /** Timer handle used to repeatedly search for the world map */
        FTimerHandle WorldMapSearchHandle;

        /** Attempt to cache a reference to the world map */
        void TryCacheWorldMap();

        /** Update current selection when the world map changes */
	UFUNCTION()
	void HandleTerritorySelected(ATerritory* Territory);

	/** Focus the overview camera on the supplied territory. */
	void FocusOverviewCameraOnTerritory(ATerritory* Territory);

	/** Clear any active overview camera lock. */
        void ClearOverviewCameraFocus();

public:
        /** Called every frame */
        virtual void Tick(float DeltaTime) override;

        /** Called to bind functionality to input */
        virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

        /** Handle forward/backward movement input */
        UFUNCTION(BlueprintCallable, Category="Input")
        void MoveForward(float Value);

        /** Handle right/left movement input */
        UFUNCTION(BlueprintCallable, Category="Input")
        void MoveRight(float Value);

        /** Handle up/down movement input */
        UFUNCTION(BlueprintCallable, Category="Input")
        void MoveUp(float Value);

        /** Handle yaw input */
        UFUNCTION(BlueprintCallable, Category="Input")
        void Turn(float Value);

        /** Handle pitch input */
        UFUNCTION(BlueprintCallable, Category="Input")
        void LookUp(float Value);

        /** Adjust zoom level when the battle camera is active. */
        UFUNCTION(BlueprintCallable, Category="Input")
        void AdjustZoom(float Value);

        /** Handle selection action */
        UFUNCTION(BlueprintCallable, Category="Input")
        void Select();

        /** Ability triggers */
        UFUNCTION(BlueprintCallable, Category="Abilities")
        void AbilityOne();

        UFUNCTION(BlueprintCallable, Category="Abilities")
        void AbilityTwo();

        UFUNCTION(BlueprintCallable, Category="Abilities")
        void AbilityThree();

        /** Begin the strategic initiative camera sequence. */
        bool BeginStrategicInitiativeCameraView();

        /** End the strategic initiative camera sequence. */
        void EndStrategicInitiativeCameraView();

        /** True if the strategic initiative camera is currently active. */
        bool IsStrategicInitiativeCameraActive() const { return OverviewDiceCameraState.bActive; }

protected:
        /** Camera boom positioning the camera behind the character */
        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera", meta=(AllowPrivateAccess="true"))
        USpringArmComponent* CameraBoom;

        /** Follow camera */
        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera", meta=(AllowPrivateAccess="true"))
        UCameraComponent* FollowCamera;

        /** Base zoom distance applied when enabling the battle camera or when no lock target is active. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float DefaultBattleZoom = 650.f;

        /** Default downward pitch applied to the battle camera. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float DefaultBattlePitch = -60.f;

        /** Horizontal pan speed (cm/s) for the battle camera. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float BattlePanSpeed = 1800.f;

        /** Acceleration factor controlling how quickly panning reaches target speed. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float BattlePanSmoothing = 6.f;

        /** Sensitivity multiplier applied to mouse yaw while in battle mode. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float BattleMouseYawSpeed = 1.5f;

        /** Sensitivity multiplier applied to mouse pitch while in battle mode. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float BattleMousePitchSpeed = 1.0f;

        /** Lowest pitch angle permitted for the battle camera. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float MinBattlePitch = -80.f;

        /** Highest pitch angle permitted for the battle camera. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float MaxBattlePitch = -35.f;

        /** Minimum allowed zoom distance when using the battle camera. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float MinBattleZoom = 250.f;

        /** Maximum allowed zoom distance when using the battle camera. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float MaxBattleZoom = 3200.f;

        /** Amount applied to the desired zoom each time the mouse wheel turns. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float BattleZoomStep = 180.f;

        /** Interpolation speed for zoom transitions. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float BattleZoomInterpSpeed = 6.f;

        /** Interpolation speed for updating camera rotation. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float BattleRotationInterpSpeed = 8.f;

        /** Interpolation speed when focusing on an active fighter. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float BattleLockInterpSpeed = 6.f;

        /** Camera lag speed applied while panning in battle mode. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float BattleCameraLagSpeed = 12.f;

        /** Zoom distance applied whenever the camera locks onto a fighter. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float LockedBattleZoom = 425.f;

        /** Pitch used for the immersive over-the-shoulder lock-on view. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float LockedBattlePitch = -20.f;

        /** Additional yaw applied whenever the camera automatically faces a lock target. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float LockedBattleYawOffset = 0.f;

        /** Pivot offset from the focused fighter while locked on (X/Y shift the pawn, Z raises the pivot). */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        FVector BattleLockRelativeOffset = FVector(0.f, 0.f, 220.f);

        /** Whether to automatically yaw the camera to face the lock target. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        bool bAutoFaceLockTarget = true;

        /** Keyboard yaw speed (degrees per second) applied while locked onto a fighter. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float BattleKeyboardYawSpeed = 120.f;

        /** Keyboard pitch speed (degrees per second) applied while locked onto a fighter. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float BattleKeyboardPitchSpeed = 90.f;

        /** Minimum allowed zoom distance on the overview map. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Overview")
        float OverviewMinZoom = 650.f;

        /** Maximum allowed zoom distance on the overview map. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Overview")
        float OverviewMaxZoom = 3200.f;

        /** Amount applied to the overview zoom each time the mouse wheel turns. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Overview")
        float OverviewZoomStep = 180.f;

        /** Interpolation speed when smoothing overview zoom updates. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Overview")
        float OverviewZoomInterpSpeed = 6.f;

        /** Interpolation speed when sliding the overview camera toward a focus point. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Overview")
        float OverviewPanInterpSpeed = 5.f;

        /** Interpolation speed applied when pitching toward the locked overview angle. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Overview")
        float OverviewRotationInterpSpeed = 5.f;

        /** Height offset applied when focusing above a selected territory. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Overview")
        float OverviewFocusHeight = 100.f;

        /** Pitch applied while the overview camera is locked onto a territory. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Overview")
        float OverviewLockedPitch = -70.f;

        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Overview")
        float OverviewTopDownPitch = -85.f;

        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Overview")
        float OverviewSpawnZoom = 2200.f;

        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Overview")
        FVector OverviewPivotOffset = FVector(0.f, 0.f, 100.f);

        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Overview")
        float OverviewDiceCameraZoom = 2000.f;

        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Overview")
        float OverviewDiceCameraPitch = -85.f;

        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Overview")
        FVector OverviewDiceCameraOffset = FVector::ZeroVector;

private:
        /** Perform per-frame updates while the battle camera is active. */
        void UpdateBattleCamera(float DeltaTime);

        /** Reset any active camera lock. */
        void ClearBattleCameraLock();

	/** Update the strategic overview camera each frame. */
	void UpdateOverviewCamera(float DeltaTime);
	void InitializeOverviewCamera();
	FVector ComputeOverviewPivotLocation() const;
	void RefreshOverviewPivot();

        /** True if the tactical battle camera behaviour is enabled. */
        bool bBattleCameraActive = false;

        /** Tracks whether the camera is currently focused on an active fighter. */
        bool bBattleCameraLocked = false;

        /** Actor currently being focused by the battle camera. */
        TWeakObjectPtr<AActor> LockedBattleActor;

        /** Cached desired rotation for smooth interpolation. */
        FRotator DesiredBattleRotation;

        /** Current rotation the camera boom is interpolating toward. */
        FRotator CurrentBattleRotation;

        /** Desired zoom distance managed by the battle camera. */
        float DesiredBattleZoom = 0.f;

        /** Smoothed velocity applied when panning the battle camera. */
        FVector BattleCameraVelocity = FVector::ZeroVector;

        /** Desired zoom level for the overview camera. */
        float DesiredOverviewZoom = 0.f;

        /** Cached default pitch restored when releasing the overview focus. */
        float OverviewDefaultPitch = 0.f;

	/** Current desired focus location for the overview camera. */
	FVector OverviewFocusLocation = FVector::ZeroVector;
	/** Cached pivot location representing the centre of the world map. */
	FVector OverviewWorldPivot = FVector::ZeroVector;

        /** True while the overview camera is locked onto a territory. */
        bool bOverviewCameraLocked = false;

        /** Territory currently used as the overview focus point. */
        TWeakObjectPtr<ATerritory> LockedOverviewTerritory;

	struct FOverviewDiceCameraState
	{
		bool bActive = false;
		bool bWasLocked = false;
		FVector OriginalLocation = FVector::ZeroVector;
		FVector OriginalFocusLocation = FVector::ZeroVector;
		FRotator OriginalControlRotation = FRotator::ZeroRotator;
		float OriginalZoom = 0.f;
		TWeakObjectPtr<ATerritory> OriginalLockedTerritory;
	};

	FOverviewDiceCameraState OverviewDiceCameraState;
	bool bHasInitializedOverviewCamera = false;

        /** Tracks if the player has manually rotated the camera while locked on. */
        bool bHasManuallyRotatedWhileLocked = false;

        /** Convert the configured relative lock offset into world space. */
        FVector GetBattleLockWorldOffset(const AActor* FocusActor) const;

        /** Stores current movement input for battle camera panning. */
        FVector2D BattleMoveInput = FVector2D::ZeroVector;

        /** Cached relative rotation of the camera boom prior to enabling battle mode. */
        FRotator CachedDefaultBoomRotation;

        /** Cached arm length prior to enabling battle mode. */
        float CachedDefaultArmLength = 0.f;

        /** Cached state for inheriting controller rotation. */
        bool bCachedUsePawnControlRotation = false;

        /** Cached inherit settings for the spring arm. */
        bool bCachedInheritPitch = true;
        bool bCachedInheritYaw = true;
        bool bCachedInheritRoll = true;

        /** Cached lag configuration for restoring after battle mode. */
        bool bCachedCameraLag = false;
        float CachedCameraLagSpeed = 0.f;

        /** Cached collision test state for restoring after battle mode. */
        bool bCachedDoCollisionTest = true;

        /** Tracks the current automated transition request, if any. */
        bool bCameraTransitionActive = false;
        float CameraTransitionDuration = 0.f;
        float CameraTransitionElapsed = 0.f;
        FVector CameraTransitionStartLocation = FVector::ZeroVector;
        FVector CameraTransitionTargetLocation = FVector::ZeroVector;
        FRotator CameraTransitionStartRotation = FRotator::ZeroRotator;
        FRotator CameraTransitionTargetRotation = FRotator::ZeroRotator;
        float CameraTransitionStartZoom = 0.f;
        float CameraTransitionTargetZoom = 0.f;

        /** Update the automated camera transition if one is active. */
        void UpdateCameraTransition(float DeltaTime);
};
