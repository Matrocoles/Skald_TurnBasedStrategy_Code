#include "Skald_PlayerState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Skald.h"
#include "SkaldLogging.h"
#include "Skald_GameState.h"
#include "Skald_PlayerController.h"
#include "Territory.h"
#include "UI/SkaldMainHUDWidget.h"

ASkaldPlayerState::ASkaldPlayerState()
    : DeployableUnits(0), InitiativeRoll(0),
      bHasAcknowledgedStrategicInitiative(false), Resources(0),
      PlayerDisplayName(TEXT("")), Faction(ESkaldFaction::None), bIsAI(false),
      bHasLockedIn(false), IsEliminated(false) {}

FString ASkaldPlayerState::GetResolvedPlayerName(const TCHAR *Context) const {
  FString Name = GetPlayerName();
  if (Name.IsEmpty()) {
    Name = PlayerDisplayName;
  }

  if (Name.IsEmpty()) {
    UE_LOG(LogSkald, Warning,
           TEXT("%s: PlayerState %s missing assigned name"), Context,
           *GetName());
    Name = TEXT("Unknown");
  }

  return Name;
}

void ASkaldPlayerState::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  DOREPLIFETIME(ASkaldPlayerState, PlayerDisplayName);
  DOREPLIFETIME(ASkaldPlayerState, Faction);
  DOREPLIFETIME(ASkaldPlayerState, PendingArmy);
  DOREPLIFETIME(ASkaldPlayerState, PendingArmyBudget);
  DOREPLIFETIME(ASkaldPlayerState, bArmyLockedIn);
  DOREPLIFETIME(ASkaldPlayerState, bIsAI);
  DOREPLIFETIME(ASkaldPlayerState, DeployableUnits);
  DOREPLIFETIME(ASkaldPlayerState, InitiativeRoll);
  DOREPLIFETIME(ASkaldPlayerState, bHasAcknowledgedStrategicInitiative);
  DOREPLIFETIME(ASkaldPlayerState, Resources);
  DOREPLIFETIME(ASkaldPlayerState, bHasLockedIn);
  DOREPLIFETIME(ASkaldPlayerState, IsEliminated);
}

void ASkaldPlayerState::OnRep_DeployableUnits() {
  if (APlayerController *PC = GetOwner<APlayerController>()) {
    if (ASkaldPlayerController *SkaldPC = Cast<ASkaldPlayerController>(PC)) {
      if (USkaldMainHUDWidget *HUD = SkaldPC->GetHUDWidget()) {
        HUD->UpdateDeployableUnits(DeployableUnits);
      }
    }
  }
}

void ASkaldPlayerState::OnRep_HasLockedIn() {
  if (UWorld *World = GetWorld()) {
    if (ASkaldGameState *GS = World->GetGameState<ASkaldGameState>()) {
      GS->OnPlayersUpdated.Broadcast();
    }
  }
}

void ASkaldPlayerState::OnRep_IsEliminated() {
  if (UWorld *World = GetWorld()) {
    if (ASkaldGameState *GS = World->GetGameState<ASkaldGameState>()) {
      GS->OnPlayersUpdated.Broadcast();
    }
  }
}

void ASkaldPlayerState::OnRep_PlayerDisplayName() {
  if (UWorld *World = GetWorld()) {
    for (TActorIterator<ATerritory> It(World); It; ++It) {
      ATerritory *Territory = *It;
      if (Territory && Territory->OwningPlayer == this) {
        Territory->RefreshAppearance();
      }
    }

    if (ASkaldGameState *GS = World->GetGameState<ASkaldGameState>()) {
      GS->OnPlayersUpdated.Broadcast();
    }
  }
}

void ASkaldPlayerState::OnRep_IsAI() {
  if (UWorld *World = GetWorld()) {
    if (ASkaldGameState *GS = World->GetGameState<ASkaldGameState>()) {
      GS->OnPlayersUpdated.Broadcast();
    }
  }
}

void ASkaldPlayerState::OnRep_StrategicInitiativeAcknowledged() {
  if (UWorld *World = GetWorld()) {
    if (ASkaldGameState *GS = World->GetGameState<ASkaldGameState>()) {
      GS->OnPlayersUpdated.Broadcast();
    }
  }
}

void ASkaldPlayerState::OnRep_PlayerId() {
  Super::OnRep_PlayerId();

  if (APlayerController *PC = GetOwner<APlayerController>()) {
    if (ASkaldPlayerController *SkaldPC = Cast<ASkaldPlayerController>(PC)) {
      if (USkaldMainHUDWidget *HUD = SkaldPC->GetHUDWidget()) {
        const bool bLocalIdChanged = (HUD->LocalPlayerID != GetPlayerId());
        HUD->LocalPlayerID = GetPlayerId();
        if (bLocalIdChanged) {
          HUD->SyncPhaseButtons(HUD->CurrentPlayerID == HUD->LocalPlayerID);
        }
      }
      SkaldPC->HandlePlayerIdUpdated();
    }
  }

  if (UWorld *World = GetWorld()) {
    if (ASkaldGameState *GS = World->GetGameState<ASkaldGameState>()) {
      GS->OnPlayersUpdated.Broadcast();
    }
  }
}
