#include "UI/FighterActivationWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Styling/SlateBrush.h"

TSharedRef<SWidget> UFighterActivationWidget::RebuildWidget() {
  if (!WidgetTree) {
    WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
  }

  if (!WidgetTree->RootWidget) {
    ActivationImage = WidgetTree->ConstructWidget<UImage>(
        UImage::StaticClass(), TEXT("ActivationImage"));
    ActivationImage->SetVisibility(ESlateVisibility::Collapsed);
    ActivationImage->SetBrushFromTexture(nullptr);
    WidgetTree->RootWidget = ActivationImage;
  } else {
    ActivationImage = Cast<UImage>(WidgetTree->RootWidget);
    if (!ActivationImage) {
      ActivationImage = WidgetTree->ConstructWidget<UImage>(
          UImage::StaticClass(), TEXT("ActivationImage"));
      ActivationImage->SetVisibility(ESlateVisibility::Collapsed);
      ActivationImage->SetBrushFromTexture(nullptr);
      WidgetTree->RootWidget = ActivationImage;
    }
  }

  return Super::RebuildWidget();
}

void UFighterActivationWidget::NativeConstruct() {
  Super::NativeConstruct();
  RefreshBrush();
}

void UFighterActivationWidget::SetIconTextures(UTexture2D *ActiveTexture,
                                               UTexture2D *SpentTexture) {
  ActiveStateTexture = ActiveTexture;
  SpentStateTexture = SpentTexture;
  RefreshBrush();
}

void UFighterActivationWidget::SetActivationState(
    EFighterActivationIndicatorState NewState) {
  if (CurrentState == NewState) {
    return;
  }

  CurrentState = NewState;
  RefreshBrush();
}

void UFighterActivationWidget::RefreshBrush() {
  if (!ActivationImage) {
    return;
  }

  UTexture2D *DesiredTexture = nullptr;
  FLinearColor DesiredTint = FLinearColor::White;
  ESlateVisibility DesiredVisibility = ESlateVisibility::Collapsed;

  switch (CurrentState) {
  case EFighterActivationIndicatorState::Active:
    DesiredTexture = ActiveStateTexture.Get();
    DesiredTint = ActiveTint;
    DesiredVisibility = ESlateVisibility::HitTestInvisible;
    break;
  case EFighterActivationIndicatorState::Spent:
    DesiredTexture = SpentStateTexture.Get();
    DesiredTint = SpentTint;
    DesiredVisibility = ESlateVisibility::HitTestInvisible;
    break;
  default:
    DesiredTexture = nullptr;
    DesiredTint = FLinearColor::Transparent;
    DesiredVisibility = ESlateVisibility::Collapsed;
    break;
  }

  if (DesiredTexture) {
    ActivationImage->SetBrushFromTexture(DesiredTexture, true);
  } else {
    FSlateBrush EmptyBrush;
    ActivationImage->SetBrush(EmptyBrush);
  }

  ActivationImage->SetColorAndOpacity(DesiredTint);
  ActivationImage->SetVisibility(DesiredVisibility);
}
