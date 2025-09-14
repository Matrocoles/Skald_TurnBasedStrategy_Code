#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/PlayerController.h"
#include "SkaldTypes.h"
#include "TimerManager.h"
#include "Skald_PlayerController.generated.h"

class ATurnManager;
class UUserWidget;
class USkaldMainHUDWidget;
class UChoosePlayerWidget;
class UBattleHUDWidget;
class ATerritory;
class ASkaldGameMode;
class ASkaldGameState;
class USkaldGameInstance;
class UFighterSelectionWidget;
class AWorldMap;
class AFighterPawn;
class UGridOverlayComponent;

/** Command issued by the player during a battle. */
UENUM()
enum class EBattleCommandMode : uint8 { None, Move, Attack };

/**
 * Player controller capable of participating in turn based gameplay.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API ASkaldPlayerController : public APlayerController {
  GENERATED_BODY()

  /** Allow the game mode to trigger binding once the world map exists. */
  friend class ASkaldGameMode;

public:
  ASkaldPlayerController();

  virtual void BeginPlay() override;

  virtual void OnRep_PlayerState() override;

  UFUNCTION(BlueprintCallable, Category = "Turn")
  virtual void StartTurn();

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void EndTurn();

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void EndPhase();

  /** Set the turn manager responsible for sequencing play. */
  UFUNCTION(BlueprintCallable, Category = "Turn")
  void SetTurnManager(ATurnManager *Manager);

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void ShowTurnAnnouncement(const FString &PlayerName, bool bIsMyTurn);

  UFUNCTION(BlueprintCallable, Category = "Turn")
  void NotifyTurnEnded(const FString &PlayerName);

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
  void ServerInitPlayerState(const FString &Name, ESkaldFaction Faction,
                             int32 NumAIPlayers);

protected:
  /** Widget class to instantiate for the player's HUD.
   *  Settable via Blueprint or loaded in the constructor. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
  TSubclassOf<UUserWidget> HUDWidgetClass;

  /** Widget class used for in-battle HUD. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
  TSubclassOf<UBattleHUDWidget> BattleHUDWidgetClass;

  /** Widget class used for the player faction selection screen. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
  TSubclassOf<UChoosePlayerWidget> ChoosePlayerWidgetClass;

  /** Widget class used for displaying battle victory. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
  TSubclassOf<UUserWidget> VictoryWidgetClass;

  /** Reference to the HUD widget instance. */
  UPROPERTY(BlueprintReadOnly, Category = "UI",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<UUserWidget> HUDRef;

  /** Typed reference to the main HUD widget. */
  UPROPERTY(BlueprintReadOnly, Category = "UI",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<USkaldMainHUDWidget> MainHudWidget;

  /** Typed reference to the battle HUD widget. */
  UPROPERTY(BlueprintReadOnly, Category = "UI",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<UBattleHUDWidget> BattleHudWidget;

  /** Fighter selection widget used during battle setup. */
  UPROPERTY(BlueprintReadOnly, Category = "UI",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<UFighterSelectionWidget> FighterSelectionWidget;

  /** Player selection widget instance. */
  UPROPERTY(BlueprintReadOnly, Category = "UI",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<UChoosePlayerWidget> ChoosePlayerWidget;

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

  /** Current command selection when issuing grid battle orders. */
  EBattleCommandMode CurrentCommandMode;

  /** Ensures ServerInitPlayerState and lock-in handling run only once. */
  bool bHasInitialized;

  /** Default mouse capture behavior for this controller. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
  EMouseCaptureMode DefaultMouseCaptureMode;

  virtual void SetupInputComponent() override;

  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  /** Begin selecting a move destination. */
  UFUNCTION()
  void BeginMoveMode();

  /** Begin selecting an attack target. */
  UFUNCTION()
  void BeginAttackMode();

  /** Handle the player clicking on the grid. */
  UFUNCTION()
  void HandleGridClick();

public:
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

  /**
   * Legacy alias for HandleFactionLockedIn.
   * Enables player movement and look input after locking in their faction.
   */
  UFUNCTION()
  void HandlePlayerLockedIn();

  /** React to the player finishing their pre-game selection. */
  UFUNCTION()
  void HandleFactionLockedIn();

  void HandleFighterSelectionLockedIn();

  /** React to the end of a battle. */
  UFUNCTION()
  void HandleBattleEnded(ESkaldFaction WinningFaction, int32 AttackerCasualties,
                         int32 DefenderCasualties);

  UFUNCTION()
  void HandleActiveFighterChanged(AFighterPawn* NewFighter);

  /** Server-side processing of an attack request. */
  UFUNCTION(Server, Reliable)
  void ServerHandleAttack(int32 FromID, int32 ToID, int32 ArmySent,
                          bool bUseSiege);

  /** Handle HUD siege build requests. */
  UFUNCTION(BlueprintCallable, Category = "UI")
  void HandleBuildSiegeRequested(int32 TerritoryID, ESiegeWeapon SiegeType);

  /** Server-side processing of a siege build request. */
  UFUNCTION(Server, Reliable)
  void ServerBuildSiege(int32 TerritoryID, ESiegeWeapon SiegeType);

  /** Server-side processing of a treasure dig request. */
  UFUNCTION(Server, Reliable)
  void ServerDigTreasure(int32 TerritoryID);

  /** Server-side processing of a move request. */
  UFUNCTION(Server, Reliable)
  void ServerHandleMove(int32 FromID, int32 ToID, int32 Troops);

  /** Server-side processing of a unit deployment. */
  UFUNCTION(Server, Reliable)
  void ServerDeployUnits(int32 TerritoryID, int32 Amount);

  /** Server-side processing of a territory selection. Pass -1 to deselect. */
  UFUNCTION(Server, Reliable)
  void ServerSelectTerritory(int32 TerritoryID);

  /** Client-side update for a territory selection. Pass -1 to clear. */
  UFUNCTION(Client, Reliable)
  void ClientSelectTerritory(int32 TerritoryID);

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
protected:
  UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Turn",
            meta = (ExposeOnSpawn = true))
  TObjectPtr<ATurnManager> TurnManager;

private:
  /** Cache references to key game singletons and bind delegates. */
  void CacheGameReferences();

  /** Set up the main HUD widget for the local player. */
  void InitializeHUDWidget();

  /** Create the faction selection widget for the local player. */
  void InitializeChoosePlayerWidget();

  void InitializeBattleHUD();
  UGridOverlayComponent* FindGridOverlay() const;

  /** Attempt to locate the world map and bind to its selection event. */
  void TryBindWorldMap();

  /** Timer used to poll for the world map actor until it exists. */
  FTimerHandle WorldMapSearchHandle;

  /** Whether the current map is a battle map. */
  UPROPERTY(BlueprintReadOnly, Category = "Turn",
            meta = (AllowPrivateAccess = "true"))
  bool bIsBattleMap = false;

  /** Detect if the current level is a battle map and update bIsBattleMap. */
  void DetectBattleMap();

  /** Re-run fighter selection setup after level transitions. */
  void HandlePostLoadMap(UWorld *LoadedWorld);

  void BuildPlayerDataArray(TArray<FS_PlayerData> &OutPlayers) const;

  bool ValidateAttack(int32 FromID, int32 ToID, int32 ArmySent, bool bUseSiege,
                      FString *OutError);

  UFUNCTION(Client, Reliable)
  void NotifyActionError(const FString &Message);

  /** Ensure a valid TurnManager is available, attempting reacquisition if
   * needed. */
  bool EnsureTurnManager(const TCHAR *Caller);

  /** Create the fighter selection widget if we are on a battle map and it is
   * not already shown. */
  void InitializeFighterSelectionIfNeeded();
};
