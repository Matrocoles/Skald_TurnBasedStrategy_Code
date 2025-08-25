#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Skald_PlayerController.generated.h"

class ATurnManager;
class USkaldMainHUDWidget;

/**
 * Player controller capable of participating in turn based gameplay.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API ASkaldPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    ASkaldPlayerController();

    virtual void BeginPlay() override;

    UFUNCTION(BlueprintCallable, Category="Turn")
    void StartTurn();

    UFUNCTION(BlueprintCallable, Category="Turn")
    void EndTurn();

    UFUNCTION(BlueprintCallable, Category="Turn")
    void MakeAIDecision();

    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Turn")
    bool IsAIController() const;

    /** Set the turn manager responsible for sequencing play. */
    UFUNCTION(BlueprintCallable, Category="Turn")
    void SetTurnManager(ATurnManager* Manager);

protected:
    /** Whether this controller is controlled by AI. */
    UPROPERTY(BlueprintReadOnly, Category="Turn")
    bool bIsAI;

    /** Widget class to instantiate for the player's HUD.
     *  Expected to be assigned in the Blueprint subclass to avoid
     *  hard loading during CDO construction. */
    UPROPERTY(EditDefaultsOnly, Category="UI")
    TSubclassOf<USkaldMainHUDWidget> MainHudWidgetClass;

    /** Reference to the HUD widget instance. */
    UPROPERTY()
    USkaldMainHUDWidget* MainHudWidget;

    /** Handle HUD attack submissions. */
    UFUNCTION()
    void HandleAttackRequested(int32 FromID, int32 ToID, int32 ArmySent);

    /** Handle HUD move submissions. */
    UFUNCTION()
    void HandleMoveRequested(int32 FromID, int32 ToID, int32 Troops);

    /** Handle HUD end-attack confirmations. */
    UFUNCTION()
    void HandleEndAttackRequested(bool bConfirmed);

    /** Handle HUD end-movement confirmations. */
    UFUNCTION()
    void HandleEndMovementRequested(bool bConfirmed);

    /** Reference to the game's turn manager.
     *  Exposed to Blueprints so BP_Skald_PlayerController can bind to
     *  turn events without keeping an external pointer that might be
     *  uninitialised.
     */
    UPROPERTY(BlueprintReadOnly, Category="Turn", meta=(AllowPrivateAccess="true"))
    TObjectPtr<ATurnManager> TurnManager;
};

