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
#include "WorldMap.h"
#include "Kismet/GameplayStatics.h"

ASkaldPlayerState::ASkaldPlayerState()
    : DeployableUnits(0), InitiativeRoll(0), Resources(0),
      PlayerDisplayName(TEXT("")), Faction(ESkaldFaction::None), bIsAI(false),
      bHasLockedIn(false), IsEliminated(false), StablePlayerId(INDEX_NONE) {}

void ASkaldPlayerState::BeginPlay() {
  Super::BeginPlay();

  EnsureDefaultPlayerName();

  if (HasAuthority()) {
    RefreshStablePlayerId();
  }
}

FString ASkaldPlayerState::GetResolvedPlayerName(const TCHAR *Context) const {
  FString Name = GetPlayerName();
  if (Name.IsEmpty()) {
    Name = PlayerDisplayName;
  }

  if (Name.IsEmpty()) {
    UE_LOG(LogSkald, Verbose,
           TEXT("%s: PlayerState %s missing assigned name; applying fallback."),
           Context, *GetName());
    const_cast<ASkaldPlayerState *>(this)->EnsureDefaultPlayerName();

    Name = GetPlayerName();
    if (Name.IsEmpty()) {
      Name = PlayerDisplayName;
    }
  }

  if (Name.IsEmpty()) {
    Name = TEXT("Player");
  }

  return Name;
}

void ASkaldPlayerState::EnsureDefaultPlayerName() {
  FString DesiredName = PlayerDisplayName;
  if (DesiredName.IsEmpty()) {
    const int32 StableId = GetAuthoritativePlayerId();
    if (StableId > 0) {
      DesiredName = FString::Printf(TEXT("Player %d"), StableId);
    } else {
      DesiredName = TEXT("Player");
    }
  }

  if (PlayerDisplayName.IsEmpty()) {
    PlayerDisplayName = DesiredName;
  }

  const FString CurrentPlayerName = GetPlayerName();
  if (CurrentPlayerName.IsEmpty() || CurrentPlayerName != PlayerDisplayName) {
    SetPlayerName(PlayerDisplayName);
  }
}

void ASkaldPlayerState::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  DOREPLIFETIME(ASkaldPlayerState, PlayerDisplayName);
  DOREPLIFETIME(ASkaldPlayerState, Faction);
  DOREPLIFETIME(ASkaldPlayerState, PendingArmy);
  DOREPLIFETIME(ASkaldPlayerState, PendingArmyBudget);
  DOREPLIFETIME(ASkaldPlayerState, bArmyLockedIn);
  DOREPLIFETIME(ASkaldPlayerState, bIsActiveBattlePlayer);
  DOREPLIFETIME(ASkaldPlayerState, bIsAI);
  DOREPLIFETIME(ASkaldPlayerState, DeployableUnits);
  DOREPLIFETIME(ASkaldPlayerState, InitiativeRoll);
  DOREPLIFETIME(ASkaldPlayerState, Resources);
  DOREPLIFETIME(ASkaldPlayerState, bHasLockedIn);
  DOREPLIFETIME(ASkaldPlayerState, IsEliminated);
  DOREPLIFETIME(ASkaldPlayerState, StablePlayerId);
  DOREPLIFETIME(ASkaldPlayerState, SelectedTerritory);
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

void ASkaldPlayerState::OnRep_PendingArmyBudget()
{
  if (APlayerController* PC = GetOwner<APlayerController>())
  {
    if (ASkaldPlayerController* SkaldPC = Cast<ASkaldPlayerController>(PC))
    {
      UE_LOG(LogSkaldBattle, Log,
             TEXT("OnRep_PendingArmyBudget: PlayerId=%d Budget=%d Controller=%s"),
             GetPlayerId(), PendingArmyBudget, *GetNameSafe(SkaldPC));
      SkaldPC->InitializeFighterSelectionIfNeeded();
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

  // Keep the replicated engine player name aligned with the display name so
  // listen-server hosts and clients see the same identity everywhere (chat,
  // scoreboards, territory labels).
  const FString CurrentName = GetPlayerName();
  if (!PlayerDisplayName.IsEmpty() && CurrentName != PlayerDisplayName) {
    SetPlayerName(PlayerDisplayName);
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

  EnsureDefaultPlayerName();

  RefreshStablePlayerId();

  if (APlayerController *PC = GetOwner<APlayerController>()) {
    if (ASkaldPlayerController *SkaldPC = Cast<ASkaldPlayerController>(PC)) {
      if (USkaldMainHUDWidget *HUD = SkaldPC->GetHUDWidget()) {
        const int32 StableId = GetStablePlayerId();
        const bool bLocalIdChanged = (HUD->LocalPlayerID != StableId);
        HUD->LocalPlayerID = StableId;
        if (bLocalIdChanged) {
          HUD->SyncPhaseButtons(HUD->CurrentPlayerID == StableId);
        }
      }

      SkaldPC->HandlePlayerIdUpdated();

      // Resolve turn ownership locally whenever a stable ID replicates so
      // each owning client can enable their own HUD/input without waiting on
      // host-driven prompts.
      SkaldPC->RefreshTurnDataFromState();
      SkaldPC->HandleReplicatedTurnOwnership();
    }
  }

  if (UWorld *World = GetWorld()) {
    if (ASkaldGameState *GS = World->GetGameState<ASkaldGameState>()) {
      GS->OnPlayersUpdated.Broadcast();
    }
  }
}

void ASkaldPlayerState::CopyProperties(APlayerState *NewPlayerState) {
  Super::CopyProperties(NewPlayerState);

  if (ASkaldPlayerState *SkaldPS = Cast<ASkaldPlayerState>(NewPlayerState)) {
    SkaldPS->PlayerDisplayName = PlayerDisplayName;
    SkaldPS->Faction = Faction;
    SkaldPS->PendingArmy = PendingArmy;
    SkaldPS->PendingArmyBudget = PendingArmyBudget;
    SkaldPS->bArmyLockedIn = bArmyLockedIn;
    SkaldPS->bIsActiveBattlePlayer = bIsActiveBattlePlayer;
    SkaldPS->bIsAI = bIsAI;
    SkaldPS->DeployableUnits = DeployableUnits;
    SkaldPS->InitiativeRoll = InitiativeRoll;
    SkaldPS->Resources = Resources;
    SkaldPS->bHasLockedIn = bHasLockedIn;
    SkaldPS->IsEliminated = IsEliminated;
    SkaldPS->StablePlayerId = StablePlayerId;
    SkaldPS->SelectedTerritory = SelectedTerritory;
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

int32 ASkaldPlayerState::GetStablePlayerId() const {
  if (StablePlayerId != INDEX_NONE) {
    return StablePlayerId;
  }

  return GetAuthoritativePlayerId();
}

void ASkaldPlayerState::RefreshStablePlayerId() {
  if (!HasAuthority()) {
    return;
  }

  const int32 NewStableId = GetAuthoritativePlayerId();
  if (StablePlayerId == NewStableId) {
    return;
  }

  StablePlayerId = NewStableId;
  OnRep_StablePlayerId();
  ForceNetUpdate();
}

void ASkaldPlayerState::OnRep_StablePlayerId() {
  if (APlayerController *PC = GetOwner<APlayerController>()) {
    if (ASkaldPlayerController *SkaldPC = Cast<ASkaldPlayerController>(PC)) {
      if (USkaldMainHUDWidget *HUD = SkaldPC->GetHUDWidget()) {
        const bool bLocalIdChanged = (HUD->LocalPlayerID != StablePlayerId);
        HUD->LocalPlayerID = StablePlayerId;
        if (bLocalIdChanged) {
          HUD->SyncPhaseButtons(HUD->CurrentPlayerID == HUD->LocalPlayerID);
        }
      }
      SkaldPC->HandlePlayerIdUpdated();
    }
  }

  if (SelectedTerritory) {
    OnRep_SelectedTerritory();
  }
}

void ASkaldPlayerState::OnRep_SelectedTerritory() {
  const FString TerritoryDesc = SelectedTerritory
                                    ? FString::Printf(TEXT("%s (%d)"),
                                                      *SelectedTerritory->GetName(),
                                                      SelectedTerritory->TerritoryID)
                                    : FString(TEXT("None"));
  UE_LOG(LogSkald, Log,
         TEXT("OnRep_SelectedTerritory for %s (StableId=%d) now %s"),
         *GetResolvedPlayerName(TEXT("OnRep_SelectedTerritory")),
         GetStablePlayerId(), *TerritoryDesc);
  ApplySelectedTerritoryToWorldMap(true);
}

void ASkaldPlayerState::SetSelectedTerritory(ATerritory *Territory) {
  if (!HasAuthority()) {
    return;
  }

  SelectedTerritory = Territory;

  // Immediately apply the change on the server so listen-server hosts and
  // dedicated servers maintain consistent local selection caches.
  OnRep_SelectedTerritory();
  ForceNetUpdate();
}

void ASkaldPlayerState::ApplySelectedTerritoryToWorldMap(bool bAllowRetry) {
  if (!SelectedTerritory) {
    ClearSelectionReplayTimer();
    return;
  }

  const int32 StableId = GetStablePlayerId();
  if (StableId == INDEX_NONE) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  if (AWorldMap *WorldMap = Cast<AWorldMap>(
          UGameplayStatics::GetActorOfClass(World, AWorldMap::StaticClass()))) {
    WorldMap->SelectTerritory(SelectedTerritory.Get(), true, StableId);
    ClearSelectionReplayTimer();
  } else if (bAllowRetry && !World->GetTimerManager().IsTimerActive(
                                 SelectionReplayTimerHandle)) {
    World->GetTimerManager().SetTimer(
        SelectionReplayTimerHandle, this,
        &ASkaldPlayerState::RetryApplySelectedTerritory, 0.25f, true);
  }
}

void ASkaldPlayerState::ClearSelectionReplayTimer() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(SelectionReplayTimerHandle);
  }
}

void ASkaldPlayerState::RetryApplySelectedTerritory() {
  ApplySelectedTerritoryToWorldMap(true);
  if (!SelectedTerritory) {
    ClearSelectionReplayTimer();
  }
}
