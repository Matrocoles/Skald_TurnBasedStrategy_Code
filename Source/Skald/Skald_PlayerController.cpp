#include "Skald_PlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "ChoosePlayerWidget.h"
#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "FighterDataLibrary.h"
#include "FighterPawn.h"
#include "GridBattleManager.h"
#include "GridOverlayComponent.h"
#include "InputCoreTypes.h"
#include "Internationalization/Text.h"
#include "Kismet/GameplayStatics.h"
#include "Skald.h"
#include "SkaldTypes.h"
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
#include "UI/FighterSelectionWidget.h"
#include "UI/InGameMenuWidget.h"
#include "UI/SkaldMainHUDWidget.h"
#include "UI/SkaldUIHelpers.h"
#include "UObject/ConstructorHelpers.h"
#include "WorldMap.h"

#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"

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

bool IsCursorOverInteractableSlateWidget() {
  if (!FSlateApplication::IsInitialized()) {
    return false;
  }

  FSlateApplication &SlateApp = FSlateApplication::Get();
  const FVector2D CursorPos = SlateApp.GetCursorPos();

  TArray<TSharedRef<SWindow>> Windows = SlateApp.GetInteractiveTopLevelWindows();
  if (Windows.Num() == 0) {
    return false;
  }

  const FWidgetPath WidgetPath =
      SlateApp.LocateWindowUnderMouse(CursorPos, Windows);

  for (int32 Index = WidgetPath.Widgets.Num() - 1; Index >= 0; --Index) {
    const FArrangedWidget &ArrangedWidget = WidgetPath.Widgets[Index];
    if (ArrangedWidget.Widget->IsInteractable()) {
      return true;
    }
  }

  return false;
}
}

ASkald_BattleGameMode *ASkaldPlayerController::ResolveBattleGameMode() {
  if (UWorld *World = GetWorld()) {
    if (ASkald_BattleGameMode *BattleGM =
            World->GetAuthGameMode<ASkald_BattleGameMode>()) {
      return BattleGM;
    }
  }

  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    return GI->GetActiveBattleGameMode();
  }

  return nullptr;
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
  InGameMenuWidgetClass = UInGameMenuWidget::StaticClass();
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
    CachedGameInstance->OnBattleMapStateChanged.RemoveDynamic(
        this, &ASkaldPlayerController::HandleBattleMapStateChanged);
    CachedGameInstance->OnBattleMapStateChanged.AddDynamic(
        this, &ASkaldPlayerController::HandleBattleMapStateChanged);
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

  if (ASkaldGameState *SGS =
          GetWorld() ? GetWorld()->GetGameState<ASkaldGameState>() : nullptr) {
    if (SGS->BattlePhase == EBattlePhase::Deploy) {
      UE_LOG(LogSkaldBattle, Verbose,
             TEXT("PlayerController %s detected Deploy phase at BeginPlay"),
             *GetName());
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

  if (CachedGameInstance) {
    CachedGameInstance->OnBattleMapStateChanged.RemoveDynamic(
        this, &ASkaldPlayerController::HandleBattleMapStateChanged);
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

  if (InGameMenuWidget) {
    InGameMenuWidget->RemoveFromParent();
    InGameMenuWidget = nullptr;
  }

  if (AWorldMap *WorldMap = CachedWorldMap.Get()) {
    if (WorldMap->OnTerritorySelected.IsAlreadyBound(
            this, &ASkaldPlayerController::HandleTerritorySelected)) {
      WorldMap->OnTerritorySelected.RemoveDynamic(
          this, &ASkaldPlayerController::HandleTerritorySelected);
    }
  }
  CachedWorldMap.Reset();

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(WorldMapSearchHandle);
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
  FocusGameViewport(this);
  bShowMouseCursor = true;
  bEnableClickEvents = true;
  bEnableMouseOverEvents = true;
  DefaultMouseCaptureMode = EMouseCaptureMode::NoCapture;
}

void ASkaldPlayerController::HideMainHUD() {
  if (MainHUD) {
    MainHUD->SetVisibility(ESlateVisibility::Collapsed);
    UWidgetBlueprintLibrary::SetInputMode_GameOnly(this);
    bShowMouseCursor = false;
  }
}

void ASkaldPlayerController::ToggleInGameMenu() {
  if (!IsLocalController()) {
    return;
  }

  if (InGameMenuWidget &&
      InGameMenuWidget->GetVisibility() != ESlateVisibility::Hidden &&
      InGameMenuWidget->GetVisibility() != ESlateVisibility::Collapsed) {
    HideInGameMenu();
  } else {
    ShowInGameMenu();
  }
}

void ASkaldPlayerController::ShowInGameMenu() {
  if (!IsLocalController()) {
    return;
  }

  if (!InGameMenuWidget) {
    if (!InGameMenuWidgetClass) {
      InGameMenuWidgetClass = UInGameMenuWidget::StaticClass();
    }

    if (InGameMenuWidgetClass) {
      InGameMenuWidget = CreateWidget<UInGameMenuWidget>(this, InGameMenuWidgetClass);
      if (InGameMenuWidget) {
        InGameMenuWidget->SetVisibility(ESlateVisibility::Hidden);
        InGameMenuWidget->AddToViewport(90);
      }
    }
  }

  if (!InGameMenuWidget) {
    return;
  }

  if (!InGameMenuWidget->IsInViewport()) {
    InGameMenuWidget->AddToViewport(90);
  }

  InGameMenuWidget->SetVisibility(ESlateVisibility::Visible);
  UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(
      this, InGameMenuWidget, EMouseLockMode::DoNotLock, /*bHideCursorDuringCapture*/ false);
  bShowMouseCursor = true;
}

void ASkaldPlayerController::HideInGameMenu() {
  if (!IsLocalController()) {
    return;
  }

  if (!InGameMenuWidget) {
    return;
  }

  InGameMenuWidget->SetVisibility(ESlateVisibility::Hidden);
  UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(
      this, nullptr, EMouseLockMode::DoNotLock, /*bHideCursorDuringCapture*/ false);
  bShowMouseCursor = true;
}

void ASkaldPlayerController::TryBindWorldMap() {
  UWorld *World = GetWorld();
  if (!World) {
    return;
  }

  if (CachedWorldMap.IsValid()) {
    World->GetTimerManager().ClearTimer(WorldMapSearchHandle);
    return;
  }
  CachedWorldMap.Reset();

  if (AWorldMap *FoundWorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          World, AWorldMap::StaticClass()))) {
    CachedWorldMap = FoundWorldMap;
    if (!FoundWorldMap->OnTerritorySelected.IsAlreadyBound(
            this, &ASkaldPlayerController::HandleTerritorySelected)) {
      FoundWorldMap->OnTerritorySelected.AddDynamic(
          this, &ASkaldPlayerController::HandleTerritorySelected);
      ensureMsgf(FoundWorldMap->OnTerritorySelected.IsAlreadyBound(
                     this, &ASkaldPlayerController::HandleTerritorySelected),
                 TEXT("Failed to bind HandleTerritorySelected to WorldMap."));
    }
    World->GetTimerManager().ClearTimer(WorldMapSearchHandle);
  } else {
    World->GetTimerManager().SetTimer(WorldMapSearchHandle, this,
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

  // If we've already locked in and are waiting for the battle HUD to take over,
  // keep the fighter selection screen hidden so we don't immediately flip the
  // input mode back to UI-only.
  if (bBattleHUDReadyToShow) {
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

void ASkaldPlayerController::Client_ShowFighterSelection_Implementation(
    int32 MaxBudget, ESkaldFaction Faction) {
  ShowFighterSelectionUI(MaxBudget, Faction);
}

void ASkaldPlayerController::Server_LockInSelection_Implementation(
    const TArray<FFighterDefinition> &SelectedFighters)
{
  UE_LOG(LogSkaldBattle, Log,
         TEXT("Server_LockInSelection: %s sent %d fighters"), *GetName(),
         SelectedFighters.Num());

  if (ASkald_BattleGameMode *GameMode = ResolveBattleGameMode())
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
      UE_LOG(LogSkaldBattle, Log,
             TEXT("PlayerController %s entering Deploy phase; fighters will be"
                  " spawned automatically."),
             *GetName());
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

  if (ASkald_BattleGameMode *BattleGM = ResolveBattleGameMode()) {
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
      BattleHudWidget->OnActivatePressed.AddDynamic(
          this, &ASkaldPlayerController::HandleActivatePressed);
      BattleHudWidget->OnEndTurnPressed.AddDynamic(
          this, &ASkaldPlayerController::HandleEndTurnPressed);
      BattleHudWidget->SetEndTurnVisibility(false);
      BattleHudWidget->SetActivateEnabled(false);
      BattleHudWidget->SetEndTurnEnabled(false);
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
    GI->GridBattleManager->OnRoundStarted.RemoveAll(this);
    GI->GridBattleManager->OnRoundStarted.AddDynamic(
        this, &ASkaldPlayerController::HandleRoundStarted);
    GI->GridBattleManager->OnBattleEnded.RemoveDynamic(
        this, &ASkaldPlayerController::HandleBattleEnded);
    GI->GridBattleManager->OnBattleEnded.AddDynamic(
        this, &ASkaldPlayerController::HandleBattleEnded);
    GI->GridBattleManager->OnAttackResolved.RemoveAll(this);
    GI->GridBattleManager->OnAttackResolved.AddDynamic(
        this, &ASkaldPlayerController::HandleAttackResolved);
    GI->GridBattleManager->OnAttackRejected.RemoveAll(this);
    GI->GridBattleManager->OnAttackRejected.AddDynamic(
        this, &ASkaldPlayerController::HandleAttackRejected);
    ActiveFighter = GI->GridBattleManager->GetActiveFighter();

    const int32 CurrentRound = GI->GridBattleManager->GetCurrentRound();
    if (CurrentRound > 0) {
      UpdateBattleRoundDisplay(CurrentRound,
                               GI->GridBattleManager->GetInitiativeWinner());
    }
  }

  DetermineControlledBattleSide();
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

  if (UWorld *World = GetWorld()) {
    if (AWorldMap *WorldMap = Cast<AWorldMap>(
            UGameplayStatics::GetActorOfClass(World, AWorldMap::StaticClass()))) {
      WorldMap->SetWorldActive(true);
    }
  }
}

void ASkaldPlayerController::HideOverworldHUDForBattle() {
  HideMainHUD();

  bBattleHUDVisible = false;
  bBattleHUDReadyToShow = false;
  if (BattleHudWidget) {
    BattleHudWidget->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (UWorld *World = GetWorld()) {
    if (AWorldMap *WorldMap = Cast<AWorldMap>(
            UGameplayStatics::GetActorOfClass(World, AWorldMap::StaticClass()))) {
      WorldMap->SetWorldActive(false);
    }
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
  FocusGameViewport(this);
  bShowMouseCursor = true;
  bEnableClickEvents = true;
  bEnableMouseOverEvents = true;
  DefaultMouseCaptureMode = EMouseCaptureMode::NoCapture;
}

UGridOverlayComponent *ASkaldPlayerController::FindGridOverlay() const {
  if (UWorld *World = GetWorld()) {
    return Skald::GridOverlay::FindActiveGridOverlay(World);
  }

  return nullptr;
}

AFighterPawn *ASkaldPlayerController::FindFighterAtCell(
    const FIntPoint &Cell) const {
  if (UWorld *World = GetWorld()) {
    for (TActorIterator<AFighterPawn> It(World); It; ++It) {
      AFighterPawn *Fighter = *It;
      if (Fighter && Fighter->OccupiesCell(Cell)) {
        return Fighter;
      }
    }
  }
  return nullptr;
}

void ASkaldPlayerController::HandleActiveFighterChanged(
    AFighterPawn *NewFighter) {
  if (NewFighter && NewFighter->IsAlive()) {
    LockedActiveFighter = NewFighter;
    SetSelectedFighter(NewFighter, true);
    if (bBattleHUDReadyToShow && !bBattleHUDVisible) {
      EnsureBattleHUDVisible();
    }
  } else {
    LockedActiveFighter = nullptr;
  }

  CancelCommandMode();
  UpdateBattleHUDButtons();
  UpdateBattlePlayersTurnDisplay();
  if (!NewFighter) {
    UpdateBattleHUDSelection();
  }

  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    if (NewFighter && NewFighter->IsAlive()) {
      Grid->HighlightSelection(NewFighter);
    } else {
      Grid->ClearSelectionHighlight();
    }
  }
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

      auto MatchesCurrentMap = [&](const TSoftObjectPtr<UWorld> &MapPtr) {
        return CurrentMap.Equals(MapPtr.ToSoftObjectPath().GetAssetName(),
                                 ESearchCase::IgnoreCase);
      };

      for (const TSoftObjectPtr<UWorld> &Map : TM->BattleMaps) {
        if (MatchesCurrentMap(Map)) {
          bDetectedBattleMap = true;
          break;
        }
      }

      if (!bDetectedBattleMap) {
        for (const FBattleMapDescriptor &Entry : TM->BattleMapEntries) {
          if (MatchesCurrentMap(Entry.Map)) {
            bDetectedBattleMap = true;
            break;
          }
        }
      }
    }
  }

  bIsBattleMap = bDetectedBattleMap;

  if (bIsBattleMap) {
    HideOverworldHUDForBattle();
    if (CachedGameInstance && CachedGameInstance->GridBattleManager &&
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
  if (!HasAuthority()) {
    ServerEndPhase();
    return;
  }

  HandleEndPhaseInternal();
}

void ASkaldPlayerController::ServerEndPhase_Implementation() {
  HandleEndPhaseInternal();
}

void ASkaldPlayerController::HandleEndPhaseInternal() {
  if (!EnsureTurnManager(TEXT("EndPhase"))) {
    return;
  }

  if (HasAuthority()) {
    ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
    if (!PS) {
      UE_LOG(LogSkald, Warning,
             TEXT("HandleEndPhaseInternal: %s has no PlayerState; rejecting."),
             *GetName());
      return;
    }

    if (ASkaldGameState *GS = GetWorld()->GetGameState<ASkaldGameState>()) {
      const int32 MyIndex = GS->PlayerArray.IndexOfByKey(PS);
      if (MyIndex == INDEX_NONE || GS->CurrentTurnIndex != MyIndex) {
        UE_LOG(LogSkald, Warning,
               TEXT("HandleEndPhaseInternal: %s attempted to end phase out of turn."),
               *GetName());
        return;
      }
    } else {
      UE_LOG(LogSkald, Warning,
             TEXT("HandleEndPhaseInternal: %s missing GameState; rejecting."),
             *GetName());
      return;
    }
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

  ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS) {
    NotifyActionError(TEXT("Missing player state"));
    return;
  }

  if (Source->OwningPlayer != PS || Target->OwningPlayer != PS) {
    NotifyActionError(TEXT("You may only move between your territories"));
    return;
  }

  const int32 MaxMovable = Source->ArmyUnits - 1;
  if (Troops <= 0 || Troops > MaxMovable) {
    NotifyActionError(TEXT("Invalid troop count for movement"));
    return;
  }

  TArray<ATerritory *> Path;
  if (!WorldMap->FindPath(Source, Target, Path) || Path.Num() < 2) {
    NotifyActionError(
        TEXT("Selected territories must be connected by a friendly path"));
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
  if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>()) {
    if (GI->bIsInBattleMap && TerritoryID >= 0) {
      return;
    }
  }

  UE_LOG(LogSkald, Log, TEXT("ServerSelectTerritory called with %d"),
         TerritoryID);
  AWorldMap *WorldMap = Cast<AWorldMap>(
      UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));
  if (!WorldMap) {
    return;
  }

  if (!WorldMap->IsWorldActive() && TerritoryID >= 0) {
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

  if (!WorldMap->IsWorldActive() && TerritoryID >= 0) {
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

void ASkaldPlayerController::HandleBattleMapStateChanged(bool /*bInBattleMap*/) {
  DetectBattleMap();
  InitializeFighterSelectionIfNeeded();
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

  SelectedFighter = nullptr;
  LockedActiveFighter = nullptr;
  CancelCommandMode();
  UpdateBattleHUDButtons();

  UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(
      this, nullptr, EMouseLockMode::DoNotLock, false);
  FocusGameViewport(this);
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
    EnsureBattleHUDVisible();
  }
}

void ASkaldPlayerController::SetupInputComponent() {
  Super::SetupInputComponent();

  if (InputComponent) {
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this,
                            &ASkaldPlayerController::HandleGridClick);
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this,
                            &ASkaldPlayerController::HandleRightClick);
    InputComponent->BindKey(EKeys::O, IE_Pressed, this,
                            &ASkaldPlayerController::ToggleInGameMenu);
  }
}

void ASkaldPlayerController::BeginMoveMode() {
  if (!LockedActiveFighter || !IsFriendlyFighter(LockedActiveFighter))
    return;
  CurrentCommandMode = EBattleCommandMode::Move;
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    Grid->HighlightMovement(LockedActiveFighter);
  }
}

void ASkaldPlayerController::BeginAttackMode() {
  if (!LockedActiveFighter || !IsFriendlyFighter(LockedActiveFighter))
    return;
  CurrentCommandMode = EBattleCommandMode::Attack;
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    Grid->HighlightAttack(LockedActiveFighter);
  }
}

void ASkaldPlayerController::HandleGridClick() {
  if (!IsLocalController())
    return;

  if (IsCursorOverInteractableSlateWidget()) {
    return;
  }

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
  if (!GetHitResultUnderCursor(ECC_Visibility, /*bTraceComplex*/ false, Hit)) {
    return;
  }

  UGridOverlayComponent *Grid = FindGridOverlay();
  if (!Grid)
    return;

  const FVector WorldLocation = Hit.bBlockingHit ? Hit.ImpactPoint : Hit.Location;
  const FIntPoint Cell = Grid->WorldToGrid(WorldLocation);
  if (!Grid->IsCellInBounds(Cell)) {
    return;
  }

  AFighterPawn *CellFighter = FindFighterAtCell(Cell);

  switch (CurrentCommandMode) {
  case EBattleCommandMode::Move: {
    if (!LockedActiveFighter) {
      CancelCommandMode();
      break;
    }
    if (!IsFriendlyFighter(LockedActiveFighter)) {
      CancelCommandMode();
      break;
    }
    FIntPoint TargetAnchor = Cell;
    if (Grid) {
      const int32 FootprintSize = LockedActiveFighter->GetFootprintSideLength();
      const FIntPoint StartCell = LockedActiveFighter->GetCurrentCell();
      const int32 MovementRange = LockedActiveFighter->Stats.Movement;
      const TArray<FIntPoint> PreviousCells = LockedActiveFighter->GetOccupiedCells();

      int32 BestDistanceToStart = MAX_int32;
      int32 BestDistanceToClicked = MAX_int32;
      bool bFoundValidAnchor = false;
      bool bBestAnchorMoves = false;

      for (int32 Dy = 0; Dy < FootprintSize; ++Dy) {
        for (int32 Dx = 0; Dx < FootprintSize; ++Dx) {
          const FIntPoint CandidateAnchor = Cell - FIntPoint(Dx, Dy);

          if (!Grid->IsCellInBounds(CandidateAnchor)) {
            continue;
          }

          const int32 DistanceToStart =
              FMath::Abs(CandidateAnchor.X - StartCell.X) +
              FMath::Abs(CandidateAnchor.Y - StartCell.Y);
          if (DistanceToStart > MovementRange) {
            continue;
          }

          const TArray<FIntPoint> CandidateCells =
              LockedActiveFighter->GetOccupiedCells(CandidateAnchor);

          bool bCanOccupyCandidate = true;
          for (const FIntPoint &CandidateCell : CandidateCells) {
            if (!Grid->IsCellInBounds(CandidateCell) ||
                Grid->IsObscured(CandidateCell)) {
              bCanOccupyCandidate = false;
              break;
            }

            const bool bCellPreviouslyOccupied =
                PreviousCells.Contains(CandidateCell);
            if (!bCellPreviouslyOccupied && Grid->IsOccupied(CandidateCell)) {
              bCanOccupyCandidate = false;
              break;
            }
          }

          if (!bCanOccupyCandidate) {
            continue;
          }

          const bool bCandidateMoves = DistanceToStart > 0;
          const int32 CandidateAnchorDistance =
              FMath::Abs(CandidateAnchor.X - Cell.X) +
              FMath::Abs(CandidateAnchor.Y - Cell.Y);

          bool bUseCandidate = false;
          if (!bFoundValidAnchor) {
            bUseCandidate = true;
          } else if (bCandidateMoves != bBestAnchorMoves) {
            bUseCandidate = bCandidateMoves && !bBestAnchorMoves;
          } else if (DistanceToStart < BestDistanceToStart) {
            bUseCandidate = true;
          } else if (DistanceToStart == BestDistanceToStart &&
                     CandidateAnchorDistance < BestDistanceToClicked) {
            bUseCandidate = true;
          }

          if (bUseCandidate) {
            BestDistanceToStart = DistanceToStart;
            BestDistanceToClicked = CandidateAnchorDistance;
            TargetAnchor = CandidateAnchor;
            bBestAnchorMoves = bCandidateMoves;
            bFoundValidAnchor = true;
          }
        }
      }

      if (!bFoundValidAnchor) {
        TargetAnchor = Cell;
      }
    }

    LockedActiveFighter->MoveToCell(TargetAnchor);
    CancelCommandMode();
    UpdateBattleHUDButtons();
    break;
  }
  case EBattleCommandMode::Attack: {
    if (!LockedActiveFighter) {
      CancelCommandMode();
      break;
    }
    if (!IsFriendlyFighter(LockedActiveFighter)) {
      CancelCommandMode();
      break;
    }
    const auto IsValidEnemyTarget = [&](AFighterPawn *Candidate) {
      return Candidate && Candidate != LockedActiveFighter &&
             Candidate->IsAlive() && !IsFriendlyFighter(Candidate);
    };

    AFighterPawn *TargetPawn = CellFighter;
    FIntPoint TargetCell = Cell;

    if (!IsValidEnemyTarget(TargetPawn)) {
      TargetPawn = nullptr;

      FVector TraceStart = Hit.TraceStart;
      FVector TraceEnd = Hit.TraceEnd;

      if (TraceStart == TraceEnd) {
        FVector MouseWorldLocation, MouseWorldDirection;
        if (DeprojectMousePositionToWorld(MouseWorldLocation, MouseWorldDirection)) {
          if (APlayerCameraManager *CameraManager = PlayerCameraManager) {
            TraceStart = CameraManager->GetCameraLocation();
          } else {
            TraceStart = MouseWorldLocation;
          }
          TraceEnd = TraceStart + MouseWorldDirection * 100000.f;
        }
      }

      if (UWorld *World = GetWorld()) {
        TArray<FHitResult> AdditionalHits;
        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HandleGridClickAttack),
                                          /*bTraceComplex*/ false);
        if (LockedActiveFighter) {
          QueryParams.AddIgnoredActor(LockedActiveFighter);
        }

        if (World->LineTraceMultiByChannel(AdditionalHits, TraceStart, TraceEnd,
                                           ECC_Visibility, QueryParams)) {
          for (const FHitResult &CandidateHit : AdditionalHits) {
            AFighterPawn *CandidatePawn =
                Cast<AFighterPawn>(CandidateHit.GetActor());
            if (!IsValidEnemyTarget(CandidatePawn)) {
              continue;
            }

            const FIntPoint CandidateCell = CandidatePawn->GetCurrentCell();
            if (!Grid->IsCellInBounds(CandidateCell)) {
              continue;
            }

            TargetPawn = CandidatePawn;
            TargetCell = CandidateCell;
            break;
          }
        }
      }
    }

    if (IsValidEnemyTarget(TargetPawn)) {
      CellFighter = TargetPawn;
      LockedActiveFighter->PerformAttack(TargetPawn);
    }
    CancelCommandMode();
    UpdateBattleHUDButtons();
    break;
  }
  default:
    break;
  }

  HighlightClickedCell(Grid, Cell);

  if (CurrentCommandMode != EBattleCommandMode::None) {
    return;
  }

  if (CellFighter && CellFighter->IsAlive()) {
    if (LockedActiveFighter && LockedActiveFighter != CellFighter) {
      return;
    }

    SetSelectedFighter(CellFighter);
    return;
  }

  if (!LockedActiveFighter) {
    ClearSelectedFighter();
  }
}

void ASkaldPlayerController::HandleActivatePressed() {
  if (!IsLocalController() || !SelectedFighter)
    return;

  UE_LOG(LogSkaldBattle, Log,
         TEXT("[BattleHUD] Activate pressed for %s. Locked=%s"),
         *SelectedFighter->GetHumanReadableName(),
         LockedActiveFighter ? *LockedActiveFighter->GetHumanReadableName()
                             : TEXT("<None>"));

  if (SelectedFighter->HasActivatedThisRound()) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("[BattleHUD] Activate ignored: %s already acted this round."),
           *SelectedFighter->GetHumanReadableName());
    NotifyActionError(FString(TEXT("Fighter Already Activated.")));
    return;
  }

  if (!IsFriendlyFighter(SelectedFighter)) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("[BattleHUD] Activate ignored: %s is not friendly."),
           *SelectedFighter->GetHumanReadableName());
    NotifyActionError(FString(TEXT("Cannot activate enemy fighter.")));
    return;
  }

  if (LockedActiveFighter && LockedActiveFighter != SelectedFighter) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("[BattleHUD] Activate ignored: Locked fighter %s differs from %s."),
           *LockedActiveFighter->GetHumanReadableName(),
           *SelectedFighter->GetHumanReadableName());
    NotifyActionError(FString(TEXT("Another fighter is already active.")));
    return;
  }

  if (!CachedGameInstance) {
    CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  }
  if (!CachedGameInstance || !CachedGameInstance->GridBattleManager) {
    UE_LOG(LogSkaldBattle, Warning,
           TEXT("[BattleHUD] Activate failed: Missing GridBattleManager."));
    return;
  }

  if (!CachedGameInstance->GridBattleManager->CanActivateFighter(SelectedFighter)) {
    UE_LOG(LogSkaldBattle, Log,
           TEXT("[BattleHUD] Activation rejected for %s (Round=%d, AttackerTurn=%s)"),
           *SelectedFighter->GetHumanReadableName(),
           CachedGameInstance->GridBattleManager->GetCurrentRound(),
           CachedGameInstance->GridBattleManager->IsAttackerTurn()
               ? TEXT("true")
               : TEXT("false"));
    NotifyActionError(FString(TEXT("Cannot activate this fighter right now.")));
    return;
  }

  if (CachedGameInstance->GridBattleManager->ActivateFighter(SelectedFighter)) {
    UE_LOG(LogSkaldBattle, Log,
           TEXT("[BattleHUD] Activation succeeded for %s"),
           *SelectedFighter->GetHumanReadableName());
    LockedActiveFighter = SelectedFighter;
    if (bBattleHUDReadyToShow && !bBattleHUDVisible) {
      EnsureBattleHUDVisible();
    }
    UpdateBattleHUDButtons();
  }
}

void ASkaldPlayerController::HandleEndTurnPressed() {
  UE_LOG(LogSkaldBattle, Log,
         TEXT("[BattleHUD] End Turn pressed. Locked=%s"),
         LockedActiveFighter ? *LockedActiveFighter->GetHumanReadableName()
                             : TEXT("<None>"));
  if (!LockedActiveFighter) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("[BattleHUD] End Turn ignored: No fighter locked."));
    return;
  }

  if (!IsFriendlyFighter(LockedActiveFighter)) {
    UE_LOG(LogSkaldBattle, Verbose,
           TEXT("[BattleHUD] End Turn ignored: Fighter %s is not friendly."),
           *LockedActiveFighter->GetHumanReadableName());
    return;
  }

  if (!CachedGameInstance) {
    CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  }
  if (!CachedGameInstance || !CachedGameInstance->GridBattleManager) {
    UE_LOG(LogSkaldBattle, Warning,
           TEXT("[BattleHUD] End Turn failed: Missing GridBattleManager."));
    return;
  }

  const int32 RoundNumber = CachedGameInstance->GridBattleManager->GetCurrentRound();
  const bool bAttackerTurn = CachedGameInstance->GridBattleManager->IsAttackerTurn();
  UE_LOG(LogSkaldBattle, Log,
         TEXT("[BattleHUD] Finishing activation for %s (Round=%d, AttackerTurn=%s)"),
         *LockedActiveFighter->GetHumanReadableName(), RoundNumber,
         bAttackerTurn ? TEXT("true") : TEXT("false"));
  CachedGameInstance->GridBattleManager->FinishActivation(
      LockedActiveFighter, EGridActivationFinishReason::Manual);
  LockedActiveFighter = nullptr;
  UpdateBattleHUDButtons();
  CancelCommandMode();
}

void ASkaldPlayerController::HandleRightClick() {
  if (!IsLocalController())
    return;

  if (!bIsBattleMap)
    return;

  CancelCommandMode();
  UpdateBattleHUDButtons();
}

void ASkaldPlayerController::HandleRoundStarted(int32 RoundNumber,
                                                ESkaldFaction InitiativeWinner) {
  DetermineControlledBattleSide();

  LockedActiveFighter = nullptr;
  CancelCommandMode();
  UpdateBattleRoundDisplay(RoundNumber, InitiativeWinner);
  UpdateBattlePlayersTurnDisplay();
  UpdateBattleHUDSelection();
  UpdateBattleHUDButtons();
}

void ASkaldPlayerController::CancelCommandMode() {
  CurrentCommandMode = EBattleCommandMode::None;
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    Grid->ClearHighlights();
  }
  if (BattleHudWidget) {
    BattleHudWidget->ClearCommandPreviews();
  }
}

void ASkaldPlayerController::HighlightClickedCell(UGridOverlayComponent *Grid,
                                                  const FIntPoint &Cell) {
  if (!Grid || !Grid->IsCellInBounds(Cell)) {
    return;
  }

  bool bRestoredCommandHighlights = false;
  if (CurrentCommandMode == EBattleCommandMode::Move) {
    if (LockedActiveFighter && IsFriendlyFighter(LockedActiveFighter)) {
      Grid->HighlightMovement(LockedActiveFighter);
      bRestoredCommandHighlights = true;
    }
  } else if (CurrentCommandMode == EBattleCommandMode::Attack) {
    if (LockedActiveFighter && IsFriendlyFighter(LockedActiveFighter)) {
      Grid->HighlightAttack(LockedActiveFighter);
      bRestoredCommandHighlights = true;
    }
  }

  if (!bRestoredCommandHighlights) {
    Grid->ClearHighlights();
  }

  Grid->HighlightCell(Cell, Grid->SelectionHighlightColor.ToFColor(true), 0.f,
                      false);
}

void ASkaldPlayerController::SetSelectedFighter(AFighterPawn *Fighter,
                                                bool bForce) {
  if (!bForce && SelectedFighter == Fighter)
    return;

  SelectedFighter = Fighter;
  UpdateBattleHUDSelection();
  UpdateBattleHUDButtons();
}

void ASkaldPlayerController::ClearSelectedFighter() {
  if (!SelectedFighter)
    return;

  SelectedFighter = nullptr;
  UpdateBattleHUDSelection();
  UpdateBattleHUDButtons();
}

void ASkaldPlayerController::UpdateBattleHUDSelection() {
  if (!BattleHudWidget)
    return;

  BattleHudWidget->BindToFighter(SelectedFighter);
  if (SelectedFighter) {
    BattleHudWidget->SetSelectedFighterName(
        FText::FromName(SelectedFighter->GetFighterId()));
  } else {
    BattleHudWidget->SetSelectedFighterName(FText::GetEmpty());
  }
}

void ASkaldPlayerController::UpdateBattleHUDButtons() {
  if (!BattleHudWidget)
    return;

  bool bCanActivate = false;
  if (SelectedFighter && !LockedActiveFighter && IsFriendlyFighter(SelectedFighter)) {
    if (!CachedGameInstance) {
      CachedGameInstance = GetGameInstance<USkaldGameInstance>();
    }
    if (CachedGameInstance && CachedGameInstance->GridBattleManager) {
      bCanActivate =
          CachedGameInstance->GridBattleManager->CanActivateFighter(SelectedFighter);
    }
  }

  BattleHudWidget->SetActivateEnabled(bCanActivate);
  const bool bHasActiveFighter = LockedActiveFighter != nullptr;
  const bool bHasFriendlyActive =
      LockedActiveFighter && IsFriendlyFighter(LockedActiveFighter);
  BattleHudWidget->SetEndTurnVisibility(bHasActiveFighter);
  BattleHudWidget->SetEndTurnEnabled(bHasFriendlyActive);
}

void ASkaldPlayerController::UpdateBattleRoundDisplay(
    int32 RoundNumber, ESkaldFaction InitiativeWinner) {
  if (!BattleHudWidget)
    return;

  const FText RoundText = FText::Format(
      NSLOCTEXT("Skald", "BattleRound", "Round {0}"), FText::AsNumber(RoundNumber));

  FText InitiativeText = NSLOCTEXT("Skald", "BattleInitiativeNone",
                                   "Initiative: None");
  if (InitiativeWinner != ESkaldFaction::None) {
    if (const UEnum *FactionEnum = StaticEnum<ESkaldFaction>()) {
      const FText WinnerText =
          FactionEnum->GetDisplayNameTextByValue(static_cast<int64>(InitiativeWinner));
      InitiativeText = FText::Format(
          NSLOCTEXT("Skald", "BattleInitiative", "Initiative: {0}"),
          WinnerText);
    }
  }

  BattleHudWidget->SetRoundInfo(RoundText, InitiativeText);
}

void ASkaldPlayerController::UpdateBattlePlayersTurnDisplay() {
  if (!BattleHudWidget)
    return;

  USkaldGameInstance *GI = CachedGameInstance;
  if (!GI) {
    GI = GetGameInstance<USkaldGameInstance>();
    CachedGameInstance = GI;
  }

  if (!GI) {
    BattleHudWidget->SetPlayersTurnLabel(FText::GetEmpty());
    return;
  }

  bool bAttackerTurn = true;
  if (GI->GridBattleManager) {
    bAttackerTurn = GI->GridBattleManager->IsAttackerTurn();
  } else if (LockedActiveFighter) {
    bAttackerTurn = LockedActiveFighter->bIsAttacker;
  }

  const FS_BattlePayload &Battle = GI->PendingBattle;
  FString PlayerName =
      bAttackerTurn ? Battle.AttackerDisplayName : Battle.DefenderDisplayName;
  const int32 PlayerId =
      bAttackerTurn ? Battle.AttackerPlayerID : Battle.DefenderPlayerID;

  if (PlayerName.IsEmpty()) {
    ASkaldGameState *GameState = CachedGameState;
    if (!GameState && GetWorld()) {
      GameState = GetWorld()->GetGameState<ASkaldGameState>();
      CachedGameState = GameState;
    }
    if (GameState) {
      if (ASkaldPlayerState *PS = GameState->GetPlayerById(PlayerId)) {
        PlayerName =
            ResolvePlayerName(PS, TEXT("BattleHUD_PlayerTurnDisplay"));
      }
    }
  }

  if (PlayerName.IsEmpty()) {
    PlayerName = bAttackerTurn ? TEXT("Attackers") : TEXT("Defenders");
  }

  const FText Label = PlayerName.IsEmpty()
                           ? FText::GetEmpty()
                           : FText::Format(
                                 NSLOCTEXT("Skald", "BattlePlayersTurnLabel",
                                           "{0}'s Turn"),
                                 FText::FromString(PlayerName));
  BattleHudWidget->SetPlayersTurnLabel(Label);
}

void ASkaldPlayerController::HandleAttackResolved(AFighterPawn *Attacker,
                                                  AFighterPawn *Defender,
                                                  int32 Roll, bool bHit,
                                                  int32 Damage) {
  if (!BattleHudWidget) {
    return;
  }

  BattleHudWidget->ShowDiceRoll(Roll);
}

void ASkaldPlayerController::HandleAttackRejected(AFighterPawn *Attacker,
                                                  AFighterPawn *Defender,
                                                  const FText &Reason) {
  if (!IsFriendlyFighter(Attacker)) {
    return;
  }

  const FString ReasonString = Reason.ToString();
  if (ReasonString.IsEmpty()) {
    return;
  }

  NotifyActionError(ReasonString);
}

bool ASkaldPlayerController::IsFriendlyFighter(const AFighterPawn *Fighter) const {
  if (!Fighter)
    return false;

  return Fighter->bIsAttacker ? bControlsAttackerSide : bControlsDefenderSide;
}

void ASkaldPlayerController::DetermineControlledBattleSide() {
  bControlsAttackerSide = false;
  bControlsDefenderSide = false;

  const ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>();
  if (!PS)
    return;

  if (!CachedGameInstance) {
    CachedGameInstance = GetGameInstance<USkaldGameInstance>();
  }
  if (!CachedGameInstance)
    return;

  const FS_BattlePayload &Battle = CachedGameInstance->PendingBattle;
  const int32 PlayerID = PS->GetPlayerId();
  if (PlayerID == Battle.AttackerPlayerID) {
    bControlsAttackerSide = true;
  }
  if (PlayerID == Battle.DefenderPlayerID) {
    bControlsDefenderSide = true;
  }
}

void ASkaldPlayerController::HandlePlayerIdUpdated() {
  DetermineControlledBattleSide();
  UpdateBattleHUDButtons();
}

void ASkaldPlayerController::HandleBattleEnded(ESkaldFaction WinningFaction,
                                               int32 AttackerCasualties,
                                               int32 DefenderCasualties) {
  CancelCommandMode();
  SelectedFighter = nullptr;
  LockedActiveFighter = nullptr;
  bControlsAttackerSide = false;
  bControlsDefenderSide = false;

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
