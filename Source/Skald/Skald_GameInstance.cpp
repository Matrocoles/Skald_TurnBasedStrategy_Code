#include "Skald_GameInstance.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/CoreDelegates.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "UObject/CoreUObjectDelegates.h"
#else
#include "UObject/UObjectGlobals.h"
#endif
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameViewportClient.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Skald.h"
#include "SkaldLogging.h"
#include "Skald_BattleGameMode.h"
#include "Skald_BattleLevelManager.h"
#include "Skald_GameMode.h"
#include "Skald_PlayerController.h"
#include "Skald_TurnManager.h"
#include "TimerManager.h"
#include "Styling/CoreStyle.h"
#include "UI/SkaldUIHelpers.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
USoundBase *SelectRandomSound(const TArray<TObjectPtr<USoundBase>> &Candidates)
{
  const int32 CandidateCount = Candidates.Num();
  if (CandidateCount == 0)
  {
    return nullptr;
  }

  for (int32 Attempt = 0; Attempt < CandidateCount; ++Attempt)
  {
    const int32 Index = FMath::RandHelper(CandidateCount);
    if (Candidates.IsValidIndex(Index))
    {
      if (USoundBase *ResolvedSound = Candidates[Index])
      {
        return ResolvedSound;
      }
    }
  }

  for (USoundBase *FallbackSound : Candidates)
  {
    if (FallbackSound)
    {
      return FallbackSound;
    }
  }

  return nullptr;
}

UAudioComponent *SpawnBattleSound(UObject *WorldContextObject, USoundBase *Sound,
                                  USoundClass *MasterSoundClass,
                                  USoundAttenuation *AttenuationSettings,
                                  const FVector *Location)
{
  if (!WorldContextObject || !Sound)
  {
    return nullptr;
  }

  UAudioComponent *SpawnedComponent = nullptr;

  if (Location)
  {
    SpawnedComponent = UGameplayStatics::SpawnSoundAtLocation(
        WorldContextObject, Sound, *Location, FRotator::ZeroRotator, 1.f, 1.f,
        0.f, AttenuationSettings);
  }
  else
  {
    SpawnedComponent = UGameplayStatics::SpawnSound2D(WorldContextObject, Sound);
  }

  if (SpawnedComponent && MasterSoundClass)
  {
    SpawnedComponent->SoundClassOverride = MasterSoundClass;
  }

  return SpawnedComponent;
}
} // namespace

void USkaldGameInstance::Init() {
  Super::Init();
  SeedCombatRandomStream(FMath::Rand());
  TakenFactions.Empty();
  if (Faction != ESkaldFaction::None) {
    TakenFactions.Add(Faction);
  }

  if (!PostWorldBeginPlayHandle.IsValid()) {
#if UE_VERSION_OLDER_THAN(5, 5, 0)
    PostWorldBeginPlayHandle =
        FWorldDelegates::OnWorldBeginPlay.AddUObject(
            this, &USkaldGameInstance::HandleWorldBeginPlay);
#else
    PostWorldBeginPlayHandle =
        FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
            this, &USkaldGameInstance::HandleWorldBeginPlay);
#endif
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
  if (PostWorldBeginPlayHandle.IsValid()) {
#if UE_VERSION_OLDER_THAN(5, 5, 0)
    FWorldDelegates::OnWorldBeginPlay.Remove(PostWorldBeginPlayHandle);
#else
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(
        PostWorldBeginPlayHandle);
#endif
    PostWorldBeginPlayHandle.Reset();
  }

  Super::Shutdown();
}

void USkaldGameInstance::SetTravelState(const FSkaldTravelState &InState) {
  TravelState = InState;
  TravelState.bValid = true;
  if (TravelState.CachedTerritories.Num() > 0) {
    CachedWorldMapTerritories = TravelState.CachedTerritories;
    PendingTravelTerritories = TravelState.CachedTerritories;
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

void USkaldGameInstance::SetPendingTravelSnapshot(
    const TArray<FS_Territory> &Snapshot) {
  PendingTravelTerritories = Snapshot;

  if (PendingTravelTerritories.Num() > 0 && CachedWorldMapTerritories.Num() == 0) {
    CachedWorldMapTerritories = PendingTravelTerritories;
  }

  UE_LOG(LogSkald, Verbose,
         TEXT("GameInstance pending travel snapshot set (%d territories)"),
         PendingTravelTerritories.Num());
}

void USkaldGameInstance::ClearPendingTravelSnapshot() {
  if (PendingTravelTerritories.Num() > 0) {
    PendingTravelTerritories.Reset();
    UE_LOG(LogSkald, Verbose,
           TEXT("GameInstance pending travel snapshot cleared"));
  }
}

void USkaldGameInstance::SetPendingReturnMap(const FString &InReturnMap) {
  if (PendingReturnMap.Equals(InReturnMap, ESearchCase::CaseSensitive)) {
    return;
  }

  PendingReturnMap = InReturnMap;

  const TCHAR *LoggedValue = PendingReturnMap.IsEmpty()
                                 ? TEXT("<Empty>")
                                 : *PendingReturnMap;
  UE_LOG(LogSkald, Log, TEXT("GameInstance pending return map set to %s"),
         LoggedValue);
}

void USkaldGameInstance::PlayRandomDiceRollVariant(
    UObject *WorldContextObject) const {
  if (!WorldContextObject) {
    return;
  }

  if (USoundBase *Sound = SelectRandomSound(DiceRollVariants)) {
    SpawnBattleSound(WorldContextObject, Sound, MasterSoundClass.Get(), nullptr,
                     nullptr);
  }
}

void USkaldGameInstance::PlayAttackPrepareCue(
    UObject *WorldContextObject, const FVector &Location) const {
  if (USoundBase *Sound = SelectRandomSound(AttackPrepareCues)) {
    SpawnBattleSound(WorldContextObject, Sound, MasterSoundClass.Get(),
                     AttackCueAttenuation.Get(), &Location);
  }
}

void USkaldGameInstance::PlayAttackResolveCue(
    UObject *WorldContextObject, const FVector &Location) const {
  if (USoundBase *Sound = SelectRandomSound(AttackResolveCues)) {
    SpawnBattleSound(WorldContextObject, Sound, MasterSoundClass.Get(),
                     AttackCueAttenuation.Get(), &Location);
  }
}

void USkaldGameInstance::PlayAttackCritCue(UObject *WorldContextObject,
                                           const FVector &Location) const {
  if (USoundBase *Sound = SelectRandomSound(AttackCritCues)) {
    SpawnBattleSound(WorldContextObject, Sound, MasterSoundClass.Get(),
                     AttackCueAttenuation.Get(), &Location);
  }
}

void USkaldGameInstance::ClearPendingReturnMap() {
  if (PendingReturnMap.IsEmpty()) {
    return;
  }

  UE_LOG(LogSkald, Verbose,
         TEXT("GameInstance pending return map cleared (was '%s')"),
         *PendingReturnMap);
  PendingReturnMap.Reset();
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

void USkaldGameInstance::HandleWorldBeginPlay(UWorld *LoadedWorld) {
  if (!LoadedWorld || LoadedWorld->GetGameInstance() != this) {
    return;
  }

  SetTravelPending(false);

  if (LoadedWorld->GetNetMode() == NM_Client) {
    return;
  }

  if (bPendingBattleResolution && PendingBattleResolution.bValid) {
    RequestPendingBattleResolution(LoadedWorld);
  }
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

void USkaldGameInstance::RequestPendingBattleResolution(UWorld *LoadedWorld) {
  if (!bPendingBattleResolution || !PendingBattleResolution.bValid) {
    return;
  }

  UWorld *World = LoadedWorld ? LoadedWorld : GetWorld();
  if (!World) {
    return;
  }

  World->GetTimerManager().ClearTimer(PendingBattleResolutionKickoffHandle);

  FTimerDelegate ResolveDelegate = FTimerDelegate::CreateWeakLambda(
      this, [this]() { AttemptResolvePendingBattle(0); });
  World->GetTimerManager().SetTimerForNextTick(ResolveDelegate);
}

void USkaldGameInstance::AttemptResolvePendingBattle(int32 Attempt) {
  if (!bPendingBattleResolution || !PendingBattleResolution.bValid) {
    return;
  }

  UWorld *World = GetWorld();
  if (!World || World->GetNetMode() == NM_Client) {
    return;
  }

  ATurnManager *TurnManager = nullptr;
  if (ASkaldGameMode *GameMode = World->GetAuthGameMode<ASkaldGameMode>()) {
    TurnManager = GameMode->GetTurnManager();
  }
  if (!TurnManager) {
    if (AActor *Actor =
            UGameplayStatics::GetActorOfClass(World, ATurnManager::StaticClass())) {
      TurnManager = Cast<ATurnManager>(Actor);
    }
  }

  if (TurnManager) {
    World->GetTimerManager().ClearTimer(PendingBattleResolutionKickoffHandle);
    TurnManager->ResolveGridBattleResult();
    return;
  }

  constexpr int32 MaxAttempts = 40;
  if (Attempt + 1 >= MaxAttempts) {
    UE_LOG(LogSkald, Warning,
           TEXT("GameInstance pending battle resolution could not locate a turn manager after %d attempts."),
           Attempt + 1);
    return;
  }

  constexpr float RetryDelaySeconds = 0.05f;
  FTimerDelegate RetryDelegate = FTimerDelegate::CreateWeakLambda(
      this, [this, Attempt]() { AttemptResolvePendingBattle(Attempt + 1); });
  World->GetTimerManager().SetTimer(PendingBattleResolutionKickoffHandle,
                                    RetryDelegate, RetryDelaySeconds, false);
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
  PendingReturnMap.Reset();
  CachedWorldMapTerritories.Empty();
  PendingTravelTerritories.Empty();
  TravelState = FSkaldTravelState();

  SeedCombatRandomStream(FMath::Rand());
  SavedTurnIndex = 0;
  SavedTurnPlayerId = 0;
  SavedTurnPhase = ETurnPhase::Reinforcement;
  bResumeTurns = false;
  LoadedSaveGame = nullptr;

  if (DeployWidget) {
    DeployWidget->RemoveFromParent();
    DeployWidget = nullptr;
  }
}
