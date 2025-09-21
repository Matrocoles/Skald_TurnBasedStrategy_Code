#include "Skald_GameInstance.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Kismet/GameplayStatics.h"
#include "Skald.h"
#include "Blueprint/UserWidget.h"

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

void USkaldGameInstance::SetTravelState(const FSkaldTravelState &InState) {
  TravelState = InState;
  TravelState.bValid = true;
  UE_LOG(LogSkald, Log,
         TEXT("GameInstance travel state set: Expected=%d Attacker=%d Defender=%d HumanTerritories=%d"),
         TravelState.ExpectedControllers, TravelState.AttackerTerritory,
         TravelState.DefenderTerritory, TravelState.HumanOwnedTerritories.Num());
}

void USkaldGameInstance::SetTravelPending(bool bInPending) {
  if (bTravelPending == bInPending) {
    return;
  }

  bTravelPending = bInPending;
  UE_LOG(LogSkald, Log, TEXT("GameInstance travel pending set: %s"),
         bTravelPending ? TEXT("true") : TEXT("false"));
}

void USkaldGameInstance::SeedCombatRandomStream(int32 Seed) {
  CombatRandomStream.Initialize(Seed);
}

void USkaldGameInstance::ShowDeployWidget() {
  if (!DeployWidget) {
    TSubclassOf<UUserWidget> WidgetClass = DeployWidgetClass;
    if (!WidgetClass) {
      UE_LOG(LogSkald, Warning,
             TEXT("ShowDeployWidget: DeployWidgetClass not set."));
      return;
    }

    DeployWidget = CreateWidget<UUserWidget>(this, WidgetClass);
  }

  if (!DeployWidget) {
    return;
  }

  if (DeployWidgetSlateHandle.IsValid()) {
    // Already displayed via the viewport.
    return;
  }

  if (UWorld *World = GetWorld()) {
    if (UGameViewportClient *Viewport = World->GetGameViewport()) {
      DeployWidgetSlateHandle = DeployWidget->TakeWidget();
      Viewport->AddViewportWidgetContent(DeployWidgetSlateHandle.ToSharedRef());
    }
  }
}

void USkaldGameInstance::HideDeployWidget() {
  if (!DeployWidgetSlateHandle.IsValid()) {
    return;
  }

  if (UWorld *World = GetWorld()) {
    if (UGameViewportClient *Viewport = World->GetGameViewport()) {
      Viewport->RemoveViewportWidgetContent(DeployWidgetSlateHandle.ToSharedRef());
    }
  }

  DeployWidgetSlateHandle.Reset();
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
