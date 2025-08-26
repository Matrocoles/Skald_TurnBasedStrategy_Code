#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "SkaldTypes.h"
#include "SkaldMainHUDWidget.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;
class ATerritory;
class UConfirmAttackWidget;
class UWidget;

// Delegates broadcasting user UI actions to game logic
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FSkaldAttackRequested, int32,
                                               FromID, int32, ToID, int32,
                                               ArmySent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSkaldEndAttackRequested, bool,
                                            bConfirmed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSkaldEngineeringRequested, int32,
                                             CapitalID, uint8, UpgradeType);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSkaldDigTreasureRequested, int32,
                                            TerritoryID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FSkaldMoveRequested, int32,
                                               FromID, int32, ToID, int32,
                                               Troops);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSkaldEndMovementRequested, bool,
                                            bConfirmed);

/**
 * Base HUD widget that exposes state and events for the game logic.
 *
 * Blueprint subclasses are expected to create the visual elements. Typical
 * wiring:
 *  - Buttons in the BP call the BlueprintCallable functions such as
 * SubmitAttack or SubmitMove. (e.g. AttackButton->OnClicked ->
 * SubmitAttack(SourceID, TargetID, ArmySent))
 *  - PlayerController binds to the multicast delegates to forward actions to
 * server RPCs: HUDWidget->OnAttackRequested.AddDynamic(this,
 * &ASKald_PlayerController::Server_RequestAttack);
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API USkaldMainHUDWidget : public UUserWidget {
  GENERATED_BODY()

public:
  // Identity / state (read by BP)
  UPROPERTY(BlueprintReadWrite, Category = "Skald|State")
  int32 LocalPlayerID = -1;

  UPROPERTY(BlueprintReadWrite, Category = "Skald|State")
  int32 CurrentPlayerID = -1;

  UPROPERTY(BlueprintReadWrite, Category = "Skald|State")
  int32 TurnNumber = 1;

  UPROPERTY(BlueprintReadWrite, Category = "Skald|State")
  ETurnPhase CurrentPhase = ETurnPhase::Reinforcement;

  // Selection helpers used by Attack/Move flows
  UPROPERTY(BlueprintReadWrite, Category = "Skald|Selection")
  int32 SelectedSourceID = -1;

  UPROPERTY(BlueprintReadWrite, Category = "Skald|Selection")
  int32 SelectedTargetID = -1;

  UPROPERTY(BlueprintReadWrite, Category = "Skald|Selection")
  bool bSelectingForAttack = false;

  UPROPERTY(BlueprintReadWrite, Category = "Skald|Selection")
  bool bSelectingForMove = false;

  // Cached list of players for UI list building
  UPROPERTY(BlueprintReadWrite, Category = "Skald|Data")
  TArray<FS_PlayerData> CachedPlayers;

  // Delegates (BlueprintAssignable) — UI → game actions
  UPROPERTY(BlueprintAssignable, Category = "Skald|Events")
  FSkaldAttackRequested OnAttackRequested;

  UPROPERTY(BlueprintAssignable, Category = "Skald|Events")
  FSkaldEndAttackRequested OnEndAttackRequested;

  UPROPERTY(BlueprintAssignable, Category = "Skald|Events")
  FSkaldEngineeringRequested OnEngineeringRequested;

  UPROPERTY(BlueprintAssignable, Category = "Skald|Events")
  FSkaldDigTreasureRequested OnDigTreasureRequested;

  UPROPERTY(BlueprintAssignable, Category = "Skald|Events")
  FSkaldMoveRequested OnMoveRequested;

  UPROPERTY(BlueprintAssignable, Category = "Skald|Events")
  FSkaldEndMovementRequested OnEndMovementRequested;

  // BlueprintCallable functions — game → HUD (push updates)
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void UpdateTurnBanner(int32 InCurrentPlayerID, int32 InTurnNumber);

  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void UpdatePhaseBanner(ETurnPhase InPhase);

  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void UpdateTerritoryInfo(const FString &TerritoryName,
                           const FString &OwnerName, int32 ArmyCount);

  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void RefreshPlayerList(const TArray<FS_PlayerData> &Players);

  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void RefreshFromState(int32 InCurrentPlayerID, int32 InTurnNumber,
                        ETurnPhase InPhase,
                        const TArray<FS_PlayerData> &Players);

  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void ShowTurnAnnouncement(const FString &PlayerName);

  /** Rebuilds the cached player list into PlayerListBox. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void RebuildPlayerList(const TArray<FS_PlayerData> &Players);

  /** Show a message indicating the turn is ending. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void ShowEndingTurn();

  /** Hide the ending turn message. */
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void HideEndingTurn();

  // BlueprintCallable functions — selection UX helpers
  UFUNCTION(BlueprintCallable, Category = "Skald|Selection")
  void BeginAttackSelection();

  UFUNCTION(BlueprintCallable, Category = "Skald|Selection")
  void SubmitAttack(int32 FromID, int32 ToID, int32 ArmySent);

  UFUNCTION(BlueprintCallable, Category = "Skald|Selection")
  void CancelAttackSelection();

  UFUNCTION(BlueprintCallable, Category = "Skald|Selection")
  void BeginMoveSelection();

  UFUNCTION(BlueprintCallable, Category = "Skald|Selection")
  void SubmitMove(int32 FromID, int32 ToID, int32 Troops);

  UFUNCTION(BlueprintCallable, Category = "Skald|Selection")
  void CancelMoveSelection();

  UFUNCTION(BlueprintCallable, Category = "Skald|Selection")
  void OnTerritoryClickedUI(ATerritory* Territory);

  // BlueprintImplementableEvent hooks — BP subclass draws UI
  UFUNCTION(BlueprintImplementableEvent, Category = "Skald|HUD")
  void BP_SetTurnText(int32 InTurnNumber, int32 InCurrentPlayerID);

  UFUNCTION(BlueprintImplementableEvent, Category = "Skald|HUD")
  void BP_SetPhaseText(ETurnPhase InPhase);

  UFUNCTION(BlueprintImplementableEvent, Category = "Skald|HUD")
  void BP_SetTerritoryPanel(const FString &TerritoryName,
                            const FString &OwnerName, int32 ArmyCount);

  UFUNCTION(BlueprintImplementableEvent, Category = "Skald|HUD")
  void BP_SetPhaseButtons(ETurnPhase InPhase, bool bIsMyTurn);

  UFUNCTION(BlueprintImplementableEvent, Category = "Skald|HUD")
  void BP_ShowTurnAnnouncement(const FString &PlayerName);

  // Helper so PlayerController can refresh button enable state after it knows
  // turn ownership
  UFUNCTION(BlueprintCallable, Category = "Skald|HUD")
  void SyncPhaseButtons(bool bIsMyTurn);

public:
  // Bound widget references - optional so subclasses can customise layouts
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UTextBlock *TurnText;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UTextBlock *PhaseText;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UTextBlock *SelectionPrompt;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UButton *AttackButton;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UButton *MoveButton;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UButton *EndTurnButton;

  // Container where RebuildPlayerList will spawn entries
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UVerticalBox *PlayerListBox;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UTextBlock *EndingTurnText;
  UWidget *SelectionPrompt;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Widgets")
  TSubclassOf<UConfirmAttackWidget> ConfirmAttackWidgetClass;

protected:
  // Internal handlers for widget actions
  UFUNCTION()
  void HandleEndTurnClicked();

  UFUNCTION()
  void HandleAttackApproved();

  UPROPERTY()
  UConfirmAttackWidget *ActiveConfirmWidget = nullptr;

  UPROPERTY()
  TArray<ATerritory *> HighlightedTerritories;

  virtual void NativeConstruct() override;
};
