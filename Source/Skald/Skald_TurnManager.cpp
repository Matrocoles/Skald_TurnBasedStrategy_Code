#include "Skald_TurnManager.h"
#include "Kismet/GameplayStatics.h"
#include "Skald_GameInstance.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "GridBattleManager.h"
#include "Territory.h"
#include "UI/SkaldMainHUDWidget.h"
#include "WorldMap.h"
#include "Skald_GameMode.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ATurnManager::ATurnManager() {
  PrimaryActorTick.bCanEverTick = false;
  bReplicates = true;
  CurrentIndex = 0;
}

void ATurnManager::BeginPlay() {
  Super::BeginPlay();

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    if (GI->GridBattleManager) {
      ResolveGridBattleResult();
    }

    if (GI->bResumeTurns) {
      CurrentIndex = GI->SavedTurnIndex;
      CurrentPhase = GI->SavedTurnPhase;
      GI->bResumeTurns = false;

      if (Controllers.IsValidIndex(CurrentIndex)) {
        if (ASkaldPlayerController *Controller = Controllers[CurrentIndex].Get()) {
          Controller->StartTurn();
          BroadcastCurrentPhase();
        }
      }
    }
  }
}

void ATurnManager::RegisterController(ASkaldPlayerController *Controller) {
  if (IsValid(Controller)) {
    Controllers.Add(Controller);
    Controller->SetTurnManager(this);
  }
}

void ATurnManager::StartTurns() {
  SortControllersByInitiative();
  CurrentIndex = 0;
  if (Controllers.IsValidIndex(CurrentIndex)) {
    if (ASkaldPlayerController *CurrentController = Controllers[CurrentIndex].Get()) {
      ASkaldPlayerState *PS =
          CurrentController->GetPlayerState<ASkaldPlayerState>();
      const FString PlayerName = PS ? PS->DisplayName : TEXT("Unknown");

      // Determine reinforcements and resources based on owned territories.
      if (PS) {
        int32 Owned = 0;
        int32 ResourceGain = 0;
        if (AWorldMap *WorldMap =
                Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
                    GetWorld(), AWorldMap::StaticClass()))) {
          for (ATerritory *Terr : WorldMap->Territories) {
            if (Terr && Terr->OwningPlayer == PS) {
              ++Owned;
              ResourceGain += Terr->Resources;
            }
          }
        }
        const int32 Reinforcements = FMath::CeilToInt(Owned / 3.0f);
        PS->ArmyPool = Reinforcements;
        PS->Resources += ResourceGain;
        PS->ForceNetUpdate();
        BroadcastArmyPool(PS);
        BroadcastResources(PS);
      }

      CurrentPhase = ETurnPhase::Reinforcement;
      for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr : Controllers) {
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
      if (ASkaldGameMode *GM =
              GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
        GM->CheckVictoryConditions();
      }
    }
  }

  OnWorldStateChanged.Broadcast();
}

void ATurnManager::AdvanceTurn() {
  ASkaldPlayerController *PreviousController =
      Controllers.IsValidIndex(CurrentIndex) ? Controllers[CurrentIndex].Get() : nullptr;

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

  int32 FoundIndex = Controllers.IndexOfByPredicate(
      [PreviousController](const TWeakObjectPtr<ASkaldPlayerController> &Ptr) {
        return Ptr.Get() == PreviousController;
      });
  CurrentIndex = (FoundIndex != INDEX_NONE) ? FoundIndex : Controllers.Num() - 1;

  CurrentIndex = (CurrentIndex + 1) % Controllers.Num();
  if (ASkaldPlayerController *CurrentController = Controllers[CurrentIndex].Get()) {
    ASkaldPlayerState *PS =
        CurrentController->GetPlayerState<ASkaldPlayerState>();
    const FString PlayerName = PS ? PS->DisplayName : TEXT("Unknown");

    // Calculate reinforcements and resources for the new active player.
    if (PS) {
      int32 Owned = 0;
      int32 ResourceGain = 0;
      if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
              GetWorld(), AWorldMap::StaticClass()))) {
        for (ATerritory *Terr : WorldMap->Territories) {
          if (Terr && Terr->OwningPlayer == PS) {
            ++Owned;
            ResourceGain += Terr->Resources;
          }
        }
      }
      const int32 Reinforcements = FMath::CeilToInt(Owned / 3.0f);
      PS->ArmyPool = Reinforcements;
      PS->Resources += ResourceGain;
      PS->ForceNetUpdate();
      BroadcastArmyPool(PS);
      BroadcastResources(PS);
    }

    CurrentPhase = ETurnPhase::Reinforcement;
    for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr : Controllers) {
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
    if (ASkaldGameMode *GM =
            GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
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
    World->ServerTravel(TEXT("BattleMap"));
  }
}

void ATurnManager::ResolveGridBattleResult() {
  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI || !GI->GridBattleManager) {
    return;
  }

  const FS_BattlePayload Battle = GI->PendingBattle;
  PendingBattle = Battle;

  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    return;
  }

  ATerritory *Source = WorldMap->GetTerritoryById(Battle.FromTerritoryID);
  ATerritory *Target = WorldMap->GetTerritoryById(Battle.TargetTerritoryID);
  if (!Source || !Target) {
    return;
  }

  const int32 AttackerID =
      Source->OwningPlayer ? Source->OwningPlayer->GetPlayerId() : -1;
  const int32 DefenderID =
      Target->OwningPlayer ? Target->OwningPlayer->GetPlayerId() : -1;
  const int32 InitialSourceArmy = Source->ArmyStrength;
  const int32 InitialTargetArmy = Target->ArmyStrength;

  const int32 AttackerSurvivors = GI->GridBattleManager->GetAttackerSurvivors();
  const int32 DefenderSurvivors = GI->GridBattleManager->GetDefenderSurvivors();

  Source->ArmyStrength -= Battle.ArmyCountSent;

  int32 WinningPlayerID = DefenderID;
  int32 NewOwnerPlayerID = DefenderID;
  if (AttackerSurvivors > 0 && DefenderSurvivors <= 0) {
    Target->OwningPlayer = Source->OwningPlayer;
    Target->ArmyStrength = AttackerSurvivors;
    WinningPlayerID = AttackerID;
    NewOwnerPlayerID = AttackerID;
  } else {
    Target->ArmyStrength = DefenderSurvivors;
  }

  const int32 AttackerCasualties = Battle.ArmyCountSent - AttackerSurvivors;
  const int32 DefenderCasualties = InitialTargetArmy - DefenderSurvivors;

  Source->RefreshAppearance();
  Target->RefreshAppearance();

  Source->ForceNetUpdate();
  Target->ForceNetUpdate();

  GI->PendingBattle = FS_BattlePayload();
  GI->GridBattleManager = nullptr;

  if (ASkaldGameMode *GM =
          GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
    GM->CheckVictoryConditions();
  }

  ClientBattleResolved(WinningPlayerID, AttackerCasualties,
                       DefenderCasualties, Source->TerritoryID,
                       Target->TerritoryID, NewOwnerPlayerID,
                       Source->ArmyStrength, Target->ArmyStrength);
}

void ATurnManager::ClientBattleResolved_Implementation(
    int32 WinningPlayerID, int32 AttackerCasualties, int32 DefenderCasualties,
    int32 FromTerritoryID, int32 TargetTerritoryID, int32 NewOwnerPlayerID,
    int32 SourceArmy, int32 TargetArmy) {
  if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          GetWorld(), AWorldMap::StaticClass()))) {
    ATerritory *Source = WorldMap->GetTerritoryById(FromTerritoryID);
    ATerritory *Target = WorldMap->GetTerritoryById(TargetTerritoryID);
    if (Source) {
      Source->ArmyStrength = SourceArmy;
      Source->RefreshAppearance();
      Source->ForceNetUpdate();
    }
    if (Target) {
      ASkaldPlayerState *NewOwner = nullptr;
      for (TActorIterator<ASkaldPlayerState> It(GetWorld()); It; ++It) {
        if (It->GetPlayerId() == NewOwnerPlayerID) {
          NewOwner = *It;
          break;
        }
      }
      Target->OwningPlayer = NewOwner;
      Target->ArmyStrength = TargetArmy;
      Target->RefreshAppearance();
      Target->ForceNetUpdate();
    }
  }

  for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator();
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

  if (CurrentPhase == ETurnPhase::Attack) {
    CurrentPhase = ETurnPhase::Engineering;
  } else if (CurrentPhase == ETurnPhase::Engineering) {
    CurrentPhase = ETurnPhase::Treasure;
  } else if (CurrentPhase == ETurnPhase::Treasure) {
    CurrentPhase = ETurnPhase::Movement;
  } else if (CurrentPhase == ETurnPhase::Movement) {
    CurrentPhase = ETurnPhase::EndTurn;
  } else if (CurrentPhase == ETurnPhase::EndTurn) {
    CurrentPhase = ETurnPhase::Revolt;
  } else {
    return;
  }

  BroadcastCurrentPhase();
}

void ATurnManager::BroadcastArmyPool(ASkaldPlayerState *ForPlayer) {
  if (!ForPlayer) {
    return;
  }
  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr : Controllers) {
    if (ASkaldPlayerController *Controller = ControllerPtr.Get()) {
      if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
        HUD->UpdateDeployableUnits(ForPlayer->ArmyPool);
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

  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr : Controllers) {
    if (ASkaldPlayerController *Controller = ControllerPtr.Get()) {
      if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
        HUD->UpdateResources(ForPlayer->Resources);
      }
    }
  }

  OnWorldStateChanged.Broadcast();
}

void ATurnManager::BroadcastCurrentPhase() {
  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr : Controllers) {
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
