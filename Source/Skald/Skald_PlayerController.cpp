#include "Skald_PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include <limits>
#include "Kismet/GameplayStatics.h"
#include "Skald.h"
#include "SkaldTypes.h"
#include "Skald_GameInstance.h"
#include "Skald_GameMode.h"
#include "Skald_GameState.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "Territory.h"
#include "UI/SkaldMainHUDWidget.h"
#include "ChoosePlayerWidget.h"
#include "WorldMap.h"

ASkaldPlayerController::ASkaldPlayerController() {
  bIsAI = false;
  TurnManager = nullptr;
  HUDRef = nullptr;
  MainHudWidget = nullptr;

  bShowMouseCursor = true;
  bEnableClickEvents = true;
  bEnableMouseOverEvents = true;
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

  // Create and show the HUD widget if a class has been assigned (expected via
  // blueprint).
  if (MainHudWidgetClass) {
    MainHudWidget = CreateWidget<USkaldMainHUDWidget>(this, MainHudWidgetClass);
    if (MainHudWidget) {
      HUDRef = MainHudWidget;
      MainHudWidget->AddToViewport();
      MainHudWidget->SetVisibility(ESlateVisibility::Hidden);

      if (CachedGameState) {
        TArray<FS_PlayerData> Players;
        for (APlayerState *PSBase : CachedGameState->PlayerArray) {
          if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase)) {
            FS_PlayerData Data;
            Data.PlayerID = PS->GetPlayerId();
            Data.PlayerName = PS->DisplayName;
            Data.IsAI = PS->bIsAI;
            Data.Faction = PS->Faction;
            Players.Add(Data);
          }
        }

        const ASkaldPlayerState *CurrentPS =
            CachedGameState->GetCurrentPlayer();
        const int32 CurrentID = CurrentPS ? CurrentPS->GetPlayerId() : -1;
        MainHudWidget->RefreshFromState(CurrentID, /*TurnNumber*/ 1,
                                        ETurnPhase::Reinforcement, Players);
      }

      if (ASkaldPlayerState *LocalPS = GetPlayerState<ASkaldPlayerState>()) {
        MainHudWidget->UpdateResources(LocalPS->Resources);
      }

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
           TEXT("MainHudWidgetClass is null; HUD will not be displayed."));
  }

  if (!ChoosePlayerWidget) {
    if (UClass *ChooseWidgetClass = LoadClass<UChoosePlayerWidget>(
            nullptr,
            TEXT("/Game/Blueprints/UI/Skald_ChoosePlayerWidget.Skald_ChoosePlayerWidget_C"))) {
      ChoosePlayerWidget =
          CreateWidget<UChoosePlayerWidget>(this, ChooseWidgetClass);
      if (ChoosePlayerWidget) {
        ChoosePlayerWidget->OnPlayerLockedIn.AddDynamic(
            this, &ASkaldPlayerController::HandlePlayerLockedIn);
        ChoosePlayerWidget->AddToViewport();
      }
    }
  }

  if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          GetWorld(), AWorldMap::StaticClass()))) {
    WorldMap->OnTerritorySelected.AddDynamic(
        this, &ASkaldPlayerController::HandleTerritorySelected);
  }

  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    bIsAI = PS->bIsAI;
  }
}

void ASkaldPlayerController::ServerInitPlayerState_Implementation(
    const FString &Name, ESkaldFaction Faction) {
  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    PS->DisplayName = Name;
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

  if (!TurnManager) {
    UE_LOG(
        LogSkald, Warning,
        TEXT("EndTurn called without a TurnManager. Attempting to reacquire."));

    if (!CachedGameMode) {
      CachedGameMode = GetWorld()->GetAuthGameMode<ASkaldGameMode>();
    }

    if (CachedGameMode) {
      SetTurnManager(CachedGameMode->GetTurnManager());
    }

    if (!TurnManager) {
      UE_LOG(LogSkald, Warning,
             TEXT("TurnManager still missing; aborting EndTurn."));
      return;
    }
  }

  TurnManager->AdvanceTurn();
}

void ASkaldPlayerController::EndPhase() {
  if (!TurnManager) {
    UE_LOG(
        LogSkald, Warning,
        TEXT(
            "EndPhase called without a TurnManager. Attempting to reacquire."));

    if (!CachedGameMode) {
      CachedGameMode = GetWorld()->GetAuthGameMode<ASkaldGameMode>();
    }

    if (CachedGameMode) {
      SetTurnManager(CachedGameMode->GetTurnManager());
    }

    if (!TurnManager) {
      UE_LOG(LogSkald, Warning,
             TEXT("TurnManager still missing; aborting EndPhase."));
      return;
    }
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
  while (true) {
    if (Phase == ETurnPhase::Reinforcement) {
      TArray<ATerritory *> OwnedTerritories;
      for (ATerritory *Territory : WorldMap->Territories) {
        if (Territory && Territory->OwningPlayer == PS) {
          OwnedTerritories.Add(Territory);
        }
      }

      int32 SpreadIndex = 0;
      while (PS->ArmyPool > 0 && OwnedTerritories.Num() > 0) {
        ATerritory *TargetTerritory =
            OwnedTerritories[SpreadIndex % OwnedTerritories.Num()];
        ++TargetTerritory->ArmyStrength;
        TargetTerritory->RefreshAppearance();
        --PS->ArmyPool;
        --PS->Resources;
        ++SpreadIndex;
      }
      PS->ForceNetUpdate();
      TurnManager->BroadcastArmyPool(PS);
      TurnManager->BroadcastResources(PS);

      TurnManager->AdvancePhase();
      Phase = TurnManager->GetCurrentPhase();
      continue;
    }

    if (Phase == ETurnPhase::Attack) {
      ATerritory *BestSource = nullptr;
      ATerritory *BestTarget = nullptr;
      int32 WeakestStrength = std::numeric_limits<int32>::max();

      for (ATerritory *Source : WorldMap->Territories) {
        if (!Source || Source->OwningPlayer != PS ||
            Source->ArmyStrength <= 1) {
          continue;
        }

        for (ATerritory *Neighbor : Source->AdjacentTerritories) {
          if (!Neighbor || Neighbor->OwningPlayer == PS) {
            continue;
          }

          if (Neighbor->ArmyStrength < WeakestStrength) {
            BestSource = Source;
            BestTarget = Neighbor;
            WeakestStrength = Neighbor->ArmyStrength;
          }
        }
      }

      if (BestSource && BestTarget && BestSource->ArmyStrength > 1) {
        const int32 ArmySent = BestSource->ArmyStrength - 1;
        HandleAttackRequested(BestSource->TerritoryID, BestTarget->TerritoryID,
                              ArmySent, false);
      }

      TurnManager->AdvancePhase();
      Phase = TurnManager->GetCurrentPhase();
      continue;
    }

    if (Phase == ETurnPhase::Engineering || Phase == ETurnPhase::Treasure) {
      TurnManager->AdvancePhase();
      Phase = TurnManager->GetCurrentPhase();
      continue;
    }

    if (Phase == ETurnPhase::Movement) {
      ATerritory *BestSource = nullptr;
      ATerritory *BestTarget = nullptr;
      int32 WeakestStrength = std::numeric_limits<int32>::max();

      for (ATerritory *Source : WorldMap->Territories) {
        if (!Source || Source->OwningPlayer != PS ||
            Source->ArmyStrength <= 1) {
          continue;
        }

        for (ATerritory *Neighbor : Source->AdjacentTerritories) {
          if (!Neighbor || Neighbor->OwningPlayer != PS) {
            continue;
          }

          if (Neighbor->ArmyStrength < WeakestStrength) {
            BestSource = Source;
            BestTarget = Neighbor;
            WeakestStrength = Neighbor->ArmyStrength;
          }
        }
      }

      if (BestSource && BestTarget) {
        int32 TroopsToMove = BestSource->ArmyStrength / 2;
        TroopsToMove =
            FMath::Clamp(TroopsToMove, 1, BestSource->ArmyStrength - 1);
        HandleMoveRequested(BestSource->TerritoryID, BestTarget->TerritoryID,
                            TroopsToMove);
      }

      EndTurn();
      return;
    }

    // Any other phase ends the turn immediately.
    EndTurn();
    return;
  }
}

bool ASkaldPlayerController::IsAIController() const { return bIsAI; }

void ASkaldPlayerController::HandleAttackRequested(int32 FromID, int32 ToID,
                                                   int32 ArmySent,
                                                   bool bUseSiege) {
  UE_LOG(LogSkald, Log, TEXT("HUD attack from %d to %d with %d"), FromID, ToID,
         ArmySent);

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

  if (!Source->IsAdjacentTo(Target)) {
    NotifyActionError(TEXT("Cannot attack non-adjacent territory"));
    return;
  }

  if (ArmySent <= 0 || ArmySent >= Source->ArmyStrength) {
    NotifyActionError(TEXT("Invalid army count for attack"));
    return;
  }

  if (!SkaldHelpers::MeetsCapitalAttackRequirement(Target->bIsCapital,
                                                   ArmySent)) {
    NotifyActionError(TEXT("Insufficient forces to attack capital"));
    return;
  }

  ServerHandleAttack(FromID, ToID, ArmySent, bUseSiege);
}

void ASkaldPlayerController::ServerHandleAttack_Implementation(int32 FromID,
                                                               int32 ToID,
                                                               int32 ArmySent,
                                                               bool bUseSiege) {
  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    NotifyActionError(TEXT("World map not found"));
    return;
  }

  ATerritory *Source = WorldMap->GetTerritoryById(FromID);
  ATerritory *Target = WorldMap->GetTerritoryById(ToID);
  if (!Source || !Target || !Source->IsAdjacentTo(Target)) {
    return;
  }

  if (ArmySent <= 0 || ArmySent >= Source->ArmyStrength) {
    return;
  }

  if (!SkaldHelpers::MeetsCapitalAttackRequirement(Target->bIsCapital,
                                                   ArmySent)) {
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
  int32 DefendingForces = Target->ArmyStrength;
  if (bUseSiege && CachedGameMode) {
    CachedGameMode->ConsumeSiege(FromID);
  }

  Source->ArmyStrength -= ArmySent;

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
    Target->ArmyStrength = AttackingForces;
  } else {
    Target->ArmyStrength = DefendingForces;
  }

  Source->RefreshAppearance();
  Target->RefreshAppearance();

  Source->ForceNetUpdate();
  Target->ForceNetUpdate();

  if (TurnManager) {
    for (ASkaldPlayerController* Controller : TurnManager->GetControllers()) {
      if (USkaldMainHUDWidget* HUD = Controller ? Controller->GetHUDWidget() : nullptr) {
        FString OwnerName = Target->OwningPlayer
                                ? Target->OwningPlayer->DisplayName
                                : TEXT("Neutral");
        HUD->UpdateTerritoryInfo(Target->TerritoryName, OwnerName,
                                 Target->ArmyStrength);
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

  if (Troops <= 0 || Troops >= Source->ArmyStrength) {
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

  if (Troops <= 0 || Troops >= Source->ArmyStrength) {
    return;
  }

  if (!WorldMap->MoveBetween(Source, Target, Troops)) {
    return;
  }

  if (TurnManager) {
    for (ASkaldPlayerController* Controller : TurnManager->GetControllers()) {
      if (USkaldMainHUDWidget* HUD = Controller ? Controller->GetHUDWidget() : nullptr) {
        FString SourceOwner = Source->OwningPlayer
                                  ? Source->OwningPlayer->DisplayName
                                  : TEXT("Neutral");
        HUD->UpdateTerritoryInfo(Source->TerritoryName, SourceOwner,
                                 Source->ArmyStrength);
        FString TargetOwner = Target->OwningPlayer
                                  ? Target->OwningPlayer->DisplayName
                                  : TEXT("Neutral");
        HUD->UpdateTerritoryInfo(Target->TerritoryName, TargetOwner,
                                 Target->ArmyStrength);
      }
    }
  }
}

void ASkaldPlayerController::ServerBuildSiege_Implementation(
    int32 TerritoryID, E_SiegeWeapons SiegeType) {
  if (CachedGameMode) {
    CachedGameMode->BuildSiegeAtTerritory(TerritoryID, SiegeType);
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
  if (!Terr) {
    return;
  }

  FString OwnerName =
      Terr->OwningPlayer ? Terr->OwningPlayer->DisplayName : TEXT("Neutral");

  Terr->ForceNetUpdate();

  if (TurnManager) {
    for (ASkaldPlayerController* Controller : TurnManager->GetControllers()) {
      if (USkaldMainHUDWidget* HUD = Controller ? Controller->GetHUDWidget() : nullptr) {
        HUD->UpdateTerritoryInfo(Terr->TerritoryName, OwnerName,
                                 Terr->ArmyStrength);
      }
    }
  }
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
    PS->ForceNetUpdate();
    if (TurnManager) {
      TurnManager->BroadcastResources(PS);
    }
  }
}

void ASkaldPlayerController::HandleBuildSiegeRequested(
    int32 TerritoryID, E_SiegeWeapons SiegeType) {
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
    if (Terr->OwningPlayer == PS && Terr->HasTreasure) {
      Terr->HasTreasure = false;
      Terr->RefreshAppearance();
      PS->Resources += 5;
      PS->ForceNetUpdate();
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
}

void ASkaldPlayerController::NotifyActionError(const FString &Message) {
  UE_LOG(LogSkald, Warning, TEXT("%s"), *Message);
  if (MainHudWidget) {
    MainHudWidget->ShowErrorMessage(Message);
  } else if (GEngine) {
    GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, Message);
  }
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
      Data.PlayerName = PS->DisplayName;
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
      FString OwnerName = Terr->OwningPlayer ? Terr->OwningPlayer->DisplayName
                                             : TEXT("Neutral");
      MainHudWidget->UpdateTerritoryInfo(Terr->TerritoryName, OwnerName,
                                         Terr->ArmyStrength);
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
    MainHudWidget->UpdateDeployableUnits(PS->ArmyPool);
    MainHudWidget->UpdateResources(PS->Resources);
  }
  if (TurnManager) {
    MainHudWidget->UpdatePhaseBanner(TurnManager->GetCurrentPhase());
  }
}

void ASkaldPlayerController::HandlePlayerLockedIn() {
  if (ChoosePlayerWidget) {
    ChoosePlayerWidget->OnPlayerLockedIn.RemoveDynamic(
        this, &ASkaldPlayerController::HandlePlayerLockedIn);
    ChoosePlayerWidget->RemoveFromParent();
  }
  if (MainHudWidget) {
    MainHudWidget->SetVisibility(ESlateVisibility::Visible);
  }
}
