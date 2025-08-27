#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Skald_PlayerController.generated.h"

class ATurnManager;
class UUserWidget;
class USkaldMainHUDWidget;
class ATerritory;
class ASkaldGameMode;
class ASkaldGameState;
class USkaldGameInstance;

/**
 * Player controller capable of participating in turn based gameplay.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API ASkaldPlayerController : public APlayerController {
  GENERATED_BODY()

public:
  ASkaldPlayerController();

  virtual void BeginPlay() override;

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void StartTurn();

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void EndTurn();

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void EndPhase();

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void MakeAIDecision();

  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Turn")
  bool IsAIController() const;

  /** Set the turn manager responsible for sequencing play. */
  UFUNCTION(BlueprintCallable, Category = "Turn")
  void SetTurnManager(ATurnManager *Manager);

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void ShowTurnAnnouncement(const FString &PlayerName, bool bIsMyTurn);

  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleTerritorySelected(ATerritory *Terr);

  /** Accessor for the main HUD widget instance. */
  UFUNCTION(BlueprintCallable, Category = "UI")
  USkaldMainHUDWidget *GetHUDWidget() const { return MainHudWidget; }

  /** Retrieve the turn manager controlling this player. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Turn")
  ATurnManager *GetTurnManager() const { return TurnManager; }

protected:
  /** Whether this controller is controlled by AI. */
  UPROPERTY(BlueprintReadOnly, Category = "Turn")
  bool bIsAI;

  /** Widget class to instantiate for the player's HUD.
   *  Expected to be assigned in the Blueprint subclass to avoid
   *  hard loading during CDO construction. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
  TSubclassOf<USkaldMainHUDWidget> MainHudWidgetClass;

  /** Reference to the HUD widget instance. */
  UPROPERTY(BlueprintReadOnly, Category = "UI",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<UUserWidget> HUDRef;

  /** Typed reference to the main HUD widget. */
  UPROPERTY(BlueprintReadOnly, Category = "UI",
            meta = (AllowPrivateAccess = "true"))
  USkaldMainHUDWidget *MainHudWidget;

  /** Cached references to core game singletons for blueprint access */
  UPROPERTY(BlueprintReadOnly, Category = "Game",
            meta = (AllowPrivateAccess = "true"))
  ASkaldGameMode *CachedGameMode;

  UPROPERTY(BlueprintReadOnly, Category = "Game",
            meta = (AllowPrivateAccess = "true"))
  ASkaldGameState *CachedGameState;

  UPROPERTY(BlueprintReadOnly, Category = "Game",
            meta = (AllowPrivateAccess = "true"))
  USkaldGameInstance *CachedGameInstance;

  /** Handle HUD attack submissions.
   *  Bound to USkaldMainHUDWidget::OnAttackRequested in the HUD.
   *  Blueprint widgets invoke this when an attack is submitted.
   */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleAttackRequested(int32 FromID, int32 ToID, int32 ArmySent);

  /** Handle HUD move submissions.
   *  Bound to USkaldMainHUDWidget::OnMoveRequested in the HUD.
   *  Called when a move action is confirmed from a widget.
   */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleMoveRequested(int32 FromID, int32 ToID, int32 Troops);

  /** Handle HUD end-attack confirmations.
   *  Bound to USkaldMainHUDWidget::OnEndAttackRequested.
   *  Widgets call this after the player finishes attacking.
   */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleEndAttackRequested(bool bConfirmed);

  /** Handle HUD end-movement confirmations.
   *  Bound to USkaldMainHUDWidget::OnEndMovementRequested.
   *  Invoked when the HUD signals the end of movement phase.
   */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleEndMovementRequested(bool bConfirmed);

  /** React to player list changes in the game state. */
  UFUNCTION()
  void HandlePlayersUpdated();

  /** React to faction selections in the game instance. */
  UFUNCTION()
  void HandleFactionsUpdated();

  /** Reference to the game's turn manager.
   *  Exposed to Blueprints so BP_Skald_PlayerController can bind to
   *  turn events without keeping an external pointer that might be
   *  uninitialised.
   */
  UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Turn",
            meta = (ExposeOnSpawn = true))
  TObjectPtr<ATurnManager> TurnManager;
};
