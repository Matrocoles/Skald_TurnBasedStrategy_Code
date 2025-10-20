#include "UI/BattleHUDWidget.h"
#include "SkaldLogging.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/Engine.h"
#include "Styling/SlateBrush.h"
#include "Engine/World.h"
#include "FighterPawn.h"
#include "GridOverlayComponent.h"
#include "UI/CombatFloaterPoolSubsystem.h"
#include "UI/W_DiceResolutionPanel.h"
#include "UI/W_FloatingText.h"
#include "Math/UnrealMathUtility.h"
#include "Math/Vector2D.h"
#include "TimerManager.h"
#include "CanvasItem.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/WeakObjectPtrTemplates.h"

UBattleHUDWidget::UBattleHUDWidget(const FObjectInitializer &ObjectInitializer)
    : Super(ObjectInitializer) {
  FloaterWidgetClass = UW_FloatingText::StaticClass();
}

void UBattleHUDWidget::NativeConstruct() {
  Super::NativeConstruct();

  if (MoveButton) {
    MoveButton->OnClicked.AddDynamic(this,
                                     &UBattleHUDWidget::HandleMovePressed);
  }
  if (AttackButton) {
    AttackButton->OnClicked.AddDynamic(this,
                                       &UBattleHUDWidget::HandleAttackPressed);
  }
  SetActionButtonsVisibility(false);
  if (ActivateButton) {
    ActivateButton->OnClicked.AddDynamic(
        this, &UBattleHUDWidget::HandleActivatePressed);
    ActivateButton->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (EndTurnButton) {
    EndTurnButton->OnClicked.AddDynamic(
        this, &UBattleHUDWidget::HandleEndTurnPressed);
  }

  if (RollInitiativeButton) {
    RollInitiativeButton->OnClicked.AddDynamic(
        this, &UBattleHUDWidget::HandleInitiativeRollPressed);
    RollInitiativeButton->SetVisibility(ESlateVisibility::Collapsed);
    RollInitiativeButton->SetIsEnabled(true);
  }

  if (DiceResolutionPanel) {
    DiceResolutionPanel->OnResolutionComplete.AddDynamic(
        this, &UBattleHUDWidget::HandleDicePanelResolved);
    TArray<UTexture2D *> DiceFaceTexturePtrs;
    DiceFaceTexturePtrs.Reserve(DiceFaceTextures.Num());
    for (const TObjectPtr<UTexture2D> &Texture : DiceFaceTextures) {
      DiceFaceTexturePtrs.Add(Texture.Get());
    }
    DiceResolutionPanel->SetDiceFaceTextures(DiceFaceTexturePtrs);
  }

  ApplyDiceResolutionPanelLayoutInternal(DefaultDiceResolutionPanelLayout);

  if (InitiativePromptText) {
    InitiativePromptText->SetText(FText::GetEmpty());
    InitiativePromptText->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (DiceRollerImage) {
    DiceRollerImage->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (UCombatFloaterPoolSubsystem *FloaterPool = ResolveFloaterPool()) {
    if (FloaterWidgetClass) {
      FloaterPool->FloaterWidgetClass = FloaterWidgetClass;
    }
  }
}

void UBattleHUDWidget::NativeTick(const FGeometry &MyGeometry,
                                  float InDeltaTime) {
  Super::NativeTick(MyGeometry, InDeltaTime);
  UpdateCombatFloaters(InDeltaTime);
}

void UBattleHUDWidget::NativeDestruct() {
  if (DiceResolutionPanel) {
    DiceResolutionPanel->OnResolutionComplete.RemoveDynamic(
        this, &UBattleHUDWidget::HandleDicePanelResolved);
  }

  PendingDiceResolutions.Reset();
  bDiceResolutionActive = false;
  ActiveDiceResolution = FBattleQueuedDiceResolution();

  if (DiceRollerRenderTarget) {
    DiceRollerRenderTarget->OnCanvasRenderTargetUpdate.RemoveDynamic(
        this, &UBattleHUDWidget::HandleDiceRenderTargetUpdate);
    DiceRollerRenderTarget = nullptr;
  }

  while (ActiveFloaters.Num() > 0) {
    ReleaseFloaterAtIndex(ActiveFloaters.Num() - 1);
  }
  CachedFloaterPool.Reset();

  Super::NativeDestruct();
}

void UBattleHUDWidget::ShowInitiativePrompt(const FText &PromptText) {
  if (InitiativePromptText) {
    InitiativePromptText->SetText(PromptText);
    InitiativePromptText->SetVisibility(ESlateVisibility::HitTestInvisible);
  }
  if (RollInitiativeButton) {
    RollInitiativeButton->SetVisibility(ESlateVisibility::Collapsed);
    RollInitiativeButton->SetIsEnabled(false);
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(InitiativeRollButtonDelayTimer);
    World->GetTimerManager().SetTimer(
        InitiativeRollButtonDelayTimer, this,
        &UBattleHUDWidget::RevealInitiativeRollButton, 1.0f, false);
  }
}

void UBattleHUDWidget::HideInitiativePrompt() {
  if (InitiativePromptText) {
    InitiativePromptText->SetText(FText::GetEmpty());
    InitiativePromptText->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (RollInitiativeButton) {
    RollInitiativeButton->SetVisibility(ESlateVisibility::Collapsed);
    RollInitiativeButton->SetIsEnabled(true);
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(InitiativeRollButtonDelayTimer);
  }
}

void UBattleHUDWidget::RefreshStats() { UpdateStatPanel(); }

void UBattleHUDWidget::BindToFighter(AFighterPawn *Fighter) {
  if (BoundFighter) {
    BoundFighter->OnHealthChanged.RemoveDynamic(
        this, &UBattleHUDWidget::HandleHealthChanged);
    BoundFighter->OnActionsChanged.RemoveDynamic(
        this, &UBattleHUDWidget::HandleActionsChanged);
  }
  BoundFighter = Fighter;
  if (BoundFighter) {
    BoundFighter->OnHealthChanged.AddDynamic(
        this, &UBattleHUDWidget::HandleHealthChanged);
    BoundFighter->OnActionsChanged.AddDynamic(
        this, &UBattleHUDWidget::HandleActionsChanged);
    UpdateStatPanel();
    if (FighterNameText) {
      FighterNameText->SetText(FText::FromName(BoundFighter->GetFighterId()));
    }
    if (FighterImage) {
      if (UTexture2D *PortraitTexture = BoundFighter->GetPortraitTexture()) {
        FighterImage->SetBrushFromTexture(PortraitTexture);
        FighterImage->SetVisibility(ESlateVisibility::HitTestInvisible);
      } else {
        FighterImage->SetBrushFromTexture(nullptr);
        FighterImage->SetVisibility(ESlateVisibility::Collapsed);
      }
    }
  } else {
    if (HealthText) {
      HealthText->SetText(FText::GetEmpty());
    }
    if (AttackText) {
      AttackText->SetText(FText::GetEmpty());
    }
    if (CriticalDamageText) {
      CriticalDamageText->SetText(FText::GetEmpty());
    }
    if (MoveText) {
      MoveText->SetText(FText::GetEmpty());
    }
    if (ActionsText) {
      ActionsText->SetText(FText::GetEmpty());
    }
    if (StrengthText) {
      StrengthText->SetText(FText::GetEmpty());
    }
    if (DefenceText) {
      DefenceText->SetText(FText::GetEmpty());
    }
    if (AttackRangeText) {
      AttackRangeText->SetText(FText::GetEmpty());
    }
    if (AttackDiceText) {
      AttackDiceText->SetText(FText::GetEmpty());
    }
    if (FighterNameText) {
      FighterNameText->SetText(FText::GetEmpty());
    }
    if (FighterImage) {
      FighterImage->SetBrushFromTexture(nullptr);
      FighterImage->SetVisibility(ESlateVisibility::Collapsed);
    }
  }

  UpdateActionButtonVisibility();
}

void UBattleHUDWidget::HandleMovePressed() {
  OnMovePressed.Broadcast();
  bAttackSelected = false;
  if (!BoundFighter) {
    return;
  }
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    if (bMoveSelected) {
      Grid->ClearHighlights();
      bMoveSelected = false;
    } else {
      Grid->HighlightMovement(BoundFighter);
      bMoveSelected = true;
    }
  }
}

void UBattleHUDWidget::HandleAttackPressed() {
  OnAttackPressed.Broadcast();
  bMoveSelected = false;
  if (!BoundFighter) {
    return;
  }
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    if (bAttackSelected) {
      Grid->ClearHighlights();
      bAttackSelected = false;
    } else {
      Grid->HighlightAttack(BoundFighter);
      bAttackSelected = true;
    }
  }
}

void UBattleHUDWidget::HandleActivatePressed() {
  OnActivatePressed.Broadcast();
  ClearCommandPreviews();
}

void UBattleHUDWidget::HandleEndTurnPressed() {
  OnEndTurnPressed.Broadcast();
  ClearCommandPreviews();
}

void UBattleHUDWidget::HandleInitiativeRollPressed() {
  if (RollInitiativeButton) {
    RollInitiativeButton->SetIsEnabled(false);
  }
  HideInitiativePrompt();
  OnInitiativeRollRequested.Broadcast();
}

void UBattleHUDWidget::RevealInitiativeRollButton() {
  if (RollInitiativeButton) {
    RollInitiativeButton->SetVisibility(ESlateVisibility::Visible);
    RollInitiativeButton->SetIsEnabled(true);
  }
}

void UBattleHUDWidget::HandleHealthChanged(int32 NewHealth) {
  if (HealthText) {
    HealthText->SetText(FText::AsNumber(NewHealth));
  }
}

void UBattleHUDWidget::HandleActionsChanged(int32 NewActions) {
  if (ActionsText) {
    ActionsText->SetText(FText::AsNumber(NewActions));
  }

  UpdateActionButtonVisibility();
}

void UBattleHUDWidget::UpdateStatPanel() {
  if (!BoundFighter) {
    return;
  }
  if (HealthText) {
    HealthText->SetText(FText::AsNumber(BoundFighter->Stats.Health));
  }
  if (AttackText) {
    AttackText->SetText(FText::AsNumber(BoundFighter->Stats.AttackDamage));
  }
  if (CriticalDamageText) {
    const int32 CriticalDamage =
        BoundFighter->Stats.AttackDamage +
        BoundFighter->Stats.CriticalBonusDamage;
    CriticalDamageText->SetText(FText::AsNumber(CriticalDamage));
  }
  if (MoveText) {
    MoveText->SetText(FText::AsNumber(BoundFighter->Stats.Movement));
  }
  if (ActionsText) {
    ActionsText->SetText(FText::AsNumber(BoundFighter->ActionsRemaining));
  }
  if (StrengthText) {
    StrengthText->SetText(FText::AsNumber(BoundFighter->Stats.Strength));
  }
  if (DefenceText) {
    DefenceText->SetText(FText::AsNumber(BoundFighter->Stats.Defence));
  }
  if (AttackRangeText) {
    AttackRangeText->SetText(FText::AsNumber(BoundFighter->Stats.AttackRange));
  }
  if (AttackDiceText) {
    AttackDiceText->SetText(FText::AsNumber(BoundFighter->Stats.AttackDice));
  }

  UpdateActionButtonVisibility();
}

void UBattleHUDWidget::SetRoundInfo(const FText &RoundLabel,
                                    const FText &InitiativeLabel) {
  if (RoundText) {
    RoundText->SetText(RoundLabel);
  }
  if (InitiativeText) {
    InitiativeText->SetText(InitiativeLabel);
    const bool bHasInitiativeText = !InitiativeLabel.IsEmptyOrWhitespace();
    InitiativeText->SetVisibility(bHasInitiativeText
                                      ? ESlateVisibility::HitTestInvisible
                                      : ESlateVisibility::Collapsed);

    if (UWorld *World = GetWorld()) {
      FTimerManager &TimerManager = World->GetTimerManager();
      TimerManager.ClearTimer(InitiativeHideTimer);

      if (bHasInitiativeText) {
        const TWeakObjectPtr<UBattleHUDWidget> WeakThis(this);
        FTimerDelegate TimerDelegate;
        TimerDelegate.BindLambda([WeakThis]() {
          if (WeakThis.IsValid()) {
            WeakThis->HideInitiativeText();
          }
        });

        TimerManager.SetTimer(InitiativeHideTimer, TimerDelegate, 3.f, false);
      }
    }
  }
}

void UBattleHUDWidget::SetPlayersTurnLabel(const FText &PlayerLabel) {
  if (PlayersTurnText) {
    PlayersTurnText->SetText(PlayerLabel);
  }
}

void UBattleHUDWidget::SetSelectedFighterName(const FText &Name) {
  if (FighterNameText) {
    FighterNameText->SetText(Name);
  }
  if (FighterImage) {
    UTexture2D *PortraitTexture =
        BoundFighter ? BoundFighter->GetPortraitTexture() : nullptr;
    if (PortraitTexture) {
      FighterImage->SetBrushFromTexture(PortraitTexture);
      FighterImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    } else {
      FighterImage->SetBrushFromTexture(nullptr);
      FighterImage->SetVisibility(ESlateVisibility::Collapsed);
    }
  }
}

void UBattleHUDWidget::SetActivateEnabled(bool bEnabled) {
  if (ActivateButton) {
    ActivateButton->SetIsEnabled(bEnabled);
  }
}

void UBattleHUDWidget::SetActivateVisibility(bool bVisible) {
  if (ActivateButton) {
    ActivateButton->SetVisibility(bVisible ? ESlateVisibility::Visible
                                           : ESlateVisibility::Collapsed);
  }
}

void UBattleHUDWidget::SetEndTurnEnabled(bool bEnabled) {
  if (EndTurnButton) {
    EndTurnButton->SetIsEnabled(bEnabled);
  }
}

void UBattleHUDWidget::SetEndTurnVisibility(bool bVisible) {
  if (EndTurnButton) {
    EndTurnButton->SetVisibility(
        bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
  }
}

void UBattleHUDWidget::SetActionButtonsVisibility(bool bVisible) {
  bActionButtonsUnlocked = bVisible;
  UpdateActionButtonVisibility();
}

void UBattleHUDWidget::ShowDiceRoll(int32 RollValue, float DisplayDuration) {
  if (!DiceRollerImage) {
    return;
  }

  const float DiceDisplaySize = 90.f;
  const float DiceBoardPadding = 24.f;
  const FVector2D DiceOffset(0.f, 80.f);

  UObject *DiceResource = nullptr;
  const int32 Index = RollValue - 1;
  if (DiceFaceTextures.IsValidIndex(Index)) {
    DiceResource = DiceFaceTextures[Index];
  }

  if (!DiceResource) {
    if (!DiceRollerRenderTarget) {
      DiceRollerRenderTarget = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(
          this, UCanvasRenderTarget2D::StaticClass(), 256, 256);
      if (DiceRollerRenderTarget) {
        DiceRollerRenderTarget->OnCanvasRenderTargetUpdate.AddDynamic(
            this, &UBattleHUDWidget::HandleDiceRenderTargetUpdate);
      }
    }

    if (DiceRollerRenderTarget) {
      PendingDiceRenderValue = RollValue;
      DiceRollerRenderTarget->ClearColor = FLinearColor::Transparent;
      DiceRollerRenderTarget->UpdateResourceImmediate();
      DiceResource = DiceRollerRenderTarget;
    }
  }

  bool bDisplayedRoll = false;
  if (DiceResource) {
    if (UTexture2D *Texture = Cast<UTexture2D>(DiceResource)) {
      DiceRollerImage->SetBrushFromTexture(Texture, true);
    } else {
      FSlateBrush Brush = DiceRollerImage->GetBrush();
      Brush.SetResourceObject(DiceResource);
      Brush.ImageSize = FVector2D(DiceDisplaySize, DiceDisplaySize);
      DiceRollerImage->SetBrush(Brush);
    }
    DiceRollerImage->SetDesiredSizeOverride(
        FVector2D(DiceDisplaySize, DiceDisplaySize));
    DiceRollerImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));

    if (UCanvasPanelSlot *DiceSlot = Cast<UCanvasPanelSlot>(DiceRollerImage->Slot)) {
      DiceSlot->SetAnchors(FAnchors(0.5f, 0.f));
      DiceSlot->SetAlignment(FVector2D(0.5f, 0.f));
      DiceSlot->SetPosition(DiceOffset);
      DiceSlot->SetSize(FVector2D(DiceDisplaySize, DiceDisplaySize));
    }

    DiceRollerImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    if (DiceBoardImage) {
      DiceBoardImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));

      if (UCanvasPanelSlot *BoardSlot = Cast<UCanvasPanelSlot>(DiceBoardImage->Slot)) {
        BoardSlot->SetAnchors(FAnchors(0.5f, 0.f));
        BoardSlot->SetAlignment(FVector2D(0.5f, 0.f));
        BoardSlot->SetPosition(DiceOffset - FVector2D(0.f, DiceBoardPadding * 0.5f));
        BoardSlot->SetSize(FVector2D(DiceDisplaySize + DiceBoardPadding,
                                     DiceDisplaySize + DiceBoardPadding));
      }

      DiceBoardImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    }

    bDisplayedRoll = true;
  } else {
    DiceRollerImage->SetVisibility(ESlateVisibility::Collapsed);
    if (DiceBoardImage) {
      DiceBoardImage->SetVisibility(ESlateVisibility::Collapsed);
    }
  }

  if (bDisplayedRoll && DiceRollSound) {
    UGameplayStatics::PlaySound2D(this, DiceRollSound);
  }

  if (UWorld *World = GetWorld()) {
    FTimerManager &TimerManager = World->GetTimerManager();
    TimerManager.ClearTimer(DiceRollerHideTimer);

    const TWeakObjectPtr<UBattleHUDWidget> WeakThis(this);
    FTimerDelegate TimerDelegate;
    TimerDelegate.BindLambda([WeakThis]() {
      if (WeakThis.IsValid()) {
        WeakThis->HideDiceRoller();
      }
    });

    const float ClampedDuration = FMath::Clamp(DisplayDuration, 0.35f, 1.25f);
    TimerManager.SetTimer(DiceRollerHideTimer, TimerDelegate, ClampedDuration,
                          false);
  }
}

void UBattleHUDWidget::ClearCommandPreviews() {
  bMoveSelected = false;
  bAttackSelected = false;
  if (UGridOverlayComponent *Grid = FindGridOverlay()) {
    Grid->ClearHighlights();
  }
}

void UBattleHUDWidget::UpdateActionButtonVisibility() {
  const bool bShouldShow = bActionButtonsUnlocked && BoundFighter &&
                           BoundFighter->ActionsRemaining > 0;
  const ESlateVisibility DesiredVisibility =
      bShouldShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;

  if (MoveButton) {
    MoveButton->SetVisibility(DesiredVisibility);
  }
  if (AttackButton) {
    AttackButton->SetVisibility(DesiredVisibility);
  }
}

UGridOverlayComponent *UBattleHUDWidget::FindGridOverlay() const {
  if (UWorld *World = GetWorld()) {
    return Skald::GridOverlay::FindActiveGridOverlay(World);
  }

  return nullptr;
}

void UBattleHUDWidget::HideDiceRoller() {
  if (!DiceRollerImage) {
    return;
  }
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(DiceRollerHideTimer);
  }
  DiceRollerImage->SetVisibility(ESlateVisibility::Collapsed);
  DiceRollerImage->SetBrushFromTexture(nullptr);
  PendingDiceRenderValue = 0;
  if (DiceBoardImage) {
    DiceBoardImage->SetVisibility(ESlateVisibility::Collapsed);
  }
}

void UBattleHUDWidget::HandleDiceRenderTargetUpdate(UCanvas *Canvas, int32 Width,
                                                    int32 Height) {
  if (!Canvas || Width <= 0 || Height <= 0) {
    return;
  }

  const FVector2D Size(Width, Height);
  const FLinearColor BackgroundColor(0.f, 0.f, 0.f, 0.75f);
  FCanvasTileItem Tile(FVector2D::ZeroVector, Size, BackgroundColor);
  Tile.BlendMode = SE_BLEND_Translucent;
  Canvas->DrawItem(Tile);

  const FLinearColor BorderColor(1.f, 1.f, 1.f, 0.85f);
  const float BorderThickness = 6.f;
  FCanvasBoxItem Border(FVector2D::ZeroVector, Size);
  Border.SetColor(BorderColor);
  Border.LineThickness = BorderThickness;
  Canvas->DrawItem(Border);

  if (!GEngine) {
    return;
  }

  UFont *Font = GEngine->GetLargeFont();
  if (!Font) {
    Font = GEngine->GetMediumFont();
  }
  if (!Font) {
    Font = GEngine->GetSmallFont();
  }

  if (!Font || PendingDiceRenderValue <= 0) {
    return;
  }

  const FString RollString = FString::FromInt(PendingDiceRenderValue);
  FCanvasTextItem TextItem(FVector2D::ZeroVector, FText::FromString(RollString),
                           Font, FLinearColor::White);
  TextItem.bCentreX = true;
  TextItem.bCentreY = true;
  TextItem.EnableShadow(FLinearColor::Black);
  TextItem.Scale = FVector2D(2.6f, 2.6f);

  const FVector2D Center(Width * 0.5f, Height * 0.5f);
  Canvas->DrawItem(TextItem, Center);
}

void UBattleHUDWidget::HideInitiativeText() {
  if (!InitiativeText) {
    return;
  }
  InitiativeText->SetVisibility(ESlateVisibility::Collapsed);
}

void UBattleHUDWidget::ShowCombatFloater(const FVector &WorldLocation,
                                         const FText &Message,
                                         const FLinearColor &Tint, float Scale,
                                         bool bUseMissStyling,
                                         float LifetimeOverride) {
  UCombatFloaterPoolSubsystem *Pool = ResolveFloaterPool();
  if (!Pool) {
    return;
  }

  if (FloaterWidgetClass) {
    Pool->FloaterWidgetClass = FloaterWidgetClass;
  }

  APlayerController *OwningController = GetOwningPlayer();
  if (!OwningController) {
    return;
  }

  if (UW_FloatingText *Floater = Pool->SpawnFloater(OwningController)) {
    Floater->SetText(Message);
    Floater->SetColorAndOpacity(Tint);
    Floater->SetFloaterOpacity(1.f);
    Floater->SetTagStyle(bUseMissStyling);
    Floater->SetFloaterScale(Scale);

    FBattleActiveFloater &Entry = ActiveFloaters.AddDefaulted_GetRef();
    Entry.Floater = Floater;
    Entry.AnchorLocation = WorldLocation;
    Entry.InitialOffset = FVector2D(FMath::RandRange(-18.f, 18.f), 0.f);
    Entry.HorizontalDirection = FMath::RandBool() ? 1.f : -1.f;
    Entry.Lifetime = LifetimeOverride > 0.f ? LifetimeOverride : FloaterLifetime;
    Entry.Lifetime = FMath::Max(Entry.Lifetime, 0.1f);
    Entry.FadeDuration = FloaterFadeDuration;
    Entry.Elapsed = 0.f;
    Entry.Scale = Scale;

    Floater->UpdateProjection(WorldLocation, Entry.InitialOffset,
                              FloaterClampMargin);
  }
}

void UBattleHUDWidget::ShowAttackResultFloater(AFighterPawn *Target,
                                               const FDiceRollResult &Result) {
  const bool bAnyHits = Result.HitCount > 0 || Result.TotalDamage > 0;
  const int32 Damage = Result.TotalDamage;

  // TODO: integrate with CombatFloaterPoolSubsystem if available.
  UE_LOG(LogSkald, Verbose,
         TEXT("ShowAttackResultFloater: Target=%s Hit=%d Damage=%d Criticals=%d Misses=%d"),
         Target ? *Target->GetName() : TEXT("None"), bAnyHits ? 1 : 0, Damage,
         Result.CriticalHitCount, Result.MissCount);
}

void UBattleHUDWidget::QueueDiceResolution(AFighterPawn *Attacker,
                                           AFighterPawn *Defender,
                                           const FDiceRollResult &Result) {
  FBattleQueuedDiceResolution Entry;
  Entry.Attacker = Attacker;
  Entry.Defender = Defender;
  Entry.Result = Result;
  PendingDiceResolutions.Add(MoveTemp(Entry));

  if (!bDiceResolutionActive) {
    ProcessNextDiceResolution();
  }
}

void UBattleHUDWidget::ProcessNextDiceResolution() {
  if (bDiceResolutionActive) {
    return;
  }

  if (PendingDiceResolutions.Num() == 0) {
    return;
  }

  bDiceResolutionActive = true;
  ActiveDiceResolution = PendingDiceResolutions[0];
  PendingDiceResolutions.RemoveAt(0);

  if (!DiceResolutionPanel) {
    OnResolutionComplete.Broadcast(ActiveDiceResolution.Attacker.Get(),
                                   ActiveDiceResolution.Defender.Get(),
                                   ActiveDiceResolution.Result);
    bDiceResolutionActive = false;
    ProcessNextDiceResolution();
    return;
  }

  const FDiceResolutionPanelLayout Layout = ResolveDiceResolutionPanelLayout(
      ActiveDiceResolution.Attacker.Get(), ActiveDiceResolution.Defender.Get(),
      ActiveDiceResolution.Result);
  ApplyDiceResolutionPanelLayoutInternal(Layout);

  DiceResolutionPanel->BeginResolution(ActiveDiceResolution.Result);
}

void UBattleHUDWidget::HandleDicePanelResolved(
    const FDiceRollResult &Result) {
  HideDiceRoller();

  if (!bDiceResolutionActive) {
    OnResolutionComplete.Broadcast(nullptr, nullptr, Result);
    return;
  }

  OnResolutionComplete.Broadcast(ActiveDiceResolution.Attacker.Get(),
                                 ActiveDiceResolution.Defender.Get(),
                                 ActiveDiceResolution.Result);

  bDiceResolutionActive = false;
  ActiveDiceResolution = FBattleQueuedDiceResolution();

  ProcessNextDiceResolution();
}

void UBattleHUDWidget::UpdateCombatFloaters(float DeltaSeconds) {
  if (ActiveFloaters.Num() == 0) {
    return;
  }

  for (int32 Index = ActiveFloaters.Num() - 1; Index >= 0; --Index) {
    FBattleActiveFloater &Entry = ActiveFloaters[Index];
    UW_FloatingText *Floater = Entry.Floater.Get();
    if (!Floater) {
      ActiveFloaters.RemoveAtSwap(Index);
      continue;
    }

    Entry.Elapsed += DeltaSeconds;
    const float Lifetime = FMath::Max(Entry.Lifetime, 0.1f);
    const float Normalised = FMath::Clamp(Entry.Elapsed / Lifetime, 0.f, 1.f);

    const float VerticalArc = FMath::Sin(Normalised * PI) * FloaterArcHeight;
    const float Horizontal = Entry.HorizontalDirection * FloaterHorizontalDrift *
                             Normalised;
    const FVector2D Offset = Entry.InitialOffset +
                             FVector2D(Horizontal, -VerticalArc);

    Floater->UpdateProjection(Entry.AnchorLocation, Offset, FloaterClampMargin);

    const float FadeStart = FMath::Max(Lifetime - Entry.FadeDuration, 0.f);
    float Opacity = 1.f;
    if (Entry.Elapsed >= FadeStart && Entry.FadeDuration > KINDA_SMALL_NUMBER) {
      Opacity = FMath::Clamp((Lifetime - Entry.Elapsed) / Entry.FadeDuration,
                             0.f, 1.f);
    }
    Floater->SetFloaterOpacity(Opacity);
    Floater->SetFloaterScale(Entry.Scale);

    if (Entry.Elapsed >= Lifetime) {
      ReleaseFloaterAtIndex(Index);
    }
  }
}

void UBattleHUDWidget::ReleaseFloaterAtIndex(int32 Index) {
  if (!ActiveFloaters.IsValidIndex(Index)) {
    return;
  }

  if (UW_FloatingText *Floater = ActiveFloaters[Index].Floater.Get()) {
    if (UCombatFloaterPoolSubsystem *Pool = ResolveFloaterPool()) {
      Pool->ReleaseFloater(Floater);
    } else {
      Floater->RemoveFromParent();
    }
  }

  ActiveFloaters.RemoveAtSwap(Index);
}

UCombatFloaterPoolSubsystem *UBattleHUDWidget::ResolveFloaterPool() {
  if (CachedFloaterPool.IsValid()) {
    return CachedFloaterPool.Get();
  }

  if (UWorld *World = GetWorld()) {
    if (UCombatFloaterPoolSubsystem *Pool =
            World->GetSubsystem<UCombatFloaterPoolSubsystem>()) {
      CachedFloaterPool = Pool;
      return Pool;
    }
  }

  return nullptr;
}

void UBattleHUDWidget::SetDefaultDiceResolutionPanelLayout(
    const FDiceResolutionPanelLayout &Layout) {
  DefaultDiceResolutionPanelLayout = Layout;
  ApplyDiceResolutionPanelLayoutInternal(DefaultDiceResolutionPanelLayout);
}

void UBattleHUDWidget::ApplyDiceResolutionPanelLayout(
    const FDiceResolutionPanelLayout &Layout) {
  ApplyDiceResolutionPanelLayoutInternal(Layout);
}

FDiceResolutionPanelLayout
UBattleHUDWidget::ResolveDiceResolutionPanelLayout_Implementation(
    AFighterPawn *Attacker, AFighterPawn *Defender,
    const FDiceRollResult &Result) const {
  return DefaultDiceResolutionPanelLayout;
}

void UBattleHUDWidget::ApplyDiceResolutionPanelLayoutInternal(
    const FDiceResolutionPanelLayout &Layout) {
  if (!DiceResolutionPanel || !Layout.bApplyLayout) {
    return;
  }

  if (UPanelSlot *PanelSlot = DiceResolutionPanel->Slot) {
    if (UCanvasPanelSlot *CanvasSlot = Cast<UCanvasPanelSlot>(PanelSlot)) {
      if (Layout.bOverrideAnchors) {
        CanvasSlot->SetAnchors(Layout.Anchors);
      }

      if (Layout.bOverrideAlignment) {
        CanvasSlot->SetAlignment(Layout.Alignment);
      }

      if (Layout.bOverridePosition) {
        CanvasSlot->SetPosition(Layout.Position);
      }

      if (Layout.bOverrideSize) {
        CanvasSlot->SetSize(Layout.Size);
      }
    }
  }
}
