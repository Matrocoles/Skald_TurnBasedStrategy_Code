#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "Templates/WeakObjectPtr.h"

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

protected:
        /** Called when the game starts or when spawned */
        virtual void BeginPlay() override;

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

protected:
        /** Camera boom positioning the camera behind the character */
        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera", meta=(AllowPrivateAccess="true"))
        USpringArmComponent* CameraBoom;

        /** Follow camera */
        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera", meta=(AllowPrivateAccess="true"))
        UCameraComponent* FollowCamera;

        /** Base zoom distance applied when enabling the battle camera. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float DefaultBattleZoom = 1400.f;

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
        float MinBattleZoom = 600.f;

        /** Maximum allowed zoom distance when using the battle camera. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        float MaxBattleZoom = 2600.f;

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

        /** Optional positional offset applied when locking onto a fighter. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        FVector BattleLockOffset = FVector::ZeroVector;

        /** Whether to automatically yaw the camera to face the lock target. */
        UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|Battle")
        bool bAutoFaceLockTarget = true;

private:
        /** Perform per-frame updates while the battle camera is active. */
        void UpdateBattleCamera(float DeltaTime);

        /** Reset any active camera lock. */
        void ClearBattleCameraLock();

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
};
