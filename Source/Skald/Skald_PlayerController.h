#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SkaldTypes.h"
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

  /** Send the local player's initial data to the server for replication. */
  UFUNCTION(Server, Reliable)
  void ServerInitPlayerState(const FString &Name, ESkaldFaction Faction);

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
  TObjectPtr<USkaldMainHUDWidget> MainHudWidget;

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
  void HandleAttackRequested(int32 FromID, int32 ToID, int32 ArmySent,
                             bool bUseSiege);

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

  /** Handle HUD engineering action requests. */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleEngineeringRequested(int32 CapitalID, uint8 UpgradeType);

  /** Handle HUD treasure digging requests. */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleDigTreasureRequested(int32 TerritoryID);

  /** React to player list changes in the game state. */
  UFUNCTION()
  void HandlePlayersUpdated();

  /** React to faction selections in the game instance. */
  UFUNCTION()
  void HandleFactionsUpdated();

  /** React to world state changes broadcast by the turn manager. */
  UFUNCTION()
  void HandleWorldStateChanged();

  /** Server-side processing of an attack request. */
  UFUNCTION(Server, Reliable)
  void ServerHandleAttack(int32 FromID, int32 ToID, int32 ArmySent,
                          bool bUseSiege);

  /** Handle HUD siege build requests. */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleBuildSiegeRequested(int32 TerritoryID, E_SiegeWeapons SiegeType);

  /** Server-side processing of a siege build request. */
  UFUNCTION(Server, Reliable)
  void ServerBuildSiege(int32 TerritoryID, E_SiegeWeapons SiegeType);

  /** Server-side processing of a treasure dig request. */
  UFUNCTION(Server, Reliable)
  void ServerDigTreasure(int32 TerritoryID);

  /** Server-side processing of a move request. */
  UFUNCTION(Server, Reliable)
  void ServerHandleMove(int32 FromID, int32 ToID, int32 Troops);

  /** Server-side processing of a territory selection. */
  UFUNCTION(Server, Reliable)
  void ServerSelectTerritory(int32 TerritoryID);

  /** Phase change handlers. */
  UFUNCTION(BlueprintCallable, Category = "Turn")
  void HandleEngineeringPhase();

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void HandleTreasurePhase();

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void HandleMovementPhase();

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void HandleEndTurnPhase();

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void HandleRevoltPhase();

  /** Reference to the game's turn manager.
   *  Exposed to Blueprints so BP_Skald_PlayerController can bind to
   *  turn events without keeping an external pointer that might be
   *  uninitialised.
   */
  UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Turn",
            meta = (ExposeOnSpawn = true))
  TObjectPtr<ATurnManager> TurnManager;

private:
  void BuildPlayerDataArray(TArray<FS_PlayerData> &OutPlayers) const;
  void NotifyActionError(const FString &Message);
};
