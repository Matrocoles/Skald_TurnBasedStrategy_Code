#include "Skald_GameInstance.h"

#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Skald.h"
#include "SkaldLogging.h"
#include "Skald_BattleGameMode.h"
#include "Skald_BattleLevelManager.h"
#include "Skald_PlayerController.h"
#include "Styling/CoreStyle.h"
#include "UI/SkaldUIHelpers.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "UObject/CoreUObjectDelegates.h"

void USkaldGameInstance::Init() {
  Super::Init();
  SeedCombatRandomStream(FMath::Rand());
  TakenFactions.Empty();
  if (Faction != ESkaldFaction::None) {
    TakenFactions.Add(Faction);
  }

  if (!PostLoadMapHandle.IsValid()) {
    PostLoadMapHandle =
        FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
            this, &USkaldGameInstance::HandlePostLoadMap);
  }

  PendingBattle = FS_BattlePayload();
  PendingBattleResolution = FGridBattleResolution();
  bPendingBattleResolution = false;
  SetBattleMapActive(false);

  if (!BattleLevelStreamingManager) {
    BattleLevelStreamingManager = NewObject<USkaldBattleLevelManager>(this);
    BattleLevelStreamingManager->Initialise(this);
  }

  if (GEngine) {
    GEngine->OnNetworkFailure().AddUObject(
        this, &USkaldGameInstance::HandleNetworkFailure);
  }
}

void USkaldGameInstance::Shutdown() {
  if (PostLoadMapHandle.IsValid()) {
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
    PostLoadMapHandle.Reset();
  }

  Super::Shutdown();
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

  UGameViewportClient *Viewport = GetGameViewportClient();
  if (!Viewport) {
    if (!bTravelPending) {
      TravelLoadingOverlay.Reset();
    }
    return;
  }

  if (bTravelPending) {
    if (!TravelLoadingOverlay.IsValid()) {
      TSharedRef<SOverlay> Overlay = SNew(SOverlay)
          + SOverlay::Slot()
                .HAlign(HAlign_Fill)
                .VAlign(VAlign_Fill)[SNew(SImage).ColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.7f))]
          + SOverlay::Slot()
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)[SNew(SBorder)
                                           .Padding(FMargin(40.f))
                                           .BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.85f))
                                           .HAlign(HAlign_Center)
                                           .VAlign(VAlign_Center)[SNew(STextBlock)
                                                                     .Justification(ETextJustify::Center)
                                                                     .Text(NSLOCTEXT("Skald", "TravelLoadingText", "Loading overworld..."))
                                                                     .Font(FCoreStyle::GetDefaultFontStyle("Bold", 32))
                                                                     .ColorAndOpacity(FLinearColor::White)]];

      TravelLoadingOverlay = Overlay;
      Viewport->AddViewportWidgetContent(Overlay, 100);
    }
  } else {
    if (TravelLoadingOverlay.IsValid()) {
      Viewport->RemoveViewportWidgetContent(TravelLoadingOverlay.ToSharedRef());
      TravelLoadingOverlay.Reset();
    }
  }
}

void USkaldGameInstance::HandlePostLoadMap(UWorld * /*LoadedWorld*/) {
  SetTravelPending(false);
}

void USkaldGameInstance::SetActiveBattleGameMode(
    ASkald_BattleGameMode *InGameMode) {
  ASkald_BattleGameMode *Previous = ActiveBattleGameMode.Get();
  if (Previous == InGameMode) {
    return;
  }

  if (!InGameMode) {
    if (Previous) {
      UE_LOG(LogSkald, Log,
             TEXT("GameInstance cleared active battle game mode %s"),
             *GetNameSafe(Previous));
    }
    ActiveBattleGameMode = nullptr;
    return;
  }

  ActiveBattleGameMode = InGameMode;
  UE_LOG(LogSkald, Log,
         TEXT("GameInstance set active battle game mode: %s"),
         *GetNameSafe(InGameMode));
}

void USkaldGameInstance::SetBattleMapActive(bool bInBattleMap) {
  if (bIsInBattleMap == bInBattleMap) {
    return;
  }

  bIsInBattleMap = bInBattleMap;
  OnBattleMapStateChanged.Broadcast(bIsInBattleMap);
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
  if (BattleLevelStreamingManager) {
    BattleLevelStreamingManager->ReleaseBattleLevel();
  }
  if (ASkald_BattleGameMode *BattleGM = ActiveBattleGameMode.Get()) {
    if (!BattleGM->IsActorBeingDestroyed()) {
      BattleGM->Destroy();
    }
  }
  SetActiveBattleGameMode(nullptr);
  SetBattleMapActive(false);
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
