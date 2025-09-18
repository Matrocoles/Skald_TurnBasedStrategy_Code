#include "Skald_GameInstance.h"

#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"

void USkaldGameInstance::Init() {
  Super::Init();
  SeedCombatRandomStream(FMath::Rand());
  TakenFactions.Empty();
  if (Faction != ESkaldFaction::None) {
    TakenFactions.Add(Faction);
  }

  PendingBattle = FS_BattlePayload();
  PendingBattleResolution = FGridBattleResolution();
  bPendingBattleResolution = false;

  if (GEngine) {
    GEngine->OnNetworkFailure().AddUObject(
        this, &USkaldGameInstance::HandleNetworkFailure);
  }
}

void USkaldGameInstance::SeedCombatRandomStream(int32 Seed) {
  CombatRandomStream.Initialize(Seed);
}

void USkaldGameInstance::HandleNetworkFailure(
    UWorld *World, UNetDriver * /*Driver*/,
    ENetworkFailure::Type /*FailureType*/, const FString &ErrorString) {
  if (GEngine) {
    GEngine->AddOnScreenDebugMessage(
        -1, 5.f, FColor::Red,
        FString::Printf(TEXT("Network failure: %s"), *ErrorString));
  }

  bIsMultiplayer = false;
  bIsHost = false;

  // Clear any per-session state so a fresh lobby is created after reconnecting.
  JoinAddress.Empty();
  AIPlayersToSpawn = 1;
  TakenFactions.Empty();
  if (Faction != ESkaldFaction::None) {
    TakenFactions.Add(Faction);
  }
  OnFactionsUpdated.Broadcast();
  PendingBattle = FS_BattlePayload();
  PendingBattleResolution = FGridBattleResolution();
  bPendingBattleResolution = false;
  GridBattleManager = nullptr;
  SeedCombatRandomStream(FMath::Rand());
  SavedTurnIndex = 0;
  SavedTurnPhase = ETurnPhase::Reinforcement;
  bResumeTurns = false;
  LoadedSaveGame = nullptr;

  const FName LobbyMap(TEXT("/Game/Blueprints/Maps/Skald_Lobby"));
  UGameplayStatics::OpenLevel(World, LobbyMap);
}
