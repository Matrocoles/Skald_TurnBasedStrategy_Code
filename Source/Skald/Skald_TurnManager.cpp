#include "Skald_TurnManager.h"
#include "Kismet/GameplayStatics.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "Territory.h"
#include "UI/SkaldMainHUDWidget.h"
#include "WorldMap.h"

ATurnManager::ATurnManager() {
  PrimaryActorTick.bCanEverTick = false;
  CurrentIndex = 0;
}

void ATurnManager::BeginPlay() { Super::BeginPlay(); }

void ATurnManager::RegisterController(ASkaldPlayerController *Controller) {
  if (Controller) {
    Controllers.Add(Controller);
    Controller->SetTurnManager(this);
  }
}

void ATurnManager::StartTurns() {
  SortControllersByInitiative();
  CurrentIndex = 0;
  if (Controllers.IsValidIndex(CurrentIndex)) {
    ASkaldPlayerController *CurrentController = Controllers[CurrentIndex];
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
      PS->ArmyPool += Reinforcements;
      PS->ForceNetUpdate();
      BroadcastArmyPool(PS);
    }

    CurrentPhase = ETurnPhase::Reinforcement;
    for (ASkaldPlayerController *Controller : Controllers) {
      if (Controller) {
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

void ATurnManager::AdvanceTurn() {
  if (Controllers.Num() == 0) {
    return;
  }

  CurrentIndex = (CurrentIndex + 1) % Controllers.Num();
  ASkaldPlayerController *CurrentController = Controllers[CurrentIndex];
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
    PS->ArmyPool += Reinforcements;
    PS->ForceNetUpdate();
    BroadcastArmyPool(PS);
  }

  CurrentPhase = ETurnPhase::Reinforcement;
  for (ASkaldPlayerController *Controller : Controllers) {
    if (Controller) {
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

void ATurnManager::SortControllersByInitiative() {
  Controllers.Sort(
      [](const ASkaldPlayerController &A, const ASkaldPlayerController &B) {
        const ASkaldPlayerState *PSA = A.GetPlayerState<ASkaldPlayerState>();
        const ASkaldPlayerState *PSB = B.GetPlayerState<ASkaldPlayerState>();
        const int32 RollA = PSA ? PSA->InitiativeRoll : 0;
        const int32 RollB = PSB ? PSB->InitiativeRoll : 0;
        return RollA > RollB;
      });
}

void ATurnManager::TriggerGridBattle(const FS_BattlePayload &Battle) {
  PendingBattle = Battle;
  // Load a battle map where the grid based combat takes place.
  UGameplayStatics::OpenLevel(this, FName("BattleMap"));
}

void ATurnManager::BeginAttackPhase() {
  // Enter the attack phase and notify all HUDs so they can swap controls.
  CurrentPhase = ETurnPhase::Attack;

  for (ASkaldPlayerController *Controller : Controllers) {
    if (Controller) {
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

  for (ASkaldPlayerController *Controller : Controllers) {
    if (Controller) {
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
  for (ASkaldPlayerController *Controller : Controllers) {
    if (USkaldMainHUDWidget *HUD = Controller->GetHUDWidget()) {
      HUD->UpdateDeployableUnits(ForPlayer->ArmyPool);
    }
  }
}
