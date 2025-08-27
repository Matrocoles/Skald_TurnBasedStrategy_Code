#include "Skald_TurnManager.h"
#include "Kismet/GameplayStatics.h"
#include "Skald_GameInstance.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "GridBattleManager.h"
#include "Territory.h"
#include "UI/SkaldMainHUDWidget.h"
#include "WorldMap.h"

ATurnManager::ATurnManager() {
  PrimaryActorTick.bCanEverTick = false;
  CurrentIndex = 0;
}

void ATurnManager::BeginPlay() {
  Super::BeginPlay();

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    if (GI->GridBattleManager) {
      ResolveGridBattleResult();
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

      // Determine reinforcements based on owned territories.
      if (PS) {
        int32 Owned = 0;
        if (AWorldMap *WorldMap =
                Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
                    GetWorld(), AWorldMap::StaticClass()))) {
          for (ATerritory *Terr : WorldMap->Territories) {
            if (Terr && Terr->OwningPlayer == PS) {
              ++Owned;
            }
          }
        }
        const int32 Reinforcements = FMath::CeilToInt(Owned / 3.0f);
        PS->ArmyPool = Reinforcements;
        PS->ForceNetUpdate();
        BroadcastArmyPool(PS);
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
    }
  }
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

    // Calculate reinforcements for the new active player.
    if (PS) {
      int32 Owned = 0;
      if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
              GetWorld(), AWorldMap::StaticClass()))) {
        for (ATerritory *Terr : WorldMap->Territories) {
          if (Terr && Terr->OwningPlayer == PS) {
            ++Owned;
          }
        }
      }
      const int32 Reinforcements = FMath::CeilToInt(Owned / 3.0f);
      PS->ArmyPool = Reinforcements;
      PS->ForceNetUpdate();
      BroadcastArmyPool(PS);
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
  }
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
  PendingBattle = Battle;

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    GI->PendingBattle = Battle;
    if (!GI->GridBattleManager) {
      GI->GridBattleManager = NewObject<UGridBattleManager>(GI);
    }
  }

  // Load a battle map where the grid based combat takes place.
  UGameplayStatics::OpenLevel(this, FName("BattleMap"));
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

  const int32 AttackerSurvivors = GI->GridBattleManager->GetAttackerSurvivors();
  const int32 DefenderSurvivors = GI->GridBattleManager->GetDefenderSurvivors();

  Source->ArmyStrength -= Battle.ArmyCountSent;

  if (AttackerSurvivors > 0 && DefenderSurvivors <= 0) {
    Target->OwningPlayer = Source->OwningPlayer;
    Target->ArmyStrength = AttackerSurvivors;
  } else {
    Target->ArmyStrength = DefenderSurvivors;
  }

  Source->RefreshAppearance();
  Target->RefreshAppearance();

  Source->ForceNetUpdate();
  Target->ForceNetUpdate();

  GI->PendingBattle = FS_BattlePayload();
  GI->GridBattleManager = nullptr;
}

void ATurnManager::BeginAttackPhase() {
  // Enter the attack phase and notify all HUDs so they can swap controls.
  CurrentPhase = ETurnPhase::Attack;

  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr : Controllers) {
    if (ASkaldPlayerController *Controller = ControllerPtr.Get()) {
      if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
        HUD->UpdatePhaseBanner(ETurnPhase::Attack);
      }
    }
  }
}

void ATurnManager::AdvancePhase() {
  if (CurrentPhase == ETurnPhase::Reinforcement) {
    BeginAttackPhase();
    return;
  }

  if (CurrentPhase == ETurnPhase::Attack) {
    CurrentPhase = ETurnPhase::Movement;
  } else if (CurrentPhase == ETurnPhase::Movement) {
    CurrentPhase = ETurnPhase::EndTurn;
  } else {
    return;
  }

  for (const TWeakObjectPtr<ASkaldPlayerController> &ControllerPtr : Controllers) {
    if (ASkaldPlayerController *Controller = ControllerPtr.Get()) {
      if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
        HUD->UpdatePhaseBanner(CurrentPhase);
      }
    }
  }
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
}
