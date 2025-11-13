#include "Skald_PlayerController.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "ChoosePlayerWidget.h"
#include "Components/InputComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "FighterDataLibrary.h"
#include "FighterPawn.h"
#include "FactionCursorData.h"
#include "Abilities/SkaldAbilityComponent.h"
#include "Abilities/SkaldAbilityTypes.h"
#include "GridBattleManager.h"
#include "GridOverlayComponent.h"
#include "InputCoreTypes.h"
#include "Internationalization/Text.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Skald.h"
#include "SkaldTypes.h"
#include "Skald_GameInstance.h"
#include "Skald_GameMode.h"
#include "Skald_BattleGameMode.h"
#include "SkaldLogging.h"
#include "Skald_GameState.h"
#include "Skald_PlayerState.h"
#include "Skald_PlayerCharacter.h"
#include "Skald_TurnManager.h"
#include "Territory.h"
#include "TimerManager.h"
#include "SkaldDiceManager.h"
#include "SkaldDiceOverlayWidget.h"
#include "SkaldDiceResultWidget.h"
#include "UI/BattleHUDWidget.h"
#include "UI/BattleResultWidget.h"
#include "UI/FighterSelectionWidget.h"
#include "UI/InGameMenuWidget.h"
#include "UI/SkaldMainHUDWidget.h"
#include "UI/SkaldUIHelpers.h"
#include "UObject/ConstructorHelpers.h"
#include "WorldMap.h"

#include "Misc/EngineVersionComparison.h"
#include "Misc/CoreDelegates.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "UObject/CoreUObjectDelegates.h"
#else
#include "UObject/UObjectGlobals.h"
#endif

#include "Framework/Application/SlateApplication.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "GenericPlatform/IPlatformCursor.h"
using FSkaldCursorPtr = TSharedPtr<IPlatformCursor>;
#else
#include "GenericPlatform/ICursor.h"
using FSkaldCursorPtr = TSharedPtr<ICursor>;
#endif
#include "Layout/WidgetPath.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"

#include "Net/UnrealNetwork.h"

namespace {
FString ResolvePlayerName(const ASkaldPlayerState *PlayerState,
                          const TCHAR *Context) {
  if (!PlayerState) {
    return TEXT("Neutral");
  }

  return PlayerState->GetResolvedPlayerName(Context);
}

constexpr const TCHAR *PendingBattleAttackError =
    TEXT("A battle is already being prepared. Please wait for it to begin.");
constexpr const TCHAR *PendingBattlePhaseError =
    TEXT("Cannot end the phase while a battle is awaiting confirmation.");

constexpr float FighterDeathEffectHeightOffset = 120.f;
constexpr int32 VeilStepRange = 3;
const FColor VeilStepHighlightColor(128, 64, 255, 215);
const FName VeilStepAbilityId(TEXT("Ability_Elf_Skirmish"));

bool IsCursorOverInteractableSlateWidget() {
  if (!FSlateApplication::IsInitialized()) {
    return false;
  }

  FSlateApplication &SlateApp = FSlateApplication::Get();
  const FVector2D CursorPos = SlateApp.GetCursorPos();

  TArray<TSharedRef<SWindow>> Windows = SlateApp.GetInteractiveTopLevelWindows();
  if (Windows.Num() == 0) {
    return false;
  }

  const FWidgetPath WidgetPath =
      SlateApp.LocateWindowUnderMouse(CursorPos, Windows);

  for (int32 Index = WidgetPath.Widgets.Num() - 1; Index >= 0; --Index) {
    const FArrangedWidget &ArrangedWidget = WidgetPath.Widgets[Index];
    if (ArrangedWidget.Widget->IsInteractable()) {
      return true;
    }
  }

  return false;
}
}


ASkald_BattleGameMode *ASkaldPlayerController::ResolveBattleGameMode() {
  if (UWorld *World = GetWorld()) {
    if (ASkald_BattleGameMode *BattleGM =
            World->GetAuthGameMode<ASkald_BattleGameMode>()) {
      return BattleGM;
    }
  }

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    return GI->GetActiveBattleGameMode();
  }

  return nullptr;
}

ASkaldPlayerController::ASkaldPlayerController() {
  TurnManager = nullptr;
  HUDRef = nullptr;
  MainHUD = nullptr;
  BattleHudWidget = nullptr;
  BattleResultWidget = nullptr;
  CurrentCommandMode = EBattleCommandMode::None;
  bHasInitialized = false;
  CurrentFaction = ESkaldFaction::None;
  ActiveCursorTrailFX = nullptr;
  ActiveCursorTrailTemplate.Reset();
  bWasHoveringInteractable = false;

  bShowMouseCursor = true;
  bEnableClickEvents = true;
  bEnableMouseOverEvents = true;
  DefaultMouseCaptureMode = EMouseCaptureMode::NoCapture;

  // Default to the native HUD widget class. This avoids loading a
  // blueprint-derived widget that may not exist or may be corrupt.
  MainHUDClass = USkaldMainHUDWidget::StaticClass();
  BattleHUDWidgetClass = UBattleHUDWidget::StaticClass();
  FighterSelectionWidgetClass = UFighterSelectionWidget::StaticClass();
  VictoryWidgetClass = UBattleResultWidget::StaticClass();
  InGameMenuWidgetClass = UInGameMenuWidget::StaticClass();
  bBattleHUDVisible = false;
  bBattleHUDReadyToShow = false;

  static ConstructorHelpers::FClassFinder<UChoosePlayerWidget> ChooseBP(
      TEXT("/Game/Blueprints/UI/Skald_ChoosePlayerWidget"));
  if (ChooseBP.Succeeded()) {
    ChoosePlayerWidgetClass = ChooseBP.Class;
  }

  if (!HitCameraShakeClass || !MissCameraShakeClass) {
    UE_LOG(LogSkald, Verbose,
           TEXT("ASkaldPlayerController camera shake classes unset. Assign Blueprint overrides in defaults to enable feedback."));
  }
}

void ASkaldPlayerController::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  DOREPLIFETIME(ASkaldPlayerController, TurnManager);
}

void ASkaldPlayerController::CacheGameReferences() {
  CachedGameState = GetWorld()->GetGameState<ASkaldGameState>();
  if (!CachedGameState) {
    UE_LOG(LogSkald, Error,
           TEXT("ASkaldPlayerController could not find ASkaldGameState."));
  } else {
    CachedGameState->OnPlayersUpdated.AddDynamic(
        this, &ASkaldPlayerController::HandlePlayersUpdated);
  }

  CachedGameMode = GetWorld()->GetAuthGameMode<ASkaldGameMode>();
  if (!CachedGameMode) {
    UE_LOG(LogSkald, Error,
           TEXT("ASkaldPlayerController could not find ASkaldGameMode."));
  }

  CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  if (!CachedGameInstance) {
    UE_LOG(LogSkald, Error,
           TEXT("ASkaldPlayerController could not find USkaldGameInstance."));
  } else {
    CachedGameInstance->OnFactionsUpdated.AddDynamic(
        this, &ASkaldPlayerController::HandleFactionsUpdated);
    CachedGameInstance->OnBattleMapStateChanged.RemoveDynamic(
        this, &ASkaldPlayerController::HandleBattleMapStateChanged);
    CachedGameInstance->OnBattleMapStateChanged.AddDynamic(
        this, &ASkaldPlayerController::HandleBattleMapStateChanged);
  }
}

bool ASkaldPlayerController::TryUseAbilitySlot(ESkaldAbilitySlot Slot) {
  if (!HasAuthority()) {
    ServerTryUseAbilitySlot(Slot);
    return false;
  }

  FText FailureReason;

  if (!SelectedFighter) {
    FailureReason = NSLOCTEXT("SkaldAbilities", "AbilityNoSelection",
                               "Select a fighter before using abilities.");
  } else if (!IsFriendlyFighter(SelectedFighter)) {
    FailureReason = NSLOCTEXT("SkaldAbilities", "AbilityEnemyFighter",
                               "Cannot trigger abilities on enemy fighters.");
  } else if (USkaldAbilityComponent *AbilityComponent =
                 SelectedFighter->GetAbilityComponent()) {
    if (AbilityComponent->TryBeginAbility(Slot, FailureReason)) {
      UpdateBattleHUDButtons();
      return true;
    }
  } else {
    FailureReason = NSLOCTEXT("SkaldAbilities", "AbilityComponentMissing",
                               "This fighter has no abilities configured.");
  }

  if (!FailureReason.IsEmpty()) {
    NotifyActionError(FailureReason.ToString());
  }

  return false;
}

void ASkaldPlayerController::HandleAbilityInput(ESkaldAbilitySlot Slot) {
  if (!IsLocalController()) {
    return;
  }

  if (HandleAbilityTargetingInput(Slot)) {
    return;
  }

  TryUseAbilitySlot(Slot);
}

void ASkaldPlayerController::ServerTryUseAbilitySlot_Implementation(
    ESkaldAbilitySlot Slot) {
  TryUseAbilitySlot(Slot);
}

bool ASkaldPlayerController::IsVeilStepAbility(
    const USkaldAbilityComponent *AbilityComp, ESkaldAbilitySlot Slot) const {
  if (!AbilityComp) {
    return false;
  }

  const FSkaldAbilityState *State = AbilityComp->FindAbilityState(Slot);
  if (!State || !State->Definition.IsValid()) {
    return false;
  }

  return State->Definition.AbilityId == VeilStepAbilityId;
}

bool ASkaldPlayerController::TryBeginVeilStepTargeting(ESkaldAbilitySlot Slot) {
  if (!SelectedFighter) {
    return false;
  }

  USkaldAbilityComponent *AbilityComponent =
      SelectedFighter->GetAbilityComponent();
  if (!AbilityComponent) {
    const FText ErrorText = NSLOCTEXT(
        "SkaldAbilities", "AbilityComponentMissing",
        "This fighter has no abilities configured.");
    NotifyActionError(ErrorText.ToString());
    return true;
  }

  if (!IsVeilStepAbility(AbilityComponent, Slot)) {
    return false;
  }

  if (!IsFriendlyFighter(SelectedFighter)) {
    const FText ErrorText = NSLOCTEXT(
        "SkaldAbilities", "AbilityEnemyFighter",
        "Cannot trigger abilities on enemy fighters.");
    NotifyActionError(ErrorText.ToString());
    return true;
  }

  FText FailureReason;
  if (!AbilityComponent->CanActivateAbility(Slot, &FailureReason)) {
    if (!FailureReason.IsEmpty()) {
      NotifyActionError(FailureReason.ToString());
    }
    return true;
  }

  BeginVeilStepTargeting(SelectedFighter, Slot);
  return true;
}

void ASkaldPlayerController::BeginVeilStepTargeting(AFighterPawn *Fighter,
                                                    ESkaldAbilitySlot Slot) {
  if (!Fighter) {
    return;
  }

  CancelCommandMode();
  PendingVeilStepSlot = Slot;
  PendingVeilStepFighter = Fighter;
  CurrentCommandMode = EBattleCommandMode::VeilStep;

  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    HighlightVeilStepOptions(Fighter, Grid);
  }
}

void ASkaldPlayerController::HighlightVeilStepOptions(
    AFighterPawn *Fighter, UGridOverlayComponent *Grid) const {
  if (!Fighter || !Grid) {
    return;
  }

  Grid->ClearHighlights();

  const FColor SelectionColor = Grid->SelectionHighlightColor.ToFColor(true);
  const TArray<FIntPoint> CurrentCells = Fighter->GetOccupiedCells();
  const FIntPoint CurrentAnchor = Fighter->GetCurrentCell();
  for (const FIntPoint &Cell : CurrentCells) {
    Grid->HighlightCell(Cell, SelectionColor, 0.f, false);
  }

  const int32 GridWidth = Grid->GetWidth();
  const int32 GridHeight = Grid->GetLength();

  for (int32 Y = 0; Y < GridHeight; ++Y) {
    for (int32 X = 0; X < GridWidth; ++X) {
      const FIntPoint Anchor(X, Y);
      if (Anchor == CurrentAnchor) {
        continue;
      }

      if (!IsVeilStepDestinationValid(Fighter, Grid, Anchor)) {
        continue;
      }

      const TArray<FIntPoint> TargetCells = Fighter->GetOccupiedCells(Anchor);
      for (const FIntPoint &Cell : TargetCells) {
        Grid->HighlightCell(Cell, VeilStepHighlightColor, 0.f, false);
      }
    }
  }
}

bool ASkaldPlayerController::FindBestVeilStepAnchor(
    AFighterPawn *Fighter, UGridOverlayComponent *Grid,
    const FIntPoint &ClickedCell, FIntPoint &OutAnchor) const {
  if (!Fighter || !Grid) {
    return false;
  }

  const int32 FootprintSize = Fighter->GetFootprintSideLength();
  bool bFoundAnchor = false;
  int32 BestDistance = MAX_int32;

  for (int32 Dy = 0; Dy < FootprintSize; ++Dy) {
    for (int32 Dx = 0; Dx < FootprintSize; ++Dx) {
      const FIntPoint Candidate = ClickedCell - FIntPoint(Dx, Dy);
      if (!IsVeilStepDestinationValid(Fighter, Grid, Candidate)) {
        continue;
      }

      const int32 DistanceToClick = FMath::Max(
          FMath::Abs(Candidate.X - ClickedCell.X),
          FMath::Abs(Candidate.Y - ClickedCell.Y));

      if (!bFoundAnchor || DistanceToClick < BestDistance) {
        BestDistance = DistanceToClick;
        OutAnchor = Candidate;
        bFoundAnchor = true;
      }
    }
  }

  if (bFoundAnchor) {
    return true;
  }

  if (IsVeilStepDestinationValid(Fighter, Grid, ClickedCell)) {
    OutAnchor = ClickedCell;
    return true;
  }

  return false;
}

bool ASkaldPlayerController::IsVeilStepDestinationValid(
    AFighterPawn *Fighter, UGridOverlayComponent *Grid,
    const FIntPoint &Anchor) const {
  if (!Fighter || !Grid) {
    return false;
  }

  const FIntPoint StartCell = Fighter->GetCurrentCell();
  const int32 Distance = FMath::Max(FMath::Abs(Anchor.X - StartCell.X),
                                    FMath::Abs(Anchor.Y - StartCell.Y));
  if (Distance > VeilStepRange) {
    return false;
  }

  const TArray<FIntPoint> CurrentCells = Fighter->GetOccupiedCells(StartCell);
  const TArray<FIntPoint> TargetCells = Fighter->GetOccupiedCells(Anchor);
  if (TargetCells.Num() == 0) {
    return false;
  }

  for (const FIntPoint &Cell : TargetCells) {
    if (!Grid->IsCellInBounds(Cell) || Grid->IsObscured(Cell)) {
      return false;
    }

    const bool bPreviouslyOccupied = CurrentCells.Contains(Cell);
    if (!bPreviouslyOccupied && Grid->IsOccupied(Cell)) {
      return false;
    }
  }

  bool bHasLineOfSight = false;
  for (const FIntPoint &FromCell : CurrentCells) {
    for (const FIntPoint &ToCell : TargetCells) {
      if (Grid->HasLineOfSight(FromCell, ToCell)) {
        bHasLineOfSight = true;
        break;
      }
    }

    if (bHasLineOfSight) {
      break;
    }
  }

  return bHasLineOfSight;
}

bool ASkaldPlayerController::ExecuteVeilStepInternal(
    AFighterPawn *Fighter, ESkaldAbilitySlot Slot, const FIntPoint &TargetAnchor,
    FText *OutError) {
  if (!Fighter) {
    if (OutError) {
      *OutError = NSLOCTEXT("SkaldAbilities", "AbilityNoSelection",
                             "Select a fighter before using abilities.");
    }
    return false;
  }

  USkaldAbilityComponent *AbilityComponent = Fighter->GetAbilityComponent();
  if (!AbilityComponent || !IsVeilStepAbility(AbilityComponent, Slot)) {
    if (OutError) {
      *OutError = NSLOCTEXT("SkaldAbilities", "AbilityUnavailable",
                             "No ability is assigned to that slot.");
    }
    return false;
  }

  UGridOverlayComponent *Grid = Fighter->GetGrid();
  if (!IsVeilStepDestinationValid(Fighter, Grid, TargetAnchor)) {
    if (OutError) {
      *OutError = NSLOCTEXT("SkaldAbilities", "VeilStepInvalidDestination",
                             "Select a visible empty tile within 3 squares.");
    }
    return false;
  }

  FText FailureReason;
  if (!AbilityComponent->TryBeginAbility(Slot, FailureReason)) {
    if (OutError) {
      *OutError = FailureReason;
    }
    return false;
  }

  if (!Fighter->TryTeleportToCell(TargetAnchor, VeilStepRange, true)) {
    UE_LOG(LogSkaldBattle, Warning,
           TEXT("Veil Step teleport failed for %s despite validation."),
           *Fighter->GetHumanReadableName());
    if (OutError) {
      *OutError = NSLOCTEXT("SkaldAbilities", "VeilStepTeleportFailed",
                             "Unable to teleport to that cell.");
    }
    return false;
  }

  return true;
}

void ASkaldPlayerController::ServerExecuteVeilStep_Implementation(
    AFighterPawn *Fighter, ESkaldAbilitySlot Slot, FIntPoint TargetAnchor) {
  FText Error;
  if (!ExecuteVeilStepInternal(Fighter, Slot, TargetAnchor, &Error)) {
    if (!Error.IsEmpty()) {
      NotifyActionError(Error.ToString());
    }
  }
}

bool ASkaldPlayerController::HandleAbilityTargetingInput(
    ESkaldAbilitySlot Slot) {
  if (TryBeginSpecialAbilityTargeting(Slot)) {
    return true;
  }

  if (!SelectedFighter) {
    return false;
  }

  if (!IsFriendlyFighter(SelectedFighter)) {
    const FText ErrorText = NSLOCTEXT(
        "SkaldAbilities", "AbilityEnemyFighter",
        "Cannot trigger abilities on enemy fighters.");
    NotifyActionError(ErrorText.ToString());
    return true;
  }

  USkaldAbilityComponent *AbilityComponent =
      SelectedFighter->GetAbilityComponent();
  if (!AbilityComponent) {
    const FText ErrorText =
        NSLOCTEXT("SkaldAbilities", "AbilityComponentMissing",
                  "This fighter has no abilities configured.");
    NotifyActionError(ErrorText.ToString());
    return true;
  }

  const FSkaldAbilityState *State =
      AbilityComponent->FindAbilityState(Slot);
  if (!State || !State->Definition.IsValid()) {
    return false;
  }

  const FSkaldAbilityTargetingInfo TargetingInfo =
      GetAbilityTargetingInfo(State->Definition.AbilityId);
  if (TargetingInfo.CommandMode == EBattleCommandMode::None) {
    return false;
  }

  FText FailureReason;
  if (!AbilityComponent->CanActivateAbility(Slot, &FailureReason)) {
    if (!FailureReason.IsEmpty()) {
      NotifyActionError(FailureReason.ToString());
    }
    return true;
  }

  if (!TryBeginAbilityCommand(SelectedFighter, Slot, TargetingInfo,
                              State->Definition.AbilityId)) {
    return true;
  }

  return true;
}

bool ASkaldPlayerController::TryBeginSpecialAbilityTargeting(
    ESkaldAbilitySlot Slot) {
  if (TryBeginVeilStepTargeting(Slot)) {
    return true;
  }

  return false;
}

bool ASkaldPlayerController::TryBeginAbilityCommand(
    AFighterPawn *Fighter, ESkaldAbilitySlot Slot,
    const FSkaldAbilityTargetingInfo &Targeting, const FName AbilityId) {
  if (!Fighter || Targeting.CommandMode == EBattleCommandMode::None) {
    return false;
  }

  CancelCommandMode();

  FPendingAbilityCommand Command;
  Command.SourceFighter = Fighter;
  Command.Slot = Slot;
  Command.AbilityId = AbilityId;
  Command.Targeting = Targeting;

  PendingAbilityCommand = Command;
  CurrentCommandMode = Targeting.CommandMode;

  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    HighlightAbilityCommandOptions(Command, Grid);
  }

  return true;
}

namespace {
const FColor AbilityTargetHighlightColor(255, 196, 0, 215);
}

void ASkaldPlayerController::HighlightAbilityCommandOptions(
    const FPendingAbilityCommand &Command,
    UGridOverlayComponent *Grid) const {
  if (!Grid) {
    return;
  }

  Grid->ClearHighlights();

  AFighterPawn *Source = Command.SourceFighter.Get();
  if (!Source) {
    return;
  }

  const FColor SelectionColor = Grid->SelectionHighlightColor.ToFColor(true);
  const TArray<FIntPoint> SourceCells = Source->GetOccupiedCells();
  TSet<FIntPoint> OccupiedCells;
  for (const FIntPoint &Cell : SourceCells) {
    OccupiedCells.Add(Cell);
    Grid->HighlightCell(Cell, SelectionColor, 0.f, false);
  }

  const int32 Range = Command.Targeting.RangeOverride == INDEX_NONE
                           ? Source->Stats.AttackRange
                           : Command.Targeting.RangeOverride;

  if (Range >= 0) {
    const bool bTargetsCells = Command.Targeting.CommandMode ==
                               EBattleCommandMode::AbilityTargetCell;
    const FColor RangeColor =
        bTargetsCells && Command.Targeting.bAllowEmptyCell
            ? Grid->MovementHighlightColor.ToFColor(true)
            : Grid->AttackHighlightColor.ToFColor(true);

    const int32 GridWidth = Grid->GetWidth();
    const int32 GridHeight = Grid->GetLength();
    for (int32 Y = 0; Y < GridHeight; ++Y) {
      for (int32 X = 0; X < GridWidth; ++X) {
        const FIntPoint Cell(X, Y);
        if (OccupiedCells.Contains(Cell)) {
          continue;
        }

        bool bWithinRange = false;
        for (const FIntPoint &SourceCell : SourceCells) {
          const int32 Distance = FMath::Max(
              FMath::Abs(SourceCell.X - Cell.X),
              FMath::Abs(SourceCell.Y - Cell.Y));
          if (Distance > Range) {
            continue;
          }

          if (Command.Targeting.bRequireLineOfSight &&
              !Grid->HasLineOfSight(SourceCell, Cell)) {
            continue;
          }

          bWithinRange = true;
          break;
        }

        if (!bWithinRange) {
          continue;
        }

        Grid->HighlightCell(Cell, RangeColor, 0.f, false);
      }
    }
  }

  if (Command.Targeting.CommandMode ==
          EBattleCommandMode::AbilityTargetEnemy ||
      Command.Targeting.CommandMode ==
          EBattleCommandMode::AbilityTargetAlly) {
    UGridBattleManager *BattleManager = GetBattleManager();
    if (!BattleManager) {
      return;
    }

    const bool bTargetEnemy = Command.Targeting.CommandMode ==
                              EBattleCommandMode::AbilityTargetEnemy;

    const TArray<AFighterPawn *> Fighters =
        BattleManager->GetInitiativeOrderSnapshot();
    for (AFighterPawn *Candidate : Fighters) {
      if (!Candidate) {
        continue;
      }

      const bool bIsSelf = Candidate == Source;
      if (bIsSelf && !Command.Targeting.bAllowSelfTarget) {
        continue;
      }

      const bool bIsEnemy = Candidate->Faction != Source->Faction;
      if (bTargetEnemy != bIsEnemy) {
        continue;
      }

      const int32 Distance = Source->GetFootprintDistanceToFighter(Candidate);
      if (Range >= 0 && Distance > Range) {
        continue;
      }

      if (Command.Targeting.bRequireLineOfSight) {
        if (!Source->HasLineOfSightToFighter(Candidate, Range, Grid)) {
          continue;
        }
      }

      if (bIsSelf) {
        continue;
      }
      const TArray<FIntPoint> Cells = Candidate->GetOccupiedCells();
      for (const FIntPoint &Cell : Cells) {
        Grid->HighlightCell(Cell, AbilityTargetHighlightColor, 0.f, false);
      }
    }
    return;
  }

  if (Command.Targeting.CommandMode ==
      EBattleCommandMode::AbilityTargetCell) {
    const int32 GridWidth = Grid->GetWidth();
    const int32 GridHeight = Grid->GetLength();
    for (int32 Y = 0; Y < GridHeight; ++Y) {
      for (int32 X = 0; X < GridWidth; ++X) {
        const FIntPoint Cell(X, Y);
        const int32 Distance = Source->GetFootprintDistanceToCell(Cell);
        if (Range >= 0 && Distance > Range) {
          continue;
        }

        if (Command.Targeting.bRequireLineOfSight) {
          bool bHasLineOfSight = false;
          for (const FIntPoint &SourceCell : SourceCells) {
            if (Grid->HasLineOfSight(SourceCell, Cell)) {
              bHasLineOfSight = true;
              break;
            }
          }

          if (!bHasLineOfSight) {
            continue;
          }
        }

        if (!Command.Targeting.bAllowEmptyCell && !Grid->IsOccupied(Cell)) {
          continue;
        }

        if (Command.AbilityId == TEXT("Ability_Ravpack_Line") &&
            Grid->HasTrapMarker(Cell)) {
          continue;
        }

        Grid->HighlightCell(Cell, AbilityTargetHighlightColor, 0.f, false);
      }
    }
  }
}

FSkaldAbilityTargetingInfo ASkaldPlayerController::GetAbilityTargetingInfo(
    FName AbilityId) const {
  FSkaldAbilityTargetingInfo Info;

  struct FAbilityTargetingPreset {
    EBattleCommandMode CommandMode = EBattleCommandMode::None;
    int32 RangeOverride = INDEX_NONE;
    bool bRequireLineOfSight = true;
    bool bAllowEmptyCell = false;
  };

  // Maintain the per-ability targeting defaults in a single table so we can
  // easily audit which actives provide extended range compared to a fighter's
  // native attack profile. Deep Delve Mortar (6 tiles), Starfall Invocation
  // (8 tiles) and Grave Grasp (3 tiles) are the only abilities whose selection
  // range exceeds their standard attack stat.
  static const TMap<FName, FAbilityTargetingPreset> TargetingPresets = {
      {TEXT("Ability_Human_Skirmish"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE}},
      {TEXT("Ability_Orc_Skirmish"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE}},
      {TEXT("Ability_Inflicted_Skirmish"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE}},
      {TEXT("Ability_Ravpack_Skirmish"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE}},
      {TEXT("Ability_Orc_Line"), {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE}},
      {TEXT("Ability_Dwarf_Elite"),
       {EBattleCommandMode::AbilityTargetEnemy, 6}},
      {TEXT("Ability_Elf_Line"), {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE}},
      {TEXT("Ability_Undead_Line"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE}},
      {TEXT("Ability_Gnoll_Skirmish"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE}},
      {TEXT("Ability_Gnoll_Elite"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE}},
      {TEXT("Ability_Empire_Skirmish"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE}},
      {TEXT("Ability_Empire_Elite"),
       {EBattleCommandMode::AbilityTargetEnemy, INDEX_NONE}},
      {TEXT("Ability_Ravpack_Line"),
       {EBattleCommandMode::AbilityTargetCell, 1, true, true}},
      {TEXT("Ability_Elf_Elite"), {EBattleCommandMode::AbilityTargetEnemy, 8}},
      {TEXT("Ability_Undead_Skirmish"),
       {EBattleCommandMode::AbilityTargetEnemy, 3}},
  };

  if (const FAbilityTargetingPreset *Preset = TargetingPresets.Find(AbilityId)) {
    Info.CommandMode = Preset->CommandMode;
    Info.RangeOverride = Preset->RangeOverride;
    Info.bRequireLineOfSight = Preset->bRequireLineOfSight;
    Info.bAllowEmptyCell = Preset->bAllowEmptyCell;
    return Info;
  }

  return Info;
}

bool ASkaldPlayerController::ValidateAbilityTargetFighter(
    const FPendingAbilityCommand &Command, AFighterPawn *Target,
    FText &OutError) const {
  OutError = FText::GetEmpty();

  AFighterPawn *Source = Command.SourceFighter.Get();
  if (!Source || !Target) {
    OutError = NSLOCTEXT("SkaldAbilities", "AbilityNoSelection",
                         "Select a fighter before using abilities.");
    return false;
  }

  if (Target == Source && !Command.Targeting.bAllowSelfTarget) {
    OutError = NSLOCTEXT("SkaldAbilities", "AbilitySelfInvalid",
                         "Cannot target the source fighter with this ability.");
    return false;
  }

  const bool bExpectEnemy = Command.Targeting.CommandMode ==
                            EBattleCommandMode::AbilityTargetEnemy;
  const bool bIsEnemy = Target->Faction != Source->Faction;
  if (bExpectEnemy != bIsEnemy) {
    OutError = bExpectEnemy
                   ? NSLOCTEXT("SkaldAbilities", "AbilityEnemyRequired",
                               "Select an enemy fighter.")
                   : NSLOCTEXT("SkaldAbilities", "AbilityAllyRequired",
                               "Select a friendly fighter.");
    return false;
  }

  const int32 Range = Command.Targeting.RangeOverride == INDEX_NONE
                          ? Source->Stats.AttackRange
                          : Command.Targeting.RangeOverride;
  const int32 Distance = Source->GetFootprintDistanceToFighter(Target);
  if (Range >= 0 && Distance > Range) {
    OutError = NSLOCTEXT("SkaldAbilities", "AbilityTargetOutOfRange",
                         "Target is out of range.");
    return false;
  }

  if (Command.Targeting.bRequireLineOfSight) {
    UGridOverlayComponent *Grid = Source->GetGrid();
    if (Grid && !Source->HasLineOfSightToFighter(Target, Range, Grid)) {
      OutError = NSLOCTEXT("SkaldAbilities", "AbilityRequiresLineOfSight",
                           "No line of sight to target.");
      return false;
    }
  }

  return true;
}

bool ASkaldPlayerController::ValidateAbilityTargetCell(
    const FPendingAbilityCommand &Command, const FIntPoint &Cell,
    FText &OutError) const {
  OutError = FText::GetEmpty();

  AFighterPawn *Source = Command.SourceFighter.Get();
  if (!Source) {
    OutError = NSLOCTEXT("SkaldAbilities", "AbilityNoSelection",
                         "Select a fighter before using abilities.");
    return false;
  }

  UGridOverlayComponent *Grid = Source->GetGrid();
  if (!Grid || !Grid->IsCellInBounds(Cell)) {
    OutError = NSLOCTEXT("SkaldAbilities", "AbilityCellInvalid",
                         "Select a valid cell on the grid.");
    return false;
  }

  const int32 Range = Command.Targeting.RangeOverride == INDEX_NONE
                          ? Source->Stats.AttackRange
                          : Command.Targeting.RangeOverride;
  const int32 Distance = Source->GetFootprintDistanceToCell(Cell);
  if (Range >= 0 && Distance > Range) {
    OutError = NSLOCTEXT("SkaldAbilities", "AbilityTargetOutOfRange",
                         "Target is out of range.");
    return false;
  }

  if (Command.Targeting.bRequireLineOfSight) {
    const TArray<FIntPoint> SourceCells = Source->GetOccupiedCells();
    bool bHasLine = false;
    for (const FIntPoint &SourceCell : SourceCells) {
      if (Grid->HasLineOfSight(SourceCell, Cell)) {
        bHasLine = true;
        break;
      }
    }

    if (!bHasLine) {
      OutError = NSLOCTEXT("SkaldAbilities", "AbilityRequiresLineOfSight",
                           "No line of sight to target.");
      return false;
    }
  }

  if (!Command.Targeting.bAllowEmptyCell && !Grid->IsOccupied(Cell)) {
    OutError = NSLOCTEXT("SkaldAbilities", "AbilityRequiresTarget",
                         "Select an occupied cell.");
    return false;
  }

  if (Command.AbilityId == TEXT("Ability_Ravpack_Line")) {
    if (Grid->IsOccupied(Cell)) {
      OutError = NSLOCTEXT("SkaldAbilities", "AbilityTrapCellOccupied",
                           "That tile is already occupied.");
      return false;
    }

    if (Grid->HasTrapMarker(Cell)) {
      OutError = NSLOCTEXT("SkaldAbilities", "AbilityTrapCellBlocked",
                           "A trap already covers that tile.");
      return false;
    }
  }

  return true;
}

bool ASkaldPlayerController::ExecuteAbilityCommandInternal(
    const FPendingAbilityCommand &Command, AFighterPawn *TargetFighter,
    const FIntPoint *TargetCell, FText *OutError) {
  AFighterPawn *Source = Command.SourceFighter.Get();
  if (!Source) {
    if (OutError) {
      *OutError = NSLOCTEXT("SkaldAbilities", "AbilityNoSelection",
                             "Select a fighter before using abilities.");
    }
    return false;
  }

  USkaldAbilityComponent *AbilityComponent = Source->GetAbilityComponent();
  if (!AbilityComponent) {
    if (OutError) {
      *OutError = NSLOCTEXT("SkaldAbilities", "AbilityComponentMissing",
                             "This fighter has no abilities configured.");
    }
    return false;
  }

  const FSkaldAbilityState *State =
      AbilityComponent->FindAbilityState(Command.Slot);
  if (!State || !State->Definition.IsValid() ||
      State->Definition.AbilityId != Command.AbilityId) {
    if (OutError) {
      *OutError = NSLOCTEXT("SkaldAbilities", "AbilityUnavailable",
                             "No ability is assigned to that slot.");
    }
    return false;
  }

  FText FailureReason;
  if (!AbilityComponent->CanActivateAbility(Command.Slot, &FailureReason)) {
    if (OutError) {
      *OutError = FailureReason;
    }
    return false;
  }

  if (Command.Targeting.CommandMode ==
          EBattleCommandMode::AbilityTargetEnemy ||
      Command.Targeting.CommandMode ==
          EBattleCommandMode::AbilityTargetAlly) {
    if (!TargetFighter) {
      if (OutError) {
        *OutError = NSLOCTEXT("SkaldAbilities", "AbilityRequiresTarget",
                               "Select a valid target.");
      }
      return false;
    }

    if (!ValidateAbilityTargetFighter(Command, TargetFighter, FailureReason)) {
      if (OutError) {
        *OutError = FailureReason;
      }
      return false;
    }

    if (!TryExecuteAbilityOnFighter(Source, Command.Slot, TargetFighter,
                                    FailureReason)) {
      if (OutError) {
        *OutError = FailureReason;
      }
      return false;
    }

    return true;
  }

  if (Command.Targeting.CommandMode ==
      EBattleCommandMode::AbilityTargetCell) {
    if (!TargetCell) {
      if (OutError) {
        *OutError = NSLOCTEXT("SkaldAbilities", "AbilityRequiresTarget",
                               "Select a valid target.");
      }
      return false;
    }

    if (!ValidateAbilityTargetCell(Command, *TargetCell, FailureReason)) {
      if (OutError) {
        *OutError = FailureReason;
      }
      return false;
    }

    if (!TryExecuteAbilityAtCell(Source, Command.Slot, *TargetCell,
                                 FailureReason)) {
      if (OutError) {
        *OutError = FailureReason;
      }
      return false;
    }

    return true;
  }

  if (OutError) {
    *OutError = NSLOCTEXT("SkaldAbilities", "AbilityUnsupported",
                           "Ability targeting not supported.");
  }
  return false;
}

void ASkaldPlayerController::ServerExecuteAbilityOnFighter_Implementation(
    AFighterPawn *Source, ESkaldAbilitySlot Slot, AFighterPawn *Target) {
  if (!Source) {
    return;
  }

  FSkaldAbilityTargetingInfo Targeting =
      GetAbilityTargetingInfo(FName());

  if (USkaldAbilityComponent *AbilityComponent = Source->GetAbilityComponent()) {
    if (const FSkaldAbilityState *State =
            AbilityComponent->FindAbilityState(Slot)) {
      Targeting = GetAbilityTargetingInfo(State->Definition.AbilityId);
      FPendingAbilityCommand Command;
      Command.SourceFighter = Source;
      Command.Slot = Slot;
      Command.AbilityId = State->Definition.AbilityId;
      Command.Targeting = Targeting;

      FText Error;
      if (!ExecuteAbilityCommandInternal(Command, Target, nullptr, &Error)) {
        if (!Error.IsEmpty()) {
          NotifyActionError(Error.ToString());
        }
      }
    }
  }
}

void ASkaldPlayerController::ServerExecuteAbilityAtCell_Implementation(
    AFighterPawn *Source, ESkaldAbilitySlot Slot, FIntPoint Target) {
  if (!Source) {
    return;
  }

  if (USkaldAbilityComponent *AbilityComponent = Source->GetAbilityComponent()) {
    if (const FSkaldAbilityState *State =
            AbilityComponent->FindAbilityState(Slot)) {
      FPendingAbilityCommand Command;
      Command.SourceFighter = Source;
      Command.Slot = Slot;
      Command.AbilityId = State->Definition.AbilityId;
      Command.Targeting = GetAbilityTargetingInfo(State->Definition.AbilityId);

      FText Error;
      if (!ExecuteAbilityCommandInternal(Command, nullptr, &Target, &Error)) {
        if (!Error.IsEmpty()) {
          NotifyActionError(Error.ToString());
        }
      }
    }
  }
}

bool ASkaldPlayerController::TryExecuteAbilityOnFighter(
    AFighterPawn *Source, ESkaldAbilitySlot Slot, AFighterPawn *Target,
    FText &OutError) {
  OutError = FText::GetEmpty();

  if (!Source || !Target) {
    OutError = NSLOCTEXT("SkaldAbilities", "AbilityRequiresTarget",
                         "Select a valid target.");
    return false;
  }

  USkaldAbilityComponent *AbilityComponent = Source->GetAbilityComponent();
  if (!AbilityComponent) {
    OutError = NSLOCTEXT("SkaldAbilities", "AbilityComponentMissing",
                         "This fighter has no abilities configured.");
    return false;
  }

  FText FailureReason;
  if (!AbilityComponent->TryBeginAbility(Slot, FailureReason)) {
    OutError = FailureReason;
    return false;
  }

  const FSkaldAbilityState *State =
      AbilityComponent->FindAbilityState(Slot);
  if (State &&
      State->Definition.CostType == ESkaldAbilityCostType::Action) {
    Source->TryRestoreAction();
  }

  Source->PerformAttack(Target);

  if (State && State->Definition.AbilityId == TEXT("Ability_Elf_Line")) {
    Source->TryRestoreAction();
  }
  return true;
}

bool ASkaldPlayerController::TryExecuteAbilityAtCell(AFighterPawn *Source,
                                                     ESkaldAbilitySlot Slot,
                                                     const FIntPoint &Cell,
                                                     FText &OutError) {
  OutError = FText::GetEmpty();

  if (!Source) {
    OutError = NSLOCTEXT("SkaldAbilities", "AbilityRequiresTarget",
                         "Select a valid target.");
    return false;
  }

  USkaldAbilityComponent *AbilityComponent = Source->GetAbilityComponent();
  if (!AbilityComponent) {
    OutError = NSLOCTEXT("SkaldAbilities", "AbilityComponentMissing",
                         "This fighter has no abilities configured.");
    return false;
  }

  const FSkaldAbilityState *State = AbilityComponent->FindAbilityState(Slot);
  if (!State || !State->Definition.IsValid()) {
    OutError = NSLOCTEXT("SkaldAbilities", "AbilityUnavailable",
                         "No ability is assigned to that slot.");
    return false;
  }

  const FName AbilityId = State->Definition.AbilityId;
  const bool bHasPendingTrap = AbilityComponent->HasPendingTrapForAbility(AbilityId);

  if (!bHasPendingTrap) {
    FText FailureReason;
    if (!AbilityComponent->TryBeginAbility(Slot, FailureReason)) {
      OutError = FailureReason;
      return false;
    }
  }

  FText PlacementError;
  if (!AbilityComponent->DeployTrapAtCell(Cell, AbilityId, PlacementError)) {
    OutError = PlacementError;
    return false;
  }

  return true;
}

void ASkaldPlayerController::InitializeHUDWidget() {
  if (MainHUD) {
    return;
  }

  if (!MainHUDClass) {
    UE_LOG(LogSkald, Warning,
           TEXT("MainHUDClass is null; HUD will not be displayed."));
    return;
  }

  if (!IsLocalPlayerController()) {
    UE_LOG(LogSkald, Verbose,
           TEXT("InitializeHUDWidget skipped: controller %s is not local."),
           *GetName());
    return;
  }

  if (!GetLocalPlayer()) {
    UE_LOG(LogSkald, Verbose,
           TEXT("InitializeHUDWidget skipped: controller %s has no local player."),
           *GetName());
    return;
  }

  MainHUD = CreateWidget<USkaldMainHUDWidget>(this, MainHUDClass);
  if (!MainHUD) {
    return;
  }

  HUDRef = MainHUD;
  MainHUD->AddToViewport(10);
  MainHUD->SetIsFocusable(true);
  MainHUD->SetVisibility(ESlateVisibility::Hidden);

  EnsureDiceWidgets();

  if (CachedGameState) {
    TArray<FS_PlayerData> Players;
    BuildPlayerDataArray(Players);
    const ASkaldPlayerState *CurrentPS = CachedGameState->GetCurrentPlayer();
    const int32 CurrentID = CurrentPS ? CurrentPS->GetPlayerId() : -1;
    MainHUD->RefreshFromState(CurrentID, /*TurnNumber*/ 1,
                              ETurnPhase::Reinforcement, Players);
  }

  // Ensure local player details are registered with the HUD once available.
  OnRep_PlayerState();

  MainHUD->OnAttackRequested.AddDynamic(
      this, &ASkaldPlayerController::HandleAttackRequested);
  MainHUD->OnPrepareForBattleReady.AddDynamic(
      this, &ASkaldPlayerController::HandlePrepareForBattleReady);
  MainHUD->OnRetreatRequested.AddDynamic(
      this, &ASkaldPlayerController::HandleRetreatRequested);
  MainHUD->OnRetreatDestinationChosen.AddDynamic(
      this, &ASkaldPlayerController::HandleRetreatDestinationSelected);
  MainHUD->OnMoveRequested.AddDynamic(
      this, &ASkaldPlayerController::HandleMoveRequested);
  MainHUD->OnEndAttackRequested.AddDynamic(
      this, &ASkaldPlayerController::HandleEndAttackRequested);
  MainHUD->OnEndMovementRequested.AddDynamic(
      this, &ASkaldPlayerController::HandleEndMovementRequested);
  MainHUD->OnEngineeringRequested.AddDynamic(
      this, &ASkaldPlayerController::HandleEngineeringRequested);
  MainHUD->OnBuildSiegeRequested.AddDynamic(
      this, &ASkaldPlayerController::HandleBuildSiegeRequested);
  MainHUD->OnDigTreasureRequested.AddDynamic(
      this, &ASkaldPlayerController::HandleDigTreasureRequested);
  MainHUD->OnStrategicInitiativeRollRequested.AddDynamic(
      this, &ASkaldPlayerController::HandleStrategicInitiativeRollRequested);

  if (bAwaitingStrategicInitiativeRoll) {
    const FText PromptText = NSLOCTEXT("Skald", "StrategicInitiativePrompt",
                                       "Roll for initiative");
    MainHUD->ShowStrategicInitiativePrompt(PromptText);
  } else if (PendingStrategicInitiativeRoll > 0) {
    ShowPendingStrategicInitiativeResult();
  }

  if (bPendingReadyPrompt) {
    UE_LOG(LogSkaldReady, Verbose,
           TEXT("InitializeHUDWidget detected pending ready prompt for %s; attempting to display."),
           *GetName());
    TryShowPendingReadyPrompt();
  }

  // Ensure the freshly constructed HUD reflects the current world/turn state
  // even if no world state change has been broadcast since the player joined
  // (e.g. after loading a save game mid-turn).
  HandleWorldStateChanged();

  // Notify the game mode that the HUD is now ready so world start checks can
  // proceed only after widgets are initialized.
  if (CachedGameMode) {
    CachedGameMode->TryInitializeWorldAndStart();
  }
}

void ASkaldPlayerController::InitializeChoosePlayerWidget() {
  if (ChoosePlayerWidget || !ChoosePlayerWidgetClass) {
    return;
  }

  ChoosePlayerWidget =
      CreateWidget<UChoosePlayerWidget>(this, ChoosePlayerWidgetClass);
  if (!ChoosePlayerWidget) {
    return;
  }

  ChoosePlayerWidget->OnPlayerLockedIn.AddDynamic(
      this, &ASkaldPlayerController::HandleFactionLockedIn);
  ChoosePlayerWidget->AddToViewport();

  // While the player is choosing their faction, restrict controls to the UI.
  FocusWidgetUIOnly(this, ChoosePlayerWidget);
  SetIgnoreMoveInput(true);
  SetIgnoreLookInput(true);
}

void ASkaldPlayerController::AutoInitializeFromLobbySelection() {
  if (bHasInitialized) {
    return;
  }

  FString DesiredName;
  ESkaldFaction DesiredFaction = ESkaldFaction::None;
  int32 DesiredAIPlayers = CachedGameInstance ? CachedGameInstance->AIPlayersToSpawn : 0;

  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    if (!PS->PlayerDisplayName.IsEmpty()) {
      DesiredName = PS->PlayerDisplayName;
    } else {
      DesiredName = PS->GetPlayerName();
    }

    if (PS->Faction != ESkaldFaction::None) {
      DesiredFaction = PS->Faction;
    }
  }

  if (DesiredName.IsEmpty() && CachedGameInstance) {
    DesiredName = CachedGameInstance->DisplayName;
  }

  if (DesiredFaction == ESkaldFaction::None && CachedGameInstance) {
    DesiredFaction = CachedGameInstance->Faction;
  }

  const bool bMissingName = DesiredName.IsEmpty();
  const bool bMissingFaction = DesiredFaction == ESkaldFaction::None;

  if (bMissingName || bMissingFaction) {
    UE_LOG(LogSkald, Verbose,
           TEXT("AutoInitializeFromLobbySelection: awaiting lobby data. MissingName=%s MissingFaction=%s"),
           bMissingName ? TEXT("true") : TEXT("false"),
           bMissingFaction ? TEXT("true") : TEXT("false"));

    if (UWorld *World = GetWorld()) {
      FTimerManager &TimerManager = World->GetTimerManager();
      if (!TimerManager.IsTimerActive(LobbyAutoInitHandle)) {
        TimerManager.SetTimer(LobbyAutoInitHandle, this,
                              &ASkaldPlayerController::AutoInitializeFromLobbySelection,
                              0.25f, false);
      }
    }
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(LobbyAutoInitHandle);
  }

  if (CachedGameInstance) {
    CachedGameInstance->DisplayName = DesiredName;
    CachedGameInstance->Faction = DesiredFaction;
    if (DesiredFaction != ESkaldFaction::None) {
      CachedGameInstance->TakenFactions.AddUnique(DesiredFaction);
    }
  }

  if (!CachedGameMode || !CachedGameMode->IsWorldInitialized()) {
    ServerInitPlayerState(DesiredName, DesiredFaction, DesiredAIPlayers);
  }

  HandleFactionLockedIn();
}

void ASkaldPlayerController::BeginPlay() {
  Super::BeginPlay();

  if (PostWorldBeginPlayHandle.IsValid()) {
#if UE_VERSION_OLDER_THAN(5, 5, 0)
    FWorldDelegates::OnWorldBeginPlay.Remove(PostWorldBeginPlayHandle);
#else
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostWorldBeginPlayHandle);
#endif
    PostWorldBeginPlayHandle.Reset();
  }

#if UE_VERSION_OLDER_THAN(5, 5, 0)
  PostWorldBeginPlayHandle = FWorldDelegates::OnWorldBeginPlay.AddUObject(
      this, &ASkaldPlayerController::HandleWorldBeginPlay);
#else
  PostWorldBeginPlayHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
      this, &ASkaldPlayerController::HandleWorldBeginPlay);
#endif

  CacheGameReferences();

  if (UWorld *World = GetWorld()) {
    if (!FighterSpawnedHandle.IsValid()) {
      FighterSpawnedHandle = World->AddOnActorSpawnedHandler(
          FOnActorSpawned::FDelegate::CreateUObject(
              this, &ASkaldPlayerController::HandleActorSpawned));
    }
  }

  DetectBattleMap();

  if (ASkaldGameState *SGS =
          GetWorld() ? GetWorld()->GetGameState<ASkaldGameState>() : nullptr) {
    if (SGS->BattlePhase == EBattlePhase::Deploy) {
      UE_LOG(LogSkaldBattle, Verbose,
             TEXT("PlayerController %s detected Deploy phase at BeginPlay"),
             *GetName());
    }
  }

  if (IsLocalPlayerController() && GetLocalPlayer() != nullptr) {
    if (!bIsBattleMap) {
      InitializeHUDWidget();
      ShowMainHUD();
    } else {
      UE_LOG(LogSkald, Log, TEXT("[HUD] Skipping MainHUD in BattleGameMode"));
    }
    if (CachedGameInstance && CachedGameInstance->bIsMultiplayer &&
        !CachedGameInstance->bIsHost) {
      if (GEngine) {
        GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green,
                                         TEXT("Connected to host"));
      }
    }
    if (CachedGameInstance && CachedGameInstance->bIsMultiplayer) {
      AutoInitializeFromLobbySelection();
    } else if (CachedGameInstance && !bHasInitialized) {
      if (!CachedGameMode || !CachedGameMode->IsWorldInitialized()) {
        ServerInitPlayerState(CachedGameInstance->DisplayName,
                              CachedGameInstance->Faction,
                              CachedGameInstance->AIPlayersToSpawn);
      }
      HandleFactionLockedIn();
    }
    InitializeFighterSelectionIfNeeded();
    RefreshFactionCursorFromState();
  }

  TryBindWorldMap();
}

void ASkaldPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason) {
  if (PostWorldBeginPlayHandle.IsValid()) {
#if UE_VERSION_OLDER_THAN(5, 5, 0)
    FWorldDelegates::OnWorldBeginPlay.Remove(PostWorldBeginPlayHandle);
#else
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostWorldBeginPlayHandle);
#endif
    PostWorldBeginPlayHandle.Reset();
  }

  ClearFactionCursor();

  if (CachedGameInstance) {
    CachedGameInstance->OnBattleMapStateChanged.RemoveDynamic(
        this, &ASkaldPlayerController::HandleBattleMapStateChanged);
  }

  ResetPendingReadyPromptState();

  if (MainHUD) {
    MainHUD->RemoveFromParent();
    MainHUD = nullptr;
    HUDRef = nullptr;
  }

  if (ChoosePlayerWidget) {
    ChoosePlayerWidget->RemoveFromParent();
    ChoosePlayerWidget = nullptr;
  }

  if (FighterSelectionWidget) {
    FighterSelectionWidget->RemoveFromParent();
    FighterSelectionWidget = nullptr;
  }

  if (BattleHudWidget) {
    BattleHudWidget->RemoveFromParent();
    BattleHudWidget = nullptr;
  }

  if (DiceOverlayWidget) {
    DiceOverlayWidget->RemoveFromParent();
    DiceOverlayWidget = nullptr;
  }

  if (DiceResultWidget) {
    DiceResultWidget->RemoveFromParent();
    DiceResultWidget = nullptr;
  }

  if (UWorld *TimerWorld = GetWorld()) {
    TimerWorld->GetTimerManager().ClearTimer(InitiativeResultHideTimer);
  }

  for (const TWeakObjectPtr<AFighterPawn> &TrackedFighter : ObservedFriendlyFighters) {
    if (AFighterPawn *Fighter = TrackedFighter.Get()) {
      Fighter->OnDestroyed.RemoveDynamic(
          this, &ASkaldPlayerController::HandleTrackedFighterDestroyed);
    }
  }
  ObservedFriendlyFighters.Reset();

  if (UWorld *World = GetWorld()) {
    if (FighterSpawnedHandle.IsValid()) {
      World->RemoveOnActorSpawnedHandler(FighterSpawnedHandle);
      FighterSpawnedHandle.Reset();
    }
    World->GetTimerManager().ClearTimer(LobbyAutoInitHandle);
  }

  if (BattleResultWidget) {
    BattleResultWidget->RemoveFromParent();
    BattleResultWidget = nullptr;
  }

  if (InGameMenuWidget) {
    InGameMenuWidget->RemoveFromParent();
    InGameMenuWidget = nullptr;
  }

  if (AWorldMap *WorldMap = CachedWorldMap.Get()) {
    if (WorldMap->OnTerritorySelected.IsAlreadyBound(
            this, &ASkaldPlayerController::HandleTerritorySelected)) {
      WorldMap->OnTerritorySelected.RemoveDynamic(
          this, &ASkaldPlayerController::HandleTerritorySelected);
    }
  }
  CachedWorldMap.Reset();

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(WorldMapSearchHandle);
  }

  if (bDiceDelegatesBound) {
    if (USkaldDiceManager *DiceManager = ResolveDiceManager()) {
      DiceManager->OnDiceRollCompleted.RemoveDynamic(
          this, &ASkaldPlayerController::HandlePhysicalDiceRollCompleted);
      DiceManager->OnDiceRollStarted.RemoveDynamic(
          this, &ASkaldPlayerController::HandleDiceRollStarted);
    }
    bDiceDelegatesBound = false;
  }

  ResetAttackDiceSequence();
  ResetManualDiceSequence();

  Super::EndPlay(EndPlayReason);
}

void ASkaldPlayerController::ShowMainHUD() {
  if (MainHUD) {
    MainHUD->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
  }

  UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(
      this, nullptr, EMouseLockMode::DoNotLock, /*bHideCursorDuringCapture*/
      false);
  FocusGameViewport(this);
  bShowMouseCursor = true;
  bEnableClickEvents = true;
  bEnableMouseOverEvents = true;
  DefaultMouseCaptureMode = EMouseCaptureMode::NoCapture;
}

void ASkaldPlayerController::HideMainHUD() {
  if (MainHUD) {
    MainHUD->SetVisibility(ESlateVisibility::Collapsed);
    UWidgetBlueprintLibrary::SetInputMode_GameOnly(this);
    bShowMouseCursor = false;
  }
}

void ASkaldPlayerController::ToggleInGameMenu() {
  if (!IsLocalController()) {
    return;
  }

  if (InGameMenuWidget &&
      InGameMenuWidget->GetVisibility() != ESlateVisibility::Hidden &&
      InGameMenuWidget->GetVisibility() != ESlateVisibility::Collapsed) {
    HideInGameMenu();
  } else {
    ShowInGameMenu();
  }
}

void ASkaldPlayerController::ShowInGameMenu() {
  if (!IsLocalController()) {
    return;
  }

  if (!InGameMenuWidget) {
    if (!InGameMenuWidgetClass) {
      InGameMenuWidgetClass = UInGameMenuWidget::StaticClass();
    }

    if (InGameMenuWidgetClass) {
      InGameMenuWidget = CreateWidget<UInGameMenuWidget>(this, InGameMenuWidgetClass);
      if (InGameMenuWidget) {
        InGameMenuWidget->SetVisibility(ESlateVisibility::Hidden);
        InGameMenuWidget->AddToViewport(90);
      }
    }
  }

  if (!InGameMenuWidget) {
    return;
  }

  if (!InGameMenuWidget->IsInViewport()) {
    InGameMenuWidget->AddToViewport(90);
  }

  InGameMenuWidget->SetVisibility(ESlateVisibility::Visible);
  UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(
      this, InGameMenuWidget, EMouseLockMode::DoNotLock, /*bHideCursorDuringCapture*/ false);
  bShowMouseCursor = true;
}

void ASkaldPlayerController::HideInGameMenu() {
  if (!IsLocalController()) {
    return;
  }

  if (!InGameMenuWidget) {
    return;
  }

  InGameMenuWidget->SetVisibility(ESlateVisibility::Hidden);
  UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(
      this, nullptr, EMouseLockMode::DoNotLock, /*bHideCursorDuringCapture*/ false);
  bShowMouseCursor = true;
}

void ASkaldPlayerController::TryBindWorldMap() {
  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  if (CachedWorldMap.IsValid()) {
    World->GetTimerManager().ClearTimer(WorldMapSearchHandle);
    return;
  }
  CachedWorldMap.Reset();

  if (AWorldMap *FoundWorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          World, AWorldMap::StaticClass()))) {
    CachedWorldMap = FoundWorldMap;
    if (!FoundWorldMap->OnTerritorySelected.IsAlreadyBound(
            this, &ASkaldPlayerController::HandleTerritorySelected)) {
      FoundWorldMap->OnTerritorySelected.AddDynamic(
          this, &ASkaldPlayerController::HandleTerritorySelected);
      ensureMsgf(FoundWorldMap->OnTerritorySelected.IsAlreadyBound(
                     this, &ASkaldPlayerController::HandleTerritorySelected),
                 TEXT("Failed to bind HandleTerritorySelected to WorldMap."));
    }
    World->GetTimerManager().ClearTimer(WorldMapSearchHandle);
  } else {
    World->GetTimerManager().SetTimer(WorldMapSearchHandle, this,
                                      &ASkaldPlayerController::TryBindWorldMap,
                                      0.5f, false);
  }
}

const FFactionCursorDefinition *ASkaldPlayerController::ResolveCursorDefinition() const {
  if (!FactionCursorData || CurrentFaction == ESkaldFaction::None) {
    return nullptr;
  }

  return FactionCursorData->FactionCursors.Find(CurrentFaction);
}

void ASkaldPlayerController::RefreshFactionCursorFromState() {
  if (!IsLocalController()) {
    return;
  }

  ESkaldFaction DesiredFaction = ESkaldFaction::None;
  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    DesiredFaction = PS->Faction;
  }

  if (DesiredFaction == ESkaldFaction::None && CachedGameInstance) {
    DesiredFaction = CachedGameInstance->Faction;
  }

  if (DesiredFaction == ESkaldFaction::None || !FactionCursorData ||
      !FactionCursorData->FactionCursors.Contains(DesiredFaction)) {
    ClearFactionCursor();
    return;
  }

  if (CurrentFaction != DesiredFaction) {
    CurrentFaction = DesiredFaction;
  }

  ApplyFactionCursor();
}

void ASkaldPlayerController::ApplyFactionCursor() {
  if (!IsLocalController()) {
    return;
  }

  const FFactionCursorDefinition *Definition = ResolveCursorDefinition();
  if (!Definition) {
    ClearFactionCursor();
    return;
  }

  if (FSlateApplication::IsInitialized()) {
    FSlateApplication &SlateApplication = FSlateApplication::Get();
    const FSkaldCursorPtr PlatformCursor = SlateApplication.GetPlatformCursor();
    if (PlatformCursor.IsValid()) {
      const FString CursorPath =
          Definition->CursorTexture.IsNull()
              ? FString()
              : Definition->CursorTexture.ToSoftObjectPath().ToString();

      if (!CursorPath.IsEmpty()) {
        SlateApplication.SetHardwareCursor(EMouseCursor::Default,
                                           FName(*CursorPath),
                                           Definition->CursorHotspot);
      } else {
        SlateApplication.SetHardwareCursor(EMouseCursor::Default, NAME_None,
                                           FVector2D::ZeroVector);
      }
    }
  }

  if (Definition->CursorTrailFX.IsNull()) {
    if (ActiveCursorTrailFX) {
      ActiveCursorTrailFX->DestroyComponent();
      ActiveCursorTrailFX = nullptr;
      ActiveCursorTrailTemplate.Reset();
    }
  } else {
    UNiagaraSystem *Trail = Definition->CursorTrailFX.LoadSynchronous();
    if (Trail) {
      const bool bNeedsNewTrail =
          !ActiveCursorTrailFX || ActiveCursorTrailTemplate.Get() != Trail;
      if (bNeedsNewTrail) {
        if (ActiveCursorTrailFX) {
          ActiveCursorTrailFX->DestroyComponent();
          ActiveCursorTrailFX = nullptr;
        }

        ActiveCursorTrailFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
            this, Trail, FVector::ZeroVector, FRotator::ZeroRotator,
            FVector::OneVector, /*bAutoDestroy*/ false,
            /*bAutoActivate*/ true);
        if (ActiveCursorTrailFX) {
          ActiveCursorTrailFX->SetUsingAbsoluteLocation(true);
          ActiveCursorTrailFX->SetUsingAbsoluteRotation(true);
          ActiveCursorTrailFX->SetUsingAbsoluteScale(true);
        }
        ActiveCursorTrailTemplate = Trail;
      }
    } else if (ActiveCursorTrailFX) {
      ActiveCursorTrailFX->DestroyComponent();
      ActiveCursorTrailFX = nullptr;
      ActiveCursorTrailTemplate.Reset();
    }
  }
}

void ASkaldPlayerController::ClearFactionCursor() {
  if (ActiveCursorTrailFX) {
    ActiveCursorTrailFX->DestroyComponent();
    ActiveCursorTrailFX = nullptr;
  }
  ActiveCursorTrailTemplate.Reset();
  if (IsLocalController() && FSlateApplication::IsInitialized()) {
    const FSkaldCursorPtr PlatformCursor =
        FSlateApplication::Get().GetPlatformCursor();
    if (PlatformCursor.IsValid()) {
      FSlateApplication::Get().SetHardwareCursor(EMouseCursor::Default, NAME_None,
                                                FVector2D::ZeroVector);
    }
  }

  CurrentFaction = ESkaldFaction::None;
}

void ASkaldPlayerController::PlayCursorHoverSound() {
  if (!IsLocalController()) {
    return;
  }

  const FFactionCursorDefinition *Definition = ResolveCursorDefinition();
  if (!Definition) {
    return;
  }

  if (USoundBase *Hover = Definition->HoverSound.LoadSynchronous()) {
    UGameplayStatics::PlaySound2D(this, Hover);
  }
}

void ASkaldPlayerController::PlayCursorClickSound() {
  if (!IsLocalController()) {
    return;
  }

  const FFactionCursorDefinition *Definition = ResolveCursorDefinition();
  if (!Definition) {
    return;
  }

  if (USoundBase *Click = Definition->ClickSound.LoadSynchronous()) {
    UGameplayStatics::PlaySound2D(this, Click);
  }
}

void ASkaldPlayerController::UpdateCursorFX() {
  if (!ActiveCursorTrailFX) {
    return;
  }

  FVector2D MousePos;
  if (!GetMousePosition(MousePos.X, MousePos.Y)) {
    return;
  }

  FVector WorldPos;
  FVector WorldDir;
  if (!DeprojectScreenPositionToWorld(MousePos.X, MousePos.Y, WorldPos,
                                      WorldDir)) {
    return;
  }

  ActiveCursorTrailFX->SetWorldLocation(WorldPos + WorldDir * 50.f);
}

void ASkaldPlayerController::OnRep_PlayerState() {
  Super::OnRep_PlayerState();

  if (!bHasInitialized && CachedGameInstance &&
      CachedGameInstance->bIsMultiplayer) {
    AutoInitializeFromLobbySelection();
  }

  RefreshFactionCursorFromState();

  if (!MainHUD) {
    return;
  }

  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    MainHUD->LocalPlayerID = PS->GetPlayerId();
    MainHUD->UpdateResources(PS->Resources);
    MainHUD->SyncPhaseButtons(MainHUD->CurrentPlayerID ==
                                    MainHUD->LocalPlayerID);
  }
}

void ASkaldPlayerController::OnPossess(APawn *InPawn) {
  Super::OnPossess(InPawn);

  if (!IsLocalController()) {
    return;
  }

  UpdateBattleCameraMode();
}

void ASkaldPlayerController::PlayerTick(float DeltaTime) {
  Super::PlayerTick(DeltaTime);

  if (!IsLocalController()) {
    return;
  }

  UpdateCursorFX();

  const bool bIsHoveringInteractable = IsCursorOverInteractableSlateWidget();
  if (bIsHoveringInteractable && !bWasHoveringInteractable) {
    PlayCursorHoverSound();
  }
  bWasHoveringInteractable = bIsHoveringInteractable;
}

void ASkaldPlayerController::ServerInitPlayerState_Implementation(
    const FString &Name, ESkaldFaction Faction, int32 NumAIPlayers) {
  UE_LOG(LogSkald, Log,
         TEXT("ServerInitPlayerState_Implementation: Name=%s Faction=%d AI=%d"),
         *Name, static_cast<int32>(Faction), NumAIPlayers);

  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    FString EffectiveName = Name;
    if (EffectiveName.IsEmpty()) {
      EffectiveName = PS->PlayerDisplayName.IsEmpty() ? PS->GetPlayerName()
                                                     : PS->PlayerDisplayName;
    }

    ESkaldFaction EffectiveFaction =
        Faction != ESkaldFaction::None ? Faction : PS->Faction;

    UE_LOG(LogSkald, Log,
           TEXT("ServerInitPlayerState_Implementation: PlayerState=%s"),
           *PS->GetName());
    if (!EffectiveName.IsEmpty()) {
      PS->PlayerDisplayName = EffectiveName;
      PS->SetPlayerName(EffectiveName);
    }

    if (EffectiveFaction != ESkaldFaction::None) {
      PS->Faction = EffectiveFaction;
    }

    if (USkaldGameInstance *GI = Cast<USkaldGameInstance>(GetGameInstance())) {
      if (!GI->bIsMultiplayer || IsLocalController()) {
        GI->AIPlayersToSpawn = NumAIPlayers;
      }
      if (!EffectiveName.IsEmpty()) {
        GI->DisplayName = EffectiveName;
      }
      if (EffectiveFaction != ESkaldFaction::None) {
        GI->Faction = EffectiveFaction;
        GI->TakenFactions.AddUnique(EffectiveFaction);
      }
    }

    if (ASkaldGameMode *GM = GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
      if (!GM->IsWorldInitialized()) {
        UE_LOG(LogSkald, Log,
               TEXT("ServerInitPlayerState_Implementation: Notify GameMode %s"),
               *GM->GetName());
        GM->HandlePlayerLockedIn(PS);
      } else {
        UE_LOG(LogSkald, Log,
               TEXT("ServerInitPlayerState_Implementation: World already "
                    "initialized"));
      }
    } else {
      UE_LOG(LogSkald, Warning,
             TEXT("ServerInitPlayerState_Implementation: GameMode is null"));
    }
  } else {
    UE_LOG(LogSkald, Warning,
           TEXT("ServerInitPlayerState_Implementation: PlayerState is null"));
  }
}

void ASkaldPlayerController::SetTurnManager(ATurnManager *Manager) {
  ApplyTurnManager(Manager);
}

void ASkaldPlayerController::OnRep_TurnManager() {
  ApplyTurnManager(TurnManager);
}

void ASkaldPlayerController::ApplyTurnManager(ATurnManager *Manager) {
  if (TurnManager) {
    TurnManager->OnWorldStateChanged.RemoveDynamic(
        this, &ASkaldPlayerController::HandleWorldStateChanged);
  }

  TurnManager = Manager;

  if (TurnManager) {
    TurnManager->OnWorldStateChanged.AddDynamic(
        this, &ASkaldPlayerController::HandleWorldStateChanged);
    if (IsLocalPlayerController() && GetLocalPlayer() != nullptr) {
      InitializeFighterSelectionIfNeeded();
    }

    // When the turn manager becomes available (including after loading a
    // saved game) immediately synchronise the HUD so players can interact with
    // the correct phase buttons without waiting for the next broadcast.
    HandleWorldStateChanged();
  }
}

void ASkaldPlayerController::InitializeFighterSelectionIfNeeded() {
  if (!IsLocalController()) {
    return;
  }

  if (!CachedGameInstance) {
    CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  }

  const bool bOnBattleMap =
      bIsBattleMap || (CachedGameInstance && CachedGameInstance->bIsInBattleMap);
  if (!bOnBattleMap) {
    if (FighterSelectionWidget) {
      FighterSelectionWidget->RemoveFromParent();
      FighterSelectionWidget = nullptr;
    }
    return;
  }

  // If we've already locked in and are waiting for the battle HUD to take over,
  // keep the fighter selection screen hidden so we don't immediately flip the
  // input mode back to UI-only.
  if (bBattleHUDReadyToShow) {
    if (FighterSelectionWidget) {
      FighterSelectionWidget->RemoveFromParent();
      FighterSelectionWidget = nullptr;
    }
    return;
  }

  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS || PS->bArmyLockedIn) {
    return;
  }

  if (!CachedGameInstance) {
    return;
  }

  const FS_BattlePayload &Battle = CachedGameInstance->PendingBattle;
  const int32 PlayerID = PS->GetPlayerId();
  const bool bIsParticipant =
      Battle.AttackerPlayerID == PlayerID || Battle.DefenderPlayerID == PlayerID;

  if (!bIsParticipant || PS->PendingArmyBudget <= 0) {
    return;
  }

  if (!FighterSelectionWidget || !FighterSelectionWidget->IsInViewport()) {
    ShowFighterSelectionUI(PS->PendingArmyBudget, PS->Faction);
  }
}

void ASkaldPlayerController::ShowFighterSelectionUI(int32 MaxBudget,
                                                    ESkaldFaction Faction) {
  if (!IsLocalController()) {
    return;
  }

  HideOverworldHUDForBattle();
  bBattleHUDReadyToShow = false;

  if (!FighterSelectionWidgetClass) {
    FighterSelectionWidgetClass = UFighterSelectionWidget::StaticClass();
  }

  if (!FighterSelectionWidget ||
      FighterSelectionWidget->GetClass() != FighterSelectionWidgetClass) {
    if (FighterSelectionWidget) {
      FighterSelectionWidget->RemoveFromParent();
    }
    FighterSelectionWidget =
        CreateWidget<UFighterSelectionWidget>(this, FighterSelectionWidgetClass);
  }

  if (!FighterSelectionWidget) {
    return;
  }

  FighterSelectionWidget->PlayerFaction = Faction;
  FighterSelectionWidget->MaxCost = MaxBudget;
  FighterSelectionWidget->ChosenFighters.Reset();
  FighterSelectionWidget->CurrentCost = 0;
  FighterSelectionWidget->SetLockInButtonEnabled(true);
  const TArray<FFighterDefinition> Available =
      UFighterDataLibrary::GetFightersForFaction(this, Faction);
  FighterSelectionWidget->SetAvailableFighters(Available);
  UE_LOG(LogSkald, Log,
         TEXT("FighterSelectionWidget: Populated %d entries for faction %d"),
         Available.Num(), static_cast<int32>(Faction));
  if (Available.Num() == 0) {
    UE_LOG(LogSkald, Warning,
           TEXT("SkaldUI: [FighterSelection] No fighters available for faction %d"),
           static_cast<int32>(Faction));
  }
  FighterSelectionWidget->UpdateCostDisplay();

  FighterSelectionWidget->AddToViewport(30);
  FocusWidgetUIOnly(this, FighterSelectionWidget);
  bShowMouseCursor = true;
  bEnableClickEvents = true;
  bEnableMouseOverEvents = true;
}

void ASkaldPlayerController::Client_ShowFighterSelection_Implementation(
    int32 MaxBudget, ESkaldFaction Faction) {
  ShowFighterSelectionUI(MaxBudget, Faction);
}

void ASkaldPlayerController::Server_LockInSelection_Implementation(
    const TArray<FFighterDefinition> &SelectedFighters)
{
  UE_LOG(LogSkaldBattle, Log,
         TEXT("Server_LockInSelection: %s sent %d fighters"), *GetName(),
         SelectedFighters.Num());

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    const FS_BattlePayload &Battle = GI->PendingBattle;
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("Server_LockInSelection: PendingBattle AttackerId=%d DefenderId=%d"),
           Battle.AttackerPlayerID, Battle.DefenderPlayerID);
  } else {
    UE_LOG(LogSkaldBattle, Warning,
           TEXT("Server_LockInSelection: GameInstance missing on %s"), *GetName());
  }

  if (ASkald_BattleGameMode *GameMode = ResolveBattleGameMode()) {
    if (UClass *GameModeClass = GameMode->GetClass()) {
      const FString ClassPath = GameModeClass->GetPathName();
      UE_LOG(LogSkaldBattle, Verbose,
             TEXT("Server_LockInSelection: BattleGameMode class=%s (%s)"),
             *GameModeClass->GetName(), *ClassPath);

      if (GameModeClass == ASkald_BattleGameMode::StaticClass()) {
        UE_LOG(LogSkaldBattle, Warning,
               TEXT("Server_LockInSelection: Using native Skald_BattleGameMode. "
                    "Battle sublevels should spawn Skald_BattleGameMode_SC."));
      }
    }

    GameMode->HandleHumanLockIn(this, SelectedFighters);
  } else {
    UE_LOG(LogSkaldBattle, Warning,
           TEXT("Server_LockInSelection: BattleGameMode not resolved for %s"),
           *GetName());
  }
}

void ASkaldPlayerController::Client_OnLockInResult_Implementation(
    bool bSuccess, const FString &Reason)
{
  UE_LOG(LogSkaldUI, Log, TEXT("LockIn result: %s (%s)"),
         bSuccess ? TEXT("SUCCESS") : TEXT("FAIL"), *Reason);

  if (!bSuccess)
  {
    if (!Reason.IsEmpty())
    {
      UE_LOG(LogSkaldUI, Warning, TEXT("LockIn failed: %s"), *Reason);
    }

    if (IsLocalController() && GEngine && !Reason.IsEmpty())
    {
      GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, Reason);
    }

    if (FighterSelectionWidget)
    {
      FighterSelectionWidget->SetLockInButtonEnabled(true);
    }
    return;
  }

  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>())
  {
    UE_LOG(LogSkaldUI, Log,
           TEXT("LockIn success for PlayerId=%d PendingArmy=%d Budget=%d"),
           PS->GetPlayerId(), PS->PendingArmy.Num(), PS->PendingArmyBudget);
  }
  else
  {
    UE_LOG(LogSkaldUI, Warning,
           TEXT("LockIn success but PlayerState missing for %s"), *GetName());
  }

  HandleFighterSelectionLockedIn();
}

void ASkaldPlayerController::HandleBattlePhaseChanged() {
  if (const ASkaldGameState *SGS =
          GetWorld() ? GetWorld()->GetGameState<ASkaldGameState>() : nullptr) {
    if (SGS->BattlePhase == EBattlePhase::Deploy) {
      UE_LOG(LogSkaldBattle, Log,
             TEXT("PlayerController %s entering Deploy phase; fighters will be"
                  " spawned automatically."),
             *GetName());
    }
  }
}

bool ASkaldPlayerController::Server_CommitArmy_Validate(
    const TArray<FFighterDefinition> &Chosen) {
  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS) {
    return false;
  }

  int32 TotalCost = 0;
  for (const FFighterDefinition &Def : Chosen) {
    if (Def.Faction != PS->Faction) {
      return false;
    }
    TotalCost += FMath::Max(Def.Stats.ArmyCost, 0);
    if (TotalCost > PS->PendingArmyBudget) {
      return false;
    }
  }

  return true;
}

void ASkaldPlayerController::Server_CommitArmy_Implementation(
    const TArray<FFighterDefinition> &Chosen) {
  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI) {
    return;
  }
  const FS_BattlePayload Battle = GI->PendingBattle;

  if (PS) {
    const int32 PlayerID = PS->GetPlayerId();
    if (PlayerID != Battle.AttackerPlayerID &&
        PlayerID != Battle.DefenderPlayerID) {
      return;
    }

    TArray<FFighterDefinition> ValidFighters;
    int32 TotalCost = 0;
    for (const FFighterDefinition &Def : Chosen) {
      if (Def.Faction != PS->Faction) {
        continue;
      }
      const int32 Cost = FMath::Max(Def.Stats.ArmyCost, 0);
      if (TotalCost + Cost > PS->PendingArmyBudget) {
        break;
      }
      ValidFighters.Add(Def);
      TotalCost += Cost;
    }

    PS->PendingArmy = ValidFighters;
    PS->bArmyLockedIn = true;
  }

  if (ASkald_BattleGameMode *BattleGM = ResolveBattleGameMode()) {
    BattleGM->TryLaunchBattle();
  }
}

void ASkaldPlayerController::InitializeBattleHUD() {
  if (!IsLocalController())
    return;
  if (!BattleHUDWidgetClass)
    return;
  if (!BattleHudWidget) {
    BattleHudWidget =
        CreateWidget<UBattleHUDWidget>(this, BattleHUDWidgetClass);
    if (BattleHudWidget) {
      BattleHudWidget->AddToViewport(20);
      BattleHudWidget->SetVisibility(ESlateVisibility::Collapsed);

      // Hook HUD buttons to controller modes
      BattleHudWidget->OnMovePressed.AddDynamic(
          this, &ASkaldPlayerController::BeginMoveMode);
      BattleHudWidget->OnDisengagePressed.AddDynamic(
          this, &ASkaldPlayerController::BeginDisengageMode);
      BattleHudWidget->OnAttackPressed.AddDynamic(
          this, &ASkaldPlayerController::BeginAttackMode);
      BattleHudWidget->OnActivatePressed.AddDynamic(
          this, &ASkaldPlayerController::HandleActivatePressed);
      BattleHudWidget->OnEndTurnPressed.AddDynamic(
          this, &ASkaldPlayerController::HandleEndTurnPressed);
      BattleHudWidget->OnInitiativeRollRequested.AddDynamic(
          this, &ASkaldPlayerController::HandleInitiativeRollRequested);
      BattleHudWidget->OnAttackRollRequested.AddDynamic(
          this, &ASkaldPlayerController::HandleAttackRollRequested);
      BattleHudWidget->OnResolutionComplete.AddDynamic(
          this, &ASkaldPlayerController::HandleDiceResolutionComplete);
      BattleHudWidget->OnDiceOutcomeRevealed.AddDynamic(
          this, &ASkaldPlayerController::HandleDiceOutcomeRevealed);
      BattleHudWidget->OnAbilitySlotPressed.AddDynamic(
          this, &ASkaldPlayerController::HandleAbilityInput);
      OnSelectedFighterChanged.RemoveDynamic(
          BattleHudWidget, &UBattleHUDWidget::HandleSelectedFighterChanged);
      OnSelectedFighterChanged.AddDynamic(
          BattleHudWidget, &UBattleHUDWidget::HandleSelectedFighterChanged);
      BattleHudWidget->SetEndTurnVisibility(false);
      BattleHudWidget->SetActivateEnabled(false);
      BattleHudWidget->SetEndTurnEnabled(false);
      BattleHUD = BattleHudWidget;
    }
  }

  EnsureDiceWidgets();

  USkaldGameInstance *GI = CachedGameInstance;
  if (!GI) {
    GI = GetGameInstance<USkaldGameInstance>();
    CachedGameInstance = GI;
  }

  AFighterPawn *ActiveFighter = nullptr;
  bool bPendingInitiativePrompt = false;
  int32 PendingInitiativeRound = 0;
  // Bind to active-fighter changes
  if (GI && GI->GridBattleManager) {
    CachedBattleManager = GI->GridBattleManager;
    GI->GridBattleManager->OnActiveFighterChanged.RemoveAll(this);
    GI->GridBattleManager->OnActiveFighterChanged.AddDynamic(
        this, &ASkaldPlayerController::HandleActiveFighterChanged);
    GI->GridBattleManager->OnRoundStarted.RemoveAll(this);
    GI->GridBattleManager->OnRoundStarted.AddDynamic(
        this, &ASkaldPlayerController::HandleRoundStarted);
    GI->GridBattleManager->OnInitiativePhaseStarted.RemoveAll(this);
    GI->GridBattleManager->OnInitiativePhaseStarted.AddDynamic(
        this, &ASkaldPlayerController::HandleInitiativePhaseStarted);
    GI->GridBattleManager->OnInitiativeRollCompleted.RemoveAll(this);
    GI->GridBattleManager->OnInitiativeRollCompleted.AddDynamic(
        this, &ASkaldPlayerController::HandleInitiativeRollCompleted);
    GI->GridBattleManager->OnBattleEnded.RemoveDynamic(
        this, &ASkaldPlayerController::HandleBattleEnded);
    GI->GridBattleManager->OnBattleEnded.AddDynamic(
        this, &ASkaldPlayerController::HandleBattleEnded);
    GI->GridBattleManager->OnAttackResolved.RemoveAll(this);
    GI->GridBattleManager->OnAttackResolved.AddDynamic(
        this, &ASkaldPlayerController::HandleAttackResolved);
    GI->GridBattleManager->OnAttackRejected.RemoveAll(this);
    GI->GridBattleManager->OnAttackRejected.AddDynamic(
        this, &ASkaldPlayerController::HandleAttackRejected);
    ActiveFighter = GI->GridBattleManager->GetActiveFighter();

    const int32 CurrentRound = GI->GridBattleManager->GetCurrentRound();
    if (CurrentRound > 0) {
      UpdateBattleRoundDisplay(CurrentRound,
                               GI->GridBattleManager->GetInitiativeWinner());
    }

    bPendingInitiativePrompt = GI->GridBattleManager->IsAwaitingInitiativeRoll();
    PendingInitiativeRound = CurrentRound;
  } else {
    CachedBattleManager.Reset();
  }

  if (BattleHudWidget) {
    BattleHudWidget->HandleSelectedFighterChanged(SelectedFighter.Get());
  }

  DetermineControlledBattleSide();
  RefreshLockedInFighterList();
  UpdateBattleTerritoryLabel();
  HandleActiveFighterChanged(ActiveFighter);

  if (bPendingInitiativePrompt) {
    const int32 PromptRound = PendingInitiativeRound > 0 ? PendingInitiativeRound : 1;
    HandleInitiativePhaseStarted(PromptRound);
  }
}

void ASkaldPlayerController::ShowOverworldHUD() {
  ShowMainHUD();

  if (BattleHudWidget) {
    BattleHudWidget->RemoveFromParent();
    BattleHudWidget = nullptr;
  }

  if (FighterSelectionWidget) {
    FighterSelectionWidget->RemoveFromParent();
    FighterSelectionWidget = nullptr;
  }

  bBattleHUDVisible = false;
  bBattleHUDReadyToShow = false;

  if (UWorld *World = GetWorld()) {
    if (AWorldMap *WorldMap = Cast<AWorldMap>(
            UGameplayStatics::GetActorOfClass(World, AWorldMap::StaticClass()))) {
      WorldMap->SetWorldActive(true);
    }
  }
}

void ASkaldPlayerController::HideOverworldHUDForBattle() {
  HideMainHUD();

  bBattleHUDVisible = false;
  bBattleHUDReadyToShow = false;
  if (BattleHudWidget) {
    BattleHudWidget->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (UWorld *World = GetWorld()) {
    if (AWorldMap *WorldMap = Cast<AWorldMap>(
            UGameplayStatics::GetActorOfClass(World, AWorldMap::StaticClass()))) {
      WorldMap->SetWorldActive(false);
    }
  }
}

void ASkaldPlayerController::EnsureBattleHUDVisible() {
  if (!IsLocalController()) {
    return;
  }

  bBattleHUDReadyToShow = false;

  InitializeBattleHUD();
  if (!BattleHudWidget) {
    return;
  }

  BattleHudWidget->SetVisibility(ESlateVisibility::Visible);
  bBattleHUDVisible = true;

  // Don't focus the HUD itself or it will consume keyboard input needed for
  // camera controls; leave the viewport focused instead.
  UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(
      this, BattleHudWidget, EMouseLockMode::DoNotLock, false);
  FocusGameViewport(this);
  bShowMouseCursor = true;
  bEnableClickEvents = true;
  bEnableMouseOverEvents = true;
  DefaultMouseCaptureMode = EMouseCaptureMode::NoCapture;
}

UGridOverlayComponent *ASkaldPlayerController::FindGridOverlay() const {
  if (UWorld *World = GetWorld()) {
    return Skald::GridOverlay::FindActiveGridOverlay(World);
  }

  return nullptr;
}

AFighterPawn *ASkaldPlayerController::FindFighterAtCell(
    const FIntPoint &Cell) const {
  if (UWorld *World = GetWorld()) {
    for (TActorIterator<AFighterPawn> It(World); It; ++It) {
      AFighterPawn *Fighter = *It;
      if (Fighter && Fighter->OccupiesCell(Cell)) {
        return Fighter;
      }
    }
  }
  return nullptr;
}

UGridBattleManager *ASkaldPlayerController::GetBattleManager() const {
  if (CachedBattleManager.IsValid()) {
    return CachedBattleManager.Get();
  }

  USkaldGameInstance *GameInstance = CachedGameInstance;
  if (!GameInstance) {
    GameInstance = GetGameInstance<USkaldGameInstance>();
  }

  if (GameInstance && GameInstance->GridBattleManager) {
    CachedBattleManager = GameInstance->GridBattleManager;
    return CachedBattleManager.Get();
  }

  return nullptr;
}

void ASkaldPlayerController::HandleActiveFighterChanged(
    AFighterPawn *NewFighter) {
  if (NewFighter && NewFighter->IsAlive()) {
    LockedActiveFighter = NewFighter;
    SetSelectedFighter(NewFighter, true);
    if (bBattleHUDReadyToShow && !bBattleHUDVisible) {
      EnsureBattleHUDVisible();
    }
  } else {
    LockedActiveFighter = nullptr;
  }

  CancelCommandMode();
  UpdateBattleHUDButtons();
  UpdateBattlePlayersTurnDisplay();
  if (!NewFighter) {
    UpdateBattleHUDSelection();
  }

  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    if (NewFighter && NewFighter->IsAlive()) {
      Grid->HighlightSelection(NewFighter);
    } else {
      Grid->ClearSelectionHighlight();
    }
  }

  if (IsLocalController()) {
    if (ASkald_PlayerCharacter *CameraPawn = Cast<ASkald_PlayerCharacter>(GetPawn())) {
      if (bIsBattleMap && NewFighter && NewFighter->IsAlive()) {
        CameraPawn->FocusCameraOnActor(NewFighter);
      } else {
        CameraPawn->ClearCameraFocus();
      }
    }
  }

  if (!NewFighter && IsLocalController() && BattleTurnStartSound) {
    USkaldGameInstance *GI = CachedGameInstance;
    if (!GI) {
      GI = GetGameInstance<USkaldGameInstance>();
      CachedGameInstance = GI;
    }

    UGridBattleManager *BattleManager = GI ? GI->GridBattleManager : nullptr;
    if (BattleManager && !BattleManager->IsAwaitingInitiativeRoll()) {
      const bool bAttackerTurn = BattleManager->IsAttackerTurn();
      const bool bFriendlyTurn =
          (bAttackerTurn && bControlsAttackerSide) ||
          (!bAttackerTurn && bControlsDefenderSide);

      if (bFriendlyTurn) {
        int32 AvailableFriendlyFighters = 0;

        if (UWorld *World = GetWorld()) {
          for (TActorIterator<AFighterPawn> It(World); It; ++It) {
            AFighterPawn *Candidate = *It;
            if (!Candidate || !Candidate->IsAlive()) {
              continue;
            }

            if (!IsFriendlyFighter(Candidate)) {
              continue;
            }

            if (Candidate->bIsAttacker != bAttackerTurn) {
              continue;
            }

            if (Candidate->HasActivatedThisRound()) {
              continue;
            }

            ++AvailableFriendlyFighters;
          }
        }

        if (AvailableFriendlyFighters > 0) {
          const int32 CurrentRound =
              FMath::Max(BattleManager->GetCurrentRound(), 1);

          if (LastBattleTurnSoundRound != CurrentRound ||
              bLastBattleTurnSoundWasAttacker != bAttackerTurn ||
              LastBattleTurnSoundAvailableCount != AvailableFriendlyFighters) {
            UGameplayStatics::PlaySound2D(this, BattleTurnStartSound);
            LastBattleTurnSoundRound = CurrentRound;
            bLastBattleTurnSoundWasAttacker = bAttackerTurn;
            LastBattleTurnSoundAvailableCount = AvailableFriendlyFighters;
          }
        }
      }
    }
  }

  UpdateLockedInActiveHighlight();
  RefreshLockedInFighterTurnStates();
}

void ASkaldPlayerController::HandleActorSpawned(AActor *SpawnedActor) {
  if (!SpawnedActor || !BattleHudWidget) {
    return;
  }

  if (AFighterPawn *Fighter = Cast<AFighterPawn>(SpawnedActor)) {
    if (!Fighter->IsAlive()) {
      return;
    }

    if (IsFriendlyFighter(Fighter)) {
      RegisterObservedFighter(Fighter);
      RefreshLockedInFighterList();
    }
  }
}

void ASkaldPlayerController::RegisterObservedFighter(AFighterPawn *Fighter) {
  if (!Fighter || !IsFriendlyFighter(Fighter)) {
    return;
  }

  if (ObservedFriendlyFighters.Contains(Fighter)) {
    return;
  }

  Fighter->OnDestroyed.AddDynamic(
      this, &ASkaldPlayerController::HandleTrackedFighterDestroyed);
  ObservedFriendlyFighters.Add(Fighter);
}

void ASkaldPlayerController::HandleTrackedFighterDestroyed(
    AActor *DestroyedActor) {
  AFighterPawn *DestroyedFighter = Cast<AFighterPawn>(DestroyedActor);
  if (!DestroyedFighter) {
    return;
  }

  if (ObservedFriendlyFighters.Contains(DestroyedFighter)) {
    DestroyedFighter->OnDestroyed.RemoveDynamic(
        this, &ASkaldPlayerController::HandleTrackedFighterDestroyed);
    ObservedFriendlyFighters.Remove(DestroyedFighter);
  }

  RefreshLockedInFighterList();
}

void ASkaldPlayerController::RefreshLockedInFighterList() {
  if (!BattleHudWidget) {
    return;
  }

  USkaldGameInstance *GI = CachedGameInstance;
  if (!GI) {
    GI = GetGameInstance<USkaldGameInstance>();
    CachedGameInstance = GI;
  }

  TArray<AFighterPawn *> OrderedFighters;
  if (GI && GI->GridBattleManager) {
    OrderedFighters = GI->GridBattleManager->GetInitiativeOrderSnapshot();
  }

  TArray<AFighterPawn *> FriendlyFighters;
  TArray<AFighterPawn *> EnemyFighters;
  FriendlyFighters.Reserve(OrderedFighters.Num());
  EnemyFighters.Reserve(OrderedFighters.Num());

  auto ConsiderFighter = [&](AFighterPawn *Fighter) {
    if (!Fighter || !Fighter->IsAlive()) {
      return;
    }

    if (IsFriendlyFighter(Fighter)) {
      FriendlyFighters.AddUnique(Fighter);
    } else {
      EnemyFighters.AddUnique(Fighter);
    }
  };

  for (AFighterPawn *Fighter : OrderedFighters) {
    ConsiderFighter(Fighter);
  }

  if (UWorld *World = GetWorld()) {
    for (TActorIterator<AFighterPawn> It(World); It; ++It) {
      ConsiderFighter(*It);
    }
  }

  for (AFighterPawn *Fighter : FriendlyFighters) {
    RegisterObservedFighter(Fighter);
  }

  for (auto It = ObservedFriendlyFighters.CreateIterator(); It; ++It) {
    AFighterPawn *Tracked = It->Get();
    const bool bValid = Tracked && Tracked->IsAlive() && IsFriendlyFighter(Tracked);
    if (!bValid) {
      if (Tracked) {
        Tracked->OnDestroyed.RemoveDynamic(
            this, &ASkaldPlayerController::HandleTrackedFighterDestroyed);
      }
      It.RemoveCurrent();
    }
  }

  BattleHudWidget->SetLockedInFighters(FriendlyFighters);
  BattleHudWidget->SetEnemyLockedInFighters(EnemyFighters);
  UpdateLockedInActiveHighlight();
  UpdateLockedInSelectionHighlight();
  BattleHudWidget->RefreshLockedInFighterTurnStates();
}

void ASkaldPlayerController::RefreshLockedInFighterTurnStates() {
  if (BattleHudWidget) {
    BattleHudWidget->RefreshLockedInFighterTurnStates();
  }
}

void ASkaldPlayerController::UpdateLockedInSelectionHighlight() {
  if (!BattleHudWidget) {
    return;
  }

  AFighterPawn *Selected = SelectedFighter.Get();
  if (Selected && IsFriendlyFighter(Selected)) {
    BattleHudWidget->SetHighlightedLockedInFighter(Selected);
    BattleHudWidget->SetHighlightedEnemyLockedInFighter(nullptr);
  } else {
    BattleHudWidget->SetHighlightedLockedInFighter(nullptr);
    BattleHudWidget->SetHighlightedEnemyLockedInFighter(Selected);
  }
}

void ASkaldPlayerController::UpdateLockedInActiveHighlight() {
  if (!BattleHudWidget) {
    return;
  }

  AFighterPawn *Active = LockedActiveFighter.Get();
  if (Active && IsFriendlyFighter(Active)) {
    BattleHudWidget->SetActiveLockedInFighter(Active);
    BattleHudWidget->SetActiveEnemyLockedInFighter(nullptr);
  } else {
    BattleHudWidget->SetActiveLockedInFighter(nullptr);
    BattleHudWidget->SetActiveEnemyLockedInFighter(Active);
  }
}

void ASkaldPlayerController::RequestLockedInEntrySelection(AFighterPawn *Fighter) {
  HandleLockedInEntrySelected(Fighter);
}

void ASkaldPlayerController::HandleLockedInEntrySelected(AFighterPawn *Fighter) {
  if (!Fighter || !IsFriendlyFighter(Fighter)) {
    return;
  }

  SetSelectedFighter(Fighter, true);
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    const FIntPoint FighterCell = Fighter->GetCurrentCell();
    if (Grid->IsCellInBounds(FighterCell)) {
      HighlightClickedCell(Grid, FighterCell);
    }
    Grid->HighlightSelection(Fighter);
  }
  UpdateLockedInSelectionHighlight();

  if (IsLocalController() && bIsBattleMap) {
    if (Fighter->IsAlive()) {
      if (ASkald_PlayerCharacter *CameraPawn = Cast<ASkald_PlayerCharacter>(GetPawn())) {
        CameraPawn->FocusCameraOnActor(Fighter);
      }
    }
  }
}

void ASkaldPlayerController::DetectBattleMap() {
  const bool bWasBattleMap = bIsBattleMap;

  bool bDetectedBattleMap = false;

  if (!CachedGameInstance) {
    CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  }
  if (CachedGameInstance && CachedGameInstance->bIsInBattleMap) {
    bDetectedBattleMap = true;
  }

  FString CurrentMap;
  if (!bDetectedBattleMap) {
    CurrentMap = UGameplayStatics::GetCurrentLevelName(this, true);
    if (CurrentMap.Equals(TEXT("BattleMap"), ESearchCase::IgnoreCase)) {
      bDetectedBattleMap = true;
    }
  }

  if (!bDetectedBattleMap) {
    ATurnManager *TM = TurnManager;
    if (!TM) {
      if (!CachedGameMode) {
        CachedGameMode = GetWorld()->GetAuthGameMode<ASkaldGameMode>();
      }
      if (CachedGameMode) {
        TM = CachedGameMode->GetTurnManager();
      }
    }

    if (TM) {
      if (CurrentMap.IsEmpty()) {
        CurrentMap = UGameplayStatics::GetCurrentLevelName(this, true);
      }

      auto MatchesCurrentMap = [&](const TSoftObjectPtr<UWorld> &MapPtr) {
        return CurrentMap.Equals(MapPtr.ToSoftObjectPath().GetAssetName(),
                                 ESearchCase::IgnoreCase);
      };

      for (const TSoftObjectPtr<UWorld> &Map : TM->BattleMaps) {
        if (MatchesCurrentMap(Map)) {
          bDetectedBattleMap = true;
          break;
        }
      }

      if (!bDetectedBattleMap) {
        for (const FBattleMapDescriptor &Entry : TM->BattleMapEntries) {
          if (MatchesCurrentMap(Entry.Map)) {
            bDetectedBattleMap = true;
            break;
          }
        }
      }
    }
  }

  bIsBattleMap = bDetectedBattleMap;

  if (bIsBattleMap != bWasBattleMap) {
    UpdateBattleCameraMode();
  }

  if (bIsBattleMap && !bWasBattleMap) {
    LastStrategicInitiativeSoundRound = INDEX_NONE;
    LastBattleInitiativeSoundRound = INDEX_NONE;
    LastBattleTurnSoundRound = INDEX_NONE;
    bLastBattleTurnSoundWasAttacker = false;
    LastBattleTurnSoundAvailableCount = INDEX_NONE;
  }

  if (bIsBattleMap) {
    HideOverworldHUDForBattle();
    if (CachedGameInstance && CachedGameInstance->GridBattleManager &&
        bBattleHUDReadyToShow) {
      EnsureBattleHUDVisible();
    }
  } else {
    ShowOverworldHUD();
  }
}

void ASkaldPlayerController::HandleWorldBeginPlay(UWorld *LoadedWorld) {
  if (!LoadedWorld || LoadedWorld != GetWorld() || !IsLocalPlayerController()) {
    return;
  }

  DetectBattleMap();

  if (!bIsBattleMap) {
    InitializeHUDWidget();
    ShowOverworldHUD();
    TryBindWorldMap();
  }

  InitializeFighterSelectionIfNeeded();
}

void ASkaldPlayerController::ShowTurnAnnouncement(const FString &PlayerName,
                                                  bool bIsMyTurn) {
  if (MainHUD) {
    MainHUD->ShowTurnAnnouncement(PlayerName);
    MainHUD->ShowTurnMessage(bIsMyTurn);
  } else if (GEngine) {
    const FString Message = FString::Printf(TEXT("%s's Turn"), *PlayerName);
    GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow, Message);
  }
}

void ASkaldPlayerController::NotifyTurnEnded(const FString &PlayerName) {
  if (MainHUD) {
    MainHUD->ShowTurnEnded(PlayerName);
  }
}

bool ASkaldPlayerController::IsMyTurn() const {
  const UWorld *World = GetWorld();
  if (!World) {
    return false;
  }

  const ASkaldGameState *GameState = World->GetGameState<ASkaldGameState>();
  const ASkaldPlayerState *MyPlayerState = GetPlayerState<ASkaldPlayerState>();
  if (!GameState || !MyPlayerState) {
    return false;
  }

  if (ASkaldPlayerState *Current = GameState->GetCurrentPlayer()) {
    return Current == MyPlayerState;
  }

  return false;
}

void ASkaldPlayerController::StartTurn() {
  UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(
      this, nullptr, EMouseLockMode::DoNotLock, false);
  bShowMouseCursor = true;
  bEnableClickEvents = true;
  bEnableMouseOverEvents = true;
  DefaultMouseCaptureMode = EMouseCaptureMode::NoCapture;

  const bool bOnWorldMap =
      !bIsBattleMap &&
      !(CachedGameInstance && CachedGameInstance->bIsInBattleMap);

  if (bOnWorldMap && IsLocalController() && WorldTurnStartSound) {
    UGameplayStatics::PlaySound2D(this, WorldTurnStartSound);
  }

  // Drive GameState turn index so HUDs can react on all clients.
  if (ASkaldGameState *GS = GetWorld()->GetGameState<ASkaldGameState>()) {
    if (ASkaldPlayerState *MyPS = GetPlayerState<ASkaldPlayerState>()) {
      const int32 NewIndex = GS->PlayerArray.IndexOfByKey(MyPS);
      if (NewIndex != INDEX_NONE) {
        GS->CurrentTurnIndex =
            NewIndex; // RepNotify will fire OnTurnIndexChanged
        // If you haven't applied RepNotifies yet, you can optionally
        // direct-broadcast:
        GS->OnTurnIndexChanged.Broadcast(NewIndex);
      }
    }
  }
}

void ASkaldPlayerController::EndTurn() {
  UWidgetBlueprintLibrary::SetInputMode_GameOnly(this);
  bShowMouseCursor = false;
  if (!EnsureTurnManager(TEXT("EndTurn"))) {
    return;
  }

  TurnManager->AdvanceTurn();
}

void ASkaldPlayerController::EndPhase() {
  if (!HasAuthority()) {
    if (TurnManager && TurnManager->HasPendingBattlePreparation()) {
      NotifyActionError_Implementation(PendingBattlePhaseError);
      return;
    }

    ServerEndPhase();
    return;
  }

  HandleEndPhaseInternal();
}

void ASkaldPlayerController::ServerEndPhase_Implementation() {
  HandleEndPhaseInternal();
}

void ASkaldPlayerController::HandleEndPhaseInternal() {
  if (!EnsureTurnManager(TEXT("EndPhase"))) {
    return;
  }

  if (TurnManager->HasPendingBattlePreparation()) {
    if (IsLocalController()) {
      NotifyActionError_Implementation(PendingBattlePhaseError);
    } else {
      NotifyActionError(PendingBattlePhaseError);
    }
    return;
  }

  if (HasAuthority()) {
    ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
    if (!PS) {
      UE_LOG(LogSkald, Warning,
             TEXT("HandleEndPhaseInternal: %s has no PlayerState; rejecting."),
             *GetName());
      return;
    }

    if (ASkaldGameState *GS = GetWorld()->GetGameState<ASkaldGameState>()) {
      const int32 MyIndex = GS->PlayerArray.IndexOfByKey(PS);
      if (MyIndex == INDEX_NONE || GS->CurrentTurnIndex != MyIndex) {
        UE_LOG(LogSkald, Warning,
               TEXT("HandleEndPhaseInternal: %s attempted to end phase out of turn."),
               *GetName());
        return;
      }
    } else {
      UE_LOG(LogSkald, Warning,
             TEXT("HandleEndPhaseInternal: %s missing GameState; rejecting."),
             *GetName());
      return;
    }
  }

  ETurnPhase Phase = TurnManager->GetCurrentPhase();
  if (Phase == ETurnPhase::ArmyPlacement) {
    if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
      if (HasAuthority() && PS->DeployableUnits > 0) {
        const int32 Distributed = TurnManager->DistributeArmyPlacementUnits(PS);
        if (Distributed > 0) {
          TurnManager->BroadcastDeployableUnits(PS);
        }
      }
      if (PS->DeployableUnits < 0) {
        PS->DeployableUnits = 0;
      }
      TurnManager->BroadcastDeployableUnits(PS);
    }
  }

  TurnManager->EndCurrentPhase();
}

bool ASkaldPlayerController::ValidateAttack(int32 FromID, int32 ToID,
                                            int32 ArmySent, bool bUseSiege,
                                            FString *OutError) {
  if (TurnManager && TurnManager->HasPendingBattlePreparation()) {
    if (OutError) {
      *OutError = PendingBattleAttackError;
    }
    return false;
  }

  if (!TurnManager) {
    if (OutError) {
      *OutError = TEXT("Turn system unavailable");
    }
    return false;
  }

  if (!IsMyTurn()) {
    if (OutError) {
      *OutError = TEXT("It is not your turn");
    }
    return false;
  }

  if (TurnManager->GetCurrentPhase() != ETurnPhase::Attack) {
    if (OutError) {
      *OutError = TEXT("Attacks can only be made during the attack phase");
    }
    return false;
  }

  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    if (OutError) {
      *OutError = TEXT("World map not found");
    }
    return false;
  }

  ATerritory *Source = WorldMap->GetTerritoryById(FromID);
  ATerritory *Target = WorldMap->GetTerritoryById(ToID);
  if (!Source || !Target) {
    if (OutError) {
      *OutError = TEXT("Invalid territory selection");
    }
    return false;
  }

  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS) {
    if (OutError) {
      *OutError = TEXT("Missing player state");
    }
    return false;
  }

  if (Source->OwningPlayer != PS) {
    if (OutError) {
      *OutError = TEXT("You may only attack from territories you control");
    }
    return false;
  }

  if (!WorldMap->AreTerritoriesAdjacent(Source, Target)) {
    if (OutError) {
      *OutError = TEXT("Cannot attack non-adjacent territory");
    }
    return false;
  }

  if (ArmySent <= 0 || ArmySent >= Source->ArmyUnits) {
    if (OutError) {
      *OutError = TEXT("Invalid army count for attack");
    }
    return false;
  }

  if (!SkaldHelpers::MeetsCapitalAttackRequirement(Target->bIsCapital,
                                                   ArmySent)) {
    if (OutError) {
      *OutError = TEXT("Insufficient forces to attack capital");
    }
    return false;
  }

  return true;
}

bool ASkaldPlayerController::ValidateMoveRequest(
    AWorldMap *WorldMap, int32 FromID, int32 ToID, int32 Troops,
    ATerritory *&OutSource, ATerritory *&OutTarget, FString &OutError) const {
  OutSource = nullptr;
  OutTarget = nullptr;

  if (!WorldMap) {
    OutError = TEXT("World map not found");
    return false;
  }

  OutSource = WorldMap->GetTerritoryById(FromID);
  OutTarget = WorldMap->GetTerritoryById(ToID);
  if (!OutSource || !OutTarget) {
    OutError = TEXT("Invalid territory selection");
    return false;
  }

  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS) {
    OutError = TEXT("Missing player state");
    return false;
  }

  if (OutSource->OwningPlayer != PS || OutTarget->OwningPlayer != PS) {
    OutError = TEXT("You may only move between your territories");
    return false;
  }

  const int32 MaxMovable = OutSource->ArmyUnits - 1;
  if (Troops <= 0 || Troops > MaxMovable) {
    OutError = TEXT("Invalid troop count for movement");
    return false;
  }

  TArray<ATerritory *> Path;
  if (!WorldMap->FindPath(OutSource, OutTarget, Path) || Path.Num() < 2) {
    OutError =
        TEXT("Selected territories must be connected by a friendly path");
    return false;
  }

  return true;
}

void ASkaldPlayerController::HandleAttackRequested(int32 FromID, int32 ToID,
                                                   int32 ArmySent,
                                                   bool bUseSiege) {
  UE_LOG(LogSkald, Log, TEXT("HUD attack from %d to %d with %d"), FromID, ToID,
         ArmySent);

  if (TurnManager && TurnManager->HasPendingBattlePreparation()) {
    NotifyActionError_Implementation(PendingBattleAttackError);
    return;
  }

  FString Error;
  if (!ValidateAttack(FromID, ToID, ArmySent, bUseSiege, &Error)) {
    NotifyActionError(Error);
    return;
  }

  ServerHandleAttack(FromID, ToID, ArmySent, bUseSiege);
}

void ASkaldPlayerController::HandlePrepareForBattleReady()
{
    ServerSetReadyForBattle(true);
}

void ASkaldPlayerController::HandleRetreatRequested() {
  ServerRequestRetreat();
}

void ASkaldPlayerController::HandleRetreatDestinationSelected(int32 TerritoryID) {
  ServerConfirmRetreatDestination(TerritoryID);
}

void ASkaldPlayerController::ServerHandleAttack_Implementation(int32 FromID,
    int32 ToID,
    int32 ArmySent,
    bool bUseSiege)
{
    if (TurnManager && TurnManager->HasPendingBattlePreparation())
    {
        NotifyActionError(PendingBattleAttackError);
        return;
    }

    FString Error;
    if (!ValidateAttack(FromID, ToID, ArmySent, bUseSiege, &Error))
    {
        NotifyActionError(Error);
        return;
    }

    // ===== original attack logic (all inside the function) =====
    AWorldMap* WorldMap = Cast<AWorldMap>(
        UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
    ATerritory* Source = WorldMap ? WorldMap->GetTerritoryById(FromID) : nullptr;
    ATerritory* Target = WorldMap ? WorldMap->GetTerritoryById(ToID) : nullptr;
    if (!Source || !Target)
    {
        return;
    }

    ASkaldPlayerState* AttackerPS = Source->OwningPlayer;
    ASkaldPlayerState* DefenderPS = Target->OwningPlayer;

    // === TurnManager-driven battle (grid / advanced flow) ===
    if (TurnManager)
    {
        FS_BattlePayload Battle;
        Battle.AttackerPlayerID = AttackerPS ? AttackerPS->GetPlayerId() : -1;
        Battle.DefenderPlayerID = DefenderPS ? DefenderPS->GetPlayerId() : -1;
        Battle.FromTerritoryID = FromID;
        Battle.TargetTerritoryID = ToID;
        Battle.AttackerTerritoryName = Source->TerritoryName;
        Battle.DefenderTerritoryName = Target->TerritoryName;
        Battle.ArmyCountSent = ArmySent;
        Battle.IsCapitalAttack = Target->bIsCapital;

        if (AttackerPS)
        {
            Battle.AttackerFaction = AttackerPS->Faction;
            Battle.AttackerDisplayName =
                ResolvePlayerName(AttackerPS, TEXT("ServerHandleAttack_Attacker"));
            Battle.bAttackerIsAI = AttackerPS->bIsAI;
        }

        if (DefenderPS)
        {
            Battle.DefenderFaction = DefenderPS->Faction;
            Battle.DefenderDisplayName =
                ResolvePlayerName(DefenderPS, TEXT("ServerHandleAttack_Defender"));
            Battle.bDefenderIsAI = DefenderPS->bIsAI;
        }

        if (USkaldGameInstance* GI = GetGameInstance<USkaldGameInstance>())
        {
            if (!Battle.AttackerFactionEmblem.ToSoftObjectPath().IsValid() &&
                Battle.AttackerFaction != ESkaldFaction::None)
            {
                Battle.AttackerFactionEmblem =
                    GI->GetFactionEmblem(Battle.AttackerFaction);
            }
            if (!Battle.DefenderFactionEmblem.ToSoftObjectPath().IsValid() &&
                Battle.DefenderFaction != ESkaldFaction::None)
            {
                Battle.DefenderFactionEmblem =
                    GI->GetFactionEmblem(Battle.DefenderFaction);
            }
        }

        if (bUseSiege && CachedGameMode)
        {
            const int32 SiegeID = CachedGameMode->ConsumeSiege(FromID);
            if (SiegeID > 0)
            {
                Battle.AssignedSiegeIDs.Add(SiegeID);
            }
        }

        Battle.DefenderArmyCount = Target ? Target->ArmyUnits : 0;

        if (!CachedGameMode)
        {
            CachedGameMode = GetWorld()->GetAuthGameMode<ASkaldGameMode>();
        }

        if (CachedGameInstance)
        {
            CachedGameInstance->CacheWorldMapSnapshot(GetWorld());
        }
        else if (UWorld* World = GetWorld())
        {
            if (USkaldGameInstance* GI =
                World->GetGameInstance<USkaldGameInstance>())
            {
                GI->CacheWorldMapSnapshot(World);
            }
        }

        // Hand off to turn managers battle flow and exit.
        TurnManager->HandleAttackConfirmed(Battle);
        return;
    }

    // === Legacy / fallback auto-resolve battle logic ===
    int32 AttackingForces = ArmySent;
    int32 DefendingForces = Target->ArmyUnits;
    if (bUseSiege && CachedGameMode)
    {
        CachedGameMode->ConsumeSiege(FromID);
    }

    // The attacking army leaves its origin territory
    Source->ArmyUnits -= ArmySent;

    FRandomStream* CombatStream = nullptr;
    if (CachedGameInstance)
    {
        CachedGameInstance->SeedCombatRandomStream(FMath::Rand());
        CombatStream = &CachedGameInstance->CombatRandomStream;
    }
    else
    {
        static FRandomStream FallbackStream;
        FallbackStream.Initialize(FMath::Rand());
        CombatStream = &FallbackStream;
    }

    while (AttackingForces > 0 && DefendingForces > 0)
    {
        const int32 AttackRoll = CombatStream->RandRange(1, 6);
        const int32 DefendRoll = CombatStream->RandRange(1, 6);
        if (AttackRoll > DefendRoll)
        {
            --DefendingForces;
        }
        else
        {
            --AttackingForces;
        }
    }

    if (DefendingForces <= 0)
    {
        Target->OwningPlayer = AttackerPS;
        Target->ArmyUnits = AttackingForces;
    }
    else
    {
        Target->ArmyUnits = DefendingForces;
    }

    Source->RefreshAppearance();
    Target->RefreshAppearance();

    if (TurnManager)
    {
        for (ASkaldPlayerController* Controller : TurnManager->GetControllers())
        {
            if (USkaldMainHUDWidget* HUD =
                Controller ? Controller->GetHUDWidget() : nullptr)
            {
                const FString OwnerName =
                    ResolvePlayerName(Target->OwningPlayer,
                        TEXT("ServerHandleAttack_Update"));
                HUD->UpdateTerritoryInfo(Target->TerritoryName,
                    OwnerName,
                    Target->ArmyUnits);
            }
        }
    }
}

// ============================================================
// Manual Dice Roll RPC
// ============================================================
void ASkaldPlayerController::ServerSubmitManualAttackRoll_Implementation(AFighterPawn* Attacker, int32 RollValue)
{
    if (!Attacker)
    {
        return;
    }

    if (UGridBattleManager* BM = GetBattleManager())
    {
        // Feed the player's manual roll to the battle manager.
        BM->ApplyManualRollFromPlayer(this, Attacker, RollValue);
    }
}

void ASkaldPlayerController::ServerSetReadyForBattle_Implementation(bool bReady) {
    if (!EnsureTurnManager(TEXT("ServerSetReadyForBattle"))) {
        return;
    }

    ASkaldPlayerState* PS = GetPlayerState<ASkaldPlayerState>();
    const int32 PlayerID = PS ? PS->GetPlayerId() : -1;
    if (PlayerID < 0) {
        UE_LOG(LogSkald, Warning,
            TEXT("ServerSetReadyForBattle called with invalid PlayerID"));
        return;
    }

    TurnManager->NotifyPlayerReadyForBattle(PlayerID, bReady);
}

void ASkaldPlayerController::ServerRequestRetreat_Implementation() {
  if (!EnsureTurnManager(TEXT("ServerRequestRetreat"))) {
    return;
  }

  TurnManager->RequestDefenderRetreat(this);
}

void ASkaldPlayerController::ServerConfirmRetreatDestination_Implementation(
    int32 TerritoryID) {
  if (!EnsureTurnManager(TEXT("ServerConfirmRetreatDestination"))) {
    return;
  }

  TurnManager->ConfirmDefenderRetreatDestination(this, TerritoryID);
}

void ASkaldPlayerController::HandleMoveRequested(int32 FromID, int32 ToID,
    int32 Troops) {
    UE_LOG(LogSkald, Log, TEXT("HUD move from %d to %d with %d"), FromID, ToID,
        Troops);

    const auto ResetSelectionForRetry = [this]() {
        if (MainHUD && MainHUD->CurrentPhase == ETurnPhase::Movement) {
            MainHUD->ResetMoveSelectionAfterInvalidAttempt();
        }
        };

    if (!IsMyTurn()) {
        NotifyActionError(TEXT("You can only move troops during your turn"));
        ResetSelectionForRetry();
        return;
    }

    const bool bInMovementPhase =
        (TurnManager && TurnManager->GetCurrentPhase() == ETurnPhase::Movement) ||
        (MainHUD && MainHUD->CurrentPhase == ETurnPhase::Movement);
    if (!bInMovementPhase) {
        NotifyActionError(TEXT("Troops can only move during the movement phase"));
        ResetSelectionForRetry();
        return;
    }

    AWorldMap* WorldMap = Cast<AWorldMap>(
        UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
    FString Error;
    ATerritory* Source = nullptr;
    ATerritory* Target = nullptr;
    if (!ValidateMoveRequest(WorldMap, FromID, ToID, Troops, Source, Target,
        Error)) {
        NotifyActionError(Error);
        ResetSelectionForRetry();
        return;
    }

    ServerHandleMove(FromID, ToID, Troops);
}


void ASkaldPlayerController::ServerHandleMove_Implementation(int32 FromID,
                                                             int32 ToID,
                                                             int32 Troops) {
  FString Error;
  if (!EnsureTurnManager(TEXT("ServerHandleMove"))) {
    Error = TEXT("Unable to resolve turn manager");
    ClientHandleMoveOutcome(false, FromID, ToID, Error);
    return;
  }

  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  const int32 PlayerID = PS ? PS->GetPlayerId() : -1;
  if (PlayerID <= 0) {
    Error = TEXT("Unable to resolve player for movement");
    ClientHandleMoveOutcome(false, FromID, ToID, Error);
    return;
  }

  if (TurnManager->GetCurrentPhase() != ETurnPhase::Movement) {
    Error = TEXT("Troops can only move during the movement phase");
    ClientHandleMoveOutcome(false, FromID, ToID, Error);
    return;
  }

  if (!IsMyTurn()) {
    Error = TEXT("You can only move troops during your turn");
    ClientHandleMoveOutcome(false, FromID, ToID, Error);
    return;
  }

  if (!TurnManager->CanPerformMovementAction(PlayerID, &Error)) {
    ClientHandleMoveOutcome(false, FromID, ToID, Error);
    return;
  }

  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  ATerritory *Source = nullptr;
  ATerritory *Target = nullptr;
  if (!ValidateMoveRequest(WorldMap, FromID, ToID, Troops, Source, Target,
                           Error)) {
    ClientHandleMoveOutcome(false, FromID, ToID, Error);
    return;
  }

  if (!WorldMap || !WorldMap->MoveBetween(Source, Target, Troops)) {
    Error = TEXT("Unable to move troops right now");
    ClientHandleMoveOutcome(false, FromID, ToID, Error);
    return;
  }

  TurnManager->RecordMovementAction(PlayerID);
  const int32 RemainingMoves = TurnManager->GetMovementActionsRemaining(PlayerID);
  FString SuccessMessage;
  if (RemainingMoves > 0) {
    SuccessMessage =
        FString::Printf(TEXT("Troops moved. %d movement%s remaining this phase."),
                        RemainingMoves,
                        RemainingMoves == 1 ? TEXT("") : TEXT("s"));
  } else {
    SuccessMessage = TEXT("Troops moved. No movements remaining this phase.");
  }

  ClientHandleMoveOutcome(true, FromID, ToID, SuccessMessage);

  if (TurnManager) {
    for (ASkaldPlayerController *Controller : TurnManager->GetControllers()) {
      if (USkaldMainHUDWidget *HUD =
              Controller ? Controller->GetHUDWidget() : nullptr) {
        const FString SourceOwner =
            ResolvePlayerName(Source->OwningPlayer, TEXT("ServerHandleMove_Source"));
        HUD->UpdateTerritoryInfo(Source->TerritoryName, SourceOwner,
                                 Source->ArmyUnits);
        const FString TargetOwner =
            ResolvePlayerName(Target->OwningPlayer, TEXT("ServerHandleMove_Target"));
        HUD->UpdateTerritoryInfo(Target->TerritoryName, TargetOwner,
                                 Target->ArmyUnits);
      }
    }
  }
}

void ASkaldPlayerController::ServerBuildSiege_Implementation(
    int32 TerritoryID, ESiegeWeapon SiegeType) {
  if (CachedGameMode) {
    CachedGameMode->BuildSiegeAtTerritory(TerritoryID, SiegeType);
  }
}

void ASkaldPlayerController::ServerDeployUnits_Implementation(int32 TerritoryID,
                                                              int32 Amount) {
  if (Amount <= 0) {
    UE_LOG(LogSkald, Warning,
           TEXT("ServerDeployUnits called with non-positive amount: %d"),
           Amount);
    NotifyActionError(TEXT("Invalid deploy amount"));
    return;
  }

  if (!TurnManager) {
    UE_LOG(LogSkald, Warning,
           TEXT("ServerDeployUnits: TurnManager not available for %s"),
           *GetName());
    NotifyActionError(TEXT("Cannot deploy units right now"));
    return;
  }

  if (!IsMyTurn()) {
    UE_LOG(LogSkald, Warning,
           TEXT("ServerDeployUnits: %s attempted to deploy out of turn"),
           *GetName());
    NotifyActionError(TEXT("You may only deploy units during your turn"));
    return;
  }

  const ETurnPhase CurrentPhase = TurnManager->GetCurrentPhase();
  const bool bIsArmyPlacementPhase = CurrentPhase == ETurnPhase::ArmyPlacement;
  const bool bIsReinforcementPhase = CurrentPhase == ETurnPhase::Reinforcement;
  if (!bIsArmyPlacementPhase && !bIsReinforcementPhase) {
    UE_LOG(LogSkald, Warning,
           TEXT("ServerDeployUnits: %s attempted to deploy during phase %d"),
           *GetName(), static_cast<int32>(CurrentPhase));
    NotifyActionError(
        TEXT("Units can only be deployed during Army Placement or Reinforcement"));
    return;
  }

  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    UE_LOG(LogSkald, Warning, TEXT("ServerDeployUnits: World map not found"));
    NotifyActionError(TEXT("World map not found"));
    return;
  }

  ATerritory *Terr = WorldMap->GetTerritoryById(TerritoryID);
  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!Terr || !PS) {
    UE_LOG(LogSkald, Warning, TEXT("ServerDeployUnits: Invalid territory %d"),
           TerritoryID);
    NotifyActionError(TEXT("Invalid territory selection"));
    return;
  }

  if (!WorldMap->IsOwnedBy(Terr, PS)) {
    UE_LOG(LogSkald, Warning,
           TEXT("ServerDeployUnits: Player %d does not own territory %d"),
           PS->GetPlayerId(), TerritoryID);
    NotifyActionError(TEXT("You do not own this territory"));
    return;
  }

  if (!bIsArmyPlacementPhase && !Terr->bIsCapital) {
    UE_LOG(LogSkald, Warning,
           TEXT("ServerDeployUnits: Territory %d is not a capital"),
           TerritoryID);
    NotifyActionError(TEXT("Reinforcements can only be placed on owned capitals"));
    return;
  }

  if (PS->DeployableUnits < Amount) {
    UE_LOG(LogSkald, Warning,
           TEXT("ServerDeployUnits: Insufficient units. Have %d need %d"),
           PS->DeployableUnits, Amount);
    NotifyActionError(TEXT("Not enough deployable units"));
    return;
  }

  const int32 TerritoryId = Terr->GetTerritoryId();
  if (bIsArmyPlacementPhase) {
    const int32 AlreadyPlaced =
        PS->GetArmyPlacementDeploymentForTerritory(TerritoryId);
    const int32 MaxPerTerritory = Skald::ArmyPlacement::DeployPerTerritoryLimit;
    if (AlreadyPlaced >= MaxPerTerritory) {
      UE_LOG(LogSkald, Warning,
             TEXT("ServerDeployUnits: Territory %d already reached placement limit"),
             TerritoryID);
      NotifyActionError(TEXT("This territory has already received the maximum deployments this phase"));
      return;
    }

    if (AlreadyPlaced + Amount > MaxPerTerritory) {
      UE_LOG(LogSkald, Warning,
             TEXT("ServerDeployUnits: Placement exceeds limit. Territory=%d Current=%d Requested=%d"),
             TerritoryID, AlreadyPlaced, Amount);
      NotifyActionError(TEXT("You may only deploy up to 10 units to a territory during army placement"));
      return;
    }
  }

  Terr->ArmyUnits += Amount;
  Terr->RefreshAppearance();

  PS->DeployableUnits -= Amount;

  if (bIsArmyPlacementPhase) {
    PS->AddArmyPlacementDeployment(TerritoryId, Amount);
  }

  if (TurnManager) {
    TurnManager->BroadcastDeployableUnits(PS);
  }
}

void ASkaldPlayerController::ServerSelectTerritory_Implementation(
    int32 TerritoryID) {
  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    if (GI->bIsInBattleMap && TerritoryID >= 0) {
      return;
    }
  }

  UE_LOG(LogSkald, Log, TEXT("ServerSelectTerritory called with %d"),
         TerritoryID);
  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    return;
  }

  int32 SelectingPlayerId = INDEX_NONE;
  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    SelectingPlayerId = PS->GetPlayerId();
  }

  if (!WorldMap->IsWorldActive() && TerritoryID >= 0) {
    return;
  }

  if (TerritoryID < 0) {
    WorldMap->SelectTerritory(nullptr, false, SelectingPlayerId); // server
    WorldMap->MulticastSelectTerritory(-1, SelectingPlayerId); // replicate
    return;
  }

  ATerritory *Terr = WorldMap->GetTerritoryById(TerritoryID);
  if (!Terr) {
    return;
  }

  WorldMap->SelectTerritory(Terr, false, SelectingPlayerId); // server authority
  WorldMap->MulticastSelectTerritory(TerritoryID, SelectingPlayerId);
}

void ASkaldPlayerController::ClientSelectTerritory_Implementation(
    int32 TerritoryID) {
  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    return;
  }

  if (!WorldMap->IsWorldActive() && TerritoryID >= 0) {
    return;
  }

  ATerritory *Terr =
      TerritoryID >= 0 ? WorldMap->GetTerritoryById(TerritoryID) : nullptr;
  int32 SelectingPlayerId = INDEX_NONE;
  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    SelectingPlayerId = PS->GetPlayerId();
  }
  WorldMap->SelectTerritory(Terr, true, SelectingPlayerId);
  UE_LOG(LogSkald, Log, TEXT("ClientSelectTerritory <- %d"), TerritoryID);
}

void ASkaldPlayerController::HandleEndAttackRequested(bool bConfirmed) {
  UE_LOG(LogSkald, Log, TEXT("HUD end attack %s"),
         bConfirmed ? TEXT("confirmed") : TEXT("cancelled"));

  if (!bConfirmed) {
    return;
  }

  if (!IsMyTurn()) {
    NotifyActionError(TEXT("You can only end the phase during your turn"));
    return;
  }

  if ((TurnManager && TurnManager->GetCurrentPhase() != ETurnPhase::Attack) ||
      (!TurnManager && MainHUD &&
       MainHUD->CurrentPhase != ETurnPhase::Attack)) {
    NotifyActionError(TEXT("You are not in the attack phase"));
    return;
  }

  EndPhase();
}

void ASkaldPlayerController::HandleEndMovementRequested(bool bConfirmed) {
  UE_LOG(LogSkald, Log, TEXT("HUD end move %s"),
         bConfirmed ? TEXT("confirmed") : TEXT("cancelled"));

  if (!bConfirmed) {
    return;
  }

  if (!IsMyTurn()) {
    NotifyActionError(TEXT("You can only end the phase during your turn"));
    return;
  }

  if ((TurnManager && TurnManager->GetCurrentPhase() != ETurnPhase::Movement) ||
      (!TurnManager && MainHUD &&
       MainHUD->CurrentPhase != ETurnPhase::Movement)) {
    NotifyActionError(TEXT("You are not in the movement phase"));
    return;
  }

  EndPhase();
}

void ASkaldPlayerController::HandleEngineeringRequested(int32 CapitalID,
                                                        uint8 UpgradeType) {
  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    const int32 Cost = 10;
    PS->Resources = FMath::Max(0, PS->Resources - Cost);
    if (TurnManager) {
      TurnManager->BroadcastResources(PS);
    }
  }
}

void ASkaldPlayerController::HandleBuildSiegeRequested(int32 TerritoryID,
                                                       ESiegeWeapon SiegeType) {
  ServerBuildSiege(TerritoryID, SiegeType);
}

void ASkaldPlayerController::HandleDigTreasureRequested(int32 TerritoryID) {
  ServerDigTreasure(TerritoryID);
}

void ASkaldPlayerController::ServerDigTreasure_Implementation(
    int32 TerritoryID) {
  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    return;
  }

  ATerritory *Terr = WorldMap->GetTerritoryById(TerritoryID);
  if (!Terr) {
    return;
  }

  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    if (Terr->OwningPlayer == PS && Terr->bHasTreasure) {
      Terr->bHasTreasure = false;
      Terr->RefreshAppearance();
      PS->Resources += 5;
      if (TurnManager) {
        TurnManager->BroadcastResources(PS);
      }
    }
  }
}

void ASkaldPlayerController::HandleAttackPhase() {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        return;
      }
    }
  }

  UE_LOG(LogSkald, Log, TEXT("Attack phase started"));
  if (MainHUD) {
    MainHUD->CancelMoveSelection();
    MainHUD->CancelAttackSelection();
    MainHUD->UpdateInitiativeText(TEXT("Attack Phase"));
  }
}

void ASkaldPlayerController::HandleEngineeringPhase() {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        return;
      }
    }
  }

  UE_LOG(LogSkald, Log, TEXT("Engineering phase started"));
  if (MainHUD) {
    MainHUD->CancelAttackSelection();
    MainHUD->CancelMoveSelection();
    MainHUD->UpdateInitiativeText(TEXT("Engineering Phase"));
  }
}

void ASkaldPlayerController::HandleTreasurePhase() {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        return;
      }
    }
  }

  UE_LOG(LogSkald, Log, TEXT("Treasure phase started"));
  if (MainHUD) {
    MainHUD->CancelAttackSelection();
    MainHUD->CancelMoveSelection();
    MainHUD->UpdateInitiativeText(TEXT("Treasure Phase"));
  }
}

void ASkaldPlayerController::HandleMovementPhase() {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        return;
      }
    }
  }

  UE_LOG(LogSkald, Log, TEXT("Movement phase started"));
  if (MainHUD) {
    MainHUD->CancelAttackSelection();
    MainHUD->CancelMoveSelection();
    MainHUD->UpdateInitiativeText(TEXT("Movement Phase"));
  }
}

void ASkaldPlayerController::HandleEndTurnPhase() {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        return;
      }
    }
  }

  UE_LOG(LogSkald, Log, TEXT("EndTurn phase started"));
  if (MainHUD) {
    MainHUD->CancelAttackSelection();
    MainHUD->CancelMoveSelection();
    MainHUD->ShowEndingTurn();
    MainHUD->UpdateInitiativeText(TEXT("End Turn Phase"));
  }
}

void ASkaldPlayerController::HandleRevoltPhase() {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        return;
      }
    }
  }

  UE_LOG(LogSkald, Log, TEXT("Revolt phase started"));
  if (MainHUD) {
    MainHUD->CancelAttackSelection();
    MainHUD->CancelMoveSelection();
    MainHUD->HideEndingTurn();
    MainHUD->UpdateInitiativeText(TEXT("Revolt Phase"));
  }
}

void ASkaldPlayerController::HandleTerritorySelected(ATerritory *Terr) {
  if (!MainHUD) {
    return;
  }

  if (!Terr) {
    MainHUD->ClearTerritoryInfo();
    return;
  }

  const FString OwnerName =
      ResolvePlayerName(Terr->OwningPlayer, TEXT("HandleTerritorySelected"));
  MainHUD->UpdateTerritoryInfo(Terr->TerritoryName, OwnerName,
                                     Terr->ArmyUnits);
  MainHUD->OnTerritoryClickedUI(Terr);
}

void ASkaldPlayerController::NotifyActionError_Implementation(
    const FString &Message) {
  const bool bArmyPlacementLimitMessage =
      Message.Contains(TEXT("maximum deployments")) ||
      Message.Contains(TEXT("deploy up to 10"));
  UE_LOG(LogSkald, Warning, TEXT("%s"), *Message);
  if (MainHUD) {
    if (bArmyPlacementLimitMessage) {
      MainHUD->ShowArmyPlacementLimitWarning(FText::FromString(Message));
    } else {
      MainHUD->ShowErrorMessage(Message);
    }
  } else if (GEngine) {
    const float DisplayTime = bArmyPlacementLimitMessage ? 2.f : 4.f;
    GEngine->AddOnScreenDebugMessage(-1, DisplayTime, FColor::Red, Message);
  }
}

void ASkaldPlayerController::ClientHandleMoveOutcome_Implementation(
    bool bSuccess, int32 /*FromID*/, int32 /*ToID*/, const FString &Message) {
  if (MainHUD) {
    MainHUD->HandleMoveOutcome(bSuccess, Message);
  } else if (GEngine) {
    GEngine->AddOnScreenDebugMessage(-1, 4.f,
                                     bSuccess ? FColor::Green : FColor::Red,
                                     Message);
  }
  if (!bSuccess) {
    UE_LOG(LogSkald, Warning, TEXT("Move request failed: %s"), *Message);
  }
}

bool ASkaldPlayerController::EnsureTurnManager(const TCHAR *Caller) {
  if (TurnManager) {
    return true;
  }

  UE_LOG(LogSkald, Warning,
         TEXT("%s called without a TurnManager. Attempting to reacquire."),
         Caller);

  if (!CachedGameMode) {
    CachedGameMode = GetWorld()->GetAuthGameMode<ASkaldGameMode>();
  }

  if (CachedGameMode) {
    SetTurnManager(CachedGameMode->GetTurnManager());
  }

  if (!TurnManager) {
    UE_LOG(LogSkald, Warning, TEXT("TurnManager still missing; aborting %s."),
           Caller);
    return false;
  }

  return true;
}

void ASkaldPlayerController::BuildPlayerDataArray(
    TArray<FS_PlayerData> &OutPlayers) const {
  OutPlayers.Reset();
  if (!CachedGameState) {
    return;
  }

  TMap<int32, int32> TerritoryCounts;
  auto AccumulateOwner = [&TerritoryCounts](int32 OwnerPlayerId) {
    if (OwnerPlayerId > 0) {
      TerritoryCounts.FindOrAdd(OwnerPlayerId) += 1;
    }
  };

  bool bResolvedTerritories = false;
  if (AWorldMap *WorldMap = CachedWorldMap.Get()) {
    for (ATerritory *Territory : WorldMap->Territories) {
      if (Territory && Territory->OwningPlayer) {
        AccumulateOwner(Territory->OwningPlayer->GetPlayerId());
        bResolvedTerritories = true;
      }
    }
  }

  if (!bResolvedTerritories) {
    if (UWorld *World = GetWorld()) {
      if (AWorldMap *WorldMap =
              Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
                  World, AWorldMap::StaticClass()))) {
        for (ATerritory *Territory : WorldMap->Territories) {
          if (Territory && Territory->OwningPlayer) {
            AccumulateOwner(Territory->OwningPlayer->GetPlayerId());
            bResolvedTerritories = true;
          }
        }
      }
    }
  }

  if (!bResolvedTerritories) {
    const USkaldGameInstance *GameInstance = CachedGameInstance;
    if (!GameInstance) {
      GameInstance = GetGameInstance<USkaldGameInstance>();
    }

    if (GameInstance) {
      auto AccumulateFromSnapshots =
          [&AccumulateOwner, &bResolvedTerritories](
              const TArray<FS_Territory> &Snapshots) {
            bool bFoundData = false;
            for (const FS_Territory &Snapshot : Snapshots) {
              if (Snapshot.OwnerPlayerID > 0) {
                AccumulateOwner(Snapshot.OwnerPlayerID);
                bFoundData = true;
              }
            }
            if (bFoundData) {
              bResolvedTerritories = true;
            }
          };

      AccumulateFromSnapshots(GameInstance->GetPendingTravelSnapshot());
      if (!bResolvedTerritories) {
        AccumulateFromSnapshots(GameInstance->CachedWorldMapTerritories);
      }
      if (!bResolvedTerritories) {
        AccumulateFromSnapshots(GameInstance->GetTravelState().CachedTerritories);
      }
    }
  }

  for (APlayerState *PSBase : CachedGameState->PlayerArray) {
    if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase)) {
      FS_PlayerData Data;
      Data.PlayerID = PS->GetPlayerId();
      Data.PlayerName = PS->GetResolvedPlayerName(TEXT("BuildPlayerDataArray"));
      Data.IsAI = PS->bIsAI;
      Data.Faction = PS->Faction;
      Data.Resources = PS->Resources;
      Data.IsEliminated = PS->IsEliminated;
      Data.TerritoriesOwned = TerritoryCounts.FindRef(Data.PlayerID);
      OutPlayers.Add(Data);
    }
  }
}

void ASkaldPlayerController::HandlePlayersUpdated() {
  if (CachedGameState && MainHUD) {
    TArray<FS_PlayerData> Players;
    BuildPlayerDataArray(Players);
    MainHUD->RefreshPlayerList(Players);

    if (ASkaldPlayerState *LocalPS = GetPlayerState<ASkaldPlayerState>()) {
      MainHUD->UpdateResources(LocalPS->Resources);
    }
  }

  InitializeFighterSelectionIfNeeded();
}

void ASkaldPlayerController::HandleFactionsUpdated() {
  if (!MainHUD || !CachedGameState) {
    return;
  }

  TArray<FS_PlayerData> Players;
  BuildPlayerDataArray(Players);
  MainHUD->RefreshPlayerList(Players);

  if (ASkaldPlayerState *LocalPS = GetPlayerState<ASkaldPlayerState>()) {
    MainHUD->UpdateResources(LocalPS->Resources);
  }
}

void ASkaldPlayerController::HandleBattleMapStateChanged(bool /*bInBattleMap*/) {
  DetectBattleMap();
  InitializeFighterSelectionIfNeeded();
  RefreshFactionCursorFromState();
}

void ASkaldPlayerController::HandleWorldStateChanged() {
  if (BattleResultWidget) {
    BattleResultWidget->RemoveFromParent();
    BattleResultWidget = nullptr;
  }

  if (!CachedGameInstance) {
    CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  }
  const bool bShouldShowOverworldHUD =
      !bIsBattleMap || (CachedGameInstance && !CachedGameInstance->bIsInBattleMap);
  if (bShouldShowOverworldHUD) {
    ShowOverworldHUD();
  }

  if (!MainHUD) {
    return;
  }

  ShowMainHUD();

  // Update territory info for the currently selected territory if available.
  if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          GetWorld(), AWorldMap::StaticClass()))) {
    if (ATerritory *Terr = WorldMap->SelectedTerritory) {
      const FString OwnerName = ResolvePlayerName(
          Terr->OwningPlayer, TEXT("HandleWorldStateChanged"));
      MainHUD->UpdateTerritoryInfo(Terr->TerritoryName, OwnerName,
                                         Terr->ArmyUnits);
    }
  }

  // Refresh player list from the game state.
  if (CachedGameState) {
    TArray<FS_PlayerData> Players;
    BuildPlayerDataArray(Players);
    MainHUD->RefreshPlayerList(Players);
  }

  // Update deploy/phase banners.
  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    MainHUD->UpdateDeployableUnits(PS->DeployableUnits);
    MainHUD->UpdateResources(PS->Resources);
  }
  if (TurnManager) {
    MainHUD->UpdatePhaseBanner(TurnManager->GetCurrentPhase());
  }
}

void ASkaldPlayerController::HandlePlayerLockedIn() { HandleFactionLockedIn(); }

void ASkaldPlayerController::HandleFactionLockedIn() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(LobbyAutoInitHandle);
  }

  if (bHasInitialized) {
    return;
  }
  bHasInitialized = true;

  if (ChoosePlayerWidget) {
    ChoosePlayerWidget->OnPlayerLockedIn.RemoveDynamic(
        this, &ASkaldPlayerController::HandleFactionLockedIn);
    ChoosePlayerWidget->RemoveFromParent();
    ChoosePlayerWidget = nullptr;
  }

  if (MainHUD) {
    ShowMainHUD();
    if (CachedGameState) {
      TArray<FS_PlayerData> Players;
      BuildPlayerDataArray(Players);
      MainHUD->RefreshPlayerList(Players);
    }
    if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
      MainHUD->LocalPlayerID = PS->GetPlayerId();
      MainHUD->UpdateDeployableUnits(PS->DeployableUnits);
      MainHUD->UpdateResources(PS->Resources);
      MainHUD->SyncPhaseButtons(MainHUD->CurrentPlayerID ==
                                      MainHUD->LocalPlayerID);
    }
    if (TurnManager) {
      MainHUD->UpdatePhaseBanner(TurnManager->GetCurrentPhase());
    }
  }

  bShowMouseCursor = true;
  bEnableClickEvents = true;
  bEnableMouseOverEvents = true;
  DefaultMouseCaptureMode = EMouseCaptureMode::NoCapture;
  SetIgnoreMoveInput(false);
  SetIgnoreLookInput(false);
  RefreshFactionCursorFromState();
  TryBindWorldMap();

  // Refresh the HUD after any AI opponents have been spawned by the game
  // mode's PopulateAIPlayers call. This ensures the local player sees the
  // full roster once lock-in is complete.
  if (CachedGameState) {
    FTimerDelegate RefreshDelegate = FTimerDelegate::CreateUObject(
        this, &ASkaldPlayerController::HandlePlayersUpdated);
    GetWorldTimerManager().SetTimerForNextTick(RefreshDelegate);
  }
}

void ASkaldPlayerController::UpdateBattleCameraMode() {
  if (!IsLocalController()) {
    return;
  }

  ASkald_PlayerCharacter *CameraPawn = Cast<ASkald_PlayerCharacter>(GetPawn());
  if (!CameraPawn) {
    return;
  }

  CameraPawn->SetBattleCameraActive(bIsBattleMap);

  if (!bIsBattleMap) {
    CameraPawn->ClearCameraFocus();
    return;
  }

  if (LockedActiveFighter && LockedActiveFighter->IsAlive()) {
    CameraPawn->FocusCameraOnActor(LockedActiveFighter.Get());
  } else {
    CameraPawn->ClearCameraFocus();
  }
}

void ASkaldPlayerController::HandleFighterSelectionLockedIn() {
  UE_LOG(LogSkaldUI, Log,
         TEXT("HandleFighterSelectionLockedIn: Controller=%s Local=%s Pawn=%s"),
         *GetName(), IsLocalController() ? TEXT("true") : TEXT("false"),
         GetPawn() ? *GetPawn()->GetName() : TEXT("null"));

  if (UFighterSelectionWidget *Selection = FighterSelectionWidget) {
    Selection->RemoveFromParent();
    FighterSelectionWidget = nullptr;
  }

  bBattleHUDReadyToShow = true;

  // Ensure the HUD is initialized so the controller is bound to active-fighter
  // updates even if no pawn has been selected yet.
  InitializeBattleHUD();

  SelectedFighter = nullptr;
  LockedActiveFighter = nullptr;
  CancelCommandMode();
  UpdateBattleHUDButtons();

  UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(
      this, nullptr, EMouseLockMode::DoNotLock, false);
  FocusGameViewport(this);
  bShowMouseCursor = true;
  bEnableClickEvents = true;
  bEnableMouseOverEvents = true;
  DefaultMouseCaptureMode = EMouseCaptureMode::NoCapture;
  SetIgnoreMoveInput(false);
  SetIgnoreLookInput(false);

  if (!CachedGameInstance) {
    CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  }
  if (CachedGameInstance && CachedGameInstance->GridBattleManager &&
      !bBattleHUDVisible) {
    EnsureBattleHUDVisible();
  }

  if (CachedGameInstance && CachedGameInstance->GridBattleManager)
  {
    UE_LOG(LogSkaldUI, Verbose,
           TEXT("HandleFighterSelectionLockedIn: GridBattleManager ready (%s)"),
           *CachedGameInstance->GridBattleManager->GetName());
  }
  else if (CachedGameInstance)
  {
    UE_LOG(LogSkaldUI, Warning,
           TEXT("HandleFighterSelectionLockedIn: GridBattleManager missing (GI=%s)"),
           *CachedGameInstance->GetName());
  }
  else
  {
    UE_LOG(LogSkaldUI, Warning,
           TEXT("HandleFighterSelectionLockedIn: GameInstance missing on %s"),
           *GetName());
  }

  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>())
  {
    UE_LOG(LogSkaldUI, Log,
           TEXT("HandleFighterSelectionLockedIn: PlayerId=%d PendingArmy=%d PendingBudget=%d ArmyLocked=%s"),
           PS->GetPlayerId(), PS->PendingArmy.Num(), PS->PendingArmyBudget,
           PS->bArmyLockedIn ? TEXT("true") : TEXT("false"));
  }
  else
  {
    UE_LOG(LogSkaldUI, Warning,
           TEXT("HandleFighterSelectionLockedIn: PlayerState missing for %s"),
           *GetName());
  }
}

void ASkaldPlayerController::SetupInputComponent() {
  Super::SetupInputComponent();

  if (InputComponent) {
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this,
                            &ASkaldPlayerController::HandleGridClick);
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this,
                            &ASkaldPlayerController::HandleCursorClickSound);
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this,
                            &ASkaldPlayerController::HandleRightClick);
    InputComponent->BindKey(EKeys::O, IE_Pressed, this,
                            &ASkaldPlayerController::ToggleInGameMenu);
    InputComponent->BindKey(EKeys::Escape, IE_Pressed, this,
                            &ASkaldPlayerController::ToggleInGameMenu);
  }
}

void ASkaldPlayerController::BeginMoveMode() {
  if (!LockedActiveFighter || !IsFriendlyFighter(LockedActiveFighter))
    return;
  if (LockedActiveFighter->IsEngaged()) {
    BeginDisengageMode();
    return;
  }
  if (LockedActiveFighter->ActionsRemaining <= 0) {
    return;
  }
  CurrentCommandMode = EBattleCommandMode::Move;
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    Grid->HighlightMovement(LockedActiveFighter);
  }
}

void ASkaldPlayerController::BeginDisengageMode() {
  if (!LockedActiveFighter || !IsFriendlyFighter(LockedActiveFighter))
    return;
  if (!LockedActiveFighter->IsEngaged()) {
    BeginMoveMode();
    return;
  }
  if (LockedActiveFighter->ActionsRemaining <= 0) {
    return;
  }
  CurrentCommandMode = EBattleCommandMode::Disengage;
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    Grid->HighlightDisengage(LockedActiveFighter,
                             LockedActiveFighter->GetDisengageRange());
  }
}

void ASkaldPlayerController::BeginAttackMode() {
  if (!LockedActiveFighter || !IsFriendlyFighter(LockedActiveFighter))
    return;
  CurrentCommandMode = EBattleCommandMode::Attack;
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    Grid->HighlightAttack(LockedActiveFighter);
  }
}

void ASkaldPlayerController::HandleGridClick() {
  if (!IsLocalController())
    return;

  if (IsCursorOverInteractableSlateWidget()) {
    return;
  }

  // Non-battle map handling
  FHitResult Hit;
  if (!bIsBattleMap) {
    GetHitResultUnderCursor(ECC_Visibility, /*bTraceComplex*/ true, Hit);
    if (ATerritory *Terr = Cast<ATerritory>(Hit.GetActor())) {
      ServerSelectTerritory(Terr->TerritoryID);
    } else {
      ServerSelectTerritory(-1);
    }
    return;
  }

  // Get active fighter
  if (!GetHitResultUnderCursor(ECC_Visibility, /*bTraceComplex*/ false, Hit)) {
    return;
  }

  UGridOverlayComponent *Grid = FindGridOverlay();
  if (!Grid)
    return;

  const FVector WorldLocation = Hit.bBlockingHit ? Hit.ImpactPoint : Hit.Location;
  const FIntPoint Cell = Grid->WorldToGrid(WorldLocation);
  if (!Grid->IsCellInBounds(Cell)) {
    return;
  }

  AFighterPawn *CellFighter = FindFighterAtCell(Cell);

  switch (CurrentCommandMode) {
  case EBattleCommandMode::Move: {
    if (!LockedActiveFighter) {
      CancelCommandMode();
      break;
    }
    if (!IsFriendlyFighter(LockedActiveFighter)) {
      CancelCommandMode();
      break;
    }
    FIntPoint TargetAnchor = Cell;
    if (Grid) {
      const int32 FootprintSize = LockedActiveFighter->GetFootprintSideLength();
      const FIntPoint StartCell = LockedActiveFighter->GetCurrentCell();
      const int32 MovementRange = LockedActiveFighter->Stats.Movement;
      const TArray<FIntPoint> PreviousCells = LockedActiveFighter->GetOccupiedCells();

      int32 BestDistanceToStart = MAX_int32;
      int32 BestDistanceToClicked = MAX_int32;
      bool bFoundValidAnchor = false;
      bool bBestAnchorMoves = false;

      for (int32 Dy = 0; Dy < FootprintSize; ++Dy) {
        for (int32 Dx = 0; Dx < FootprintSize; ++Dx) {
          const FIntPoint CandidateAnchor = Cell - FIntPoint(Dx, Dy);

          if (!Grid->IsCellInBounds(CandidateAnchor)) {
            continue;
          }

          const int32 DistanceToStart = FMath::Max(
              FMath::Abs(CandidateAnchor.X - StartCell.X),
              FMath::Abs(CandidateAnchor.Y - StartCell.Y));
          if (DistanceToStart > MovementRange) {
            continue;
          }

          const TArray<FIntPoint> CandidateCells =
              LockedActiveFighter->GetOccupiedCells(CandidateAnchor);

          bool bCanOccupyCandidate = true;
          for (const FIntPoint &CandidateCell : CandidateCells) {
            if (!Grid->IsCellInBounds(CandidateCell) ||
                Grid->IsObscured(CandidateCell)) {
              bCanOccupyCandidate = false;
              break;
            }

            const bool bCellPreviouslyOccupied =
                PreviousCells.Contains(CandidateCell);
            if (!bCellPreviouslyOccupied && Grid->IsOccupied(CandidateCell)) {
              bCanOccupyCandidate = false;
              break;
            }
          }

          if (!bCanOccupyCandidate) {
            continue;
          }

          const bool bCandidateMoves = DistanceToStart > 0;
          const int32 CandidateAnchorDistance = FMath::Max(
              FMath::Abs(CandidateAnchor.X - Cell.X),
              FMath::Abs(CandidateAnchor.Y - Cell.Y));

          bool bUseCandidate = false;
          if (!bFoundValidAnchor) {
            bUseCandidate = true;
          } else if (bCandidateMoves != bBestAnchorMoves) {
            bUseCandidate = bCandidateMoves && !bBestAnchorMoves;
          } else if (DistanceToStart < BestDistanceToStart) {
            bUseCandidate = true;
          } else if (DistanceToStart == BestDistanceToStart &&
                     CandidateAnchorDistance < BestDistanceToClicked) {
            bUseCandidate = true;
          }

          if (bUseCandidate) {
            BestDistanceToStart = DistanceToStart;
            BestDistanceToClicked = CandidateAnchorDistance;
            TargetAnchor = CandidateAnchor;
            bBestAnchorMoves = bCandidateMoves;
            bFoundValidAnchor = true;
          }
        }
      }

      if (!bFoundValidAnchor) {
        TargetAnchor = Cell;
      }
    }

    LockedActiveFighter->MoveToCell(TargetAnchor);
    CancelCommandMode();
    UpdateBattleHUDButtons();
    break;
  }
  case EBattleCommandMode::VeilStep: {
    if (!PendingVeilStepFighter.IsValid() || !PendingVeilStepSlot.IsSet()) {
      CancelCommandMode();
      break;
    }

    AFighterPawn *VeilStepFighter = PendingVeilStepFighter.Get();
    FIntPoint TargetAnchor = Cell;
    bool bHasValidAnchor =
        FindBestVeilStepAnchor(VeilStepFighter, Grid, Cell, TargetAnchor);

    if (bHasValidAnchor) {
      if (HasAuthority()) {
        FText Error;
        if (!ExecuteVeilStepInternal(VeilStepFighter,
                                     PendingVeilStepSlot.GetValue(),
                                     TargetAnchor, &Error)) {
          if (!Error.IsEmpty()) {
            NotifyActionError(Error.ToString());
          }
        }
      } else {
        ServerExecuteVeilStep(VeilStepFighter,
                              PendingVeilStepSlot.GetValue(), TargetAnchor);
      }
    } else {
      const FText ErrorText = NSLOCTEXT(
          "SkaldAbilities", "VeilStepInvalidDestination",
          "Select a visible empty tile within 3 squares.");
      NotifyActionError(ErrorText.ToString());
    }

    CancelCommandMode();
    UpdateBattleHUDButtons();
    break;
  }
  case EBattleCommandMode::Disengage: {
    if (!LockedActiveFighter) {
      CancelCommandMode();
      break;
    }
    if (!IsFriendlyFighter(LockedActiveFighter)) {
      CancelCommandMode();
      break;
    }
    if (!LockedActiveFighter->IsEngaged()) {
      CancelCommandMode();
      BeginMoveMode();
      break;
    }
    const int32 DisengageRange = LockedActiveFighter->GetDisengageRange();
    if (DisengageRange <= 0) {
      CancelCommandMode();
      break;
    }
    FIntPoint TargetAnchor = Cell;
    if (Grid) {
      const int32 FootprintSize = LockedActiveFighter->GetFootprintSideLength();
      const FIntPoint StartCell = LockedActiveFighter->GetCurrentCell();
      const TArray<FIntPoint> PreviousCells = LockedActiveFighter->GetOccupiedCells();

      int32 BestDistanceToStart = MAX_int32;
      int32 BestDistanceToClicked = MAX_int32;
      bool bFoundValidAnchor = false;
      bool bBestAnchorMoves = false;

      for (int32 Dy = 0; Dy < FootprintSize; ++Dy) {
        for (int32 Dx = 0; Dx < FootprintSize; ++Dx) {
          const FIntPoint CandidateAnchor = Cell - FIntPoint(Dx, Dy);

          if (!Grid->IsCellInBounds(CandidateAnchor)) {
            continue;
          }

          const int32 DistanceToStart = FMath::Max(
              FMath::Abs(CandidateAnchor.X - StartCell.X),
              FMath::Abs(CandidateAnchor.Y - StartCell.Y));
          if (DistanceToStart > DisengageRange) {
            continue;
          }

          const TArray<FIntPoint> CandidateCells =
              LockedActiveFighter->GetOccupiedCells(CandidateAnchor);

          bool bCanOccupyCandidate = true;
          for (const FIntPoint &CandidateCell : CandidateCells) {
            if (!Grid->IsCellInBounds(CandidateCell) ||
                Grid->IsObscured(CandidateCell)) {
              bCanOccupyCandidate = false;
              break;
            }

            const bool bCellPreviouslyOccupied =
                PreviousCells.Contains(CandidateCell);
            if (!bCellPreviouslyOccupied && Grid->IsOccupied(CandidateCell)) {
              bCanOccupyCandidate = false;
              break;
            }
          }

          if (!bCanOccupyCandidate) {
            continue;
          }

          const bool bCandidateMoves = DistanceToStart > 0;
          const int32 CandidateAnchorDistance = FMath::Max(
              FMath::Abs(CandidateAnchor.X - Cell.X),
              FMath::Abs(CandidateAnchor.Y - Cell.Y));

          bool bUseCandidate = false;
          if (!bFoundValidAnchor) {
            bUseCandidate = true;
          } else if (bCandidateMoves != bBestAnchorMoves) {
            bUseCandidate = bCandidateMoves && !bBestAnchorMoves;
          } else if (DistanceToStart < BestDistanceToStart) {
            bUseCandidate = true;
          } else if (DistanceToStart == BestDistanceToStart &&
                     CandidateAnchorDistance < BestDistanceToClicked) {
            bUseCandidate = true;
          }

          if (bUseCandidate) {
            BestDistanceToStart = DistanceToStart;
            BestDistanceToClicked = CandidateAnchorDistance;
            TargetAnchor = CandidateAnchor;
            bBestAnchorMoves = bCandidateMoves;
            bFoundValidAnchor = true;
          }
        }
      }
    }

    LockedActiveFighter->TryDisengageToCell(TargetAnchor);
    CancelCommandMode();
    UpdateBattleHUDButtons();
    break;
  }
  case EBattleCommandMode::Attack: {
    if (!LockedActiveFighter) {
      CancelCommandMode();
      break;
    }
    if (!IsFriendlyFighter(LockedActiveFighter)) {
      CancelCommandMode();
      break;
    }
    const auto IsValidEnemyTarget = [&](AFighterPawn *Candidate) {
      return Candidate && Candidate != LockedActiveFighter &&
             Candidate->IsAlive() && !IsFriendlyFighter(Candidate);
    };

    AFighterPawn *TargetPawn = CellFighter;
    FIntPoint TargetCell = Cell;

    if (!IsValidEnemyTarget(TargetPawn)) {
      TargetPawn = nullptr;

      FVector TraceStart = Hit.TraceStart;
      FVector TraceEnd = Hit.TraceEnd;

      if (TraceStart == TraceEnd) {
        FVector MouseWorldLocation, MouseWorldDirection;
        if (DeprojectMousePositionToWorld(MouseWorldLocation, MouseWorldDirection)) {
          if (APlayerCameraManager *CameraManager = PlayerCameraManager) {
            TraceStart = CameraManager->GetCameraLocation();
          } else {
            TraceStart = MouseWorldLocation;
          }
          TraceEnd = TraceStart + MouseWorldDirection * 100000.f;
        }
      }

      if (UWorld *World = GetWorld()) {
        TArray<FHitResult> AdditionalHits;
        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HandleGridClickAttack),
                                          /*bTraceComplex*/ false);
        if (LockedActiveFighter) {
          QueryParams.AddIgnoredActor(LockedActiveFighter);
        }

        if (World->LineTraceMultiByChannel(AdditionalHits, TraceStart, TraceEnd,
                                           ECC_Visibility, QueryParams)) {
          for (const FHitResult &CandidateHit : AdditionalHits) {
            AFighterPawn *CandidatePawn =
                Cast<AFighterPawn>(CandidateHit.GetActor());
            if (!IsValidEnemyTarget(CandidatePawn)) {
              continue;
            }

            const FIntPoint CandidateCell = CandidatePawn->GetCurrentCell();
            if (!Grid->IsCellInBounds(CandidateCell)) {
              continue;
            }

            TargetPawn = CandidatePawn;
            TargetCell = CandidateCell;
            break;
          }
        }
      }
    }

    if (IsValidEnemyTarget(TargetPawn)) {
      CellFighter = TargetPawn;
      LockedActiveFighter->PerformAttack(TargetPawn);
    }
    CancelCommandMode();
    UpdateBattleHUDButtons();
    break;
  }
  case EBattleCommandMode::AbilityTargetEnemy:
  case EBattleCommandMode::AbilityTargetAlly:
  case EBattleCommandMode::AbilityTargetCell: {
    if (!PendingAbilityCommand.IsSet()) {
      CancelCommandMode();
      break;
    }

    const FPendingAbilityCommand &Command = PendingAbilityCommand.GetValue();
    AFighterPawn *Source = Command.SourceFighter.Get();
    if (!Source) {
      CancelCommandMode();
      break;
    }

    if (CurrentCommandMode == EBattleCommandMode::AbilityTargetCell) {
      FText Error;
      if (!ValidateAbilityTargetCell(Command, Cell, Error)) {
        if (!Error.IsEmpty()) {
          NotifyActionError(Error.ToString());
        }
        break;
      }

      if (HasAuthority()) {
        FText ServerError;
        if (!ExecuteAbilityCommandInternal(Command, nullptr, &Cell,
                                           &ServerError)) {
          if (!ServerError.IsEmpty()) {
            NotifyActionError(ServerError.ToString());
          }
        }
      } else {
        ServerExecuteAbilityAtCell(Source, Command.Slot, Cell);
      }

      CancelAbilityCommand();
      UpdateBattleHUDButtons();
      break;
    }

    AFighterPawn *TargetFighter = CellFighter;
    FText Error;
    if (!ValidateAbilityTargetFighter(Command, TargetFighter, Error)) {
      if (!Error.IsEmpty()) {
        NotifyActionError(Error.ToString());
      }
      break;
    }

    if (HasAuthority()) {
      FText ServerError;
      if (!ExecuteAbilityCommandInternal(Command, TargetFighter, nullptr,
                                         &ServerError)) {
        if (!ServerError.IsEmpty()) {
          NotifyActionError(ServerError.ToString());
        }
      }
    } else {
      ServerExecuteAbilityOnFighter(Source, Command.Slot, TargetFighter);
    }

    CancelAbilityCommand();
    UpdateBattleHUDButtons();
    break;
  }
  default:
    break;
  }

  HighlightClickedCell(Grid, Cell);

  if (CurrentCommandMode != EBattleCommandMode::None) {
    return;
  }

  if (CellFighter && CellFighter->IsAlive()) {
    if (LockedActiveFighter && LockedActiveFighter != CellFighter) {
      return;
    }

    SetSelectedFighter(CellFighter);
    return;
  }

  if (!LockedActiveFighter) {
    ClearSelectedFighter();
  }
}

void ASkaldPlayerController::HandleActivatePressed() {
  if (!IsLocalController() || !SelectedFighter)
    return;

  UE_LOG(LogSkaldBattle, Log,
         TEXT("[BattleHUD] Activate pressed for %s. Locked=%s"),
         *SelectedFighter->GetHumanReadableName(),
         LockedActiveFighter ? *LockedActiveFighter->GetHumanReadableName()
                             : TEXT("<None>"));

  if (SelectedFighter->HasActivatedThisRound()) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("[BattleHUD] Activate ignored: %s already acted this round."),
           *SelectedFighter->GetHumanReadableName());
    NotifyActionError(FString(TEXT("Fighter Already Activated.")));
    return;
  }

  if (!IsFriendlyFighter(SelectedFighter)) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("[BattleHUD] Activate ignored: %s is not friendly."),
           *SelectedFighter->GetHumanReadableName());
    NotifyActionError(FString(TEXT("Cannot activate enemy fighter.")));
    return;
  }

  if (LockedActiveFighter && LockedActiveFighter != SelectedFighter) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("[BattleHUD] Activate ignored: Locked fighter %s differs from %s."),
           *LockedActiveFighter->GetHumanReadableName(),
           *SelectedFighter->GetHumanReadableName());
    NotifyActionError(FString(TEXT("Another fighter is already active.")));
    return;
  }

  if (!CachedGameInstance) {
    CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  }
  if (!CachedGameInstance || !CachedGameInstance->GridBattleManager) {
    UE_LOG(LogSkaldBattle, Warning,
           TEXT("[BattleHUD] Activate failed: Missing GridBattleManager."));
    return;
  }

  if (CachedGameInstance->GridBattleManager->IsAwaitingInitiativeRoll()) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("[BattleHUD] Activate ignored: Awaiting initiative roll."));
    NotifyActionError(FString(TEXT("Roll for initiative before activating.")));
    return;
  }

  if (!CachedGameInstance->GridBattleManager->CanActivateFighter(SelectedFighter)) {
    UE_LOG(LogSkaldBattle, Log,
           TEXT("[BattleHUD] Activation rejected for %s (Round=%d, AttackerTurn=%s)"),
           *SelectedFighter->GetHumanReadableName(),
           CachedGameInstance->GridBattleManager->GetCurrentRound(),
           CachedGameInstance->GridBattleManager->IsAttackerTurn()
               ? TEXT("true")
               : TEXT("false"));
    NotifyActionError(FString(TEXT("Cannot activate this fighter right now.")));
    return;
  }

  if (CachedGameInstance->GridBattleManager->ActivateFighter(SelectedFighter)) {
    UE_LOG(LogSkaldBattle, Log,
           TEXT("[BattleHUD] Activation succeeded for %s"),
           *SelectedFighter->GetHumanReadableName());
    LockedActiveFighter = SelectedFighter;
    if (bBattleHUDReadyToShow && !bBattleHUDVisible) {
      EnsureBattleHUDVisible();
    }
    UpdateBattleHUDButtons();
  }
}

void ASkaldPlayerController::HandleEndTurnPressed() {
  UE_LOG(LogSkaldBattle, Log,
         TEXT("[BattleHUD] End Turn pressed. Locked=%s"),
         LockedActiveFighter ? *LockedActiveFighter->GetHumanReadableName()
                             : TEXT("<None>"));
  if (!LockedActiveFighter) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("[BattleHUD] End Turn ignored: No fighter locked."));
    return;
  }

  if (!IsFriendlyFighter(LockedActiveFighter)) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("[BattleHUD] End Turn ignored: Fighter %s is not friendly."),
           *LockedActiveFighter->GetHumanReadableName());
    return;
  }

  if (!CachedGameInstance) {
    CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  }
  if (!CachedGameInstance || !CachedGameInstance->GridBattleManager) {
    UE_LOG(LogSkaldBattle, Warning,
           TEXT("[BattleHUD] End Turn failed: Missing GridBattleManager."));
    return;
  }

  const int32 RoundNumber = CachedGameInstance->GridBattleManager->GetCurrentRound();
  const bool bAttackerTurn = CachedGameInstance->GridBattleManager->IsAttackerTurn();
  UE_LOG(LogSkaldBattle, Log,
         TEXT("[BattleHUD] Finishing activation for %s (Round=%d, AttackerTurn=%s)"),
         *LockedActiveFighter->GetHumanReadableName(), RoundNumber,
         bAttackerTurn ? TEXT("true") : TEXT("false"));
  if (BattleHudWidget) {
    BattleHudWidget->SetEndTurnVisibility(false);
  }
  CachedGameInstance->GridBattleManager->FinishActivation(
      LockedActiveFighter, EGridActivationFinishReason::Manual);
  LockedActiveFighter = nullptr;

  const bool bHadSelection = SelectedFighter != nullptr;
  ClearSelectedFighter();
  if (!bHadSelection) {
    UpdateBattleHUDButtons();
  }
  CancelCommandMode();
}

void ASkaldPlayerController::HandleCursorClickSound() {
  if (!IsLocalController()) {
    return;
  }

  if (IsCursorOverInteractableSlateWidget()) {
    PlayCursorClickSound();
  }
}

void ASkaldPlayerController::HandleRightClick() {
  if (!IsLocalController())
    return;

  if (!bIsBattleMap)
    return;

  CancelCommandMode();
  UpdateBattleHUDButtons();
}

void ASkaldPlayerController::HandleRoundStarted(int32 RoundNumber,
                                                ESkaldFaction InitiativeWinner) {
  if (BattleHudWidget) {
    BattleHudWidget->HideInitiativePrompt();
  }
  DetermineControlledBattleSide();

  LastLocalInitiativeRoll = 0;

  LastBattleTurnSoundRound = INDEX_NONE;
  bLastBattleTurnSoundWasAttacker = false;
  LastBattleTurnSoundAvailableCount = INDEX_NONE;

  LockedActiveFighter = nullptr;
  ClearSelectedFighter();
  CancelCommandMode();
  UpdateBattleRoundDisplay(RoundNumber, InitiativeWinner);
  UpdateBattlePlayersTurnDisplay();
  UpdateBattleHUDSelection();
  UpdateBattleHUDButtons();
  RefreshLockedInFighterList();
}

void ASkaldPlayerController::HandleInitiativePhaseStarted(int32 RoundNumber) {
  if (!BattleHudWidget) {
    return;
  }

  bInitiativeRollPresentationShown = false;
  ResetInitiativeDiceSequence();

  if (IsLocalController() && BattleRoundStartSound) {
    const int32 EffectiveRound = RoundNumber > 0 ? RoundNumber : 1;
    if (LastBattleInitiativeSoundRound != EffectiveRound) {
      UGameplayStatics::PlaySound2D(this, BattleRoundStartSound);
      LastBattleInitiativeSoundRound = EffectiveRound;
    }
  }

  LastLocalInitiativeRoll = 0;

  const FText PromptText = NSLOCTEXT("Skald", "BattleInitiativePrompt",
                                     "Roll for initiative");
  BattleHudWidget->ShowInitiativePrompt(PromptText);

  PrimeInitiativeDiceOverview();
}

void ASkaldPlayerController::HandleInitiativeRollCompleted(
    int32 RoundNumber, int32 AttackerRoll, int32 DefenderRoll,
    ESkaldFaction InitiativeWinner) {
  if (!BattleHudWidget) {
    return;
  }

  BattleHudWidget->HideInitiativePrompt();

  bool bShouldTriggerPresentation = true;
  if (bInitiativeRollTriggeredLocally &&
      LastLocalInitiativeAttacker == AttackerRoll &&
      LastLocalInitiativeDefender == DefenderRoll) {
    bShouldTriggerPresentation = false;
  }

  if (bShouldTriggerPresentation) {
    StartInitiativeDiceSequence(AttackerRoll, DefenderRoll);
  }

  bInitiativeRollTriggeredLocally = false;
  LastLocalInitiativeAttacker = INDEX_NONE;
  LastLocalInitiativeDefender = INDEX_NONE;
  ActiveLocalInitiativeRollId.Invalidate();

  DetermineControlledBattleSide();

  USkaldGameInstance *GI = CachedGameInstance;
  if (!GI) {
    GI = GetGameInstance<USkaldGameInstance>();
    CachedGameInstance = GI;
  }

  bool bPlayerWonInitiative = false;
  if (GI && InitiativeWinner != ESkaldFaction::None) {
    const FS_BattlePayload &Battle = GI->PendingBattle;
    if ((bControlsAttackerSide && Battle.AttackerFaction == InitiativeWinner) ||
        (bControlsDefenderSide && Battle.DefenderFaction == InitiativeWinner)) {
      bPlayerWonInitiative = true;
    }
  }

  if (bPlayerWonInitiative && InitiativeWinSound && IsLocalController()) {
    UGameplayStatics::PlaySound2D(this, InitiativeWinSound);
  }

  int32 RollToShow = LastLocalInitiativeRoll;
  if (RollToShow <= 0) {
    RollToShow = AttackerRoll;
    if (!bControlsAttackerSide && bControlsDefenderSide) {
      RollToShow = DefenderRoll;
    } else if (bControlsAttackerSide && bControlsDefenderSide) {
      RollToShow = FMath::Max(AttackerRoll, DefenderRoll);
    }
  }

  if (RollToShow > 0) {
    BattleHudWidget->ShowDiceRoll(RollToShow, 2.f);
  }

  LastLocalInitiativeRoll = 0;
}

void ASkaldPlayerController::HandleInitiativeRollRequested() {
  USkaldGameInstance *GI = CachedGameInstance;
  if (!GI) {
    GI = GetGameInstance<USkaldGameInstance>();
    CachedGameInstance = GI;
  }

  DetermineControlledBattleSide();

  int32 AttackerRequest = INDEX_NONE;
  int32 DefenderRequest = INDEX_NONE;

  if (bControlsAttackerSide) {
    AttackerRequest = 0;
  }

  if (bControlsDefenderSide) {
    DefenderRequest = 0;
  }

  LastLocalInitiativeRoll = 0;
  bInitiativeRollTriggeredLocally = true;
  LastLocalInitiativeAttacker = INDEX_NONE;
  LastLocalInitiativeDefender = INDEX_NONE;
  ActiveLocalInitiativeRollId.Invalidate();

  PrimeInitiativeDiceOverview();

  if (GI && GI->GridBattleManager) {
    GI->GridBattleManager->ConfirmInitiativeRoll(AttackerRequest, DefenderRequest);
  }
}

void ASkaldPlayerController::HandleAttackRollRequested() {
  if (!BattleHudWidget) {
    return;
  }

  if (!BattleHudWidget->IsManualAttackRollPromptActive() ||
      BattleHudWidget->IsManualDiceResolutionActive()) {
    return;
  }

  AFighterPawn *Attacker = BattleHudWidget->GetManualAttackRollAttacker();
  if (!Attacker) {
    return;
  }

  if (BeginManualDiceSequence(Attacker)) {
    return;
  }

  if (HasAuthority()) {
    Attacker->TriggerManualAttackRoll();
  } else {
    ServerTriggerManualAttackRoll(Attacker);
  }
}

void ASkaldPlayerController::HandleStrategicInitiativeRollRequested() {
  bAwaitingStrategicInitiativeRoll = false;

  if (MainHUD) {
    MainHUD->HideStrategicInitiativePrompt();

    const FText RollingText =
        NSLOCTEXT("Skald", "StrategicInitiativeRolling",
                  "Rolling for initiative...");
    MainHUD->UpdateInitiativeText(RollingText.ToString());
  }

  ServerConfirmStrategicInitiativeRollReady();
}

void ASkaldPlayerController::ShowPendingStrategicInitiativeResult() {
  if (!MainHUD || PendingStrategicInitiativeRoll <= 0) {
    return;
  }

  MainHUD->HideStrategicInitiativePrompt();
  MainHUD->ShowStrategicInitiativeRoll(PendingStrategicInitiativeRoll, 2.f);
  MainHUD->SetAwaitingStrategicInitiative(false);

  const int32 EnemyResult =
      PendingStrategicInitiativeEnemyRoll > 0 ? PendingStrategicInitiativeEnemyRoll
                                              : INDEX_NONE;
  PendingStrategicInitiativeRollId.Invalidate();

  ASkald_PlayerCharacter *CameraPawn = Cast<ASkald_PlayerCharacter>(GetPawn());
  if (CameraPawn && !CameraPawn->IsBattleCameraActive() && CameraPawn->BeginStrategicInitiativeCameraView()) {
    bStrategicInitiativeCameraActive = true;
    EnsureDiceManagerBindings();
  }

  const FGuid RollId = TriggerInitiativeDicePresentation(PendingStrategicInitiativeRoll, EnemyResult);

  if (bStrategicInitiativeCameraActive) {
    if (RollId.IsValid()) {
      PendingStrategicInitiativeRollId = RollId;
    } else {
      RestoreStrategicInitiativeCamera();
    }
  }

  if (bPendingStrategicInitiativeWin && InitiativeWinSound && IsLocalController()) {
    UGameplayStatics::PlaySound2D(this, InitiativeWinSound);
  }

  const int32 EffectiveRound = PendingStrategicInitiativeRound > 0
                                   ? PendingStrategicInitiativeRound
                                   : FMath::Max(MainHUD->TurnNumber, 1);
  const FText RoundMessage =
      FText::Format(NSLOCTEXT("Skald", "StrategicRoundStart", "Round {0} begins"),
                    FText::AsNumber(EffectiveRound));
  MainHUD->UpdateInitiativeText(RoundMessage.ToString());

  PendingStrategicInitiativeRoll = 0;
  PendingStrategicInitiativeEnemyRoll = 0;
  PendingStrategicInitiativeRound = 0;
  bPendingStrategicInitiativeWin = false;
}

void ASkaldPlayerController::ClientPromptStrategicInitiative_Implementation(
    int32 RoundNumber, int32 RollValue, bool bWonInitiative) {
  // Reset at the start of every new strategic initiative round
  bInitiativeRollPresentationShown = false;

  PendingStrategicInitiativeRound = RoundNumber;
  PendingStrategicInitiativeRoll = RollValue;
  PendingStrategicInitiativeEnemyRoll = 0;
  bPendingStrategicInitiativeWin = bWonInitiative;
  bAwaitingStrategicInitiativeRoll = true;

  ShowMainHUD();

  if (MainHUD && MainHUD->RoundStartSound) {
    const int32 EffectiveRound = RoundNumber > 0 ? RoundNumber : 1;
    if (LastStrategicInitiativeSoundRound != EffectiveRound) {
      UGameplayStatics::PlaySound2D(this, MainHUD->RoundStartSound);
      LastStrategicInitiativeSoundRound = EffectiveRound;
    }
  }

  if (MainHUD) {
    const FText PromptText = NSLOCTEXT("Skald", "StrategicInitiativePrompt",
                                       "Roll for initiative");
    MainHUD->ShowStrategicInitiativePrompt(PromptText);
    MainHUD->SetAwaitingStrategicInitiative(true);
  }
}

void ASkaldPlayerController::ServerConfirmStrategicInitiativeRollReady_Implementation() {
  if (ASkaldGameMode *GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASkaldGameMode>()
                                            : nullptr) {
    GameMode->ConfirmStrategicInitiativeRoll(this);
  }
}

void ASkaldPlayerController::ClientDisplayStrategicInitiativeResult_Implementation(
    int32 RoundNumber, int32 RollValue, int32 EnemyRoll,
    bool bWonInitiative) {
  PendingStrategicInitiativeRound = RoundNumber;
  PendingStrategicInitiativeRoll = RollValue;
  PendingStrategicInitiativeEnemyRoll = EnemyRoll;
  bPendingStrategicInitiativeWin = bWonInitiative;
  bAwaitingStrategicInitiativeRoll = false;

  ShowMainHUD();
  ShowPendingStrategicInitiativeResult();
}

void ASkaldPlayerController::ClientClearStrategicInitiativeOverlay_Implementation() {
  ShowMainHUD();

  if (MainHUD) {
    MainHUD->SetAwaitingStrategicInitiative(false);
  }
}

void ASkaldPlayerController::RegisterPendingReadyPromptRetry() {
  UWorld *World = GetWorld();
  if (!World) {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("Unable to register ready prompt retry for %s because the world is missing."),
           *GetName());
    return;
  }

  FTimerManager &TimerManager = World->GetTimerManager();
  if (TimerManager.IsTimerActive(PendingReadyPromptRetryHandle)) {
    return;
  }

  UE_LOG(LogSkaldReady, Verbose,
         TEXT("Scheduling prepare-for-battle prompt retry while waiting for HUD on %s."),
         *GetName());

  TimerManager.SetTimer(PendingReadyPromptRetryHandle, this,
                        &ASkaldPlayerController::HandlePendingReadyPromptRetry,
                        0.25f, true);
}

void ASkaldPlayerController::HandlePendingReadyPromptRetry() {
  if (!bPendingReadyPrompt) {
    UE_LOG(LogSkaldReady, Verbose,
           TEXT("Pending ready prompt retry fired for %s with no cached prompt; clearing state."),
           *GetName());
    ResetPendingReadyPromptState();
    return;
  }

  if (!MainHUD) {
    UE_LOG(LogSkaldReady, Verbose,
           TEXT("Pending ready prompt retry awaiting HUD initialization for %s."),
           *GetName());
    InitializeHUDWidget();
    return;
  }

  TryShowPendingReadyPrompt();
}

bool ASkaldPlayerController::TryShowPendingReadyPrompt() {
  if (!bPendingReadyPrompt || !MainHUD) {
    return false;
  }

  if (!ShouldDisplayPrepareForBattlePrompt(PendingReadyPrompt)) {
    UE_LOG(LogSkaldReady, Log,
           TEXT("Discarding cached prepare-for-battle prompt for %s because ready state changed."),
           *GetName());
    ResetPendingReadyPromptState();
    return true;
  }

  const FPrepareForBattlePromptData PromptCopy = PendingReadyPrompt;
  ResetPendingReadyPromptState();
  MainHUD->ShowPrepareForBattleDialog(PromptCopy);
  UE_LOG(LogSkaldReady, Verbose,
         TEXT("Displayed cached prepare-for-battle prompt for %s after HUD became available."),
         *GetName());
  return true;
}

void ASkaldPlayerController::BeginRetreatSelectionLocal(
    int32 DefendingTerritoryID, const TArray<int32> &CandidateTerritoryIDs) {
  ServerSelectTerritory(-1);

  if (!MainHUD) {
    InitializeHUDWidget();
  }

  if (MainHUD) {
    MainHUD->BeginRetreatSelection(DefendingTerritoryID, CandidateTerritoryIDs);
  }

  OnBeginRetreatSelection(DefendingTerritoryID, CandidateTerritoryIDs);
}

void ASkaldPlayerController::CompleteRetreatSelectionLocal() {
  if (MainHUD) {
    MainHUD->CompleteRetreatSelection();
  }
}

void ASkaldPlayerController::NotifyRetreatFailed(const FText &Message) {
  if (!MainHUD) {
    InitializeHUDWidget();
  }

  if (MainHUD) {
    MainHUD->ShowRetreatUnavailableMessage(Message);
  }
}

void ASkaldPlayerController::NotifyEnemyRetreated() {
  if (!MainHUD) {
    InitializeHUDWidget();
  }

  const bool bDisplayedStatus = MainHUD && MainHUD->ShowEnemyRetreatedMessage();

  if (bDisplayedStatus) {
    if (UWorld *World = GetWorld()) {
      FTimerManager &TimerManager = World->GetTimerManager();
      TimerManager.ClearTimer(EnemyRetreatHidePromptHandle);

      const TWeakObjectPtr<ASkaldPlayerController> WeakThis(this);
      FTimerDelegate TimerDelegate;
      TimerDelegate.BindLambda([WeakThis]() {
        if (WeakThis.IsValid()) {
          WeakThis->HidePrepareForBattlePromptLocal();
        }
      });

      TimerManager.SetTimer(EnemyRetreatHidePromptHandle, TimerDelegate, 2.f,
                            false);
    } else {
      HidePrepareForBattlePromptLocal();
    }
  } else {
    HidePrepareForBattlePromptLocal();
  }
}

void ASkaldPlayerController::OnBeginRetreatSelection(
    int32 /*DefendingTerritoryID*/, const TArray<int32> & /*CandidateTerritoryIDs*/) {}

bool ASkaldPlayerController::ShouldDisplayPrepareForBattlePrompt(
    const FPrepareForBattlePromptData &PromptData) {
  ASkaldPlayerState *LocalPS = GetPlayerState<ASkaldPlayerState>();
  if (!LocalPS) {
    UE_LOG(LogSkaldReady, Verbose,
           TEXT("Prepare-for-battle prompt defaulting to display for %s because PlayerState is unavailable."),
           *GetName());
    return true;
  }

  const int32 LocalPlayerID = LocalPS->GetPlayerId();
  if (LocalPlayerID <= 0) {
    UE_LOG(
        LogSkaldReady, Verbose,
        TEXT("Prepare-for-battle prompt proceeding for %s because PlayerID %d is not yet assigned."),
        *GetName(), LocalPlayerID);
    return true;
  }

  const bool bMatchesAttacker = PromptData.AttackerPlayerID == LocalPlayerID;
  const bool bMatchesDefender = PromptData.DefenderPlayerID == LocalPlayerID;

  if (!bMatchesAttacker && !bMatchesDefender) {
    UE_LOG(LogSkaldReady, Warning,
           TEXT("Skipping prepare-for-battle prompt for %s (PlayerID %d) because they are not a participant in the pending battle (Attacker=%d Defender=%d)."),
           *GetName(), LocalPlayerID, PromptData.AttackerPlayerID,
           PromptData.DefenderPlayerID);
    return false;
  }

  ASkaldGameState *GameState = CachedGameState;
  if (!GameState) {
    if (UWorld *World = GetWorld()) {
      GameState = World->GetGameState<ASkaldGameState>();
      if (GameState) {
        CachedGameState = GameState;
      }
    }
  }

  if (!GameState) {
    UE_LOG(LogSkaldReady, Verbose,
           TEXT("Prepare-for-battle prompt proceeding for %s because GameState is unavailable."),
           *GetName());
    return true;
  }

  const FSkaldBattleReadyState &ReadyState = GameState->GetPendingBattleReady();

  if (bMatchesAttacker) {
    const bool bReadyStateHasAttackerId =
        ReadyState.AttackerPlayerID > INDEX_NONE;

    if (!bReadyStateHasAttackerId) {
      UE_LOG(LogSkaldReady, Verbose,
             TEXT("Proceeding with prepare prompt for %s because attacker readiness data is not yet available."),
             *GetName());
    } else {
      if (ReadyState.AttackerPlayerID != LocalPlayerID) {
        UE_LOG(LogSkaldReady, Warning,
               TEXT("Skipping prepare prompt for %s: ready state attacker ID %d no longer matches local PlayerID %d."),
               *GetName(), ReadyState.AttackerPlayerID, LocalPlayerID);
        return false;
      }
      if (ReadyState.bAttackerIsAI) {
        UE_LOG(LogSkaldReady, Verbose,
               TEXT("Skipping prepare prompt for %s because attacker is AI-controlled."),
               *GetName());
        return false;
      }
      if (ReadyState.bAttackerReady) {
        UE_LOG(LogSkaldReady, Verbose,
               TEXT("Skipping prepare prompt for %s because attacker already readied."),
               *GetName());
        return false;
      }
    }
  }

  if (bMatchesDefender) {
    const bool bReadyStateHasDefenderId =
        ReadyState.DefenderPlayerID > INDEX_NONE;

    if (!bReadyStateHasDefenderId) {
      UE_LOG(LogSkaldReady, Verbose,
             TEXT("Proceeding with prepare prompt for %s because defender readiness data is not yet available."),
             *GetName());
    } else {
      if (ReadyState.DefenderPlayerID != LocalPlayerID) {
        UE_LOG(LogSkaldReady, Warning,
               TEXT("Skipping prepare prompt for %s: ready state defender ID %d no longer matches local PlayerID %d."),
               *GetName(), ReadyState.DefenderPlayerID, LocalPlayerID);
        return false;
      }
      if (ReadyState.bDefenderIsAI) {
        UE_LOG(LogSkaldReady, Verbose,
               TEXT("Skipping prepare prompt for %s because defender is AI-controlled."),
               *GetName());
        return false;
      }
      if (ReadyState.bDefenderReady) {
        UE_LOG(LogSkaldReady, Verbose,
               TEXT("Skipping prepare prompt for %s because defender already readied."),
               *GetName());
        return false;
      }
    }
  }

  return true;
}

void ASkaldPlayerController::ResetPendingReadyPromptState() {
  bPendingReadyPrompt = false;
  PendingReadyPrompt = FPrepareForBattlePromptData();

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(PendingReadyPromptRetryHandle);
  }
}

void ASkaldPlayerController::ShowPrepareForBattlePromptLocal(
    const FPrepareForBattlePromptData &PromptData) {
  const bool bShouldDeferLocalAuthorityPrompt = IsLocalController() && HasAuthority();

  if (bShouldDeferLocalAuthorityPrompt) {
    if (UWorld *World = GetWorld()) {
      const FPrepareForBattlePromptData PromptCopy = PromptData;
      World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(
          this, [this, PromptCopy]() {
            ShowPrepareForBattlePromptLocal_Internal(PromptCopy);
          }));
      return;
    }
  }

  ShowPrepareForBattlePromptLocal_Internal(PromptData);
}

void ASkaldPlayerController::ShowPrepareForBattlePromptLocal_Internal(
    const FPrepareForBattlePromptData &PromptData) {
  if (!MainHUD) {
    InitializeHUDWidget();
  }

  ShowMainHUD();

  if (MainHUD) {
    if (!ShouldDisplayPrepareForBattlePrompt(PromptData)) {
      UE_LOG(LogSkaldReady, Log,
             TEXT("Discarding prepare-for-battle prompt for %s; ready state no longer requires confirmation."),
             *GetName());
      ResetPendingReadyPromptState();
      return;
    }

    MainHUD->ShowPrepareForBattleDialog(PromptData);
    UE_LOG(LogSkaldReady, Verbose,
           TEXT("Displayed prepare-for-battle prompt for %s immediately."),
           *GetName());
    ResetPendingReadyPromptState();
    return;
  }

  ResetPendingReadyPromptState();
  PendingReadyPrompt = PromptData;
  bPendingReadyPrompt = true;
  UE_LOG(LogSkaldReady, Verbose,
         TEXT("MainHUD not yet available for %s; caching prepare-for-battle prompt."),
         *GetName());
  RegisterPendingReadyPromptRetry();
}

void ASkaldPlayerController::ClientBeginRetreatSelection_Implementation(
    int32 DefendingTerritoryID, const TArray<int32> &CandidateTerritoryIDs) {
  BeginRetreatSelectionLocal(DefendingTerritoryID, CandidateTerritoryIDs);
}

void ASkaldPlayerController::ClientCompleteRetreat_Implementation() {
  CompleteRetreatSelectionLocal();
}

void ASkaldPlayerController::ClientRetreatFailed_Implementation(
    const FText &Message) {
  NotifyRetreatFailed(Message);
}

void ASkaldPlayerController::ClientEnemyRetreated_Implementation() {
  NotifyEnemyRetreated();
}

void ASkaldPlayerController::ClientShowPrepareForBattle_Implementation(
    const FPrepareForBattlePromptData &PromptData) {
  ShowPrepareForBattlePromptLocal(PromptData);
}

void ASkaldPlayerController::HidePrepareForBattlePromptLocal() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(EnemyRetreatHidePromptHandle);
  }

  if (MainHUD) {
    MainHUD->HidePrepareForBattleDialog();
  }

  if (bPendingReadyPrompt) {
    UE_LOG(LogSkaldReady, Verbose,
           TEXT("Clearing cached prepare-for-battle prompt for %s due to hide notification."),
           *GetName());
  }

  ResetPendingReadyPromptState();
}

void ASkaldPlayerController::ClientHidePrepareForBattle_Implementation() {
  HidePrepareForBattlePromptLocal();
}

void ASkaldPlayerController::CancelCommandMode() {
  CurrentCommandMode = EBattleCommandMode::None;
  PendingVeilStepSlot.Reset();
  PendingVeilStepFighter.Reset();
  PendingAbilityCommand.Reset();
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    Grid->ClearHighlights();
  }
  if (BattleHudWidget) {
    BattleHudWidget->ClearCommandPreviews();
  }
}

void ASkaldPlayerController::CancelAbilityCommand() {
  const bool bWasAbilityCommand =
      CurrentCommandMode == EBattleCommandMode::AbilityTargetEnemy ||
      CurrentCommandMode == EBattleCommandMode::AbilityTargetAlly ||
      CurrentCommandMode == EBattleCommandMode::AbilityTargetCell;

  PendingAbilityCommand.Reset();

  if (!bWasAbilityCommand) {
    return;
  }

  CurrentCommandMode = EBattleCommandMode::None;

  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    Grid->ClearHighlights();
  }
}

void ASkaldPlayerController::HighlightClickedCell(UGridOverlayComponent *Grid,
                                                  const FIntPoint &Cell) {
  if (!Grid || !Grid->IsCellInBounds(Cell)) {
    return;
  }

  bool bRestoredCommandHighlights = false;
  if (CurrentCommandMode == EBattleCommandMode::Move) {
    if (LockedActiveFighter && IsFriendlyFighter(LockedActiveFighter)) {
      Grid->HighlightMovement(LockedActiveFighter);
      bRestoredCommandHighlights = true;
    }
  } else if (CurrentCommandMode == EBattleCommandMode::Disengage) {
    if (LockedActiveFighter && IsFriendlyFighter(LockedActiveFighter) &&
        LockedActiveFighter->IsEngaged()) {
      Grid->HighlightDisengage(LockedActiveFighter,
                               LockedActiveFighter->GetDisengageRange());
      bRestoredCommandHighlights = true;
    }
  } else if (CurrentCommandMode == EBattleCommandMode::Attack) {
    if (LockedActiveFighter && IsFriendlyFighter(LockedActiveFighter)) {
      Grid->HighlightAttack(LockedActiveFighter);
      bRestoredCommandHighlights = true;
    }
  } else if (CurrentCommandMode == EBattleCommandMode::AbilityTargetEnemy ||
             CurrentCommandMode == EBattleCommandMode::AbilityTargetAlly ||
             CurrentCommandMode == EBattleCommandMode::AbilityTargetCell) {
    if (PendingAbilityCommand.IsSet()) {
      const FPendingAbilityCommand &Command = PendingAbilityCommand.GetValue();
      if (Command.SourceFighter.IsValid()) {
        HighlightAbilityCommandOptions(Command, Grid);
        bRestoredCommandHighlights = true;
      }
    }
  }

  if (!bRestoredCommandHighlights) {
    Grid->ClearHighlights();
  }

  Grid->HighlightCell(Cell, Grid->SelectionHighlightColor.ToFColor(true), 0.f,
                      false);
}

void ASkaldPlayerController::SetSelectedFighter(AFighterPawn *Fighter,
                                                bool bForce) {
  AFighterPawn *PreviousSelection = SelectedFighter;
  if (!bForce && PreviousSelection == Fighter)
    return;

  if (PreviousSelection && PreviousSelection != Fighter) {
    PreviousSelection->SetSelectionIndicatorVisible(false);
  }

  SelectedFighter = Fighter;

  if (SelectedFighter) {
    SelectedFighter->SetSelectionIndicatorVisible(true);
  }

  OnSelectedFighterChanged.Broadcast(SelectedFighter);

  UpdateBattleHUDSelection();
  UpdateBattleHUDButtons();
  UpdateLockedInSelectionHighlight();
}

void ASkaldPlayerController::ClearSelectedFighter() {
  if (!SelectedFighter)
    return;

  if (SelectedFighter) {
    SelectedFighter->SetSelectionIndicatorVisible(false);
  }
  SelectedFighter = nullptr;
  OnSelectedFighterChanged.Broadcast(nullptr);
  UpdateBattleHUDSelection();
  UpdateBattleHUDButtons();
  UpdateLockedInSelectionHighlight();
}

void ASkaldPlayerController::UpdateBattleHUDSelection() {
  if (!BattleHudWidget)
    return;

  BattleHudWidget->BindToFighter(SelectedFighter);
  if (SelectedFighter) {
    BattleHudWidget->SetSelectedFighterName(
        FText::FromName(SelectedFighter->GetFighterId()));
  } else {
    BattleHudWidget->SetSelectedFighterName(FText::GetEmpty());
  }
}

void ASkaldPlayerController::UpdateBattleHUDButtons() {
  if (!BattleHudWidget)
    return;

  bool bCanActivate = false;
  bool bAwaitingInitiative = false;
  if (!CachedGameInstance) {
    CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  }
  UGridBattleManager *BattleManager =
      CachedGameInstance ? CachedGameInstance->GridBattleManager : nullptr;
  if (BattleManager) {
    bAwaitingInitiative = BattleManager->IsAwaitingInitiativeRoll();
  }

  if (!bAwaitingInitiative && SelectedFighter && !LockedActiveFighter &&
      IsFriendlyFighter(SelectedFighter) && BattleManager) {
    bCanActivate = BattleManager->CanActivateFighter(SelectedFighter);
  }

  BattleHudWidget->SetActivateEnabled(bCanActivate);
  BattleHudWidget->SetActivateVisibility(bCanActivate);
  const bool bHasActiveFighter = LockedActiveFighter != nullptr;
  const bool bHasFriendlyActive =
      LockedActiveFighter && IsFriendlyFighter(LockedActiveFighter);
  const bool bActiveFighterSelected =
      bHasFriendlyActive && LockedActiveFighter == SelectedFighter;
  const bool bShouldShowActionButtons =
      bActiveFighterSelected && LockedActiveFighter &&
      LockedActiveFighter->ActionsRemaining > 0;
  BattleHudWidget->SetActionButtonsVisibility(bShouldShowActionButtons);
  BattleHudWidget->SetEndTurnVisibility(bHasFriendlyActive);
  BattleHudWidget->SetEndTurnEnabled(bHasFriendlyActive);
}

void ASkaldPlayerController::UpdateBattleRoundDisplay(
    int32 RoundNumber, ESkaldFaction InitiativeWinner) {
  if (!BattleHudWidget)
    return;

  const FText RoundText = FText::Format(
      NSLOCTEXT("Skald", "BattleRound", "Round {0}"), FText::AsNumber(RoundNumber));

  FText InitiativeText = NSLOCTEXT("Skald", "BattleInitiativeNone",
                                   "Initiative: None");
  if (InitiativeWinner != ESkaldFaction::None) {
    if (const UEnum *FactionEnum = StaticEnum<ESkaldFaction>()) {
      const FText WinnerText =
          FactionEnum->GetDisplayNameTextByValue(static_cast<int64>(InitiativeWinner));
      InitiativeText = FText::Format(
          NSLOCTEXT("Skald", "BattleInitiative", "Initiative: {0}"),
          WinnerText);
    }
  }

  BattleHudWidget->SetRoundInfo(RoundText, InitiativeText);
}

void ASkaldPlayerController::UpdateBattleTerritoryLabel() {
  if (!BattleHudWidget) {
    return;
  }

  USkaldGameInstance *GI = CachedGameInstance;
  if (!GI) {
    GI = GetGameInstance<USkaldGameInstance>();
    CachedGameInstance = GI;
  }

  FString TerritoryName;
  int32 TargetTerritoryId = 0;

  if (GI) {
    const FS_BattlePayload &Battle = GI->PendingBattle;
    TargetTerritoryId = Battle.TargetTerritoryID;
    if (!Battle.DefenderTerritoryName.IsEmpty()) {
      TerritoryName = Battle.DefenderTerritoryName;
    }
  }

  auto ResolveFromSnapshots = [&](const TArray<FS_Territory> &Snapshots) {
    if (TerritoryName.IsEmpty() && TargetTerritoryId > 0) {
      for (const FS_Territory &Snapshot : Snapshots) {
        if (Snapshot.TerritoryID == TargetTerritoryId &&
            !Snapshot.TerritoryName.IsEmpty()) {
          TerritoryName = Snapshot.TerritoryName;
          return true;
        }
      }
    }
    return false;
  };

  if (TerritoryName.IsEmpty() && TargetTerritoryId > 0 && GI) {
    const TArray<FS_Territory> &PendingSnapshot = GI->GetPendingTravelSnapshot();
    if (!ResolveFromSnapshots(PendingSnapshot)) {
      if (!ResolveFromSnapshots(GI->CachedWorldMapTerritories)) {
        ResolveFromSnapshots(GI->GetTravelState().CachedTerritories);
      }
    }
  }

  BattleHudWidget->SetTerritoryName(TerritoryName.IsEmpty()
                                        ? FText::GetEmpty()
                                        : FText::FromString(TerritoryName));
}

void ASkaldPlayerController::UpdateBattlePlayersTurnDisplay() {
  if (!BattleHudWidget)
    return;

  USkaldGameInstance *GI = CachedGameInstance;
  if (!GI) {
    GI = GetGameInstance<USkaldGameInstance>();
    CachedGameInstance = GI;
  }

  UpdateBattleTerritoryLabel();

  if (!GI) {
    BattleHudWidget->SetPlayersTurnLabel(FText::GetEmpty());
    return;
  }

  bool bAttackerTurn = true;
  if (GI->GridBattleManager) {
    bAttackerTurn = GI->GridBattleManager->IsAttackerTurn();
  } else if (LockedActiveFighter) {
    bAttackerTurn = LockedActiveFighter->bIsAttacker;
  }

  const FS_BattlePayload &Battle = GI->PendingBattle;
  FString PlayerName =
      bAttackerTurn ? Battle.AttackerDisplayName : Battle.DefenderDisplayName;
  const int32 PlayerId =
      bAttackerTurn ? Battle.AttackerPlayerID : Battle.DefenderPlayerID;

  if (PlayerName.IsEmpty()) {
    ASkaldGameState *GameState = CachedGameState;
    if (!GameState && GetWorld()) {
      GameState = GetWorld()->GetGameState<ASkaldGameState>();
      CachedGameState = GameState;
    }
    if (GameState) {
      if (ASkaldPlayerState *PS = GameState->GetPlayerById(PlayerId)) {
        PlayerName =
            ResolvePlayerName(PS, TEXT("BattleHUD_PlayerTurnDisplay"));
      }
    }
  }

  if (PlayerName.IsEmpty()) {
    PlayerName = bAttackerTurn ? TEXT("Attackers") : TEXT("Defenders");
  }

  const FText Label = PlayerName.IsEmpty()
                           ? FText::GetEmpty()
                           : FText::Format(
                                 NSLOCTEXT("Skald", "BattlePlayersTurnLabel",
                                           "{0}'s Turn"),
                                 FText::FromString(PlayerName));
  BattleHudWidget->SetPlayersTurnLabel(Label);
}

void ASkaldPlayerController::PlayAttackFeedback(
    AFighterPawn *Attacker, AFighterPawn *Defender,
    const FDiceRollResult &Result) {
  if (!Defender) {
    return;
  }

  if (Result.DiceOutcomes.Num() > 0) {
    // Dice resolution will trigger individual outcome feedback; no aggregate
    // cue is necessary here to avoid double-playing effects.
    return;
  }

  FDiceRollOutcome SyntheticOutcome;
  SyntheticOutcome.bHit = Result.TotalDamage > 0;
  SyntheticOutcome.bCritical = Result.CriticalHitCount > 0;
  if (Attacker) {
    Attacker->TriggerAttackPresentationFX(Defender);
  }
  PlayDiceOutcomeFeedback(Attacker, Defender, SyntheticOutcome);
}

void ASkaldPlayerController::PlayDiceOutcomeFeedback(
    AFighterPawn *Attacker, AFighterPawn *Defender,
    const FDiceRollOutcome &Outcome) {
  if (!Defender) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  const FVector ImpactLocation =
      Defender->GetActorLocation() + FVector(0.f, 0.f, 120.f);
  const FRotator ImpactRotation = Attacker
                                      ? (Defender->GetActorLocation() -
                                         Attacker->GetActorLocation())
                                            .Rotation()
                                      : FRotator::ZeroRotator;

  if (Outcome.RollValue == 6) {
    USoundBase *NaturalSixSound = nullptr;
    UNiagaraSystem *NaturalSixEffect = nullptr;
    if (Attacker && Attacker->Faction != ESkaldFaction::None) {
      if (USoundBase **FactionSound =
              NaturalSixFactionSounds.Find(Attacker->Faction)) {
        NaturalSixSound = *FactionSound;
      }
      if (UNiagaraSystem **FactionEffect =
              NaturalSixFactionEffects.Find(Attacker->Faction)) {
        NaturalSixEffect = *FactionEffect;
      }
    }

    if (!NaturalSixSound) {
      NaturalSixSound = DefaultNaturalSixSound;
    }

    if (!NaturalSixEffect) {
      NaturalSixEffect = DefaultNaturalSixEffect;
    }

    if (NaturalSixEffect) {
      UNiagaraFunctionLibrary::SpawnSystemAtLocation(
          World, NaturalSixEffect, ImpactLocation, ImpactRotation);
    }

    if (UNiagaraSystem *NaturalSixDecalEffect =
            ResolveNaturalSixDecalEffect(Attacker ? Attacker->Faction
                                                  : ESkaldFaction::None)) {
      FVector DecalLocation = Defender->GetActorLocation();
      if (UGridOverlayComponent *Grid = Defender->GetGrid()) {
        const FIntPoint AnchorCell = Defender->GetCurrentCell();
        DecalLocation = Grid->GridToWorld(AnchorCell);
      }
      DecalLocation.Z += NaturalSixDecalHeightOffset;
      SpawnTimedNiagaraSystem(NaturalSixDecalEffect, DecalLocation,
                              NaturalSixDecalLifetimeSeconds,
                              NaturalSixDecalScale);
    }

    if (NaturalSixSound) {
      UGameplayStatics::PlaySoundAtLocation(this, NaturalSixSound,
                                            ImpactLocation);
    }
  }

  if (Outcome.bHit) {
    if (HitImpactEffect) {
      UNiagaraFunctionLibrary::SpawnSystemAtLocation(
          World, HitImpactEffect, ImpactLocation, ImpactRotation);
    }
    if (UNiagaraSystem *HitDecalEffect =
            ResolveHitDecalEffect(Defender ? Defender->Faction
                                           : ESkaldFaction::None)) {
      FVector DecalLocation = Defender->GetActorLocation();
      if (UGridOverlayComponent *Grid = Defender->GetGrid()) {
        const FIntPoint AnchorCell = Defender->GetCurrentCell();
        DecalLocation = Grid->GridToWorld(AnchorCell);
      }
      DecalLocation.Z += HitDecalHeightOffset;
      SpawnTimedNiagaraSystem(HitDecalEffect, DecalLocation,
                              HitDecalLifetimeSeconds, HitDecalScale);
    }
    if (HitImpactSound) {
      UGameplayStatics::PlaySoundAtLocation(this, HitImpactSound,
                                            ImpactLocation);
    }
  } else {
    if (MissImpactEffect) {
      UNiagaraFunctionLibrary::SpawnSystemAtLocation(
          World, MissImpactEffect, ImpactLocation, ImpactRotation);
    }
    if (MissImpactSound) {
      UGameplayStatics::PlaySoundAtLocation(this, MissImpactSound,
                                            ImpactLocation);
    }
  }
}

void ASkaldPlayerController::TriggerFighterDeathFeedback(AFighterPawn *Fighter) {
  if (!Fighter) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  if (UNiagaraSystem *DeathEffect =
          ResolveFighterDeathEffect(Fighter->Faction)) {
    const FVector EffectLocation =
        Fighter->GetActorLocation() +
        FVector(0.f, 0.f, FighterDeathEffectHeightOffset);
    const FRotator EffectRotation = Fighter->GetActorRotation();
    UNiagaraFunctionLibrary::SpawnSystemAtLocation(
        World, DeathEffect, EffectLocation, EffectRotation);
  }

  if (UNiagaraSystem *SplatterEffect =
          ResolveFighterDeathSplatterEffect(Fighter->Faction)) {
    FVector SplatterLocation = Fighter->GetActorLocation();
    if (UGridOverlayComponent *Grid = Fighter->GetGrid()) {
      const FIntPoint AnchorCell = Fighter->GetCurrentCell();
      SplatterLocation = Grid->GridToWorld(AnchorCell);
    }
    SplatterLocation.Z += FighterDeathSplatterHeightOffset;
    SpawnTimedNiagaraSystem(SplatterEffect, SplatterLocation,
                            FighterDeathSplatterLifetimeSeconds,
                            FighterDeathSplatterScale);
  }
}

UNiagaraSystem *
ASkaldPlayerController::ResolveFighterDeathEffect(ESkaldFaction Faction) const {
  if (Faction != ESkaldFaction::None) {
    if (UNiagaraSystem *const *FactionEffect =
            FighterDeathFactionEffects.Find(Faction)) {
      if (*FactionEffect) {
        return *FactionEffect;
      }
    }
  }

  return DefaultFighterDeathEffect;
}

UNiagaraSystem *ASkaldPlayerController::ResolveFighterDeathSplatterEffect(
    ESkaldFaction Faction) const {
  if (Faction != ESkaldFaction::None) {
    if (UNiagaraSystem *const *FactionEffect =
            FighterDeathSplatterFactionEffects.Find(Faction)) {
      if (*FactionEffect) {
        return *FactionEffect;
      }
    }
  }

  return DefaultFighterDeathSplatterEffect;
}

UNiagaraSystem *
ASkaldPlayerController::ResolveHitDecalEffect(ESkaldFaction Faction) const {
  if (Faction != ESkaldFaction::None) {
    if (UNiagaraSystem *const *FactionEffect =
            HitDecalFactionEffects.Find(Faction)) {
      if (*FactionEffect) {
        return *FactionEffect;
      }
    }
  }

  return DefaultHitDecalEffect;
}

UNiagaraSystem *ASkaldPlayerController::ResolveNaturalSixDecalEffect(
    ESkaldFaction Faction) const {
  if (Faction != ESkaldFaction::None) {
    if (UNiagaraSystem *const *FactionEffect =
            NaturalSixDecalFactionEffects.Find(Faction)) {
      if (*FactionEffect) {
        return *FactionEffect;
      }
    }
  }

  return DefaultNaturalSixDecalEffect;
}

void ASkaldPlayerController::SpawnTimedNiagaraSystem(UNiagaraSystem *Effect,
                                                     const FVector &Location,
                                                     float LifetimeSeconds,
                                                     const FVector &Scale) {
  if (!Effect || LifetimeSeconds <= 0.f) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  UNiagaraComponent *NiagaraComponent =
      UNiagaraFunctionLibrary::SpawnSystemAtLocation(
          World, Effect, Location, FRotator::ZeroRotator, Scale,
          /*bAutoDestroy=*/false, /*bAutoActivate=*/true,
          ENCPoolMethod::ManualRelease);
  if (!NiagaraComponent) {
    return;
  }

  NiagaraComponent->SetAutoDestroy(false);

  const TWeakObjectPtr<UNiagaraComponent> ComponentPtr(NiagaraComponent);

  FTimerDelegate CleanupDelegate;
  CleanupDelegate.BindLambda([ComponentPtr]() {
    if (UNiagaraComponent *Component = ComponentPtr.Get()) {
      Component->Deactivate();
      Component->DestroyComponent();
    }
  });

  FTimerHandle CleanupHandle;
  World->GetTimerManager().SetTimer(CleanupHandle, CleanupDelegate,
                                    LifetimeSeconds, false);
}

ASkaldPlayerController::FPendingDiceFeedbackState *
ASkaldPlayerController::FindPendingDiceFeedbackState(AFighterPawn *Attacker,
                                                     AFighterPawn *Defender) {
  for (int32 Index = 0; Index < PendingDiceFeedbackStates.Num();) {
    FPendingDiceFeedbackState &State = PendingDiceFeedbackStates[Index];
    AFighterPawn *StateAttacker = State.Attacker.Get();
    AFighterPawn *StateDefender = State.Defender.Get();

    if (!StateDefender) {
      PendingDiceFeedbackStates.RemoveAt(Index);
      continue;
    }

    const bool bAttackerMatches =
        (!Attacker && !StateAttacker) || StateAttacker == Attacker ||
        !StateAttacker;
    const bool bDefenderMatches =
        (!Defender && !StateDefender) || StateDefender == Defender;
    if (bAttackerMatches && bDefenderMatches) {
      return &State;
    }

    ++Index;
  }

  return nullptr;
}

void ASkaldPlayerController::RemovePendingDiceFeedbackState(
    AFighterPawn *Attacker, AFighterPawn *Defender) {
  for (int32 Index = 0; Index < PendingDiceFeedbackStates.Num(); ++Index) {
    FPendingDiceFeedbackState &State = PendingDiceFeedbackStates[Index];
    AFighterPawn *StateAttacker = State.Attacker.Get();
    AFighterPawn *StateDefender = State.Defender.Get();
    if (!StateDefender) {
      PendingDiceFeedbackStates.RemoveAt(Index);
      --Index;
      continue;
    }

    const bool bAttackerMatches =
        (!Attacker && !StateAttacker) || StateAttacker == Attacker ||
        !StateAttacker;
    const bool bDefenderMatches =
        (!Defender && !StateDefender) || StateDefender == Defender;

    if (bAttackerMatches && bDefenderMatches) {
      PendingDiceFeedbackStates.RemoveAt(Index);
      return;
    }

    if (!StateAttacker && !StateDefender) {
      PendingDiceFeedbackStates.RemoveAt(Index);
      --Index;
    }
  }
}

void ASkaldPlayerController::TriggerHighStakesCritFeedback(
    AFighterPawn *Attacker, AFighterPawn *Defender,
    const FDiceRollResult &Result) {
  if (!Result.bHighStakesCritical || !Defender) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  UNiagaraSystem *ResolvedEffect = nullptr;
  USoundBase *ResolvedSound = nullptr;
  if (Result.HighStakesFaction != ESkaldFaction::None) {
    if (UNiagaraSystem **FactionEffect =
            HighStakesCriticalFactionEffects.Find(Result.HighStakesFaction)) {
      ResolvedEffect = *FactionEffect;
    }
    if (USoundBase **FactionSound =
            HighStakesCriticalFactionSounds.Find(Result.HighStakesFaction)) {
      ResolvedSound = *FactionSound;
    }
  }

  if (!ResolvedEffect) {
    ResolvedEffect = DefaultHighStakesCriticalEffect;
  }

  if (!ResolvedSound) {
    ResolvedSound = DefaultHighStakesCriticalSound;
  }

  if (!ResolvedEffect && !ResolvedSound) {
    return;
  }

  const FVector EffectLocation =
      Defender->GetActorLocation() + FVector(0.f, 0.f, 160.f);
  const FRotator EffectRotation = Attacker
                                      ? (Defender->GetActorLocation() -
                                         Attacker->GetActorLocation())
                                            .Rotation()
                                      : FRotator::ZeroRotator;

  if (ResolvedEffect) {
    UNiagaraFunctionLibrary::SpawnSystemAtLocation(
        World, ResolvedEffect, EffectLocation, EffectRotation);
  }

  if (ResolvedSound) {
    UGameplayStatics::PlaySoundAtLocation(this, ResolvedSound,
                                          EffectLocation);
  }
}

void ASkaldPlayerController::HandleAttackResolved(AFighterPawn *Attacker,
                                                  AFighterPawn *Defender,
                                                  const FDiceRollResult &Result) {
  PlayAttackFeedback(Attacker, Defender, Result);

  if (IsLocalController() && bAutoPresentDiceRolls &&
      Result.DiceOutcomes.Num() > 0) {
    if (PendingManualSequence.bActive) {
      PendingManualSequence.Defender = Defender;
      PendingManualSequence.Result = Result;
      PendingManualSequence.bHasResult = true;
      TryCompleteManualDiceSequence();
      return;
    }

    StartAttackDiceSequence(Attacker, Defender, Result);
    return;
  }

  TriggerAttackDicePresentation(Attacker, Defender, Result);
  ProcessAttackResolutionPresentation(Attacker, Defender, Result);
}

void ASkaldPlayerController::ProcessAttackResolutionPresentation(
    AFighterPawn *Attacker, AFighterPawn *Defender,
    const FDiceRollResult &Result) {
  if (Defender) {
    FPendingDiceFeedbackState &FeedbackState =
        PendingDiceFeedbackStates.AddDefaulted_GetRef();
    FeedbackState.Attacker = Attacker;
    FeedbackState.Defender = Defender;
    FeedbackState.Result = Result;
    FeedbackState.SimulatedDefenderHealth = Result.StartingHealth;
    FeedbackState.NextRevealIndex = 0;
    FeedbackState.bTriggeredDeathFeedback = false;
    FeedbackState.bTriggeredHighStakesFeedback = false;
  }

  if (!BattleHudWidget) {
    if (MainHUD) {
      MainHUD->QueueDiceResolution(Attacker, Defender, Result);
    }
    return;
  }

  if (Defender) {
    Defender->HoldHealthDisplay(Result.StartingHealth);
  }

  BattleHudWidget->QueueDiceResolution(Attacker, Defender, Result,
                                       /*bManualReveal*/ false);
  if (MainHUD) {
    MainHUD->QueueDiceResolution(Attacker, Defender, Result);
  }

  RefreshLockedInFighterList();
}

bool ASkaldPlayerController::BeginManualDiceSequence(AFighterPawn *Attacker) {
  if (!IsLocalController() || !bAutoPresentDiceRolls || !Attacker) {
    return false;
  }

  ASkald_PlayerCharacter *CameraPawn =
      Cast<ASkald_PlayerCharacter>(GetPawn());
  const bool bHasBattleCamera =
      CameraPawn && bIsBattleMap && CameraPawn->IsBattleCameraActive();
  if (!bHasBattleCamera) {
    return false;
  }

  ResetManualDiceSequence();

  PendingManualSequence.bActive = true;
  PendingManualSequence.bHadBattleCamera = true;
  PendingManualSequence.Attacker = Attacker;
  PendingManualSequence.Defender.Reset();
  PendingManualSequence.Result = FDiceRollResult();
  PendingManualSequence.bTriggerServerRoll = !HasAuthority();
  PendingManualSequence.bAwaitingRollId = false;
  PendingManualSequence.bAwaitingRollCompletion = false;
  PendingManualSequence.bHasResult = false;
  PendingManualSequence.bPendingResolutionDispatch = false;
  PendingManualSequence.ActiveRollId.Invalidate();

  PendingManualSequence.OriginalLocation = CameraPawn->GetActorLocation();
  PendingManualSequence.OriginalRotation =
      CameraPawn->GetCurrentBattleCameraRotation();
  PendingManualSequence.OriginalZoom =
      CameraPawn->GetCurrentBattleCameraZoom();
  PendingManualSequence.OriginalLockTarget =
      CameraPawn->GetCurrentBattleCameraLockTarget();

  CameraPawn->ClearCameraFocus();

  FVector OverviewLocation = PendingManualSequence.OriginalLocation;
  FRotator OverviewRotation = PendingManualSequence.OriginalRotation;
  float OverviewZoom = 3000.f;
  if (!ComputeBattlefieldOverviewTransform(
          PendingManualSequence.OriginalRotation.Yaw, OverviewLocation,
          OverviewRotation, OverviewZoom)) {
    ResetManualDiceSequence();
    return false;
  }

  const float OverviewDuration = 0.65f;
  CameraPawn->StartCameraTransition(OverviewLocation, OverviewRotation,
                                    OverviewZoom, OverviewDuration);

  if (UWorld *World = GetWorld()) {
    FTimerDelegate OverviewDelegate = FTimerDelegate::CreateUObject(
        this, &ASkaldPlayerController::HandleManualDiceOverviewReached);
    World->GetTimerManager().SetTimer(
        PendingManualSequence.OverviewTimerHandle, OverviewDelegate,
        OverviewDuration, /*bLoop*/ false);
  } else {
    HandleManualDiceOverviewReached();
  }

  return true;
}

void ASkaldPlayerController::HandleManualDiceOverviewReached() {
  if (!PendingManualSequence.bActive) {
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(
        PendingManualSequence.OverviewTimerHandle);
  }

  AFighterPawn *Attacker = PendingManualSequence.Attacker.Get();
  if (!Attacker) {
    ResetManualDiceSequence();
    return;
  }

  if (HasAuthority()) {
    Attacker->NotifyAIAttackPresentationReady();
  } else {
    ServerNotifyAIAttackOverviewComplete(Attacker);
  }

  EnsureDiceManagerBindings();

  PendingManualSequence.bAwaitingRollCompletion = true;
  PendingManualSequence.bAwaitingRollId = true;
  PendingManualSequence.ActiveRollId.Invalidate();

  if (PendingManualSequence.bTriggerServerRoll) {
    ServerTriggerManualAttackRoll(Attacker);
  } else {
    Attacker->TriggerManualAttackRoll();
  }
}

void ASkaldPlayerController::HandleManualDiceCleanupFinished() {
  if (!PendingManualSequence.bActive) {
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(
        PendingManualSequence.CleanupDelayHandle);
  }

  ASkald_PlayerCharacter *CameraPawn =
      Cast<ASkald_PlayerCharacter>(GetPawn());
  if (!CameraPawn || !PendingManualSequence.bHadBattleCamera) {
    HandleManualDiceReturnComplete();
    return;
  }

  const float ReturnDuration = 0.6f;
  CameraPawn->StartCameraTransition(PendingManualSequence.OriginalLocation,
                                    PendingManualSequence.OriginalRotation,
                                    PendingManualSequence.OriginalZoom,
                                    ReturnDuration);

  if (UWorld *World = GetWorld()) {
    FTimerDelegate ReturnDelegate = FTimerDelegate::CreateUObject(
        this, &ASkaldPlayerController::HandleManualDiceReturnComplete);
    World->GetTimerManager().SetTimer(
        PendingManualSequence.ReturnTimerHandle, ReturnDelegate,
        ReturnDuration, /*bLoop*/ false);
  } else {
    HandleManualDiceReturnComplete();
  }
}

void ASkaldPlayerController::HandleManualDiceReturnComplete() {
  if (!PendingManualSequence.bActive) {
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(
        PendingManualSequence.ReturnTimerHandle);
  }

  ASkald_PlayerCharacter *CameraPawn =
      Cast<ASkald_PlayerCharacter>(GetPawn());
  if (CameraPawn) {
    if (PendingManualSequence.Attacker.IsValid()) {
      CameraPawn->FocusCameraOnActor(PendingManualSequence.Attacker.Get());
    } else if (PendingManualSequence.OriginalLockTarget.IsValid()) {
      CameraPawn->FocusCameraOnActor(
          PendingManualSequence.OriginalLockTarget.Get());
    }
  }

  PendingManualSequence.bPendingResolutionDispatch = true;
  TryCompleteManualDiceSequence();
}

void ASkaldPlayerController::HandleDiceRollStarted(const FGuid &RollId) {
  if (!PendingManualSequence.bActive || !PendingManualSequence.bAwaitingRollId) {
    return;
  }

  PendingManualSequence.ActiveRollId = RollId;
  PendingManualSequence.bAwaitingRollId = false;
}

void ASkaldPlayerController::TryCompleteManualDiceSequence() {
  if (!PendingManualSequence.bActive ||
      !PendingManualSequence.bPendingResolutionDispatch ||
      !PendingManualSequence.bHasResult) {
    return;
  }

  ProcessAttackResolutionPresentation(PendingManualSequence.Attacker.Get(),
                                      PendingManualSequence.Defender.Get(),
                                      PendingManualSequence.Result);
  ResetManualDiceSequence();
}

void ASkaldPlayerController::ResetManualDiceSequence() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(
        PendingManualSequence.OverviewTimerHandle);
    World->GetTimerManager().ClearTimer(
        PendingManualSequence.CleanupDelayHandle);
    World->GetTimerManager().ClearTimer(
        PendingManualSequence.ReturnTimerHandle);
  }

  PendingManualSequence = FPendingManualDiceSequence();
}

void ASkaldPlayerController::StartAttackDiceSequence(
    AFighterPawn *Attacker, AFighterPawn *Defender,
    const FDiceRollResult &Result) {
  ResetAttackDiceSequence();

  if (!IsLocalController() || !bAutoPresentDiceRolls ||
      Result.DiceOutcomes.Num() == 0) {
    TriggerAttackDicePresentation(Attacker, Defender, Result);
    ProcessAttackResolutionPresentation(Attacker, Defender, Result);
    return;
  }

  EnsureDiceManagerBindings();

  ASkald_PlayerCharacter *CameraPawn = Cast<ASkald_PlayerCharacter>(GetPawn());
  const bool bHasBattleCamera =
      CameraPawn && bIsBattleMap && CameraPawn->IsBattleCameraActive();

  if (!bHasBattleCamera) {
    FGuid RollId = TriggerAttackDicePresentation(Attacker, Defender, Result);
    if (!RollId.IsValid()) {
      ProcessAttackResolutionPresentation(Attacker, Defender, Result);
    }
    return;
  }

  PendingAttackSequence.bActive = true;
  PendingAttackSequence.Attacker = Attacker;
  PendingAttackSequence.Defender = Defender;
  PendingAttackSequence.Result = Result;
  PendingAttackSequence.PhysicalRollResults.Reset();
  PendingAttackSequence.bHasPhysicalResults = false;
  PendingAttackSequence.AttackerSnapshot =
      Attacker ? Attacker->Stats : FFighterStats();
  PendingAttackSequence.DefenderSnapshot =
      Defender ? Defender->Stats : FFighterStats();
  PendingAttackSequence.DefenderSnapshot.Health = Result.StartingHealth;
  PendingAttackSequence.bHadBattleCamera = true;
  PendingAttackSequence.OriginalLocation = CameraPawn->GetActorLocation();
  PendingAttackSequence.OriginalRotation =
      CameraPawn->GetCurrentBattleCameraRotation();
  PendingAttackSequence.OriginalZoom = CameraPawn->GetCurrentBattleCameraZoom();
  PendingAttackSequence.OriginalLockTarget =
      CameraPawn->GetCurrentBattleCameraLockTarget();
  PendingAttackSequence.ActiveRollId.Invalidate();

  CameraPawn->ClearCameraFocus();

  FVector OverviewLocation = PendingAttackSequence.OriginalLocation;
  FRotator OverviewRotation = PendingAttackSequence.OriginalRotation;
  float OverviewZoom = 3000.f;
  if (!ComputeBattlefieldOverviewTransform(
          PendingAttackSequence.OriginalRotation.Yaw, OverviewLocation,
          OverviewRotation, OverviewZoom)) {
    ResetAttackDiceSequence();
    FGuid RollId = TriggerAttackDicePresentation(Attacker, Defender, Result);
    if (!RollId.IsValid()) {
      ProcessAttackResolutionPresentation(Attacker, Defender, Result);
    }
    return;
  }

  const float OverviewDuration = 0.65f;
  CameraPawn->StartCameraTransition(OverviewLocation, OverviewRotation,
                                    OverviewZoom, OverviewDuration);

  if (UWorld *World = GetWorld()) {
    FTimerDelegate OverviewDelegate = FTimerDelegate::CreateUObject(
        this, &ASkaldPlayerController::HandleAttackDiceOverviewReached);
    World->GetTimerManager().SetTimer(PendingAttackSequence.OverviewTimerHandle,
                                      OverviewDelegate, OverviewDuration,
                                      /*bLoop*/ false);
  } else {
    HandleAttackDiceOverviewReached();
  }
}

void ASkaldPlayerController::HandleAttackDiceOverviewReached() {
  if (!PendingAttackSequence.bActive) {
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(
        PendingAttackSequence.OverviewTimerHandle);
  }

  PendingAttackSequence.ActiveRollId = TriggerAttackDicePresentation(
      PendingAttackSequence.Attacker.Get(),
      PendingAttackSequence.Defender.Get(), PendingAttackSequence.Result);

  if (!PendingAttackSequence.ActiveRollId.IsValid()) {
    HandleAttackDiceCleanupFinished();
  }
}

void ASkaldPlayerController::HandlePhysicalDiceRollCompleted(
    const FGuid &RollId, const TArray<int32> &Results) {
  if (PendingManualSequence.bActive &&
      PendingManualSequence.bAwaitingRollCompletion) {
    if (PendingManualSequence.ActiveRollId.IsValid() &&
        PendingManualSequence.ActiveRollId != RollId) {
      return;
    }

    PendingManualSequence.bAwaitingRollCompletion = false;
    PendingManualSequence.ActiveRollId.Invalidate();

    float CleanupDelay = 0.f;
    if (USkaldDiceManager *DiceManager = ResolveDiceManager()) {
      CleanupDelay = DiceManager->GetCleanupDelay();
    }

    if (UWorld *World = GetWorld()) {
      World->GetTimerManager().ClearTimer(
          PendingManualSequence.CleanupDelayHandle);

      if (CleanupDelay > KINDA_SMALL_NUMBER) {
        FTimerDelegate CleanupDelegate = FTimerDelegate::CreateUObject(
            this, &ASkaldPlayerController::HandleManualDiceCleanupFinished);
        World->GetTimerManager().SetTimer(
            PendingManualSequence.CleanupDelayHandle, CleanupDelegate,
            CleanupDelay, /*bLoop*/ false);
        return;
      }
    }

    HandleManualDiceCleanupFinished();
    return;
  }

  if (bStrategicInitiativeCameraActive &&
      (!PendingStrategicInitiativeRollId.IsValid() || PendingStrategicInitiativeRollId == RollId)) {
    RestoreStrategicInitiativeCamera();
  }

  if (PendingInitiativeSequence.bActive) {
    if (PendingInitiativeSequence.ActiveRollId.IsValid() &&
        PendingInitiativeSequence.ActiveRollId != RollId) {
      return;
    }

    PendingInitiativeSequence.ActiveRollId.Invalidate();

    float CleanupDelay = 0.f;
    if (USkaldDiceManager *DiceManager = ResolveDiceManager()) {
      CleanupDelay = DiceManager->GetCleanupDelay();
    }

    if (UWorld *World = GetWorld()) {
      World->GetTimerManager().ClearTimer(
          PendingInitiativeSequence.CleanupDelayHandle);

      if (CleanupDelay > KINDA_SMALL_NUMBER) {
        FTimerDelegate CleanupDelegate = FTimerDelegate::CreateUObject(
            this, &ASkaldPlayerController::HandleInitiativeDiceCleanupFinished);
        World->GetTimerManager().SetTimer(
            PendingInitiativeSequence.CleanupDelayHandle, CleanupDelegate,
            CleanupDelay, /*bLoop*/ false);
        return;
      }
    }

    HandleInitiativeDiceCleanupFinished();
    return;
  }

  if (!PendingAttackSequence.bActive) {
    return;
  }

  if (!PendingAttackSequence.ActiveRollId.IsValid() ||
      PendingAttackSequence.ActiveRollId != RollId) {
    return;
  }

  PendingAttackSequence.ActiveRollId.Invalidate();
  PendingAttackSequence.PhysicalRollResults = Results;
  PendingAttackSequence.bHasPhysicalResults = Results.Num() > 0;

  float CleanupDelay = 0.f;
  if (USkaldDiceManager *DiceManager = ResolveDiceManager()) {
    CleanupDelay = DiceManager->GetCleanupDelay();
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(
        PendingAttackSequence.CleanupDelayHandle);

    if (CleanupDelay > KINDA_SMALL_NUMBER) {
      FTimerDelegate CleanupDelegate = FTimerDelegate::CreateUObject(
          this, &ASkaldPlayerController::HandleAttackDiceCleanupFinished);
      World->GetTimerManager().SetTimer(
          PendingAttackSequence.CleanupDelayHandle, CleanupDelegate,
          CleanupDelay, /*bLoop*/ false);
      return;
    }
  }

  HandleAttackDiceCleanupFinished();
}

void ASkaldPlayerController::HandleAttackDiceCleanupFinished() {
  if (!PendingAttackSequence.bActive) {
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(
        PendingAttackSequence.CleanupDelayHandle);
  }

  ASkald_PlayerCharacter *CameraPawn = Cast<ASkald_PlayerCharacter>(GetPawn());
  if (!CameraPawn || !PendingAttackSequence.bHadBattleCamera) {
    HandleAttackDiceReturnComplete();
    return;
  }

  const float ReturnDuration = 0.6f;
  CameraPawn->StartCameraTransition(PendingAttackSequence.OriginalLocation,
                                    PendingAttackSequence.OriginalRotation,
                                    PendingAttackSequence.OriginalZoom,
                                    ReturnDuration);

  if (UWorld *World = GetWorld()) {
    FTimerDelegate ReturnDelegate = FTimerDelegate::CreateUObject(
        this, &ASkaldPlayerController::HandleAttackDiceReturnComplete);
    World->GetTimerManager().SetTimer(PendingAttackSequence.ReturnTimerHandle,
                                      ReturnDelegate, ReturnDuration,
                                      /*bLoop*/ false);
  } else {
    HandleAttackDiceReturnComplete();
  }
}

void ASkaldPlayerController::HandleAttackDiceReturnComplete() {
  if (!PendingAttackSequence.bActive) {
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(
        PendingAttackSequence.ReturnTimerHandle);
  }

  ASkald_PlayerCharacter *CameraPawn = Cast<ASkald_PlayerCharacter>(GetPawn());
  if (CameraPawn) {
    if (PendingAttackSequence.Attacker.IsValid()) {
      CameraPawn->FocusCameraOnActor(PendingAttackSequence.Attacker.Get());
    } else if (PendingAttackSequence.OriginalLockTarget.IsValid()) {
      CameraPawn->FocusCameraOnActor(
          PendingAttackSequence.OriginalLockTarget.Get());
    }
  }

  CompletePendingAttackSequence();
}

void ASkaldPlayerController::CompletePendingAttackSequence() {
  if (!PendingAttackSequence.bActive) {
    return;
  }

  ApplyPendingPhysicalAttackResults();

  ProcessAttackResolutionPresentation(PendingAttackSequence.Attacker.Get(),
                                      PendingAttackSequence.Defender.Get(),
                                      PendingAttackSequence.Result);
  ResetAttackDiceSequence();
}

void ASkaldPlayerController::ResetAttackDiceSequence() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(
        PendingAttackSequence.OverviewTimerHandle);
    World->GetTimerManager().ClearTimer(
        PendingAttackSequence.CleanupDelayHandle);
    World->GetTimerManager().ClearTimer(
        PendingAttackSequence.ReturnTimerHandle);
  }

  PendingAttackSequence = FPendingAttackDiceSequence();
}

void ASkaldPlayerController::ApplyPendingPhysicalAttackResults() {
  if (!PendingAttackSequence.bActive ||
      !PendingAttackSequence.bHasPhysicalResults ||
      PendingAttackSequence.PhysicalRollResults.Num() == 0) {
    return;
  }

  AFighterPawn *AttackerPawn = PendingAttackSequence.Attacker.Get();
  if (!AttackerPawn) {
    PendingAttackSequence.bHasPhysicalResults = false;
    PendingAttackSequence.PhysicalRollResults.Reset();
    return;
  }

  FDiceRollResult UpdatedResult = PendingAttackSequence.Result;
  AFighterPawn::ApplyPhysicalRollResults(
      UpdatedResult, PendingAttackSequence.PhysicalRollResults,
      PendingAttackSequence.AttackerSnapshot,
      PendingAttackSequence.DefenderSnapshot);

  UpdatedResult.HighStakesFaction =
      PendingAttackSequence.Result.HighStakesFaction;

  PendingAttackSequence.Result = UpdatedResult;
  PendingAttackSequence.bHasPhysicalResults = false;
  PendingAttackSequence.PhysicalRollResults.Reset();
}

bool ASkaldPlayerController::ComputeBattlefieldOverviewTransform(
    float CurrentYaw, FVector &OutLocation, FRotator &OutRotation,
    float &OutZoom) const {
  UGridOverlayComponent *Grid = FindGridOverlay();
  if (!Grid) {
    return false;
  }

  const int32 Width = FMath::Max(Grid->GetWidth(), 1);
  const int32 Length = FMath::Max(Grid->GetLength(), 1);

  const float CenterX = (Width - 1) * 0.5f;
  const float CenterY = (Length - 1) * 0.5f;

  const FIntPoint FloorCoord(FMath::FloorToInt(CenterX),
                             FMath::FloorToInt(CenterY));
  const FIntPoint CeilCoord(FMath::CeilToInt(CenterX),
                            FMath::CeilToInt(CenterY));

  const FVector FloorLocation = Grid->GridToWorld(FloorCoord);
  const FVector CeilLocation = Grid->GridToWorld(CeilCoord);
  OutLocation = (FloorLocation + CeilLocation) * 0.5f;

  OutRotation = FRotator(-85.f, CurrentYaw, 0.f);
  OutZoom = 3000.f;
  return true;
}

void ASkaldPlayerController::EnsureDiceManagerBindings() {
  if (bDiceDelegatesBound) {
    return;
  }

  if (USkaldDiceManager *DiceManager = ResolveDiceManager()) {
    if (!DiceManager->OnDiceRollCompleted.IsAlreadyBound(
            this, &ASkaldPlayerController::HandlePhysicalDiceRollCompleted)) {
      DiceManager->OnDiceRollCompleted.AddDynamic(
          this, &ASkaldPlayerController::HandlePhysicalDiceRollCompleted);
    }
    if (!DiceManager->OnDiceRollStarted.IsAlreadyBound(
            this, &ASkaldPlayerController::HandleDiceRollStarted)) {
      DiceManager->OnDiceRollStarted.AddDynamic(
          this, &ASkaldPlayerController::HandleDiceRollStarted);
    }
    bDiceDelegatesBound = true;
  }
}

void ASkaldPlayerController::RestoreStrategicInitiativeCamera() {
  PendingStrategicInitiativeRollId.Invalidate();

  if (!bStrategicInitiativeCameraActive) {
    return;
  }

  bStrategicInitiativeCameraActive = false;

  if (ASkald_PlayerCharacter *CameraPawn = Cast<ASkald_PlayerCharacter>(GetPawn())) {
    CameraPawn->EndStrategicInitiativeCameraView();
  }
}

void ASkaldPlayerController::HandleDiceResolutionComplete(
    AFighterPawn *Attacker, AFighterPawn *Defender,
    const FDiceRollResult &Result) {
  if (Defender) {
    Defender->ReleaseHealthDisplayHold();
  }

  FPendingDiceFeedbackState *FeedbackState =
      Defender ? FindPendingDiceFeedbackState(Attacker, Defender) : nullptr;
  const bool bHasDiceOutcomes = Result.DiceOutcomes.Num() > 0;

  if (Defender && Result.StartingHealth > 0 && Result.EndingHealth <= 0) {
    const bool bDeathTriggered =
        FeedbackState && FeedbackState->bTriggeredDeathFeedback;
    if (!bHasDiceOutcomes || !bDeathTriggered) {
      TriggerFighterDeathFeedback(Defender);
      if (FeedbackState) {
        FeedbackState->bTriggeredDeathFeedback = true;
      }
    }
  }

  if (Result.bHighStakesCritical) {
    const bool bHighStakesTriggered =
        FeedbackState && FeedbackState->bTriggeredHighStakesFeedback;
    if (!bHasDiceOutcomes || !bHighStakesTriggered) {
      TriggerHighStakesCritFeedback(Attacker, Defender, Result);

      if (UWorld *World = GetWorld()) {
        if (ASkaldGameState *GameState =
                World->GetGameState<ASkaldGameState>()) {
          GameState->RequestTransientSlowdown(0.2f, 0.25f);
        }
      }

      if (FeedbackState) {
        FeedbackState->bTriggeredHighStakesFeedback = true;
      }
    }
  }

  if (Defender) {
    RemovePendingDiceFeedbackState(Attacker, Defender);
  }

  if (!BattleHudWidget) {
    return;
  }

  if (Attacker &&
      BattleHudWidget->GetManualAttackRollAttacker() == Attacker) {
    BattleHudWidget->ExitManualAttackRollPrompt();
  }

  {
    USkaldGameInstance *GI = CachedGameInstance;
    if (!GI) {
      GI = GetGameInstance<USkaldGameInstance>();
      CachedGameInstance = GI;
    }

    if (GI) {
      PendingPresentationBattleManager = GI->GridBattleManager;
    }
  }

  if (PendingPresentationBattleManager.IsValid()) {
    ++PendingAttackPresentationNotifications;
    TryDispatchPendingAttackPresentationNotifications();
  }
}

void ASkaldPlayerController::HandleDiceOutcomeRevealed(
    AFighterPawn *Attacker, AFighterPawn *Defender,
    const FDiceRollOutcome &Outcome, int32 RevealIndex) {
  if (Attacker) {
    Attacker->TriggerAttackPresentationFX(Defender);
  }
  PlayDiceOutcomeFeedback(Attacker, Defender, Outcome);

  if (Defender && Outcome.bHit) {
    Defender->PlayImpactFlashForDamage(Outcome.Damage);
  }

  if (Defender) {
    if (FPendingDiceFeedbackState *FeedbackState =
            FindPendingDiceFeedbackState(Attacker, Defender)) {
      const int32 HealthBefore = FeedbackState->SimulatedDefenderHealth;
      const int32 DamageApplied = Outcome.bHit
                                      ? FMath::Clamp(Outcome.Damage, 0,
                                                     FMath::Max(0, HealthBefore))
                                      : 0;
      const int32 HealthAfter =
          FMath::Max(0, HealthBefore - DamageApplied);
      const bool bOutcomeKillsDefender =
          HealthBefore > 0 && HealthAfter <= 0;

      if (bOutcomeKillsDefender &&
          !FeedbackState->bTriggeredDeathFeedback) {
        TriggerFighterDeathFeedback(Defender);
        FeedbackState->bTriggeredDeathFeedback = true;
      }

      if (bOutcomeKillsDefender && Outcome.bCritical &&
          FeedbackState->Result.bHighStakesCritical &&
          !FeedbackState->bTriggeredHighStakesFeedback) {
        TriggerHighStakesCritFeedback(Attacker, Defender,
                                      FeedbackState->Result);

        if (UWorld *World = GetWorld()) {
          if (ASkaldGameState *GameState =
                  World->GetGameState<ASkaldGameState>()) {
            GameState->RequestTransientSlowdown(0.2f, 0.25f);
          }
        }

        FeedbackState->bTriggeredHighStakesFeedback = true;
      }

      FeedbackState->SimulatedDefenderHealth = HealthAfter;
      FeedbackState->NextRevealIndex = RevealIndex + 1;
    }
  }

  if (!PlayerCameraManager) {
    return;
  }

  TSubclassOf<UCameraShakeBase> CameraShake = nullptr;
  float ShakeScale = 1.f;

  if (Outcome.bHit) {
    CameraShake = HitCameraShakeClass;
    ShakeScale = Outcome.bCritical ? 1.15f : 0.75f;
  } else {
    CameraShake = MissCameraShakeClass;
    ShakeScale = 0.45f;
  }

  if (CameraShake) {
    PlayerCameraManager->StartCameraShake(CameraShake, ShakeScale);
  }
}

void ASkaldPlayerController::HandleAttackRejected(AFighterPawn *Attacker,
                                                  AFighterPawn *Defender,
                                                  const FText &Reason) {
  if (!IsFriendlyFighter(Attacker)) {
    return;
  }

  if (BattleHudWidget && Attacker &&
      BattleHudWidget->GetManualAttackRollAttacker() == Attacker) {
    BattleHudWidget->ExitManualAttackRollPrompt();
  }

  const FString ReasonString = Reason.ToString();
  if (ReasonString.IsEmpty()) {
    return;
  }

  NotifyActionError(ReasonString);
}

void ASkaldPlayerController::TryDispatchPendingAttackPresentationNotifications() {
  if (PendingAttackPresentationNotifications <= 0) {
    PendingAttackPresentationNotifications = 0;
    if (UWorld *World = GetWorld()) {
      World->GetTimerManager().ClearTimer(BattlePresentationMonitorHandle);
    }
    return;
  }

  UGridBattleManager *BattleManager = PendingPresentationBattleManager.Get();
  if (!BattleManager) {
    PendingAttackPresentationNotifications = 0;
    if (UWorld *World = GetWorld()) {
      World->GetTimerManager().ClearTimer(BattlePresentationMonitorHandle);
    }
    PendingPresentationBattleManager.Reset();
    return;
  }

  const bool bPresentationActive =
      BattleHudWidget && BattleHudWidget->IsCombatPresentationActive();

  if (bPresentationActive) {
    if (UWorld *World = GetWorld()) {
      FTimerManager &TimerManager = World->GetTimerManager();
      if (!TimerManager.IsTimerActive(BattlePresentationMonitorHandle)) {
        TimerManager.SetTimer(BattlePresentationMonitorHandle, this,
                              &ASkaldPlayerController::HandlePendingPresentationTimerTick,
                              0.05f, true);
      }
    }
    return;
  }

  BattleManager->NotifyAttackPresentationComplete();
  PendingAttackPresentationNotifications =
      FMath::Max(0, PendingAttackPresentationNotifications - 1);

  if (PendingAttackPresentationNotifications > 0) {
    TryDispatchPendingAttackPresentationNotifications();
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(BattlePresentationMonitorHandle);
  }

  PendingPresentationBattleManager.Reset();
}

void ASkaldPlayerController::HandlePendingPresentationTimerTick() {
  TryDispatchPendingAttackPresentationNotifications();
}

void ASkaldPlayerController::EnsureDiceWidgets() {
  if (!IsLocalController()) {
    return;
  }

  const bool bShouldSpawnOverlay = bAutoPresentDiceRolls || bAutoPresentInitiativeRolls;
  if (bShouldSpawnOverlay && !DiceOverlayWidget && DiceOverlayWidgetClass) {
    DiceOverlayWidget = CreateWidget<USkaldDiceOverlayWidget>(this, DiceOverlayWidgetClass);
    if (DiceOverlayWidget) {
      DiceOverlayWidget->AddToViewport(32);
      DiceOverlayWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
  }

  if (bAutoPresentInitiativeRolls && !DiceResultWidget && DiceResultWidgetClass) {
    DiceResultWidget = CreateWidget<USkaldDiceResultWidget>(this, DiceResultWidgetClass);
    if (DiceResultWidget) {
      DiceResultWidget->AddToViewport(33);
      DiceResultWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
  }
}

USkaldDiceManager *ASkaldPlayerController::ResolveDiceManager() {
  USkaldGameInstance *GI = CachedGameInstance;
  if (!GI) {
    GI = GetGameInstance<USkaldGameInstance>();
    CachedGameInstance = GI;
  }

  return GI ? GI->GetSubsystem<USkaldDiceManager>() : nullptr;
}

FLinearColor ASkaldPlayerController::ResolveFactionColor(ESkaldFaction Faction) {
  USkaldGameInstance *GI = CachedGameInstance;
  if (!GI) {
    GI = GetGameInstance<USkaldGameInstance>();
    CachedGameInstance = GI;
  }

  return GI ? GI->GetFactionColor(Faction)
            : USkaldGameInstance::GetDefaultFactionColor(Faction);
}

FLinearColor ASkaldPlayerController::ResolveBattleFactionColor(bool bAttackerSide) {
  USkaldGameInstance *GI = CachedGameInstance;
  if (!GI) {
    GI = GetGameInstance<USkaldGameInstance>();
    CachedGameInstance = GI;
  }

  if (!GI) {
    return USkaldGameInstance::GetDefaultFactionColor(ESkaldFaction::None);
  }

  const FS_BattlePayload &Battle = GI->PendingBattle;
  const ESkaldFaction Faction =
      bAttackerSide ? Battle.AttackerFaction : Battle.DefenderFaction;
  return GI->GetFactionColor(Faction);
}

FGuid ASkaldPlayerController::TriggerAttackDicePresentation(
    AFighterPawn *Attacker, AFighterPawn *Defender,
    const FDiceRollResult &Result) {
  if (!IsLocalController() || !bAutoPresentDiceRolls) {
    return FGuid();
  }

  if (Result.DiceOutcomes.Num() == 0) {
    return FGuid();
  }

  EnsureDiceWidgets();

  USkaldDiceManager *DiceManager = ResolveDiceManager();
  if (!DiceManager) {
    return FGuid();
  }

  const bool bFriendlyAttack = Attacker ? IsFriendlyFighter(Attacker) : true;

  TArray<int32> PlayerResults;
  TArray<int32> EnemyResults;
  PlayerResults.Reserve(Result.DiceOutcomes.Num());
  EnemyResults.Reserve(Result.DiceOutcomes.Num());

  const FLinearColor AttackerColor = Attacker
                                         ? ResolveFactionColor(Attacker->Faction)
                                         : ResolveBattleFactionColor(true);
  const FLinearColor DefenderColor = Defender
                                         ? ResolveFactionColor(Defender->Faction)
                                         : ResolveBattleFactionColor(false);

  for (const FDiceRollOutcome &Outcome : Result.DiceOutcomes) {
    const int32 RollValue = FMath::Clamp(Outcome.RollValue, 1, 6);
    if (bFriendlyAttack) {
      PlayerResults.Add(RollValue);
    } else {
      EnemyResults.Add(RollValue);
    }
  }

  if (PlayerResults.Num() == 0 && EnemyResults.Num() == 0) {
    return FGuid();
  }

  const FLinearColor PlayerTint = bFriendlyAttack ? AttackerColor : DefenderColor;
  const FLinearColor EnemyTint = bFriendlyAttack ? DefenderColor : AttackerColor;

  if (DiceOverlayWidget) {
    DiceOverlayWidget->SetPlayerTint(PlayerTint);
    DiceOverlayWidget->SetEnemyTint(EnemyTint);
  }

  FGuid RollId = DiceManager->PlayScriptedRoll(PlayerResults, EnemyResults, false,
                                               -1.f, PlayerTint, EnemyTint);

  if (DiceOverlayWidget) {
    DiceOverlayWidget->SetOverlayMode(ESkaldDiceOverlayMode::Attack);
  }

  return RollId;
}

FGuid ASkaldPlayerController::TriggerInitiativeDicePresentation(int32 AttackerRoll, int32 DefenderRoll)
{
    if (!IsLocalController() || !bAutoPresentInitiativeRolls)
        return FGuid();

    // Prevent duplicate initiative visuals
    if (bInitiativeRollPresentationShown)
        return FGuid();

    bInitiativeRollPresentationShown = true;

    // --- your existing code continues below ---
    EnsureDiceWidgets();

    USkaldDiceManager* DiceManager = ResolveDiceManager();

    DetermineControlledBattleSide();

    const bool bPlayerIsAttacker = bControlsAttackerSide && !bControlsDefenderSide;
    const bool bPlayerIsDefender = bControlsDefenderSide && !bControlsAttackerSide;

    TArray<int32> PlayerResults;
    TArray<int32> EnemyResults;
    PlayerResults.Reserve(1);
    EnemyResults.Reserve(1);

    int32 PlayerResultValue = INDEX_NONE;
    int32 EnemyResultValue = INDEX_NONE;

    auto AppendValue = [&](int32 Value, bool bTreatAsPlayer)
        {
            if (Value <= 0)
                return;

            const int32 Clamped = FMath::Clamp(Value, 1, 6);
            if (bTreatAsPlayer)
            {
                PlayerResults.Add(Clamped);
                if (PlayerResultValue == INDEX_NONE)
                    PlayerResultValue = Clamped;
            }
            else
            {
                EnemyResults.Add(Clamped);
                if (EnemyResultValue == INDEX_NONE)
                    EnemyResultValue = Clamped;
            }
        };

    if (bPlayerIsAttacker)
    {
        AppendValue(AttackerRoll, true);
        AppendValue(DefenderRoll, false);
    }
    else if (bPlayerIsDefender)
    {
        AppendValue(DefenderRoll, true);
        AppendValue(AttackerRoll, false);
    }
    else
    {
        AppendValue(AttackerRoll, true);
        AppendValue(DefenderRoll, false);
    }

    FLinearColor PlayerTint = ResolveBattleFactionColor(true);
    FLinearColor EnemyTint = ResolveBattleFactionColor(false);
    if (bPlayerIsDefender)
    {
        PlayerTint = ResolveBattleFactionColor(false);
        EnemyTint = ResolveBattleFactionColor(true);
    }
    else if (bPlayerIsAttacker)
    {
        PlayerTint = ResolveBattleFactionColor(true);
        EnemyTint = ResolveBattleFactionColor(false);
    }

    if (DiceOverlayWidget)
    {
        DiceOverlayWidget->SetPlayerTint(PlayerTint);
        DiceOverlayWidget->SetEnemyTint(EnemyTint);
    }

    FGuid RollId;

    // Only clients show the dice physically.
    // The server just updates HUDs with results  no duplicate rolls.
    if (HasAuthority())
    {
        // Server: broadcast results (no visual roll)
        ShowInitiativeResults(PlayerResultValue, EnemyResultValue);
    }
    else if (DiceManager && (PlayerResults.Num() > 0 || EnemyResults.Num() > 0))
    {
        // Client: show physical dice roll and result
        RollId = DiceManager->PlayScriptedRoll(PlayerResults, EnemyResults, true, -1.f,
            PlayerTint, EnemyTint);
    }

    // The overlay can be safely set client-side
    if (DiceOverlayWidget)
    {
        DiceOverlayWidget->SetOverlayMode(ESkaldDiceOverlayMode::Initiative);
    }

    return RollId;
}

void ASkaldPlayerController::PrimeInitiativeDiceOverview()
{
  if (!IsLocalController() || !bAutoPresentInitiativeRolls) {
    return;
  }

  if (PendingInitiativeSequence.bActive &&
      PendingInitiativeSequence.bOverviewPrimed &&
      PendingInitiativeSequence.bHadBattleCamera) {
    return;
  }

  ASkald_PlayerCharacter *CameraPawn = Cast<ASkald_PlayerCharacter>(GetPawn());
  if (!CameraPawn || !bIsBattleMap || !CameraPawn->IsBattleCameraActive()) {
    return;
  }

  ResetInitiativeDiceSequence();

  PendingInitiativeSequence.bActive = true;
  PendingInitiativeSequence.bHadBattleCamera = true;
  PendingInitiativeSequence.OriginalLocation = CameraPawn->GetActorLocation();
  PendingInitiativeSequence.OriginalRotation =
      CameraPawn->GetCurrentBattleCameraRotation();
  PendingInitiativeSequence.OriginalZoom =
      CameraPawn->GetCurrentBattleCameraZoom();
  PendingInitiativeSequence.OriginalLockTarget =
      CameraPawn->GetCurrentBattleCameraLockTarget();
  PendingInitiativeSequence.ActiveRollId.Invalidate();
  PendingInitiativeSequence.AttackerResult = 0;
  PendingInitiativeSequence.DefenderResult = 0;

  CameraPawn->ClearCameraFocus();

  FVector OverviewLocation = PendingInitiativeSequence.OriginalLocation;
  FRotator OverviewRotation = PendingInitiativeSequence.OriginalRotation;
  float OverviewZoom = 3000.f;
  if (!ComputeBattlefieldOverviewTransform(
          PendingInitiativeSequence.OriginalRotation.Yaw, OverviewLocation,
          OverviewRotation, OverviewZoom)) {
    ResetInitiativeDiceSequence();
    return;
  }

  const float OverviewDuration = 0.65f;
  CameraPawn->StartCameraTransition(OverviewLocation, OverviewRotation,
                                    OverviewZoom, OverviewDuration);

  PendingInitiativeSequence.bOverviewPrimed = true;
  PendingInitiativeSequence.OverviewDuration = OverviewDuration;

  if (UWorld *World = GetWorld()) {
    PendingInitiativeSequence.OverviewStartTime = World->GetTimeSeconds();
  } else {
    PendingInitiativeSequence.OverviewStartTime = 0.f;
  }
}

void ASkaldPlayerController::StartInitiativeDiceSequence(int32 AttackerRoll,
                                                         int32 DefenderRoll) {
  const bool bAlreadyPrimed = PendingInitiativeSequence.bActive &&
                              PendingInitiativeSequence.bHadBattleCamera &&
                              PendingInitiativeSequence.bOverviewPrimed;

  if (!bAlreadyPrimed) {
    ResetInitiativeDiceSequence();
  }

  if (!IsLocalController() || !bAutoPresentInitiativeRolls) {
    TriggerInitiativeDicePresentation(AttackerRoll, DefenderRoll);
    return;
  }

  EnsureDiceManagerBindings();

  ASkald_PlayerCharacter *CameraPawn =
      Cast<ASkald_PlayerCharacter>(GetPawn());
  const bool bHasBattleCamera =
      CameraPawn && bIsBattleMap && CameraPawn->IsBattleCameraActive();

  if (!bHasBattleCamera) {
    TriggerInitiativeDicePresentation(AttackerRoll, DefenderRoll);
    return;
  }

  if (!PendingInitiativeSequence.bActive) {
    PendingInitiativeSequence.bActive = true;
    PendingInitiativeSequence.bHadBattleCamera = true;
    PendingInitiativeSequence.OriginalLocation = CameraPawn->GetActorLocation();
    PendingInitiativeSequence.OriginalRotation =
        CameraPawn->GetCurrentBattleCameraRotation();
    PendingInitiativeSequence.OriginalZoom =
        CameraPawn->GetCurrentBattleCameraZoom();
    PendingInitiativeSequence.OriginalLockTarget =
        CameraPawn->GetCurrentBattleCameraLockTarget();
  }

  PendingInitiativeSequence.AttackerResult = AttackerRoll;
  PendingInitiativeSequence.DefenderResult = DefenderRoll;
  PendingInitiativeSequence.ActiveRollId.Invalidate();

  float RemainingOverview = 0.f;

  if (PendingInitiativeSequence.bOverviewPrimed) {
    if (UWorld *World = GetWorld()) {
      const float Now = World->GetTimeSeconds();
      const float Elapsed = Now - PendingInitiativeSequence.OverviewStartTime;
      RemainingOverview = FMath::Max(
          0.f, PendingInitiativeSequence.OverviewDuration - Elapsed);
    } else {
      RemainingOverview = PendingInitiativeSequence.OverviewDuration;
    }
  } else {
    CameraPawn->ClearCameraFocus();

    FVector OverviewLocation = PendingInitiativeSequence.OriginalLocation;
    FRotator OverviewRotation = PendingInitiativeSequence.OriginalRotation;
    float OverviewZoom = 3000.f;
    if (!ComputeBattlefieldOverviewTransform(
            PendingInitiativeSequence.OriginalRotation.Yaw, OverviewLocation,
            OverviewRotation, OverviewZoom)) {
      ResetInitiativeDiceSequence();
      TriggerInitiativeDicePresentation(AttackerRoll, DefenderRoll);
      return;
    }

    const float OverviewDuration = 0.65f;
    CameraPawn->StartCameraTransition(OverviewLocation, OverviewRotation,
                                      OverviewZoom, OverviewDuration);

    PendingInitiativeSequence.bOverviewPrimed = true;
    PendingInitiativeSequence.OverviewDuration = OverviewDuration;
    if (UWorld *World = GetWorld()) {
      PendingInitiativeSequence.OverviewStartTime = World->GetTimeSeconds();
    } else {
      PendingInitiativeSequence.OverviewStartTime = 0.f;
    }

    RemainingOverview = OverviewDuration;
  }

  if (RemainingOverview <= KINDA_SMALL_NUMBER) {
    HandleInitiativeDiceOverviewReached();
    return;
  }

  if (UWorld *World = GetWorld()) {
    FTimerDelegate OverviewDelegate = FTimerDelegate::CreateUObject(
        this, &ASkaldPlayerController::HandleInitiativeDiceOverviewReached);
    World->GetTimerManager().SetTimer(
        PendingInitiativeSequence.OverviewTimerHandle, OverviewDelegate,
        RemainingOverview, /*bLoop*/ false);
  } else {
    HandleInitiativeDiceOverviewReached();
  }
}

void ASkaldPlayerController::HandleInitiativeDiceOverviewReached() {
  if (!PendingInitiativeSequence.bActive) {
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(
        PendingInitiativeSequence.OverviewTimerHandle);
  }

  PendingInitiativeSequence.ActiveRollId = TriggerInitiativeDicePresentation(
      PendingInitiativeSequence.AttackerResult,
      PendingInitiativeSequence.DefenderResult);

  if (!PendingInitiativeSequence.ActiveRollId.IsValid()) {
    HandleInitiativeDiceCleanupFinished();
  }
}

void ASkaldPlayerController::HandleInitiativeDiceCleanupFinished() {
  if (!PendingInitiativeSequence.bActive) {
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(
        PendingInitiativeSequence.CleanupDelayHandle);
  }

  ASkald_PlayerCharacter *CameraPawn =
      Cast<ASkald_PlayerCharacter>(GetPawn());
  if (!CameraPawn || !PendingInitiativeSequence.bHadBattleCamera) {
    HandleInitiativeDiceReturnComplete();
    return;
  }

  const float ReturnDuration = 0.6f;
  CameraPawn->StartCameraTransition(PendingInitiativeSequence.OriginalLocation,
                                    PendingInitiativeSequence.OriginalRotation,
                                    PendingInitiativeSequence.OriginalZoom,
                                    ReturnDuration);

  if (UWorld *World = GetWorld()) {
    FTimerDelegate ReturnDelegate = FTimerDelegate::CreateUObject(
        this, &ASkaldPlayerController::HandleInitiativeDiceReturnComplete);
    World->GetTimerManager().SetTimer(
        PendingInitiativeSequence.ReturnTimerHandle, ReturnDelegate,
        ReturnDuration, /*bLoop*/ false);
  } else {
    HandleInitiativeDiceReturnComplete();
  }
}

void ASkaldPlayerController::HandleInitiativeDiceReturnComplete() {
  if (!PendingInitiativeSequence.bActive) {
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(
        PendingInitiativeSequence.ReturnTimerHandle);
  }

  ASkald_PlayerCharacter *CameraPawn =
      Cast<ASkald_PlayerCharacter>(GetPawn());
  if (CameraPawn) {
    if (PendingInitiativeSequence.OriginalLockTarget.IsValid()) {
      CameraPawn->FocusCameraOnActor(
          PendingInitiativeSequence.OriginalLockTarget.Get());
    }
  }

  CompletePendingInitiativeSequence();
}

void ASkaldPlayerController::CompletePendingInitiativeSequence() {
  if (!PendingInitiativeSequence.bActive) {
    return;
  }

  ResetInitiativeDiceSequence();
}

void ASkaldPlayerController::ResetInitiativeDiceSequence() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(
        PendingInitiativeSequence.OverviewTimerHandle);
    World->GetTimerManager().ClearTimer(
        PendingInitiativeSequence.CleanupDelayHandle);
    World->GetTimerManager().ClearTimer(
        PendingInitiativeSequence.ReturnTimerHandle);
  }

  PendingInitiativeSequence = FPendingInitiativeDiceSequence();
}

void ASkaldPlayerController::ShowInitiativeResults(int32 PlayerResult,
                                                   int32 EnemyResult) {
  if (!bAutoPresentInitiativeRolls) {
    return;
  }

  EnsureDiceWidgets();

  if (!DiceResultWidget) {
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(InitiativeResultHideTimer);
  }

  DiceResultWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
  DiceResultWidget->ShowResults(PlayerResult, EnemyResult);

  if (InitiativeResultLingerSeconds > 0.f) {
    if (UWorld *World = GetWorld()) {
      World->GetTimerManager().SetTimer(
          InitiativeResultHideTimer, this,
          &ASkaldPlayerController::HideInitiativeResults,
          InitiativeResultLingerSeconds, false);
    }
  }
}

void ASkaldPlayerController::HideInitiativeResults() {
  if (DiceResultWidget) {
    DiceResultWidget->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(InitiativeResultHideTimer);
  }
}

bool ASkaldPlayerController::IsFriendlyFighter(const AFighterPawn *Fighter) const {
  if (!Fighter)
    return false;

  return Fighter->bIsAttacker ? bControlsAttackerSide : bControlsDefenderSide;
}

void ASkaldPlayerController::DetermineControlledBattleSide() {
  bControlsAttackerSide = false;
  bControlsDefenderSide = false;

  const ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS)
    return;

  if (!CachedGameInstance) {
    CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  }
  if (!CachedGameInstance)
    return;

  const FS_BattlePayload &Battle = CachedGameInstance->PendingBattle;
  const int32 PlayerID = PS->GetPlayerId();
  if (PlayerID == Battle.AttackerPlayerID) {
    bControlsAttackerSide = true;
  }
  if (PlayerID == Battle.DefenderPlayerID) {
    bControlsDefenderSide = true;
  }
}

void ASkaldPlayerController::HandlePlayerIdUpdated() {
  DetermineControlledBattleSide();
  UpdateBattleHUDButtons();
  RefreshLockedInFighterList();
}

void ASkaldPlayerController::HandleBattleEnded(ESkaldFaction WinningFaction,
                                               int32 AttackerCasualties,
                                               int32 DefenderCasualties) {
  CancelCommandMode();
  SelectedFighter = nullptr;
  LockedActiveFighter = nullptr;
  LastLocalInitiativeRoll = 0;
  CachedBattleManager.Reset();
  bControlsAttackerSide = false;
  bControlsDefenderSide = false;
  LastStrategicInitiativeSoundRound = INDEX_NONE;
  LastBattleInitiativeSoundRound = INDEX_NONE;
  LastBattleTurnSoundRound = INDEX_NONE;
  bLastBattleTurnSoundWasAttacker = false;
  LastBattleTurnSoundAvailableCount = INDEX_NONE;
  PendingDiceFeedbackStates.Reset();

  for (const TWeakObjectPtr<AFighterPawn> &TrackedFighter : ObservedFriendlyFighters) {
    if (AFighterPawn *Fighter = TrackedFighter.Get()) {
      Fighter->OnDestroyed.RemoveDynamic(
          this, &ASkaldPlayerController::HandleTrackedFighterDestroyed);
    }
  }
  ObservedFriendlyFighters.Reset();

  if (BattleHudWidget) {
    BattleHudWidget->RemoveFromParent();
    BattleHudWidget = nullptr;
  }

  UpdateLockedInActiveHighlight();
  UpdateLockedInSelectionHighlight();

  bool bPlayerWon = false;
  bool bPlayerLost = false;
  FLinearColor PlayerFactionColor = ResolveFactionColor(ESkaldFaction::None);

  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    if (WinningFaction != ESkaldFaction::None && PS->Faction == WinningFaction) {
      bPlayerWon = true;
    } else if (WinningFaction != ESkaldFaction::None) {
      bPlayerLost = true;
    }

    PlayerFactionColor = ResolveFactionColor(PS->Faction);
  }

  if (!VictoryWidgetClass) {
    VictoryWidgetClass = UBattleResultWidget::StaticClass();
  }

  if (BattleResultWidget) {
    BattleResultWidget->RemoveFromParent();
    BattleResultWidget = nullptr;
  }

  if (VictoryWidgetClass) {
    if (UUserWidget *Widget =
            CreateWidget<UUserWidget>(this, VictoryWidgetClass)) {
      if (UBattleResultWidget *ResultWidget = Cast<UBattleResultWidget>(Widget)) {
        ResultWidget->SetBattleOutcome(bPlayerWon, bPlayerLost, AttackerCasualties,
                                       DefenderCasualties, PlayerFactionColor);
      }
      BattleResultWidget = Widget;
      BattleResultWidget->AddToViewport();
    }
  }

  HideMainHUD();

  if (!CachedGameInstance)
  {
      CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  }

  const bool bReadyForOverworldHUD =
      !bIsBattleMap || (CachedGameInstance && !CachedGameInstance->bIsInBattleMap);

  if (bReadyForOverworldHUD)
  {
      ShowOverworldHUD();
  }
} // closes the last function, NOT the class

// === Manual Attack Roll UI Flow ===

// CLIENT FUNCTIONS
void ASkaldPlayerController::ClientShowAttackRollButton_Implementation(
    AFighterPawn* Attacker, bool bAutoTriggerRoll)
{
    UBattleHUDWidget* HUD = BattleHudWidget.Get();
    if (!HUD)
    {
        HUD = GetBattleHUD();
    }

    if (HUD)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ManualDice] ClientShowAttackRollButton received for %s"), *GetNameSafe(Attacker));
        HUD->EnterManualAttackRollPrompt(Attacker);
        if (bAutoTriggerRoll)
        {
            HUD->SetAttackRollButtonVisibility(false);
        }
    }

    if (!bAutoTriggerRoll)
    {
        return;
    }

    if (!BattleHudWidget)
    {
        BattleHudWidget = HUD;
    }

    if (!BattleHudWidget)
    {
        return;
    }

    if (!BattleHudWidget->IsManualAttackRollPromptActive())
    {
        return;
    }

    HandleAttackRollRequested();
}

void ASkaldPlayerController::ClientHideAttackRollButton_Implementation()
{
    UE_LOG(LogTemp, Warning, TEXT("PC: ClientHideAttackRollButton called"));

    UBattleHUDWidget* HUD = BattleHudWidget.Get();
    if (!HUD)
    {
        HUD = GetBattleHUD();
    }

    if (HUD)
    {
        HUD->SetAttackRollButtonVisibility(false);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("PC: BattleHUD is null!"));
    }
}

// SERVER FUNCTION
void ASkaldPlayerController::ServerTriggerManualAttackRoll_Implementation(AFighterPawn* Attacker)
{
    if (!Attacker)
    {
        UE_LOG(LogTemp, Warning, TEXT("ServerTriggerManualAttackRoll called with null Attacker"));
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("ServerTriggerManualAttackRoll called for %s"), *GetNameSafe(Attacker));
    Attacker->NotifyAIAttackPresentationReady();
    Attacker->TriggerManualAttackRoll();
}

void ASkaldPlayerController::ServerNotifyAIAttackOverviewComplete_Implementation(AFighterPawn* Attacker)
{
    if (!Attacker)
    {
        return;
    }

    Attacker->NotifyAIAttackPresentationReady();
}
