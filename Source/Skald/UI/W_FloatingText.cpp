#include "UI/W_FloatingText.h"

#include "Components/InvalidationBox.h"
#include "Components/RetainerBox.h"
#include "Components/TextBlock.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Math/UnrealMathUtility.h"

UW_FloatingText::UW_FloatingText(const FObjectInitializer &ObjectInitializer)
    : Super(ObjectInitializer) {
  SetIsFocusable(false);
}

void UW_FloatingText::InitializeFloater(APlayerController *InOwningPlayer) {
  CachedPlayer = InOwningPlayer;
  bActive = true;
  SetVisibility(ESlateVisibility::HitTestInvisible);
  SetRenderOpacity(1.f);
  SetRenderScale(FVector2D(1.f, 1.f));
  SetAlignmentInViewport(FVector2D(0.5f, 0.5f));
  ApplyInvalidation();
}

void UW_FloatingText::ResetForPool() {
  CachedPlayer.Reset();
  bActive = false;
  CachedWorldLocation = FVector::ZeroVector;
  CachedScreenOffset = FVector2D::ZeroVector;

  SetRenderOpacity(1.f);
  SetRenderScale(FVector2D(1.f, 1.f));
  SetVisibility(ESlateVisibility::Collapsed);
  if (FloatingText) {
    FloatingText->SetText(FText::GetEmpty());
  }
  ApplyInvalidation();
}

void UW_FloatingText::SetText(const FText &InText) {
  if (FloatingText) {
    FloatingText->SetText(InText);
  }
  ApplyInvalidation();
}

void UW_FloatingText::SetColorAndOpacity(const FLinearColor &InColor) {
  if (FloatingText) {
    FloatingText->SetColorAndOpacity(FSlateColor(InColor));
  }
  ApplyInvalidation();
}

void UW_FloatingText::SetFloaterOpacity(float InOpacity) {
  const float ClampedOpacity = FMath::Clamp(InOpacity, 0.f, 1.f);
  SetRenderOpacity(ClampedOpacity);
  if (FloatingText) {
    FloatingText->SetOpacity(ClampedOpacity);
  }
  ApplyInvalidation();
}

void UW_FloatingText::SetFloaterScale(float InScale) {
  const float SafeScale = FMath::Max(0.01f, InScale);
  SetRenderScale(FVector2D(SafeScale, SafeScale));
  ApplyInvalidation();
}

bool UW_FloatingText::UpdateProjection(const FVector &WorldLocation,
                                       const FVector2D &ScreenOffset,
                                       float ClampMargin) {
  CachedWorldLocation = WorldLocation;
  CachedScreenOffset = ScreenOffset;

  APlayerController *PlayerController = CachedPlayer.Get();
  if (!PlayerController) {
    SetVisibility(ESlateVisibility::Collapsed);
    return false;
  }

  FVector2D ScreenPosition;
  const bool bProjected = UGameplayStatics::ProjectWorldToScreen(
      PlayerController, WorldLocation, ScreenPosition, true);
  if (!bProjected) {
    SetVisibility(ESlateVisibility::Collapsed);
    ApplyInvalidation();
    return false;
  }

  int32 ViewportX = 0;
  int32 ViewportY = 0;
  PlayerController->GetViewportSize(ViewportX, ViewportY);

  FVector2D TargetPosition = ScreenPosition + ScreenOffset;
  if (ViewportX > 0 && ViewportY > 0) {
    const float Margin = FMath::Max(ClampMargin, 0.f);
    TargetPosition.X = FMath::Clamp(TargetPosition.X, Margin,
                                    static_cast<float>(ViewportX) - Margin);
    TargetPosition.Y = FMath::Clamp(TargetPosition.Y, Margin,
                                    static_cast<float>(ViewportY) - Margin);
  }

  SetPositionInViewport(TargetPosition, true);
  SetVisibility(ESlateVisibility::HitTestInvisible);
  ApplyInvalidation();
  return true;
}

void UW_FloatingText::ApplyInvalidation() {
  if (InvalidationPanel) {
    InvalidationPanel->InvalidateLayoutAndVolatility();
  }
  if (Retainer) {
    Retainer->RequestRender();
  }
}

