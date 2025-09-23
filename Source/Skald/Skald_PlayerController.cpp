#include "Skald_PlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "ChoosePlayerWidget.h"
#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "FighterDataLibrary.h"
#include "FighterPawn.h"
#include "GridBattleManager.h"
#include "GridOverlayComponent.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/EngineVersionComparison.h"
#include "Skald.h"
#include "SkaldTypes.h"
#include "Skald_AIController.h"
#include "Skald_GameInstance.h"
#include "Skald_GameMode.h"
#include "Skald_BattleGameMode.h"
#include "SkaldLogging.h"
#include "Skald_GameState.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "Territory.h"
#include "TimerManager.h"
#include "UI/BattleHUDWidget.h"
#include "UI/BattleResultWidget.h"
#include "UI/DeployWidget.h"
#include "UI/FighterSelectionWidget.h"
#include "UI/SkaldMainHUDWidget.h"
#include "UI/SkaldUIHelpers.h"
#include "UObject/ConstructorHelpers.h"
#include "WorldMap.h"

// Portable include for FCoreUObjectDelegates across UE versions
#if __has_include("UObject/CoreUObjectDelegates.h")
    #include "UObject/CoreUObjectDelegates.h"
#elif __has_include("UObject/Package.h")
    #include "UObject/Package.h"
#else
    #include "UObject/UObjectGlobals.h"
#endif

#include "Engine/World.h"

namespace {
FString ResolvePlayerName(const ASkaldPlayerState *PlayerState,
                          const TCHAR *Context) {
  if (!PlayerState) {
    return TEXT("Neutral");
  }

  return PlayerState->GetResolvedPlayerName(Context);
}
}

ASkaldPlayerController::ASkaldPlayerController() {
  TurnManager = nullptr;
  HUDRef = nullptr;
  MainHUD = nullptr;
  BattleHudWidget = nullptr;
  BattleResultWidget = nullptr;
  CurrentCommandMode = EBattleCommandMode::None;
  bHasInitialized = false;

  bShowMouseCursor = true;
  bEnableClickEvents = true;
  bEnableMouseOverEvents = true;
  DefaultMouseCaptureMode = EMouseCaptureMode::NoCapture;

  // Default to the native HUD widget class. This avoids loading a
  // blueprint-derived widget that may not exist or may be corrupt.
  MainHUDClass = USkaldMainHUDWidget::StaticClass();
  BattleHUDWidgetClass = UBattleHUDWidget::StaticClass();
  FighterSelectionWidgetClass = UFighterSelectionWidget::StaticClass();
  VictoryWidgetClass = UBattleResultWidget::StaticClass();
  bBattleHUDVisible = false;
  bBattleHUDReadyToShow = false;

  static ConstructorHelpers::FClassFinder<UChoosePlayerWidget> ChooseBP(
      TEXT("/Game/Blueprints/UI/Skald_ChoosePlayerWidget"));
  if (ChooseBP.Succeeded()) {
    ChoosePlayerWidgetClass = ChooseBP.Class;
  }
}

void ASkaldPlayerController::CacheGameReferences() {
  CachedGameState = GetWorld()->GetGameState<ASkaldGameState>();
  if (!CachedGameState) {
    UE_LOG(LogSkald, Error,
           TEXT("ASkaldPlayerController could not find ASkaldGameState."));
  } else {
    CachedGameState->OnPlayersUpdated.AddDynamic(
        this, &ASkaldPlayerController::HandlePlayersUpdated);
  }

  CachedGameMode = GetWorld()->GetAuthGameMode<ASkaldGameMode>();
  if (!CachedGameMode) {
    UE_LOG(LogSkald, Error,
           TEXT("ASkaldPlayerController could not find ASkaldGameMode."));
  }

  CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  if (!CachedGameInstance) {
    UE_LOG(LogSkald, Error,
           TEXT("ASkaldPlayerController could not find USkaldGameInstance."));
  } else {
    CachedGameInstance->OnFactionsUpdated.AddDynamic(
        this, &ASkaldPlayerController::HandleFactionsUpdated);
  }
}

void ASkaldPlayerController::InitializeHUDWidget() {
  if (MainHUD) {
    return;
  }

  if (!MainHUDClass) {
    UE_LOG(LogSkald, Warning,
           TEXT("MainHUDClass is null; HUD will not be displayed."));
    return;
  }

  MainHUD = CreateWidget<USkaldMainHUDWidget>(this, MainHUDClass);
  if (!MainHUD) {
    return;
  }

  HUDRef = MainHUD;
  MainHUD->AddToViewport();
  MainHUD->AddToViewport(10);
  MainHUD->SetIsFocusable(true);
  MainHUD->SetVisibility(ESlateVisibility::Hidden);

  if (CachedGameState) {
    TArray<FS_PlayerData> Players;
    BuildPlayerDataArray(Players);
    const ASkaldPlayerState *CurrentPS = CachedGameState->GetCurrentPlayer();
    const int32 CurrentID = CurrentPS ? CurrentPS->GetPlayerId() : -1;
    MainHUD->RefreshFromState(CurrentID, /*TurnNumber*/ 1,
                              ETurnPhase::Reinforcement, Players);
  }

  // Ensure local player details are registered with the HUD once available.
  OnRep_PlayerState();

  MainHUD->OnAttackRequested.AddDynamic(
      this, &ASkaldPlayerController::HandleAttackRequested);
  MainHUD->OnMoveRequested.AddDynamic(
      this, &ASkaldPlayerController::HandleMoveRequested);
  MainHUD->OnEndAttackRequested.AddDynamic(
      this, &ASkaldPlayerController::HandleEndAttackRequested);
  MainHUD->OnEndMovementRequested.AddDynamic(
      this, &ASkaldPlayerController::HandleEndMovementRequested);
  MainHUD->OnEngineeringRequested.AddDynamic(
      this, &ASkaldPlayerController::HandleEngineeringRequested);
  MainHUD->OnBuildSiegeRequested.AddDynamic(
      this, &ASkaldPlayerController::HandleBuildSiegeRequested);
  MainHUD->OnDigTreasureRequested.AddDynamic(
      this, &ASkaldPlayerController::HandleDigTreasureRequested);

  // Notify the game mode that the HUD is now ready so world start checks can
  // proceed only after widgets are initialized.
  if (CachedGameMode) {
    CachedGameMode->TryInitializeWorldAndStart();
  }
}

void ASkaldPlayerController::InitializeChoosePlayerWidget() {
  if (ChoosePlayerWidget || !ChoosePlayerWidgetClass) {
    return;
  }

  ChoosePlayerWidget =
      CreateWidget<UChoosePlayerWidget>(this, ChoosePlayerWidgetClass);
  if (!ChoosePlayerWidget) {
    return;
  }

  ChoosePlayerWidget->OnPlayerLockedIn.AddDynamic(
      this, &ASkaldPlayerController::HandleFactionLockedIn);
  ChoosePlayerWidget->AddToViewport();

  // While the player is choosing their faction, restrict controls to the UI.
  FocusWidgetUIOnly(this, ChoosePlayerWidget);
  SetIgnoreMoveInput(true);
  SetIgnoreLookInput(true);
}

void ASkaldPlayerController::BeginPlay() {
  Super::BeginPlay();

  if (PostLoadMapHandle.IsValid()) {
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
    PostLoadMapHandle.Reset();
  }

  PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
      this, &ASkaldPlayerController::HandlePostLoadMap);

  CacheGameReferences();

  DetectBattleMap();

  if (ASkaldGameState* SGS = GetWorld() ? GetWorld()->GetGameState<ASkaldGameState>() : nullptr)
  {
    if (SGS->BattlePhase == EBattlePhase::Deploy)
    {
      if (HasAuthority())
      {
        Client_ShowDeployUI();
      }
      else
      {
        ShowDeployUIInternal();
      }
    }
  }

  if (IsLocalPlayerController() && GetLocalPlayer() != nullptr) {
    if (!bIsBattleMap) {
      InitializeHUDWidget();
      ShowMainHUD();
    } else {
      UE_LOG(LogSkald, Log, TEXT("[HUD] Skipping MainHUD in BattleGameMode"));
    }
    if (CachedGameInstance && CachedGameInstance->bIsMultiplayer &&
        !CachedGameInstance->bIsHost) {
      if (GEngine) {
        GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green,
                                         TEXT("Connected to host"));
      }
    }
    if (CachedGameInstance && CachedGameInstance->bIsMultiplayer) {
      InitializeChoosePlayerWidget();
    } else if (CachedGameInstance && !bHasInitialized) {
      if (!CachedGameMode || !CachedGameMode->IsWorldInitialized()) {
        ServerInitPlayerState(CachedGameInstance->DisplayName,
                              CachedGameInstance->Faction,
                              CachedGameInstance->AIPlayersToSpawn);
      }
      HandleFactionLockedIn();
    }
    InitializeFighterSelectionIfNeeded();
  }

  TryBindWorldMap();
}

void ASkaldPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason) {
  if (PostLoadMapHandle.IsValid()) {
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
    PostLoadMapHandle.Reset();
  }

  if (MainHUD) {
    MainHUD->RemoveFromParent();
    MainHUD = nullptr;
    HUDRef = nullptr;
  }

  if (ChoosePlayerWidget) {
    ChoosePlayerWidget->RemoveFromParent();
    ChoosePlayerWidget = nullptr;
  }

  if (FighterSelectionWidget) {
    FighterSelectionWidget->RemoveFromParent();
    FighterSelectionWidget = nullptr;
  }

  if (BattleHudWidget) {
    BattleHudWidget->RemoveFromParent();
    BattleHudWidget = nullptr;
  }

  if (BattleResultWidget) {
    BattleResultWidget->RemoveFromParent();
    BattleResultWidget = nullptr;
  }

  Super::EndPlay(EndPlayReason);
}

void ASkaldPlayerController::ShowMainHUD() {
  if (MainHUD) {
    MainHUD->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
  }

  UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(
      this, nullptr, EMouseLockMode::DoNotLock, /*bHideCursorDuringCapture*/
      false);
  bShowMouseCursor = true;
}

void ASkaldPlayerController::HideMainHUD() {
  if (MainHUD) {
    MainHUD->SetVisibility(ESlateVisibility::Collapsed);
    UWidgetBlueprintLibrary::SetInputMode_GameOnly(this);
    bShowMouseCursor = false;
  }
}

void ASkaldPlayerController::TryBindWorldMap() {
  if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          GetWorld(), AWorldMap::StaticClass()))) {
    if (!WorldMap->OnTerritorySelected.IsAlreadyBound(
            this, &ASkaldPlayerController::HandleTerritorySelected)) {
      WorldMap->OnTerritorySelected.AddDynamic(
          this, &ASkaldPlayerController::HandleTerritorySelected);
      ensureMsgf(WorldMap->OnTerritorySelected.IsAlreadyBound(
                     this, &ASkaldPlayerController::HandleTerritorySelected),
                 TEXT("Failed to bind HandleTerritorySelected to WorldMap."));
    }
    GetWorldTimerManager().ClearTimer(WorldMapSearchHandle);
  } else {
    GetWorldTimerManager().SetTimer(WorldMapSearchHandle, this,
                                    &ASkaldPlayerController::TryBindWorldMap,
                                    0.5f, false);
  }
}

void ASkaldPlayerController::OnRep_PlayerState() {
  Super::OnRep_PlayerState();

  if (!MainHUD) {
    return;
  }

  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    MainHUD->LocalPlayerID = PS->GetPlayerId();
    MainHUD->UpdateResources(PS->Resources);
    MainHUD->SyncPhaseButtons(MainHUD->CurrentPlayerID ==
                                    MainHUD->LocalPlayerID);
  }
}

void ASkaldPlayerController::ServerInitPlayerState_Implementation(
    const FString &Name, ESkaldFaction Faction, int32 NumAIPlayers) {
  UE_LOG(LogSkald, Log,
         TEXT("ServerInitPlayerState_Implementation: Name=%s Faction=%d AI=%d"),
         *Name, static_cast<int32>(Faction), NumAIPlayers);

  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    UE_LOG(LogSkald, Log,
           TEXT("ServerInitPlayerState_Implementation: PlayerState=%s"),
           *PS->GetName());
    PS->PlayerDisplayName = Name;
    PS->SetPlayerName(Name);
    PS->Faction = Faction;

    if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
      GI->AIPlayersToSpawn = NumAIPlayers;
    }

    if (ASkaldGameMode *GM = GetWorld()->GetAuthGameMode<ASkaldGameMode>()) {
      if (!GM->IsWorldInitialized()) {
        UE_LOG(LogSkald, Log,
               TEXT("ServerInitPlayerState_Implementation: Notify GameMode %s"),
               *GM->GetName());
        GM->HandlePlayerLockedIn(PS);
      } else {
        UE_LOG(LogSkald, Log,
               TEXT("ServerInitPlayerState_Implementation: World already "
                    "initialized"));
      }
    } else {
      UE_LOG(LogSkald, Warning,
             TEXT("ServerInitPlayerState_Implementation: GameMode is null"));
    }
  } else {
    UE_LOG(LogSkald, Warning,
           TEXT("ServerInitPlayerState_Implementation: PlayerState is null"));
  }
}

void ASkaldPlayerController::SetTurnManager(ATurnManager *Manager) {
  if (TurnManager) {
    TurnManager->OnWorldStateChanged.RemoveDynamic(
        this, &ASkaldPlayerController::HandleWorldStateChanged);
  }

  TurnManager = Manager;

  if (TurnManager) {
    TurnManager->OnWorldStateChanged.AddDynamic(
        this, &ASkaldPlayerController::HandleWorldStateChanged);
    if (IsLocalPlayerController() && GetLocalPlayer() != nullptr) {
      InitializeFighterSelectionIfNeeded();
    }
  }
}

void ASkaldPlayerController::InitializeFighterSelectionIfNeeded() {
  if (!IsLocalController()) {
    return;
  }

  if (!CachedGameInstance) {
    CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  }

  const bool bOnBattleMap =
      bIsBattleMap || (CachedGameInstance && CachedGameInstance->bIsInBattleMap);
  if (!bOnBattleMap) {
    if (FighterSelectionWidget) {
      FighterSelectionWidget->RemoveFromParent();
      FighterSelectionWidget = nullptr;
    }
    return;
  }

  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS || PS->bArmyLockedIn) {
    return;
  }

  if (!CachedGameInstance) {
    return;
  }

  const FS_BattlePayload &Battle = CachedGameInstance->PendingBattle;
  const int32 PlayerID = PS->GetPlayerId();
  const bool bIsParticipant =
      Battle.AttackerPlayerID == PlayerID || Battle.DefenderPlayerID == PlayerID;

  if (!bIsParticipant || PS->PendingArmyBudget <= 0) {
    return;
  }

  if (!FighterSelectionWidget || !FighterSelectionWidget->IsInViewport()) {
    ShowFighterSelectionUI(PS->PendingArmyBudget, PS->Faction);
  }
}

void ASkaldPlayerController::ShowFighterSelectionUI(int32 MaxBudget,
                                                    ESkaldFaction Faction) {
  if (!IsLocalController()) {
    return;
  }

  HideOverworldHUDForBattle();
  bBattleHUDReadyToShow = false;

  if (!FighterSelectionWidgetClass) {
    FighterSelectionWidgetClass = UFighterSelectionWidget::StaticClass();
  }

  if (!FighterSelectionWidget ||
      FighterSelectionWidget->GetClass() != FighterSelectionWidgetClass) {
    if (FighterSelectionWidget) {
      FighterSelectionWidget->RemoveFromParent();
    }
    FighterSelectionWidget =
        CreateWidget<UFighterSelectionWidget>(this, FighterSelectionWidgetClass);
  }

  if (!FighterSelectionWidget) {
    return;
  }

  FighterSelectionWidget->PlayerFaction = Faction;
  FighterSelectionWidget->MaxCost = MaxBudget;
  FighterSelectionWidget->ChosenFighters.Reset();
  FighterSelectionWidget->CurrentCost = 0;
  FighterSelectionWidget->SetLockInButtonEnabled(true);
  const TArray<FFighterDefinition> Available =
      UFighterDataLibrary::GetFightersForFaction(this, Faction);
  FighterSelectionWidget->SetAvailableFighters(Available);
  UE_LOG(LogSkald, Log,
         TEXT("FighterSelectionWidget: Populated %d entries for faction %d"),
         Available.Num(), static_cast<int32>(Faction));
  if (Available.Num() == 0) {
    UE_LOG(LogSkald, Warning,
           TEXT("SkaldUI: [FighterSelection] No fighters available for faction %d"),
           static_cast<int32>(Faction));
  }
  FighterSelectionWidget->UpdateCostDisplay();

  FighterSelectionWidget->AddToViewport(30);
  FocusWidgetUIOnly(this, FighterSelectionWidget);
  bShowMouseCursor = true;
  bEnableClickEvents = true;
  bEnableMouseOverEvents = true;
}

void ASkaldPlayerController::ShowDeployUIInternal() {
  if (!IsLocalController() || bDeployWidgetShown) {
    return;
  }

  bDeployWidgetShown = true;

  if (UDeployWidget *DeployWidget =
          CreateWidget<UDeployWidget>(this, UDeployWidget::StaticClass())) {
    DeployWidget->AddToViewport(1000);
  }
}

void ASkaldPlayerController::Client_ShowFighterSelection_Implementation(
    int32 MaxBudget, ESkaldFaction Faction) {
  ShowFighterSelectionUI(MaxBudget, Faction);
}

void ASkaldPlayerController::Client_ShowDeployUI_Implementation() {
  UE_LOG(LogSkaldBattle, Log, TEXT("Client_ShowDeployUI on %s"), *GetName());
  ShowDeployUIInternal();
}

void ASkaldPlayerController::Server_LockInSelection_Implementation(
    const TArray<FFighterDefinition> &SelectedFighters)
{
  UE_LOG(LogSkaldBattle, Log,
         TEXT("Server_LockInSelection: %s sent %d fighters"), *GetName(),
         SelectedFighters.Num());

  if (ASkald_BattleGameMode *GameMode =
          GetWorld()->GetAuthGameMode<ASkald_BattleGameMode>())
  {
    GameMode->HandleHumanLockIn(this, SelectedFighters);
  }
}

void ASkaldPlayerController::Client_OnLockInResult_Implementation(
    bool bSuccess, const FString &Reason)
{
  UE_LOG(LogSkaldUI, Log, TEXT("LockIn result: %s (%s)"),
         bSuccess ? TEXT("SUCCESS") : TEXT("FAIL"), *Reason);

  if (!bSuccess)
  {
    if (!Reason.IsEmpty())
    {
      UE_LOG(LogSkaldUI, Warning, TEXT("LockIn failed: %s"), *Reason);
    }

    if (IsLocalController() && GEngine && !Reason.IsEmpty())
    {
      GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, Reason);
    }

    if (FighterSelectionWidget)
    {
      FighterSelectionWidget->SetLockInButtonEnabled(true);
    }
    return;
  }

  HandleFighterSelectionLockedIn();
}

void ASkaldPlayerController::HandleBattlePhaseChanged() {
  if (const ASkaldGameState *SGS =
          GetWorld() ? GetWorld()->GetGameState<ASkaldGameState>() : nullptr) {
    if (SGS->BattlePhase == EBattlePhase::Deploy) {
      if (HasAuthority()) {
        if (const ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
          if (PS->bIsAI) {
            return;
          }
        }
        Client_ShowDeployUI();
      } else {
        ShowDeployUIInternal();
      }
    }
  }
}

bool ASkaldPlayerController::Server_CommitArmy_Validate(
    const TArray<FFighterDefinition> &Chosen) {
  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS) {
    return false;
  }

  int32 TotalCost = 0;
  for (const FFighterDefinition &Def : Chosen) {
    if (Def.Faction != PS->Faction) {
      return false;
    }
    TotalCost += FMath::Max(Def.Stats.ArmyCost, 0);
    if (TotalCost > PS->PendingArmyBudget) {
      return false;
    }
  }

  return true;
}

void ASkaldPlayerController::Server_CommitArmy_Implementation(
    const TArray<FFighterDefinition> &Chosen) {
  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>();
  if (!GI) {
    return;
  }
  const FS_BattlePayload Battle = GI->PendingBattle;

  if (PS) {
    const int32 PlayerID = PS->GetPlayerId();
    if (PlayerID != Battle.AttackerPlayerID &&
        PlayerID != Battle.DefenderPlayerID) {
      return;
    }

    TArray<FFighterDefinition> ValidFighters;
    int32 TotalCost = 0;
    for (const FFighterDefinition &Def : Chosen) {
      if (Def.Faction != PS->Faction) {
        continue;
      }
      const int32 Cost = FMath::Max(Def.Stats.ArmyCost, 0);
      if (TotalCost + Cost > PS->PendingArmyBudget) {
        break;
      }
      ValidFighters.Add(Def);
      TotalCost += Cost;
    }

    PS->PendingArmy = ValidFighters;
    PS->bArmyLockedIn = true;
  }

  if (ASkald_BattleGameMode *BattleGM =
          GetWorld()->GetAuthGameMode<ASkald_BattleGameMode>()) {
    BattleGM->TryLaunchBattle();
  }
}

void ASkaldPlayerController::InitializeBattleHUD() {
  if (!IsLocalController())
    return;
  if (!BattleHUDWidgetClass)
    return;
  if (!BattleHudWidget) {
    BattleHudWidget =
        CreateWidget<UBattleHUDWidget>(this, BattleHUDWidgetClass);
    if (BattleHudWidget) {
      BattleHudWidget->AddToViewport(20);
      BattleHudWidget->SetVisibility(ESlateVisibility::Collapsed);

      // Hook HUD buttons to controller modes
      BattleHudWidget->OnMovePressed.AddDynamic(
          this, &ASkaldPlayerController::BeginMoveMode);
      BattleHudWidget->OnAttackPressed.AddDynamic(
          this, &ASkaldPlayerController::BeginAttackMode);
    }
  }

  USkaldGameInstance *GI = CachedGameInstance;
  if (!GI) {
    GI = GetGameInstance<USkaldGameInstance>();
    CachedGameInstance = GI;
  }

  AFighterPawn *ActiveFighter = nullptr;
  // Bind to active-fighter changes
  if (GI && GI->GridBattleManager) {
    GI->GridBattleManager->OnActiveFighterChanged.RemoveAll(this);
    GI->GridBattleManager->OnActiveFighterChanged.AddDynamic(
        this, &ASkaldPlayerController::HandleActiveFighterChanged);
    GI->GridBattleManager->OnBattleEnded.RemoveDynamic(
        this, &ASkaldPlayerController::HandleBattleEnded);
    GI->GridBattleManager->OnBattleEnded.AddDynamic(
        this, &ASkaldPlayerController::HandleBattleEnded);
    ActiveFighter = GI->GridBattleManager->GetActiveFighter();
  }

  HandleActiveFighterChanged(ActiveFighter);
}

void ASkaldPlayerController::ShowOverworldHUD() {
  ShowMainHUD();

  if (BattleHudWidget) {
    BattleHudWidget->RemoveFromParent();
    BattleHudWidget = nullptr;
  }

  if (FighterSelectionWidget) {
    FighterSelectionWidget->RemoveFromParent();
    FighterSelectionWidget = nullptr;
  }

  bBattleHUDVisible = false;
  bBattleHUDReadyToShow = false;
}

void ASkaldPlayerController::HideOverworldHUDForBattle() {
  HideMainHUD();

  bBattleHUDVisible = false;
  bBattleHUDReadyToShow = false;
  if (BattleHudWidget) {
    BattleHudWidget->SetVisibility(ESlateVisibility::Collapsed);
  }
}

void ASkaldPlayerController::EnsureBattleHUDVisible() {
  if (!IsLocalController()) {
    return;
  }

  bBattleHUDReadyToShow = false;

  InitializeBattleHUD();
  if (!BattleHudWidget) {
    return;
  }

  BattleHudWidget->SetVisibility(ESlateVisibility::Visible);
  bBattleHUDVisible = true;

  UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(
      this, BattleHudWidget, EMouseLockMode::DoNotLock, false);
  bShowMouseCursor = true;
  bEnableClickEvents = true;
  bEnableMouseOverEvents = true;
  DefaultMouseCaptureMode = EMouseCaptureMode::NoCapture;
}

UGridOverlayComponent *ASkaldPlayerController::FindGridOverlay() const {
  if (UWorld *World = GetWorld()) {
    for (TActorIterator<AActor> It(World); It; ++It) {
      if (UGridOverlayComponent *Comp =
              It->FindComponentByClass<UGridOverlayComponent>()) {
        return Comp;
      }
    }
  }
  return nullptr;
}

void ASkaldPlayerController::HandleActiveFighterChanged(
    AFighterPawn *NewFighter) {
  if (BattleHudWidget) {
    BattleHudWidget->BindToFighter(NewFighter);
  }
  if (NewFighter && bBattleHUDReadyToShow && !bBattleHUDVisible) {
    EnsureBattleHUDVisible();
  }
  // Clear previous highlights when the turn swaps
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    Grid->ClearHighlights();
  }
  CurrentCommandMode = EBattleCommandMode::None;
}

void ASkaldPlayerController::DetectBattleMap() {
  bool bDetectedBattleMap = false;

  if (!CachedGameInstance) {
    CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  }
  if (CachedGameInstance && CachedGameInstance->bIsInBattleMap) {
    bDetectedBattleMap = true;
  }

  FString CurrentMap;
  if (!bDetectedBattleMap) {
    CurrentMap = UGameplayStatics::GetCurrentLevelName(this, true);
    if (CurrentMap.Equals(TEXT("BattleMap"), ESearchCase::IgnoreCase)) {
      bDetectedBattleMap = true;
    }
  }

  if (!bDetectedBattleMap) {
    ATurnManager *TM = TurnManager;
    if (!TM) {
      if (!CachedGameMode) {
        CachedGameMode = GetWorld()->GetAuthGameMode<ASkaldGameMode>();
      }
      if (CachedGameMode) {
        TM = CachedGameMode->GetTurnManager();
      }
    }

    if (TM) {
      if (CurrentMap.IsEmpty()) {
        CurrentMap = UGameplayStatics::GetCurrentLevelName(this, true);
      }

      for (const TSoftObjectPtr<UWorld> &Map : TM->BattleMaps) {
        if (CurrentMap.Equals(Map.ToSoftObjectPath().GetAssetName(),
                              ESearchCase::IgnoreCase)) {
          bDetectedBattleMap = true;
          break;
        }
      }
    }
  }

  bIsBattleMap = bDetectedBattleMap;

  if (bIsBattleMap) {
    HideOverworldHUDForBattle();
    if (CachedGameInstance && CachedGameInstance->GridBattleManager &&
        CachedGameInstance->GridBattleManager->GetActiveFighter() &&
        bBattleHUDReadyToShow) {
      EnsureBattleHUDVisible();
    }
  } else {
    ShowOverworldHUD();
  }
}

void ASkaldPlayerController::HandlePostLoadMap(UWorld *LoadedWorld) {
  if (!LoadedWorld || !IsLocalPlayerController()) {
    return;
  }

  DetectBattleMap();
  InitializeFighterSelectionIfNeeded();
}

void ASkaldPlayerController::ShowTurnAnnouncement(const FString &PlayerName,
                                                  bool bIsMyTurn) {
  if (MainHUD) {
    MainHUD->ShowTurnAnnouncement(PlayerName);
    MainHUD->ShowTurnMessage(bIsMyTurn);
  } else if (GEngine) {
    const FString Message = FString::Printf(TEXT("%s's Turn"), *PlayerName);
    GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow, Message);
  }
}

void ASkaldPlayerController::NotifyTurnEnded(const FString &PlayerName) {
  if (MainHUD) {
    MainHUD->ShowTurnEnded(PlayerName);
  }
}

void ASkaldPlayerController::StartTurn() {
  UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(
      this, nullptr, EMouseLockMode::DoNotLock, false);
  bShowMouseCursor = true;
  bEnableClickEvents = true;
  bEnableMouseOverEvents = true;
  DefaultMouseCaptureMode = EMouseCaptureMode::NoCapture;

  // Drive GameState turn index so HUDs can react on all clients.
  if (ASkaldGameState *GS = GetWorld()->GetGameState<ASkaldGameState>()) {
    if (ASkaldPlayerState *MyPS = GetPlayerState<ASkaldPlayerState>()) {
      const int32 NewIndex = GS->PlayerArray.IndexOfByKey(MyPS);
      if (NewIndex != INDEX_NONE) {
        GS->CurrentTurnIndex =
            NewIndex; // RepNotify will fire OnTurnIndexChanged
        // If you haven't applied RepNotifies yet, you can optionally
        // direct-broadcast:
        GS->OnTurnIndexChanged.Broadcast(NewIndex);
      }
    }
  }
}

void ASkaldPlayerController::EndTurn() {
  UWidgetBlueprintLibrary::SetInputMode_GameOnly(this);
  bShowMouseCursor = false;
  if (!EnsureTurnManager(TEXT("EndTurn"))) {
    return;
  }

  TurnManager->AdvanceTurn();
}

void ASkaldPlayerController::EndPhase() {
  if (!EnsureTurnManager(TEXT("EndPhase"))) {
    return;
  }

  ETurnPhase Phase = TurnManager->GetCurrentPhase();
  if (Phase == ETurnPhase::ArmyPlacement) {
    if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
      PS->DeployableUnits = 0;
      TurnManager->BroadcastDeployableUnits(PS);
    }
  }

  TurnManager->EndCurrentPhase();
}

bool ASkaldPlayerController::ValidateAttack(int32 FromID, int32 ToID,
                                            int32 ArmySent, bool bUseSiege,
                                            FString *OutError) {
  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    if (OutError) {
      *OutError = TEXT("World map not found");
    }
    return false;
  }

  ATerritory *Source = WorldMap->GetTerritoryById(FromID);
  ATerritory *Target = WorldMap->GetTerritoryById(ToID);
  if (!Source || !Target) {
    if (OutError) {
      *OutError = TEXT("Invalid territory selection");
    }
    return false;
  }

  if (!WorldMap->AreTerritoriesAdjacent(Source, Target)) {
    if (OutError) {
      *OutError = TEXT("Cannot attack non-adjacent territory");
    }
    return false;
  }

  if (ArmySent <= 0 || ArmySent >= Source->ArmyUnits) {
    if (OutError) {
      *OutError = TEXT("Invalid army count for attack");
    }
    return false;
  }

  if (!SkaldHelpers::MeetsCapitalAttackRequirement(Target->bIsCapital,
                                                   ArmySent)) {
    if (OutError) {
      *OutError = TEXT("Insufficient forces to attack capital");
    }
    return false;
  }

  return true;
}

void ASkaldPlayerController::HandleAttackRequested(int32 FromID, int32 ToID,
                                                   int32 ArmySent,
                                                   bool bUseSiege) {
  UE_LOG(LogSkald, Log, TEXT("HUD attack from %d to %d with %d"), FromID, ToID,
         ArmySent);
  FString Error;
  if (!ValidateAttack(FromID, ToID, ArmySent, bUseSiege, &Error)) {
    NotifyActionError(Error);
    return;
  }

  ServerHandleAttack(FromID, ToID, ArmySent, bUseSiege);
}

void ASkaldPlayerController::ServerHandleAttack_Implementation(int32 FromID,
                                                               int32 ToID,
                                                               int32 ArmySent,
                                                               bool bUseSiege) {
  FString Error;
  if (!ValidateAttack(FromID, ToID, ArmySent, bUseSiege, &Error)) {
    NotifyActionError(Error);
    return;
  }

  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  ATerritory *Source = WorldMap ? WorldMap->GetTerritoryById(FromID) : nullptr;
  ATerritory *Target = WorldMap ? WorldMap->GetTerritoryById(ToID) : nullptr;
  if (!Source || !Target) {
    return;
  }

  ASkaldPlayerState *AttackerPS = Source->OwningPlayer;
  ASkaldPlayerState *DefenderPS = Target->OwningPlayer;

  if (TurnManager) {
    FS_BattlePayload Battle;
    Battle.AttackerPlayerID = AttackerPS ? AttackerPS->GetPlayerId() : -1;
    Battle.DefenderPlayerID = DefenderPS ? DefenderPS->GetPlayerId() : -1;
    Battle.FromTerritoryID = FromID;
    Battle.TargetTerritoryID = ToID;
    Battle.ArmyCountSent = ArmySent;
    Battle.IsCapitalAttack = Target->bIsCapital;
    if (AttackerPS) {
      Battle.AttackerFaction = AttackerPS->Faction;
      Battle.AttackerDisplayName =
          ResolvePlayerName(AttackerPS, TEXT("ServerHandleAttack_Attacker"));
      Battle.bAttackerIsAI = AttackerPS->bIsAI;
    }
    if (DefenderPS) {
      Battle.DefenderFaction = DefenderPS->Faction;
      Battle.DefenderDisplayName =
          ResolvePlayerName(DefenderPS, TEXT("ServerHandleAttack_Defender"));
      Battle.bDefenderIsAI = DefenderPS->bIsAI;
    }
    if (bUseSiege && CachedGameMode) {
      const int32 SiegeID = CachedGameMode->ConsumeSiege(FromID);
      if (SiegeID > 0) {
        Battle.AssignedSiegeIDs.Add(SiegeID);
      }
    }
    Battle.DefenderArmyCount = Target ? Target->ArmyUnits : 0;
    if (!CachedGameMode) {
      CachedGameMode = GetWorld()->GetAuthGameMode<ASkaldGameMode>();
    }
    if (CachedGameMode) {
      CachedGameMode->CacheWorldMapSnapshot();
    }
    TurnManager->TriggerGridBattle(Battle);
    return;
  }

  int32 AttackingForces = ArmySent;
  int32 DefendingForces = Target->ArmyUnits;
  if (bUseSiege && CachedGameMode) {
    CachedGameMode->ConsumeSiege(FromID);
  }

  Source->ArmyUnits -= ArmySent;

  FRandomStream *CombatStream = nullptr;
  if (CachedGameInstance) {
    CachedGameInstance->SeedCombatRandomStream(FMath::Rand());
    CombatStream = &CachedGameInstance->CombatRandomStream;
  } else {
    static FRandomStream FallbackStream;
    FallbackStream.Initialize(FMath::Rand());
    CombatStream = &FallbackStream;
  }

  while (AttackingForces > 0 && DefendingForces > 0) {
    const int32 AttackRoll = CombatStream->RandRange(1, 6);
    const int32 DefendRoll = CombatStream->RandRange(1, 6);
    if (AttackRoll > DefendRoll) {
      --DefendingForces;
    } else {
      --AttackingForces;
    }
  }

  if (DefendingForces <= 0) {
    Target->OwningPlayer = AttackerPS;
    Target->ArmyUnits = AttackingForces;
  } else {
    Target->ArmyUnits = DefendingForces;
  }

  Source->RefreshAppearance();
  Target->RefreshAppearance();

  if (TurnManager) {
    for (ASkaldPlayerController *Controller : TurnManager->GetControllers()) {
      if (USkaldMainHUDWidget *HUD =
              Controller ? Controller->GetHUDWidget() : nullptr) {
        const FString OwnerName =
            ResolvePlayerName(Target->OwningPlayer, TEXT("ServerHandleAttack_Update"));
        HUD->UpdateTerritoryInfo(Target->TerritoryName, OwnerName,
                                 Target->ArmyUnits);
      }
    }
  }
}

void ASkaldPlayerController::HandleMoveRequested(int32 FromID, int32 ToID,
                                                 int32 Troops) {
  UE_LOG(LogSkald, Log, TEXT("HUD move from %d to %d with %d"), FromID, ToID,
         Troops);

  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    NotifyActionError(TEXT("World map not found"));
    return;
  }

  ATerritory *Source = WorldMap->GetTerritoryById(FromID);
  ATerritory *Target = WorldMap->GetTerritoryById(ToID);
  if (!Source || !Target) {
    NotifyActionError(TEXT("Invalid territory selection"));
    return;
  }

  TArray<ATerritory *> Path;
  if (!WorldMap->FindPath(Source, Target, Path)) {
    NotifyActionError(TEXT("No valid path for movement"));
    return;
  }

  if (Troops <= 0 || Troops >= Source->ArmyUnits) {
    NotifyActionError(TEXT("Invalid troop count for movement"));
    return;
  }

  ServerHandleMove(FromID, ToID, Troops);
}

void ASkaldPlayerController::ServerHandleMove_Implementation(int32 FromID,
                                                             int32 ToID,
                                                             int32 Troops) {
  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    return;
  }

  ATerritory *Source = WorldMap->GetTerritoryById(FromID);
  ATerritory *Target = WorldMap->GetTerritoryById(ToID);
  if (!Source || !Target) {
    return;
  }

  if (Troops <= 0 || Troops >= Source->ArmyUnits) {
    return;
  }

  if (!WorldMap->MoveBetween(Source, Target, Troops)) {
    return;
  }

  if (TurnManager) {
    for (ASkaldPlayerController *Controller : TurnManager->GetControllers()) {
      if (USkaldMainHUDWidget *HUD =
              Controller ? Controller->GetHUDWidget() : nullptr) {
        const FString SourceOwner =
            ResolvePlayerName(Source->OwningPlayer, TEXT("ServerHandleMove_Source"));
        HUD->UpdateTerritoryInfo(Source->TerritoryName, SourceOwner,
                                 Source->ArmyUnits);
        const FString TargetOwner =
            ResolvePlayerName(Target->OwningPlayer, TEXT("ServerHandleMove_Target"));
        HUD->UpdateTerritoryInfo(Target->TerritoryName, TargetOwner,
                                 Target->ArmyUnits);
      }
    }
  }
}

void ASkaldPlayerController::ServerBuildSiege_Implementation(
    int32 TerritoryID, ESiegeWeapon SiegeType) {
  if (CachedGameMode) {
    CachedGameMode->BuildSiegeAtTerritory(TerritoryID, SiegeType);
  }
}

void ASkaldPlayerController::ServerDeployUnits_Implementation(int32 TerritoryID,
                                                              int32 Amount) {
  if (Amount <= 0) {
    UE_LOG(LogSkald, Warning,
           TEXT("ServerDeployUnits called with non-positive amount: %d"),
           Amount);
    NotifyActionError(TEXT("Invalid deploy amount"));
    return;
  }

  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    UE_LOG(LogSkald, Warning, TEXT("ServerDeployUnits: World map not found"));
    NotifyActionError(TEXT("World map not found"));
    return;
  }

  ATerritory *Terr = WorldMap->GetTerritoryById(TerritoryID);
  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!Terr || !PS) {
    UE_LOG(LogSkald, Warning, TEXT("ServerDeployUnits: Invalid territory %d"),
           TerritoryID);
    NotifyActionError(TEXT("Invalid territory selection"));
    return;
  }

  if (!WorldMap->IsOwnedBy(Terr, PS)) {
    UE_LOG(LogSkald, Warning,
           TEXT("ServerDeployUnits: Player %d does not own territory %d"),
           PS->GetPlayerId(), TerritoryID);
    NotifyActionError(TEXT("You do not own this territory"));
    return;
  }

  if (PS->DeployableUnits < Amount) {
    UE_LOG(LogSkald, Warning,
           TEXT("ServerDeployUnits: Insufficient units. Have %d need %d"),
           PS->DeployableUnits, Amount);
    NotifyActionError(TEXT("Not enough deployable units"));
    return;
  }

  Terr->ArmyUnits += Amount;
  Terr->RefreshAppearance();

  PS->DeployableUnits -= Amount;

  if (TurnManager) {
    TurnManager->BroadcastDeployableUnits(PS);
  }
}

void ASkaldPlayerController::ServerSelectTerritory_Implementation(
    int32 TerritoryID) {
  UE_LOG(LogSkald, Log, TEXT("ServerSelectTerritory called with %d"),
         TerritoryID);
  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    return;
  }

  if (TerritoryID < 0) {
    WorldMap->SelectTerritory(nullptr);     // server authority
    WorldMap->MulticastSelectTerritory(-1); // replicate to all clients
    return;
  }

  ATerritory *Terr = WorldMap->GetTerritoryById(TerritoryID);
  if (!Terr) {
    return;
  }

  WorldMap->SelectTerritory(Terr);                 // server authority
  WorldMap->MulticastSelectTerritory(TerritoryID); // replicate to all clients
}

void ASkaldPlayerController::ClientSelectTerritory_Implementation(
    int32 TerritoryID) {
  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    return;
  }

  ATerritory *Terr =
      TerritoryID >= 0 ? WorldMap->GetTerritoryById(TerritoryID) : nullptr;
  WorldMap->SelectTerritory(Terr);
  UE_LOG(LogSkald, Log, TEXT("ClientSelectTerritory <- %d"), TerritoryID);
}

void ASkaldPlayerController::HandleEndAttackRequested(bool bConfirmed) {
  UE_LOG(LogSkald, Log, TEXT("HUD end attack %s"),
         bConfirmed ? TEXT("confirmed") : TEXT("cancelled"));
}

void ASkaldPlayerController::HandleEndMovementRequested(bool bConfirmed) {
  UE_LOG(LogSkald, Log, TEXT("HUD end move %s"),
         bConfirmed ? TEXT("confirmed") : TEXT("cancelled"));
}

void ASkaldPlayerController::HandleEngineeringRequested(int32 CapitalID,
                                                        uint8 UpgradeType) {
  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    const int32 Cost = 10;
    PS->Resources = FMath::Max(0, PS->Resources - Cost);
    if (TurnManager) {
      TurnManager->BroadcastResources(PS);
    }
  }
}

void ASkaldPlayerController::HandleBuildSiegeRequested(int32 TerritoryID,
                                                       ESiegeWeapon SiegeType) {
  ServerBuildSiege(TerritoryID, SiegeType);
}

void ASkaldPlayerController::HandleDigTreasureRequested(int32 TerritoryID) {
  ServerDigTreasure(TerritoryID);
}

void ASkaldPlayerController::ServerDigTreasure_Implementation(
    int32 TerritoryID) {
  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    return;
  }

  ATerritory *Terr = WorldMap->GetTerritoryById(TerritoryID);
  if (!Terr) {
    return;
  }

  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    if (Terr->OwningPlayer == PS && Terr->bHasTreasure) {
      Terr->bHasTreasure = false;
      Terr->RefreshAppearance();
      PS->Resources += 5;
      if (TurnManager) {
        TurnManager->BroadcastResources(PS);
      }
    }
  }
}

void ASkaldPlayerController::HandleEngineeringPhase() {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        return;
      }
    }
  }

  UE_LOG(LogSkald, Log, TEXT("Engineering phase started"));
  if (MainHUD) {
    MainHUD->CancelAttackSelection();
    MainHUD->CancelMoveSelection();
    MainHUD->UpdateInitiativeText(TEXT("Engineering Phase"));
  }
}

void ASkaldPlayerController::HandleTreasurePhase() {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        return;
      }
    }
  }

  UE_LOG(LogSkald, Log, TEXT("Treasure phase started"));
  if (MainHUD) {
    MainHUD->CancelAttackSelection();
    MainHUD->CancelMoveSelection();
    MainHUD->UpdateInitiativeText(TEXT("Treasure Phase"));
  }
}

void ASkaldPlayerController::HandleMovementPhase() {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        return;
      }
    }
  }

  UE_LOG(LogSkald, Log, TEXT("Movement phase started"));
  if (MainHUD) {
    MainHUD->CancelAttackSelection();
    MainHUD->BeginMoveSelection();
    MainHUD->UpdateInitiativeText(TEXT("Movement Phase"));
  }
}

void ASkaldPlayerController::HandleEndTurnPhase() {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        return;
      }
    }
  }

  UE_LOG(LogSkald, Log, TEXT("EndTurn phase started"));
  if (MainHUD) {
    MainHUD->ShowEndingTurn();
    MainHUD->UpdateInitiativeText(TEXT("End Turn Phase"));
  }
}

void ASkaldPlayerController::HandleRevoltPhase() {
  if (const UWorld *W = GetWorld()) {
    if (const auto *GI = W->GetGameInstance<USkaldGameInstance>()) {
      if (GI->bTravelPending) {
        return;
      }
    }
  }

  UE_LOG(LogSkald, Log, TEXT("Revolt phase started"));
  if (MainHUD) {
    MainHUD->HideEndingTurn();
    MainHUD->UpdateInitiativeText(TEXT("Revolt Phase"));
  }
}

void ASkaldPlayerController::HandleTerritorySelected(ATerritory *Terr) {
  if (!Terr || !MainHUD) {
    return;
  }

  const FString OwnerName =
      ResolvePlayerName(Terr->OwningPlayer, TEXT("HandleTerritorySelected"));
  MainHUD->UpdateTerritoryInfo(Terr->TerritoryName, OwnerName,
                                     Terr->ArmyUnits);
  MainHUD->OnTerritoryClickedUI(Terr);
}

void ASkaldPlayerController::NotifyActionError_Implementation(
    const FString &Message) {
  UE_LOG(LogSkald, Warning, TEXT("%s"), *Message);
  if (MainHUD) {
    MainHUD->ShowErrorMessage(Message);
  } else if (GEngine) {
    GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, Message);
  }
}

bool ASkaldPlayerController::EnsureTurnManager(const TCHAR *Caller) {
  if (TurnManager) {
    return true;
  }

  UE_LOG(LogSkald, Warning,
         TEXT("%s called without a TurnManager. Attempting to reacquire."),
         Caller);

  if (!CachedGameMode) {
    CachedGameMode = GetWorld()->GetAuthGameMode<ASkaldGameMode>();
  }

  if (CachedGameMode) {
    SetTurnManager(CachedGameMode->GetTurnManager());
  }

  if (!TurnManager) {
    UE_LOG(LogSkald, Warning, TEXT("TurnManager still missing; aborting %s."),
           Caller);
    return false;
  }

  return true;
}

void ASkaldPlayerController::BuildPlayerDataArray(
    TArray<FS_PlayerData> &OutPlayers) const {
  OutPlayers.Reset();
  if (!CachedGameState) {
    return;
  }

  for (APlayerState *PSBase : CachedGameState->PlayerArray) {
    if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase)) {
      FS_PlayerData Data;
      Data.PlayerID = PS->GetPlayerId();
      Data.PlayerName = PS->GetResolvedPlayerName(TEXT("BuildPlayerDataArray"));
      Data.IsAI = PS->bIsAI;
      Data.Faction = PS->Faction;
      Data.Resources = PS->Resources;
      Data.IsEliminated = PS->IsEliminated;
      OutPlayers.Add(Data);
    }
  }
}

void ASkaldPlayerController::HandlePlayersUpdated() {
  if (CachedGameState && MainHUD) {
    TArray<FS_PlayerData> Players;
    BuildPlayerDataArray(Players);
    MainHUD->RefreshPlayerList(Players);

    if (ASkaldPlayerState *LocalPS = GetPlayerState<ASkaldPlayerState>()) {
      MainHUD->UpdateResources(LocalPS->Resources);
    }
  }

  InitializeFighterSelectionIfNeeded();
}

void ASkaldPlayerController::HandleFactionsUpdated() {
  if (!MainHUD || !CachedGameState) {
    return;
  }

  TArray<FS_PlayerData> Players;
  BuildPlayerDataArray(Players);
  MainHUD->RefreshPlayerList(Players);

  if (ASkaldPlayerState *LocalPS = GetPlayerState<ASkaldPlayerState>()) {
    MainHUD->UpdateResources(LocalPS->Resources);
  }
}

void ASkaldPlayerController::HandleWorldStateChanged() {
  if (BattleResultWidget) {
    BattleResultWidget->RemoveFromParent();
    BattleResultWidget = nullptr;
  }

  if (!CachedGameInstance) {
    CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  }
  const bool bShouldShowOverworldHUD =
      !bIsBattleMap || (CachedGameInstance && !CachedGameInstance->bIsInBattleMap);
  if (bShouldShowOverworldHUD) {
    ShowOverworldHUD();
  }

  if (!MainHUD) {
    return;
  }

  ShowMainHUD();

  // Update territory info for the currently selected territory if available.
  if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          GetWorld(), AWorldMap::StaticClass()))) {
    if (ATerritory *Terr = WorldMap->SelectedTerritory) {
      const FString OwnerName = ResolvePlayerName(
          Terr->OwningPlayer, TEXT("HandleWorldStateChanged"));
      MainHUD->UpdateTerritoryInfo(Terr->TerritoryName, OwnerName,
                                         Terr->ArmyUnits);
    }
  }

  // Refresh player list from the game state.
  if (CachedGameState) {
    TArray<FS_PlayerData> Players;
    BuildPlayerDataArray(Players);
    MainHUD->RefreshPlayerList(Players);
  }

  // Update deploy/phase banners.
  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    MainHUD->UpdateDeployableUnits(PS->DeployableUnits);
    MainHUD->UpdateResources(PS->Resources);
  }
  if (TurnManager) {
    MainHUD->UpdatePhaseBanner(TurnManager->GetCurrentPhase());
  }
}

void ASkaldPlayerController::HandlePlayerLockedIn() { HandleFactionLockedIn(); }

void ASkaldPlayerController::HandleFactionLockedIn() {
  if (bHasInitialized) {
    return;
  }
  bHasInitialized = true;

  if (ChoosePlayerWidget) {
    ChoosePlayerWidget->OnPlayerLockedIn.RemoveDynamic(
        this, &ASkaldPlayerController::HandleFactionLockedIn);
    ChoosePlayerWidget->RemoveFromParent();
    ChoosePlayerWidget = nullptr;
  }

  if (MainHUD) {
    ShowMainHUD();
    if (CachedGameState) {
      TArray<FS_PlayerData> Players;
      BuildPlayerDataArray(Players);
      MainHUD->RefreshPlayerList(Players);
    }
    if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
      MainHUD->LocalPlayerID = PS->GetPlayerId();
      MainHUD->UpdateDeployableUnits(PS->DeployableUnits);
      MainHUD->UpdateResources(PS->Resources);
      MainHUD->SyncPhaseButtons(MainHUD->CurrentPlayerID ==
                                      MainHUD->LocalPlayerID);
    }
    if (TurnManager) {
      MainHUD->UpdatePhaseBanner(TurnManager->GetCurrentPhase());
    }
  }

  bShowMouseCursor = true;
  bEnableClickEvents = true;
  bEnableMouseOverEvents = true;
  DefaultMouseCaptureMode = EMouseCaptureMode::NoCapture;
  SetIgnoreMoveInput(false);
  SetIgnoreLookInput(false);
  TryBindWorldMap();

  // Refresh the HUD after any AI opponents have been spawned by the game
  // mode's PopulateAIPlayers call. This ensures the local player sees the
  // full roster once lock-in is complete.
  if (CachedGameState) {
    FTimerDelegate RefreshDelegate = FTimerDelegate::CreateUObject(
        this, &ASkaldPlayerController::HandlePlayersUpdated);
    GetWorldTimerManager().SetTimerForNextTick(RefreshDelegate);
  }
}

void ASkaldPlayerController::HandleFighterSelectionLockedIn() {
  if (UFighterSelectionWidget *Selection = FighterSelectionWidget) {
    Selection->RemoveFromParent();
    FighterSelectionWidget = nullptr;
  }

  bBattleHUDReadyToShow = true;

  // Ensure the HUD is initialized so the controller is bound to active-fighter
  // updates even if no pawn has been selected yet.
  InitializeBattleHUD();

  UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(
      this, nullptr, EMouseLockMode::DoNotLock, false);
  bShowMouseCursor = true;
  bEnableClickEvents = true;
  bEnableMouseOverEvents = true;
  DefaultMouseCaptureMode = EMouseCaptureMode::NoCapture;
  SetIgnoreMoveInput(false);
  SetIgnoreLookInput(false);

  if (!CachedGameInstance) {
    CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  }
  if (CachedGameInstance && CachedGameInstance->GridBattleManager &&
      !bBattleHUDVisible) {
    if (AFighterPawn *ActiveFighter =
            CachedGameInstance->GridBattleManager->GetActiveFighter()) {
      EnsureBattleHUDVisible();
    }
  }
}

void ASkaldPlayerController::SetupInputComponent() {
  Super::SetupInputComponent();

  if (InputComponent) {
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this,
                            &ASkaldPlayerController::HandleGridClick);
  }
}

void ASkaldPlayerController::BeginMoveMode() {
  CurrentCommandMode = EBattleCommandMode::Move;
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
      if (GI->GridBattleManager && GI->GridBattleManager->GetActiveFighter()) {
        Grid->HighlightMovement(GI->GridBattleManager->GetActiveFighter());
      }
    }
  }
}

void ASkaldPlayerController::BeginAttackMode() {
  CurrentCommandMode = EBattleCommandMode::Attack;
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
      if (GI->GridBattleManager && GI->GridBattleManager->GetActiveFighter()) {
        Grid->HighlightAttack(GI->GridBattleManager->GetActiveFighter());
      }
    }
  }
}

void ASkaldPlayerController::HandleGridClick() {
  if (!IsLocalController())
    return;

  // Non-battle map handling
  FHitResult Hit;
  if (!bIsBattleMap) {
    GetHitResultUnderCursor(ECC_Visibility, /*bTraceComplex*/ true, Hit);
    if (ATerritory *Terr = Cast<ATerritory>(Hit.GetActor())) {
      ServerSelectTerritory(Terr->TerritoryID);
    } else {
      ServerSelectTerritory(-1);
    }
    return;
  }

  // Get active fighter
  AFighterPawn *Active = nullptr;
  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    if (GI->GridBattleManager) {
      Active = GI->GridBattleManager->GetActiveFighter();
    }
  }
  if (!Active || !Active->IsAlive())
    return;

  // Trace under cursor
  GetHitResultUnderCursor(ECC_Visibility, /*bTraceComplex*/ false, Hit);

  UGridOverlayComponent *Grid = FindGridOverlay();
  if (!Grid)
    return;

  switch (CurrentCommandMode) {
  case EBattleCommandMode::Move: {
    const FVector Impact =
        Hit.bBlockingHit ? Hit.ImpactPoint : FVector::ZeroVector;
    const FIntPoint Cell = Grid->WorldToGrid(Impact);
    Active->MoveToCell(Cell);
    Grid->ClearHighlights();
    if (Active->ActionsRemaining <= 0) {
      CurrentCommandMode = EBattleCommandMode::None;
      if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
        if (GI->GridBattleManager) {
          GI->GridBattleManager->AdvanceTurn();
        }
      }
    }
    break;
  }
  case EBattleCommandMode::Attack: {
    AFighterPawn *TargetPawn = Cast<AFighterPawn>(Hit.GetActor());
    if (!TargetPawn && Hit.bBlockingHit) {
      // No pawn? For now require clicking on a pawn to attack.
    }
    if (TargetPawn && TargetPawn != Active && TargetPawn->IsAlive()) {
      if (TargetPawn->bIsAttacker != Active->bIsAttacker) {
        Active->PerformAttack(TargetPawn);
      }
    }
    Grid->ClearHighlights();
    if (Active->ActionsRemaining <= 0) {
      CurrentCommandMode = EBattleCommandMode::None;
      if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
        if (GI->GridBattleManager) {
          GI->GridBattleManager->AdvanceTurn();
        }
      }
    }
    break;
  }
  default:
    break;
  }
}

void ASkaldPlayerController::HandleBattleEnded(ESkaldFaction WinningFaction,
                                               int32 AttackerCasualties,
                                               int32 DefenderCasualties) {
  if (BattleHudWidget) {
    BattleHudWidget->RemoveFromParent();
    BattleHudWidget = nullptr;
  }

  bool bPlayerWon = false;
  bool bPlayerLost = false;
  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    if (WinningFaction != ESkaldFaction::None && PS->Faction == WinningFaction) {
      bPlayerWon = true;
    } else if (WinningFaction != ESkaldFaction::None) {
      bPlayerLost = true;
    }
  }

  if (!VictoryWidgetClass) {
    VictoryWidgetClass = UBattleResultWidget::StaticClass();
  }

  if (BattleResultWidget) {
    BattleResultWidget->RemoveFromParent();
    BattleResultWidget = nullptr;
  }

  if (VictoryWidgetClass) {
    if (UUserWidget *Widget =
            CreateWidget<UUserWidget>(this, VictoryWidgetClass)) {
      if (UBattleResultWidget *ResultWidget = Cast<UBattleResultWidget>(Widget)) {
        ResultWidget->SetBattleOutcome(bPlayerWon, bPlayerLost, AttackerCasualties,
                                       DefenderCasualties);
      }
      BattleResultWidget = Widget;
      BattleResultWidget->AddToViewport();
    }
  }

  HideMainHUD();

  if (!CachedGameInstance) {
    CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  }
  const bool bReadyForOverworldHUD =
      !bIsBattleMap || (CachedGameInstance && !CachedGameInstance->bIsInBattleMap);
  if (bReadyForOverworldHUD) {
    ShowOverworldHUD();
  }
}
