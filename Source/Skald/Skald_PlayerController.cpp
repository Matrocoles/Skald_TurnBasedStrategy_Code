#include "Skald_PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "ChoosePlayerWidget.h"
#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "FighterPawn.h"
#include "GridBattleManager.h"
#include "GridOverlayComponent.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Skald.h"
#include "SkaldTypes.h"
#include "Skald_GameInstance.h"
#include "Skald_GameMode.h"
#include "Skald_GameState.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "Territory.h"
#include "UI/BattleHUDWidget.h"
#include "UI/DeployWidget.h"
#include "UI/FighterSelectionWidget.h"
#include "UI/SkaldMainHUDWidget.h"
#include "UObject/ConstructorHelpers.h"
#include "WorldMap.h"
#include "TimerManager.h"
#include <limits>

constexpr int32 MaxAIIterations = 100;
constexpr int32 MaxWorldMapSearchAttempts = 5;

ASkaldPlayerController::ASkaldPlayerController() {
  bIsAI = false;
  TurnManager = nullptr;
  HUDRef = nullptr;
  MainHudWidget = nullptr;
  BattleHudWidget = nullptr;
  CurrentCommandMode = EBattleCommandMode::None;

  bShowMouseCursor = true;
  bEnableClickEvents = true;
  bEnableMouseOverEvents = true;

  // Default to the native HUD widget class. This avoids loading a
  // blueprint-derived widget that may not exist or may be corrupt.
  HUDWidgetClass = USkaldMainHUDWidget::StaticClass();
  BattleHUDWidgetClass = UBattleHUDWidget::StaticClass();

  WorldMapSearchAttempts = 0;

  static ConstructorHelpers::FClassFinder<UChoosePlayerWidget> ChooseBP(
      TEXT("/Game/Blueprints/UI/Skald_ChoosePlayerWidget"));
  if (ChooseBP.Succeeded()) {
    ChoosePlayerWidgetClass = ChooseBP.Class;
  }
}

void ASkaldPlayerController::BeginPlay() {
  Super::BeginPlay();
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
  }
  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    bIsAI = PS->bIsAI;
  }

  if (IsLocalPlayerController() && !IsAIController() &&
      GetLocalPlayer() != nullptr) {
    // Create and show the HUD widget if a class has been assigned (expected via
    // blueprint or constructor).
    if (HUDWidgetClass) {
      MainHudWidget = CreateWidget<USkaldMainHUDWidget>(this, HUDWidgetClass);
      if (MainHudWidget) {
        HUDRef = MainHudWidget;
        MainHudWidget->AddToViewport();
        MainHudWidget->SetVisibility(ESlateVisibility::Hidden);

        if (CachedGameState) {
          TArray<FS_PlayerData> Players;
          for (APlayerState *PSBase : CachedGameState->PlayerArray) {
            if (ASkaldPlayerState *LPS = Cast<ASkaldPlayerState>(PSBase)) {
              FS_PlayerData Data;
              Data.PlayerID = LPS->GetPlayerId();
              Data.PlayerName = LPS->PlayerDisplayName;
              Data.IsAI = LPS->bIsAI;
              Data.Faction = LPS->Faction;
              Players.Add(Data);
            }
          }

          const ASkaldPlayerState *CurrentPS =
              CachedGameState->GetCurrentPlayer();
          const int32 CurrentID = CurrentPS ? CurrentPS->GetPlayerId() : -1;
          MainHudWidget->RefreshFromState(CurrentID, /*TurnNumber*/ 1,
                                          ETurnPhase::Reinforcement, Players);
        }

        // Ensure local player details are registered with the HUD once
        // available.
        OnRep_PlayerState();

        MainHudWidget->OnAttackRequested.AddDynamic(
            this, &ASkaldPlayerController::HandleAttackRequested);
        MainHudWidget->OnMoveRequested.AddDynamic(
            this, &ASkaldPlayerController::HandleMoveRequested);
        MainHudWidget->OnEndAttackRequested.AddDynamic(
            this, &ASkaldPlayerController::HandleEndAttackRequested);
        MainHudWidget->OnEndMovementRequested.AddDynamic(
            this, &ASkaldPlayerController::HandleEndMovementRequested);
        MainHudWidget->OnEngineeringRequested.AddDynamic(
            this, &ASkaldPlayerController::HandleEngineeringRequested);
        MainHudWidget->OnBuildSiegeRequested.AddDynamic(
            this, &ASkaldPlayerController::HandleBuildSiegeRequested);
        MainHudWidget->OnDigTreasureRequested.AddDynamic(
            this, &ASkaldPlayerController::HandleDigTreasureRequested);
      }
    } else {
      UE_LOG(LogSkald, Warning,
             TEXT("HUDWidgetClass is null; HUD will not be displayed."));
    }

    if (!ChoosePlayerWidget && ChoosePlayerWidgetClass) {
      ChoosePlayerWidget =
          CreateWidget<UChoosePlayerWidget>(this, ChoosePlayerWidgetClass);
      if (ChoosePlayerWidget) {
        ChoosePlayerWidget->OnPlayerLockedIn.AddDynamic(
            this, &ASkaldPlayerController::HandleFactionLockedIn);
        ChoosePlayerWidget->AddToViewport();
        // While the player is choosing their faction, restrict controls to the
        // UI
        SetInputMode(FInputModeUIOnly());
        SetIgnoreMoveInput(true);
        SetIgnoreLookInput(true);
      }
    }

    InitializeFighterSelectionIfNeeded();
  }

  TryBindWorldMap();
}

void ASkaldPlayerController::TryBindWorldMap() {
  if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          GetWorld(), AWorldMap::StaticClass()))) {
    if (!WorldMap->OnTerritorySelected.IsAlreadyBound(
            this, &ASkaldPlayerController::HandleTerritorySelected)) {
      WorldMap->OnTerritorySelected.AddDynamic(
          this, &ASkaldPlayerController::HandleTerritorySelected);
      ensureMsgf(
          WorldMap->OnTerritorySelected.IsAlreadyBound(
              this, &ASkaldPlayerController::HandleTerritorySelected),
          TEXT("Failed to bind HandleTerritorySelected to WorldMap."));
    }
    WorldMapSearchAttempts = 0;
  } else {
    ++WorldMapSearchAttempts;
    if (WorldMapSearchAttempts >= MaxWorldMapSearchAttempts) {
      UE_LOG(LogSkald, Warning,
             TEXT("ASkaldPlayerController could not find AWorldMap after %d attempts."),
             WorldMapSearchAttempts);
    }
    GetWorldTimerManager().SetTimer(
        WorldMapSearchHandle, this,
        &ASkaldPlayerController::TryBindWorldMap, 0.5f, false);
  }
}

void ASkaldPlayerController::OnRep_PlayerState() {
  Super::OnRep_PlayerState();

  if (!MainHudWidget) {
    return;
  }

  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    MainHudWidget->LocalPlayerID = PS->GetPlayerId();
    MainHudWidget->UpdateResources(PS->Resources);
    MainHudWidget->SyncPhaseButtons(MainHudWidget->CurrentPlayerID ==
                                    MainHudWidget->LocalPlayerID);
  }
}

void ASkaldPlayerController::ServerInitPlayerState_Implementation(
    const FString &Name, ESkaldFaction Faction) {
  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    PS->PlayerDisplayName = Name;
    PS->Faction = Faction;
    PS->bHasLockedIn = true;

    if (ASkaldGameMode *GM = GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
      GM->HandlePlayerLockedIn(PS);
    }
  }
}

void ASkaldPlayerController::SetTurnManager(ATurnManager *Manager) {
  if (TurnManager) {
    TurnManager->OnWorldStateChanged.RemoveDynamic(
        this, &ASkaldPlayerController::HandleWorldStateChanged);
  }

  TurnManager = Manager;

  if (TurnManager) {
    TurnManager->OnWorldStateChanged.AddDynamic(
        this, &ASkaldPlayerController::HandleWorldStateChanged);
    if (IsLocalPlayerController() && !IsAIController() &&
        GetLocalPlayer() != nullptr) {
      InitializeFighterSelectionIfNeeded();
    }
  }
}

void ASkaldPlayerController::InitializeFighterSelectionIfNeeded() {
  if (FighterSelectionWidget || IsAIController() ||
      GetLocalPlayer() == nullptr || !IsLocalPlayerController()) {
    return;
  }

  if (!CachedGameInstance) {
    CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  }
  if (!CachedGameInstance || !CachedGameInstance->GridBattleManager) {
    return;
  }

  ATurnManager *TM = TurnManager;
  if (!TM) {
    if (!CachedGameMode) {
      CachedGameMode = GetWorld()->GetAuthGameMode<ASkaldGameMode>();
    }
    if (CachedGameMode) {
      TM = CachedGameMode->GetTurnManager();
    }
  }
  if (!TM) {
    return;
  }

  const FString CurrentMap = UGameplayStatics::GetCurrentLevelName(this, true);
  bool bIsBattleMap = false;
  for (const TSoftObjectPtr<UWorld> &Map : TM->BattleMaps) {
    if (CurrentMap.Equals(Map.ToSoftObjectPath().GetAssetName(),
                          ESearchCase::IgnoreCase)) {
      bIsBattleMap = true;
      break;
    }
  }

  if (!bIsBattleMap) {
    return;
  }

  if (UFighterSelectionWidget *Selection =
          CreateWidget<UFighterSelectionWidget>(
              this, UFighterSelectionWidget::StaticClass())) {
    FighterSelectionWidget = Selection;
    if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
      const ESkaldFaction PlayerFaction = PS->Faction;
      Selection->PlayerFaction = PlayerFaction;
      Selection->AvailableFighters =
          CachedGameInstance->GridBattleManager->GetFightersForFaction(
              PlayerFaction);
    }
    Selection->OnLockedIn.AddDynamic(
        this, &ASkaldPlayerController::HandleFighterSelectionLockedIn);
    Selection->AddToViewport();
    SetIgnoreMoveInput(true);
  }
}

void ASkaldPlayerController::ShowTurnAnnouncement(const FString &PlayerName,
                                                  bool bIsMyTurn) {
  if (MainHudWidget) {
    MainHudWidget->ShowTurnAnnouncement(PlayerName);
    MainHudWidget->ShowTurnMessage(bIsMyTurn);
  } else if (GEngine) {
    const FString Message = FString::Printf(TEXT("%s's Turn"), *PlayerName);
    GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow, Message);
  }
}

void ASkaldPlayerController::StartTurn() {
  if (bIsAI) {
    MakeAIDecision();
  } else {
    FInputModeGameAndUI InputMode;
    SetInputMode(InputMode);
  }
}

void ASkaldPlayerController::EndTurn() {
  SetInputMode(FInputModeGameOnly());
  if (!EnsureTurnManager(TEXT("EndTurn"))) {
    return;
  }

  TurnManager->AdvanceTurn();
}

void ASkaldPlayerController::EndPhase() {
  if (!EnsureTurnManager(TEXT("EndPhase"))) {
    return;
  }

  ETurnPhase Phase = TurnManager->GetCurrentPhase();
  if (Phase == ETurnPhase::ArmyPlacement) {
    if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
      PS->DeployableUnits = 0;
      TurnManager->BroadcastDeployableUnits(PS);
    }

    if (!CachedGameMode) {
      CachedGameMode = GetWorld()->GetAuthGameMode<ASkaldGameMode>();
    }

    if (CachedGameMode) {
      CachedGameMode->AdvanceArmyPlacement();
    }
    return;
  }

  TurnManager->AdvancePhase();
}

void ASkaldPlayerController::MakeAIDecision() {
  if (!TurnManager) {
    EndTurn();
    return;
  }

  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS) {
    EndTurn();
    return;
  }

  // Cache the world map for subsequent phases.
  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    EndTurn();
    return;
  }

  // Iterate through phases until the turn naturally ends.
  ETurnPhase Phase = TurnManager->GetCurrentPhase();
  int32 IterationCount = 0;
  while (Phase != ETurnPhase::EndTurn && IterationCount++ < MaxAIIterations) {
    const ETurnPhase PrevPhase = Phase;

    if (Phase == ETurnPhase::Reinforcement) {
      TArray<ATerritory *> OwnedTerritories;
      for (ATerritory *Territory : WorldMap->Territories) {
        if (Territory && Territory->OwningPlayer == PS) {
          OwnedTerritories.Add(Territory);
        }
      }

      int32 SpreadIndex = 0;
      while (PS->DeployableUnits > 0 && OwnedTerritories.Num() > 0) {
        ATerritory *TargetTerritory =
            OwnedTerritories[SpreadIndex % OwnedTerritories.Num()];
        ++TargetTerritory->ArmyUnits;
        TargetTerritory->RefreshAppearance();
        --PS->DeployableUnits;
        --PS->Resources;
        ++SpreadIndex;
      }
      TurnManager->BroadcastDeployableUnits(PS);
      TurnManager->BroadcastResources(PS);

      TurnManager->AdvancePhase();
    } else if (Phase == ETurnPhase::Attack) {
      ATerritory *BestSource = nullptr;
      ATerritory *BestTarget = nullptr;
      int32 WeakestStrength = std::numeric_limits<int32>::max();

      for (ATerritory *Source : WorldMap->Territories) {
        if (!Source || Source->OwningPlayer != PS || Source->ArmyUnits <= 1) {
          continue;
        }

        for (ATerritory *Neighbor : Source->AdjacentTerritories) {
          if (!Neighbor || Neighbor->OwningPlayer == PS) {
            continue;
          }

          if (Neighbor->ArmyUnits < WeakestStrength) {
            BestSource = Source;
            BestTarget = Neighbor;
            WeakestStrength = Neighbor->ArmyUnits;
          }
        }
      }

      if (BestSource && BestTarget && BestSource->ArmyUnits > 1) {
        const int32 ArmySent = BestSource->ArmyUnits - 1;
        HandleAttackRequested(BestSource->TerritoryID, BestTarget->TerritoryID,
                              ArmySent, false);
      }

      TurnManager->AdvancePhase();
    } else if (Phase == ETurnPhase::Engineering ||
               Phase == ETurnPhase::Treasure) {
      TurnManager->AdvancePhase();
    } else if (Phase == ETurnPhase::Movement) {
      ATerritory *BestSource = nullptr;
      ATerritory *BestTarget = nullptr;
      int32 WeakestStrength = std::numeric_limits<int32>::max();

      for (ATerritory *Source : WorldMap->Territories) {
        if (!Source || Source->OwningPlayer != PS || Source->ArmyUnits <= 1) {
          continue;
        }

        for (ATerritory *Neighbor : Source->AdjacentTerritories) {
          if (!Neighbor || Neighbor->OwningPlayer != PS) {
            continue;
          }

          if (Neighbor->ArmyUnits < WeakestStrength) {
            BestSource = Source;
            BestTarget = Neighbor;
            WeakestStrength = Neighbor->ArmyUnits;
          }
        }
      }

      if (BestSource && BestTarget) {
        int32 TroopsToMove = BestSource->ArmyUnits / 2;
        TroopsToMove = FMath::Clamp(TroopsToMove, 1, BestSource->ArmyUnits - 1);
        HandleMoveRequested(BestSource->TerritoryID, BestTarget->TerritoryID,
                            TroopsToMove);
      }

      TurnManager->AdvancePhase();
    } else {
      // Any other phase ends the turn immediately.
      UE_LOG(LogSkald, Warning,
             TEXT("MakeAIDecision encountered unexpected phase %s"),
             *UEnum::GetValueAsString(Phase));
      break;
    }

    Phase = TurnManager->GetCurrentPhase();
    if (Phase == PrevPhase) {
      UE_LOG(LogSkald, Warning,
             TEXT("MakeAIDecision phase %s did not advance; breaking"),
             *UEnum::GetValueAsString(Phase));
      break;
    }
  }

  if (IterationCount >= MaxAIIterations) {
    UE_LOG(LogSkald, Warning, TEXT("MakeAIDecision hit iteration limit (%d)"),
           MaxAIIterations);
  } else {
    UE_LOG(LogSkald, Log, TEXT("MakeAIDecision completed in %d iterations"),
           IterationCount);
  }

  EndTurn();
}

bool ASkaldPlayerController::IsAIController() const { return bIsAI; }

void ASkaldPlayerController::SetIsAIController(bool bInIsAI) {
  bIsAI = bInIsAI;
}

bool ASkaldPlayerController::ValidateAttack(int32 FromID, int32 ToID,
                                            int32 ArmySent, bool bUseSiege,
                                            FString *OutError) {
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

void ASkaldPlayerController::HandleAttackRequested(int32 FromID, int32 ToID,
                                                   int32 ArmySent,
                                                   bool bUseSiege) {
  UE_LOG(LogSkald, Log, TEXT("HUD attack from %d to %d with %d"), FromID, ToID,
         ArmySent);
  FString Error;
  if (!ValidateAttack(FromID, ToID, ArmySent, bUseSiege, &Error)) {
    NotifyActionError(Error);
    return;
  }

  ServerHandleAttack(FromID, ToID, ArmySent, bUseSiege);
}

void ASkaldPlayerController::ServerHandleAttack_Implementation(int32 FromID,
                                                               int32 ToID,
                                                               int32 ArmySent,
                                                               bool bUseSiege) {
  FString Error;
  if (!ValidateAttack(FromID, ToID, ArmySent, bUseSiege, &Error)) {
    NotifyActionError(Error);
    return;
  }

  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  ATerritory *Source = WorldMap ? WorldMap->GetTerritoryById(FromID) : nullptr;
  ATerritory *Target = WorldMap ? WorldMap->GetTerritoryById(ToID) : nullptr;
  if (!Source || !Target) {
    return;
  }

  ASkaldPlayerState *AttackerPS = Source->OwningPlayer;
  ASkaldPlayerState *DefenderPS = Target->OwningPlayer;

  if (TurnManager) {
    FS_BattlePayload Battle;
    Battle.AttackerPlayerID = AttackerPS ? AttackerPS->GetPlayerId() : -1;
    Battle.DefenderPlayerID = DefenderPS ? DefenderPS->GetPlayerId() : -1;
    Battle.FromTerritoryID = FromID;
    Battle.TargetTerritoryID = ToID;
    Battle.ArmyCountSent = ArmySent;
    Battle.IsCapitalAttack = Target->bIsCapital;
    if (bUseSiege && CachedGameMode) {
      const int32 SiegeID = CachedGameMode->ConsumeSiege(FromID);
      if (SiegeID > 0) {
        Battle.AssignedSiegeIDs.Add(SiegeID);
      }
    }
    TurnManager->TriggerGridBattle(Battle);
    return;
  }

  int32 AttackingForces = ArmySent;
  int32 DefendingForces = Target->ArmyUnits;
  if (bUseSiege && CachedGameMode) {
    CachedGameMode->ConsumeSiege(FromID);
  }

  Source->ArmyUnits -= ArmySent;

  FRandomStream *CombatStream = nullptr;
  if (CachedGameInstance) {
    CachedGameInstance->SeedCombatRandomStream(FMath::Rand());
    CombatStream = &CachedGameInstance->CombatRandomStream;
  } else {
    static FRandomStream FallbackStream;
    FallbackStream.Initialize(FMath::Rand());
    CombatStream = &FallbackStream;
  }

  while (AttackingForces > 0 && DefendingForces > 0) {
    const int32 AttackRoll = CombatStream->RandRange(1, 6);
    const int32 DefendRoll = CombatStream->RandRange(1, 6);
    if (AttackRoll > DefendRoll) {
      --DefendingForces;
    } else {
      --AttackingForces;
    }
  }

  if (DefendingForces <= 0) {
    Target->OwningPlayer = AttackerPS;
    Target->ArmyUnits = AttackingForces;
  } else {
    Target->ArmyUnits = DefendingForces;
  }

  Source->RefreshAppearance();
  Target->RefreshAppearance();

  if (TurnManager) {
    for (ASkaldPlayerController *Controller : TurnManager->GetControllers()) {
      if (USkaldMainHUDWidget *HUD =
              Controller ? Controller->GetHUDWidget() : nullptr) {
        FString OwnerName = Target->OwningPlayer
                                ? Target->OwningPlayer->PlayerDisplayName
                                : TEXT("Neutral");
        HUD->UpdateTerritoryInfo(Target->TerritoryName, OwnerName,
                                 Target->ArmyUnits);
      }
    }
  }
}

void ASkaldPlayerController::HandleMoveRequested(int32 FromID, int32 ToID,
                                                 int32 Troops) {
  UE_LOG(LogSkald, Log, TEXT("HUD move from %d to %d with %d"), FromID, ToID,
         Troops);

  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    NotifyActionError(TEXT("World map not found"));
    return;
  }

  ATerritory *Source = WorldMap->GetTerritoryById(FromID);
  ATerritory *Target = WorldMap->GetTerritoryById(ToID);
  if (!Source || !Target) {
    NotifyActionError(TEXT("Invalid territory selection"));
    return;
  }

  TArray<ATerritory *> Path;
  if (!WorldMap->FindPath(Source, Target, Path)) {
    NotifyActionError(TEXT("No valid path for movement"));
    return;
  }

  if (Troops <= 0 || Troops >= Source->ArmyUnits) {
    NotifyActionError(TEXT("Invalid troop count for movement"));
    return;
  }

  ServerHandleMove(FromID, ToID, Troops);
}

void ASkaldPlayerController::ServerHandleMove_Implementation(int32 FromID,
                                                             int32 ToID,
                                                             int32 Troops) {
  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    return;
  }

  ATerritory *Source = WorldMap->GetTerritoryById(FromID);
  ATerritory *Target = WorldMap->GetTerritoryById(ToID);
  if (!Source || !Target) {
    return;
  }

  if (Troops <= 0 || Troops >= Source->ArmyUnits) {
    return;
  }

  if (!WorldMap->MoveBetween(Source, Target, Troops)) {
    return;
  }

  if (TurnManager) {
    for (ASkaldPlayerController *Controller : TurnManager->GetControllers()) {
      if (USkaldMainHUDWidget *HUD =
              Controller ? Controller->GetHUDWidget() : nullptr) {
        FString SourceOwner = Source->OwningPlayer
                                  ? Source->OwningPlayer->PlayerDisplayName
                                  : TEXT("Neutral");
        HUD->UpdateTerritoryInfo(Source->TerritoryName, SourceOwner,
                                 Source->ArmyUnits);
        FString TargetOwner = Target->OwningPlayer
                                  ? Target->OwningPlayer->PlayerDisplayName
                                  : TEXT("Neutral");
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

  if (PS->DeployableUnits < Amount) {
    UE_LOG(LogSkald, Warning,
           TEXT("ServerDeployUnits: Insufficient units. Have %d need %d"),
           PS->DeployableUnits, Amount);
    NotifyActionError(TEXT("Not enough deployable units"));
    return;
  }

  Terr->ArmyUnits += Amount;
  Terr->RefreshAppearance();

  PS->DeployableUnits -= Amount;

  if (TurnManager) {
    TurnManager->BroadcastDeployableUnits(PS);
  }
}

void ASkaldPlayerController::ServerSelectTerritory_Implementation(
    int32 TerritoryID) {
  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    return;
  }

  ATerritory *Terr = WorldMap->GetTerritoryById(TerritoryID);
  if (!Terr || WorldMap->SelectedTerritory == Terr) {
    return;
  }

  WorldMap->MulticastSelectTerritory(Terr);
}

void ASkaldPlayerController::HandleEndAttackRequested(bool bConfirmed) {
  UE_LOG(LogSkald, Log, TEXT("HUD end attack %s"),
         bConfirmed ? TEXT("confirmed") : TEXT("cancelled"));
}

void ASkaldPlayerController::HandleEndMovementRequested(bool bConfirmed) {
  UE_LOG(LogSkald, Log, TEXT("HUD end move %s"),
         bConfirmed ? TEXT("confirmed") : TEXT("cancelled"));
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

void ASkaldPlayerController::HandleEngineeringPhase() {
  UE_LOG(LogSkald, Log, TEXT("Engineering phase started"));
  if (MainHudWidget) {
    MainHudWidget->CancelAttackSelection();
    MainHudWidget->CancelMoveSelection();
    MainHudWidget->UpdateInitiativeText(TEXT("Engineering Phase"));
  }
}

void ASkaldPlayerController::HandleTreasurePhase() {
  UE_LOG(LogSkald, Log, TEXT("Treasure phase started"));
  if (MainHudWidget) {
    MainHudWidget->CancelAttackSelection();
    MainHudWidget->CancelMoveSelection();
    MainHudWidget->UpdateInitiativeText(TEXT("Treasure Phase"));
  }
}

void ASkaldPlayerController::HandleMovementPhase() {
  UE_LOG(LogSkald, Log, TEXT("Movement phase started"));
  if (MainHudWidget) {
    MainHudWidget->CancelAttackSelection();
    MainHudWidget->BeginMoveSelection();
    MainHudWidget->UpdateInitiativeText(TEXT("Movement Phase"));
  }
}

void ASkaldPlayerController::HandleEndTurnPhase() {
  UE_LOG(LogSkald, Log, TEXT("EndTurn phase started"));
  if (MainHudWidget) {
    MainHudWidget->ShowEndingTurn();
    MainHudWidget->UpdateInitiativeText(TEXT("End Turn Phase"));
  }
}

void ASkaldPlayerController::HandleRevoltPhase() {
  UE_LOG(LogSkald, Log, TEXT("Revolt phase started"));
  if (MainHudWidget) {
    MainHudWidget->HideEndingTurn();
    MainHudWidget->UpdateInitiativeText(TEXT("Revolt Phase"));
  }
}

void ASkaldPlayerController::HandleTerritorySelected(ATerritory *Terr) {
  if (!Terr) {
    return;
  }

  ServerSelectTerritory(Terr->TerritoryID);

  if (MainHudWidget) {
    FString OwnerName = Terr->OwningPlayer
                            ? Terr->OwningPlayer->PlayerDisplayName
                            : TEXT("Neutral");
    MainHudWidget->UpdateTerritoryInfo(Terr->TerritoryName, OwnerName,
                                       Terr->ArmyUnits);
    MainHudWidget->OnTerritoryClickedUI(Terr);
  }
}

void ASkaldPlayerController::NotifyActionError_Implementation(
    const FString &Message) {
  UE_LOG(LogSkald, Warning, TEXT("%s"), *Message);
  if (MainHudWidget) {
    MainHudWidget->ShowErrorMessage(Message);
  } else if (GEngine) {
    GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, Message);
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

  for (APlayerState *PSBase : CachedGameState->PlayerArray) {
    if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase)) {
      FS_PlayerData Data;
      Data.PlayerID = PS->GetPlayerId();
      Data.PlayerName = PS->PlayerDisplayName;
      Data.IsAI = PS->bIsAI;
      Data.Faction = PS->Faction;
      Data.Resources = PS->Resources;
      Data.IsEliminated = PS->IsEliminated;
      OutPlayers.Add(Data);
    }
  }
}

void ASkaldPlayerController::HandlePlayersUpdated() {
  if (!MainHudWidget || !CachedGameState) {
    return;
  }
  TArray<FS_PlayerData> Players;
  BuildPlayerDataArray(Players);
  MainHudWidget->RefreshPlayerList(Players);

  if (ASkaldPlayerState *LocalPS = GetPlayerState<ASkaldPlayerState>()) {
    MainHudWidget->UpdateResources(LocalPS->Resources);
  }
}

void ASkaldPlayerController::HandleFactionsUpdated() {
  if (!MainHudWidget || !CachedGameState) {
    return;
  }

  TArray<FS_PlayerData> Players;
  BuildPlayerDataArray(Players);
  MainHudWidget->RefreshPlayerList(Players);

  if (ASkaldPlayerState *LocalPS = GetPlayerState<ASkaldPlayerState>()) {
    MainHudWidget->UpdateResources(LocalPS->Resources);
  }
}

void ASkaldPlayerController::HandleWorldStateChanged() {
  if (!MainHudWidget) {
    return;
  }

  // Update territory info for the currently selected territory if available.
  if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          GetWorld(), AWorldMap::StaticClass()))) {
    if (ATerritory *Terr = WorldMap->SelectedTerritory) {
      FString OwnerName = Terr->OwningPlayer
                              ? Terr->OwningPlayer->PlayerDisplayName
                              : TEXT("Neutral");
      MainHudWidget->UpdateTerritoryInfo(Terr->TerritoryName, OwnerName,
                                         Terr->ArmyUnits);
    }
  }

  // Refresh player list from the game state.
  if (CachedGameState) {
    TArray<FS_PlayerData> Players;
    BuildPlayerDataArray(Players);
    MainHudWidget->RefreshPlayerList(Players);
  }

  // Update deploy/phase banners.
  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    MainHudWidget->UpdateDeployableUnits(PS->DeployableUnits);
    MainHudWidget->UpdateResources(PS->Resources);
  }
  if (TurnManager) {
    MainHudWidget->UpdatePhaseBanner(TurnManager->GetCurrentPhase());
  }
}

void ASkaldPlayerController::HandlePlayerLockedIn() {
  HandleFactionLockedIn();
}

void ASkaldPlayerController::HandleFactionLockedIn() {
  if (ChoosePlayerWidget) {
    ChoosePlayerWidget->OnPlayerLockedIn.RemoveDynamic(
        this, &ASkaldPlayerController::HandleFactionLockedIn);
    ChoosePlayerWidget->RemoveFromParent();
    ChoosePlayerWidget = nullptr;
  }

  if (MainHudWidget) {
    MainHudWidget->SetVisibility(ESlateVisibility::Visible);
    if (CachedGameState) {
      TArray<FS_PlayerData> Players;
      BuildPlayerDataArray(Players);
      MainHudWidget->RefreshPlayerList(Players);
    }
    if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
      MainHudWidget->UpdateDeployableUnits(PS->DeployableUnits);
      MainHudWidget->UpdateResources(PS->Resources);
    }
    if (TurnManager) {
      MainHudWidget->UpdatePhaseBanner(TurnManager->GetCurrentPhase());
    }
  }

  SetInputMode(FInputModeGameAndUI());
  bShowMouseCursor = true;
  bEnableClickEvents = true;
  SetIgnoreMoveInput(false);
  SetIgnoreLookInput(false);
  TryBindWorldMap();
}

void ASkaldPlayerController::HandleFighterSelectionLockedIn() {
  if (FighterSelectionWidget) {
    UFighterSelectionWidget *Selection = FighterSelectionWidget;
    Selection->OnLockedIn.RemoveDynamic(
        this, &ASkaldPlayerController::HandleFighterSelectionLockedIn);
    Selection->RemoveFromParent();

    if (CachedGameInstance && CachedGameInstance->GridBattleManager) {
      TArray<FFighter> Fighters;
      for (const FFighterDefinition &Def : Selection->ChosenFighters) {
        FFighter Fighter;
        Fighter.Stats = Def.Stats;
        if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
          Fighter.Faction = PS->Faction;
        }
        Fighters.Add(Fighter);
      }
      CachedGameInstance->GridBattleManager->InitBattle(Fighters, Fighters);
      CachedGameInstance->GridBattleManager->RollInitiative();
      CachedGameInstance->GridBattleManager->OnBattleEnded.AddDynamic(
          this, &ASkaldPlayerController::HandleBattleEnded);
      CachedGameInstance->GridBattleManager->StartRound(
          CachedGameInstance->CombatRandomStream);

      if (BattleHUDWidgetClass) {
        BattleHudWidget =
            CreateWidget<UBattleHUDWidget>(this, BattleHUDWidgetClass);
        if (BattleHudWidget) {
          BattleHudWidget->AddToViewport();
          BattleHudWidget->OnMovePressed.AddDynamic(
              this, &ASkaldPlayerController::BeginMoveMode);
          BattleHudWidget->OnAttackPressed.AddDynamic(
              this, &ASkaldPlayerController::BeginAttackMode);
          BattleHudWidget->BindToFighter(
              CachedGameInstance->GridBattleManager->GetActiveFighter());
        }
      }
    }
    FighterSelectionWidget = nullptr;
    if (MainHudWidget) {
      MainHudWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
  }

  SetInputMode(FInputModeGameAndUI());
  SetIgnoreMoveInput(false);
  SetIgnoreLookInput(false);
}

void ASkaldPlayerController::SetupInputComponent() {
  Super::SetupInputComponent();

  if (InputComponent) {
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this,
                            &ASkaldPlayerController::HandleGridClick);
  }
}

void ASkaldPlayerController::BeginMoveMode() {
  CurrentCommandMode = EBattleCommandMode::Move;
}

void ASkaldPlayerController::BeginAttackMode() {
  CurrentCommandMode = EBattleCommandMode::Attack;
}

void ASkaldPlayerController::HandleGridClick() {
  if (!CachedGameInstance || !CachedGameInstance->GridBattleManager) {
    return;
  }

  UGridBattleManager *GridBattleManager = CachedGameInstance->GridBattleManager;
  AFighterPawn *ActiveFighter = GridBattleManager->GetActiveFighter();
  if (!ActiveFighter) {
    return;
  }

  FHitResult Hit;
  GetHitResultUnderCursor(ECC_Visibility, false, Hit);

  UGridOverlayComponent *GridOverlay = nullptr;
  if (UWorld *World = GetWorld()) {
    for (TActorIterator<AActor> It(World); It; ++It) {
      if (UGridOverlayComponent *Comp =
              It->FindComponentByClass<UGridOverlayComponent>()) {
        GridOverlay = Comp;
        break;
      }
    }
  }

  if (CurrentCommandMode == EBattleCommandMode::Move && GridOverlay) {
    const FIntPoint Cell = GridOverlay->WorldToGrid(Hit.Location);
    ActiveFighter->MoveToCell(Cell);
  } else if (CurrentCommandMode == EBattleCommandMode::Attack) {
    if (AFighterPawn *Target = Cast<AFighterPawn>(Hit.GetActor())) {
      ActiveFighter->PerformAttack(Target);
    }
  }

  GridBattleManager->AdvanceTurn();
  if (BattleHudWidget) {
    BattleHudWidget->BindToFighter(GridBattleManager->GetActiveFighter());
  }

  if (GridOverlay) {
    GridOverlay->ClearHighlights();
  }
  CurrentCommandMode = EBattleCommandMode::None;
}

void ASkaldPlayerController::HandleBattleEnded(ESkaldFaction WinningFaction,
                                               int32 AttackerCasualties,
                                               int32 DefenderCasualties) {
  if (BattleHudWidget) {
    BattleHudWidget->RemoveFromParent();
    BattleHudWidget = nullptr;
  }

  if (VictoryWidgetClass) {
    if (UUserWidget *Widget =
            CreateWidget<UUserWidget>(this, VictoryWidgetClass)) {
      Widget->AddToViewport();
    }
  }

  if (TurnManager) {
    TurnManager->ResolveGridBattleResult();
  }
}
