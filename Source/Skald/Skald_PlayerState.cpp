#include "Skald_PlayerState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Crc.h"
#include "Net/UnrealNetwork.h"
#include "OnlineSubsystemTypes.h"
#include "Skald.h"
#include "SkaldLogging.h"
#include "Skald_GameState.h"
#include "Skald_PlayerController.h"
#include "Territory.h"
#include "UI/SkaldMainHUDWidget.h"

ASkaldPlayerState::ASkaldPlayerState()
    : DeployableUnits(0), InitiativeRoll(0), Resources(0),
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

void ASkaldPlayerState::ResetArmyPlacementDeployments() {
  ArmyPlacementDeployments.Reset();
}

void ASkaldPlayerState::AddArmyPlacementDeployment(int32 TerritoryId,
                                                   int32 Amount) {
  if (Amount <= 0 || TerritoryId == INDEX_NONE) {
    return;
  }

  int32 &Value = ArmyPlacementDeployments.FindOrAdd(TerritoryId);
  Value += Amount;
}

int32 ASkaldPlayerState::GetArmyPlacementDeploymentForTerritory(
    int32 TerritoryId) const {
  if (TerritoryId == INDEX_NONE) {
    return 0;
  }

  const int32 *Found = ArmyPlacementDeployments.Find(TerritoryId);
  return Found ? *Found : 0;
}

bool ASkaldPlayerState::HasArmyPlacementDeployments() const {
  for (const TPair<int32, int32> &Entry : ArmyPlacementDeployments) {
    if (Entry.Value > 0) {
      return true;
    }
  }

  return false;
}

int32 ASkaldPlayerState::GetAuthoritativePlayerId() const {
  const int32 ResolvedPlayerId = GetPlayerId();
  if (ResolvedPlayerId != INDEX_NONE && ResolvedPlayerId >= 0) {
    return ResolvedPlayerId;
  }

  const FUniqueNetIdRepl &NetId = GetUniqueId();
  if (NetId.IsValid()) {
    if (TSharedPtr<const FUniqueNetId> UniqueIdHandle = NetId.GetUniqueNetId()) {
      const uint32 NetIdHash = FCrc::StrCrc32(*UniqueIdHandle->ToString());
      // Clamp to the positive int32 range so we can continue using a single
      // integer identifier throughout the UI/world map selection code paths.
      return static_cast<int32>(NetIdHash & 0x7fffffff);
    }

    // In some editor scenarios the replicated ID can be flagged as valid but
    // the underlying pointer has not been resolved yet. Fall back to the
    // UObject unique ID in that rare case so we always return a stable value
    // without requiring OnlineSubsystem symbols at link-time.
    UE_LOG(LogSkald, Verbose,
           TEXT("GetAuthoritativePlayerId could not resolve NetId handle for %s"),
           *GetResolvedPlayerName(TEXT("SkaldPlayerState::GetAuthoritativePlayerId")));
  }

  return static_cast<int32>(GetUniqueID());
}
