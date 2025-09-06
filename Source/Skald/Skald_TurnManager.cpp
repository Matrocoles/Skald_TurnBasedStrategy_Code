#include "Skald_TurnManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GridBattleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Skald.h"
#include "Skald_GameInstance.h"
#include "Skald_GameMode.h"
#include "Skald_GameState.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Territory.h"
#include "UI/SkaldMainHUDWidget.h"
#include "WorldMap.h"

ATurnManager::ATurnManager() {
  PrimaryActorTick.bCanEverTick = false;
  bReplicates = true;
  CurrentIndex = 0;
  CachedWorldMap = nullptr;
}

void ATurnManager::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);
  DOREPLIFETIME(ATurnManager, BattleMaps);
}

void ATurnManager::BeginPlay() {
  Super::BeginPlay();

  CachedWorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    if (GI->GridBattleManager) {
      ResolveGridBattleResult();
    }

    if (GI->bResumeTurns) {
      CurrentIndex = GI->SavedTurnIndex;
      CurrentPhase = GI->SavedTurnPhase;
      GI->bResumeTurns = false;

      if (Controllers.IsValidIndex(CurrentIndex)) {
        if (ASkaldPlayerController *Controller =
                Controllers[CurrentIndex].Get()) {
          Controller->StartTurn();
          BroadcastCurrentPhase();
        }
      }
    }
  }

  if (ASkaldGameMode *GM =
          GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
    if (!GM->IsWorldInitialized()) {
      GM->TryInitializeWorldAndStart();
    }
  }
}

void ATurnManager::RegisterController(ASkaldPlayerController *Controller) {
  if (IsValid(Controller) && !Controllers.Contains(Controller)) {
    Controllers.Add(Controller);
    Controller->SetTurnManager(this);
  }
}

void ATurnManager::StartArmyPlacementPhase() {
  CurrentPhase = ETurnPhase::ArmyPlacement;
  CurrentIndex = 0;
  BroadcastCurrentPhase();
}

void ATurnManager::ApplyReinforcementsAndResources(ASkaldPlayerState *PS,
                                                   const TCHAR *Caller) {
  if (!PS) {
    return;
  }
  int32 Owned = 0;
  int32 ResourceGain = 0;
  if (CachedWorldMap) {
    if (CachedWorldMap->Territories.Num() == 0) {
      UE_LOG(LogSkald, Error, TEXT("%s: WorldMap %s has no territories"),
             Caller, *CachedWorldMap->GetName());
      if (GEngine) {
        GEngine->AddOnScreenDebugMessage(
            -1, 5.f, FColor::Red,
            FString::Printf(TEXT("%s: %s has no territories"), Caller,
                            *CachedWorldMap->GetName()));
      }
    } else {
      for (ATerritory *Terr : CachedWorldMap->Territories) {
        if (Terr && Terr->OwningPlayer == PS) {
          ++Owned;
          ResourceGain += Terr->Resources;
        }
      }
    }
  } else {
    UE_LOG(LogSkald, Error, TEXT("%s: WorldMap actor missing"), Caller);
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red,
          FString::Printf(TEXT("%s: WorldMap missing"), Caller));
    }
  }
  const int32 Reinforcements = FMath::CeilToInt(Owned / 3.0f);
  PS->DeployableUnits += Reinforcements;
  PS->Resources += ResourceGain;
  BroadcastDeployableUnits(PS);
  BroadcastResources(PS);
}

void ATurnManager::StartTurns() {
  SortControllersByInitiative();
  CurrentIndex = 0;
  if (Controllers.Num() == 0) {
    UE_LOG(LogSkald, Error,
           TEXT("StartTurns failed: no controllers registered"));
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red,
                                       TEXT("StartTurns failed: no players"));
    }
    return;
  }

  if (!Controllers.IsValidIndex(CurrentIndex) ||
      !Controllers[CurrentIndex].IsValid()) {
    UE_LOG(LogSkald, Error,
           TEXT("StartTurns failed: invalid starting controller"));
    if (GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red,
          TEXT("StartTurns: invalid starting controller"));
    }
    return;
  }

  ASkaldPlayerController *CurrentController = Controllers[CurrentIndex].Get();
  ASkaldPlayerState *PS =
      CurrentController ? CurrentController->GetPlayerState<ASkaldPlayerState>()
                        : nullptr;
  const FString PlayerName = PS ? PS->PlayerDisplayName : TEXT("Unknown");
  ApplyReinforcementsAndResources(PS, TEXT("StartTurns"));

  CurrentPhase = ETurnPhase::Reinforcement;
  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr :
       Controllers) {
    if (ASkaldPlayerController *Controller = ControllerPtr.Get()) {
      const bool bIsActive = Controller == CurrentController;
      Controller->ShowTurnAnnouncement(PlayerName, bIsActive);
      ASkaldPlayerState *ControllerPS =
          Controller->GetPlayerState<ASkaldPlayerState>();
      const bool bIsAI = ControllerPS && ControllerPS->bIsAI;
      if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
        HUD->UpdateTurnBanner(PS ? PS->GetPlayerId() : -1, 1);
        HUD->UpdatePhaseBanner(CurrentPhase);
      } else if (!bIsAI && Controller->IsLocalController()) {
        UE_LOG(LogSkald, Warning,
               TEXT("StartTurns: Controller %s missing HUD widget"),
               *Controller->GetName());
        if (GEngine) {
          GEngine->AddOnScreenDebugMessage(
              -1, 5.f, FColor::Yellow,
              FString::Printf(TEXT("StartTurns: no HUD for %s"),
                              *Controller->GetName()));
        }
      }
    }
  }

  if (CurrentController) {
    CurrentController->StartTurn();
    if (ASkaldGameMode *GM = GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
      GM->CheckVictoryConditions();
    }
  }

  OnWorldStateChanged.Broadcast();
}

void ATurnManager::AdvanceTurn() {
  ASkaldPlayerController *PreviousController =
      Controllers.IsValidIndex(CurrentIndex) ? Controllers[CurrentIndex].Get()
                                             : nullptr;

  Controllers.RemoveAll([](const TWeakObjectPtr<ASkaldPlayerController> &Ptr) {
    if (!Ptr.IsValid()) {
      return true;
    }
    if (ASkaldPlayerController *PC = Ptr.Get()) {
      if (ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>()) {
        return PS->IsEliminated;
      }
    }
    return false;
  });
  if (Controllers.Num() == 0) {
    return;
  }

  if (!CachedWorldMap || CachedWorldMap->Territories.Num() == 0) {
    UE_LOG(LogSkald, Warning,
           TEXT("AdvanceTurn aborted: WorldMap missing or has no territories"));
    return;
  }

  int32 FoundIndex = Controllers.IndexOfByPredicate(
      [PreviousController](const TWeakObjectPtr<ASkaldPlayerController> &Ptr) {
        return Ptr.Get() == PreviousController;
      });
  CurrentIndex =
      (FoundIndex != INDEX_NONE) ? FoundIndex : Controllers.Num() - 1;

  CurrentIndex = (CurrentIndex + 1) % Controllers.Num();
  if (ASkaldPlayerController *CurrentController =
          Controllers[CurrentIndex].Get()) {
    ASkaldPlayerState *PS =
        CurrentController->GetPlayerState<ASkaldPlayerState>();
    const FString PlayerName = PS ? PS->PlayerDisplayName : TEXT("Unknown");
    ApplyReinforcementsAndResources(PS, TEXT("AdvanceTurn"));

    CurrentPhase = ETurnPhase::Reinforcement;
    for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr :
         Controllers) {
      if (ASkaldPlayerController *Controller = ControllerPtr.Get()) {
        const bool bIsActive = Controller == CurrentController;
        Controller->ShowTurnAnnouncement(PlayerName, bIsActive);
        if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
          HUD->UpdateTurnBanner(PS ? PS->GetPlayerId() : -1, 1);
          HUD->UpdatePhaseBanner(CurrentPhase);
        }
      }
    }

    CurrentController->StartTurn();
    if (ASkaldGameMode *GM = GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
      GM->CheckVictoryConditions();
    }
  }

  OnWorldStateChanged.Broadcast();
}

void ATurnManager::SortControllersByInitiative() {
  Controllers.RemoveAll([](const TWeakObjectPtr<ASkaldPlayerController> &Ptr) {
    return !Ptr.IsValid();
  });
  Controllers.Sort([](const TWeakObjectPtr<ASkaldPlayerController> &A,
                      const TWeakObjectPtr<ASkaldPlayerController> &B) {
    const ASkaldPlayerState *PSA =
        A.IsValid() ? A->GetPlayerState<ASkaldPlayerState>() : nullptr;
    const ASkaldPlayerState *PSB =
        B.IsValid() ? B->GetPlayerState<ASkaldPlayerState>() : nullptr;
    const int32 RollA = PSA ? PSA->InitiativeRoll : 0;
    const int32 RollB = PSB ? PSB->InitiativeRoll : 0;
    return RollA > RollB;
  });
}

TArray<ASkaldPlayerController *> ATurnManager::GetControllers() const {
  TArray<ASkaldPlayerController *> Result;
  for (const TWeakObjectPtr<ASkaldPlayerController> &Ptr : Controllers) {
    if (Ptr.IsValid()) {
      Result.Add(Ptr.Get());
    }
  }
  return Result;
}

void ATurnManager::TriggerGridBattle(const FS_BattlePayload &Battle) {
  FS_BattlePayload SeededBattle = Battle;
  SeededBattle.RandomSeed = FMath::Rand();
  PendingBattle = SeededBattle;

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    GI->SeedCombatRandomStream(SeededBattle.RandomSeed);
    GI->PendingBattle = SeededBattle;
    if (!GI->GridBattleManager) {
      GI->GridBattleManager = NewObject<UGridBattleManager>(GI);
    }
  }

  // Save the current turn state so it can be restored after travelling.
  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    GI->SavedTurnIndex = CurrentIndex;
    GI->SavedTurnPhase = CurrentPhase;
    GI->bResumeTurns = true;
  }

  // Load a battle map where the grid based combat takes place.
  if (UWorld *World = GetWorld()) {
    FString MapToLoad = TEXT("BattleMap");
    if (BattleMaps.Num() > 0) {
      const int32 Index = FMath::RandRange(0, BattleMaps.Num() - 1);
      const FString Selected =
          BattleMaps[Index].ToSoftObjectPath().GetLongPackageName();
      if (!Selected.IsEmpty()) {
        MapToLoad = Selected;
      }
    }
    World->ServerTravel(MapToLoad);
  }
}

void ATurnManager::ResolveGridBattleResult_Implementation() {
  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI || !GI->GridBattleManager) {
    return;
  }

  const FS_BattlePayload Battle = GI->PendingBattle;
  PendingBattle = Battle;

  if (!CachedWorldMap) {
    return;
  }

  ATerritory *Source = CachedWorldMap->GetTerritoryById(Battle.FromTerritoryID);
  ATerritory *Target =
      CachedWorldMap->GetTerritoryById(Battle.TargetTerritoryID);
  if (!Source || !Target) {
    return;
  }

  const int32 AttackerID =
      Source->OwningPlayer ? Source->OwningPlayer->GetPlayerId() : -1;
  const int32 DefenderID =
      Target->OwningPlayer ? Target->OwningPlayer->GetPlayerId() : -1;
  const int32 InitialSourceArmy = Source->ArmyUnits;
  const int32 InitialTargetArmy = Target->ArmyUnits;

  const int32 AttackerSurvivors = GI->GridBattleManager->GetAttackerSurvivors();
  const int32 DefenderSurvivors = GI->GridBattleManager->GetDefenderSurvivors();

  // Army-cost totals for potential downstream use
  const int32 AttackerSurvivorCost =
      GI->GridBattleManager->GetAttackerSurvivorCost();
  const int32 DefenderSurvivorCost =
      GI->GridBattleManager->GetDefenderSurvivorCost();

  Source->ArmyUnits -= Battle.ArmyCountSent;

  int32 WinningPlayerID = DefenderID;
  int32 NewOwnerPlayerID = DefenderID;
  if (AttackerSurvivors > 0 && DefenderSurvivors <= 0) {
    Target->OwningPlayer = Source->OwningPlayer;
    Target->ArmyUnits = AttackerSurvivors;
    WinningPlayerID = AttackerID;
    NewOwnerPlayerID = AttackerID;
  } else {
    Target->ArmyUnits = DefenderSurvivors;
  }

  const int32 AttackerCasualties =
      InitialSourceArmy - (Source->ArmyUnits + AttackerSurvivors);
  const int32 DefenderCasualties = InitialTargetArmy - DefenderSurvivors;

  Source->RefreshAppearance();
  Target->RefreshAppearance();

  GI->PendingBattle = FS_BattlePayload();
  GI->GridBattleManager = nullptr;
  PendingBattle = FS_BattlePayload();

  // Resume the saved turn sequence now that the battle has been resolved.
  if (GI->bResumeTurns) {
    CurrentIndex = GI->SavedTurnIndex;
    CurrentPhase = GI->SavedTurnPhase;
    GI->bResumeTurns = false;

    if (Controllers.IsValidIndex(CurrentIndex)) {
      if (ASkaldPlayerController *Controller =
              Controllers[CurrentIndex].Get()) {
        Controller->StartTurn();
        BroadcastCurrentPhase();
      }
    }
  }

  if (ASkaldGameMode *GM = GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
    GM->CheckVictoryConditions();
  }

  ClientBattleResolved(WinningPlayerID, AttackerCasualties, DefenderCasualties,
                       Source->TerritoryID, Target->TerritoryID,
                       NewOwnerPlayerID, Source->ArmyUnits, Target->ArmyUnits);
}

void ATurnManager::ClientBattleResolved_Implementation(
    int32 WinningPlayerID, int32 AttackerCasualties, int32 DefenderCasualties,
    int32 FromTerritoryID, int32 TargetTerritoryID, int32 NewOwnerPlayerID,
    int32 SourceArmy, int32 TargetArmy) {
  if (CachedWorldMap) {
    ATerritory *Source = CachedWorldMap->GetTerritoryById(FromTerritoryID);
    ATerritory *Target = CachedWorldMap->GetTerritoryById(TargetTerritoryID);
    if (Source) {
      Source->ArmyUnits = SourceArmy;
      Source->RefreshAppearance();
    }
    if (Target) {
      ASkaldPlayerState *NewOwner = nullptr;
      if (ASkaldGameState *GS = GetWorld()->GetGameState<ASkaldGameState>()) {
        NewOwner = GS->GetPlayerById(NewOwnerPlayerID);
      }
      Target->OwningPlayer = NewOwner;
      Target->ArmyUnits = TargetArmy;
      Target->RefreshAppearance();
    }
  }

  for (FConstPlayerControllerIterator It =
           GetWorld()->GetPlayerControllerIterator();
       It; ++It) {
    if (ASkaldPlayerController *PC = Cast<ASkaldPlayerController>(It->Get())) {
      if (USkaldMainHUDWidget *HUD = PC->GetHUDWidget()) {
        const FString Msg = FString::Printf(
            TEXT("Player %d won: A-%d D-%d casualties"), WinningPlayerID,
            AttackerCasualties, DefenderCasualties);
        HUD->UpdateInitiativeText(Msg);
      }
      PC->HandleWorldStateChanged();
    }
  }

  OnWorldStateChanged.Broadcast();
}

void ATurnManager::BeginAttackPhase() {
  // Enter the attack phase and notify all listeners so they can swap controls.
  CurrentPhase = ETurnPhase::Attack;

  BroadcastCurrentPhase();
}

void ATurnManager::AdvancePhase() {
  if (CurrentPhase == ETurnPhase::Reinforcement) {
    BeginAttackPhase();
    return;
  }

  switch (CurrentPhase) {
  case ETurnPhase::Attack:
    CurrentPhase = ETurnPhase::Engineering;
    break;
  case ETurnPhase::Engineering:
    CurrentPhase = ETurnPhase::Treasure;
    break;
  case ETurnPhase::Treasure:
    CurrentPhase = ETurnPhase::Movement;
    break;
  case ETurnPhase::Movement:
    CurrentPhase = ETurnPhase::EndTurn;
    break;
  case ETurnPhase::EndTurn:
    CurrentPhase = ETurnPhase::Revolt;
    break;
  default:
    return;
  }

  BroadcastCurrentPhase();
}

void ATurnManager::BroadcastDeployableUnits(ASkaldPlayerState *ForPlayer) {
  if (!ForPlayer) {
    return;
  }
  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr :
       Controllers) {
    if (ASkaldPlayerController *Controller = ControllerPtr.Get()) {
      ASkaldPlayerState *PS = Controller->GetPlayerState<ASkaldPlayerState>();
      if (PS == ForPlayer) {
        if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
          HUD->UpdateDeployableUnits(ForPlayer->DeployableUnits);
        }
      }
    }
  }

  OnWorldStateChanged.Broadcast();
}

void ATurnManager::BroadcastResources(ASkaldPlayerState *ForPlayer) {
  if (!ForPlayer) {
    return;
  }

  if (ASkaldGameMode *GM = GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
    GM->UpdatePlayerResources(ForPlayer);
  }

  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr :
       Controllers) {
    if (ASkaldPlayerController *Controller = ControllerPtr.Get()) {
      if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
        HUD->UpdateResources(ForPlayer->Resources);
      }
    }
  }

  OnWorldStateChanged.Broadcast();
}

void ATurnManager::BroadcastCurrentPhase() {
  const FString PhaseString = UEnum::GetValueAsString(CurrentPhase);
  UE_LOG(LogSkald, Log, TEXT("BroadcastCurrentPhase: %s"), *PhaseString);
  if (GEngine) {
    GEngine->AddOnScreenDebugMessage(
        -1, 5.f, FColor::Green,
        FString::Printf(TEXT("Current Phase: %s"), *PhaseString));
  }

  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr :
       Controllers) {
    if (ASkaldPlayerController *Controller = ControllerPtr.Get()) {
      if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
        HUD->UpdatePhaseBanner(CurrentPhase);
      }

      switch (CurrentPhase) {
      case ETurnPhase::Engineering:
        Controller->HandleEngineeringPhase();
        break;
      case ETurnPhase::Treasure:
        Controller->HandleTreasurePhase();
        break;
      case ETurnPhase::Movement:
        Controller->HandleMovementPhase();
        break;
      case ETurnPhase::EndTurn:
        Controller->HandleEndTurnPhase();
        break;
      case ETurnPhase::Revolt:
        Controller->HandleRevoltPhase();
        break;
      default:
        break;
      }
    }
  }

  OnWorldStateChanged.Broadcast();
}
