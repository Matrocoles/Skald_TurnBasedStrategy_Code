#include "Skald_GameInstance.h"

#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Skald.h"
#include "SkaldLogging.h"
#include "Blueprint/UserWidget.h"
#include "Skald_PlayerController.h"
#include "UI/SkaldUIHelpers.h"

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
  if (TravelState.CachedTerritories.Num() > 0) {
    CachedWorldMapTerritories = TravelState.CachedTerritories;
  }
  UE_LOG(LogSkald, Log,
         TEXT("GameInstance travel state set: Expected=%d Attacker=%d Defender=%d HumanTerritories=%d CachedTerritories=%d"),
         TravelState.ExpectedControllers, TravelState.AttackerTerritory,
         TravelState.DefenderTerritory, TravelState.HumanOwnedTerritories.Num(),
         TravelState.CachedTerritories.Num());

  if (TravelState.CachedTerritories.Num() == 0) {
    UE_LOG(LogSkald, Warning,
           TEXT("GameInstance travel state missing cached territories snapshot"));
  }
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

  if (!DeployWidget->IsInViewport()) {
    DeployWidget->AddToViewport();
    if (UWorld *World = GetWorld()) {
      if (APlayerController *PC = World->GetFirstPlayerController()) {
        FocusWidgetUIOnly(PC, DeployWidget);
      }
    }
  }
}

void USkaldGameInstance::HideDeployWidget() {
  if (!DeployWidget) {
    return;
  }

  if (DeployWidget->IsInViewport()) {
    DeployWidget->RemoveFromParent();
  }

  if (UWorld *World = GetWorld()) {
    if (ASkaldPlayerController *PC =
            Cast<ASkaldPlayerController>(World->GetFirstPlayerController())) {
      PC->ShowMainHUD();
    }
  }
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

  ResetSessionState();

  if (World) {
    const FName LobbyMap(TEXT("/Game/Blueprints/Maps/Skald_Lobby"));
    UGameplayStatics::OpenLevel(World, LobbyMap);
  }
}

void USkaldGameInstance::ReturnToMainMenu() {
  ResetSessionState();

  if (UWorld *World = GetWorld()) {
    const FName LobbyMap(TEXT("/Game/Blueprints/Maps/Skald_Lobby"));
    UGameplayStatics::OpenLevel(World, LobbyMap);
  }
}

void USkaldGameInstance::ResetSessionState() {
  // Clear any per-session state so a fresh lobby is created.
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
  bIsInBattleMap = false;
  bTravelPending = false;
  CachedWorldMapTerritories.Empty();
  TravelState = FSkaldTravelState();

  SeedCombatRandomStream(FMath::Rand());
  SavedTurnIndex = 0;
  SavedTurnPhase = ETurnPhase::Reinforcement;
  bResumeTurns = false;
  LoadedSaveGame = nullptr;

  if (DeployWidget) {
    DeployWidget->RemoveFromParent();
    DeployWidget = nullptr;
  }
}
