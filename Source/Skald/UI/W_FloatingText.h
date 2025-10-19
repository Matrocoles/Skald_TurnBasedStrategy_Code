#pragma once

#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "W_FloatingText.generated.h"

class UInvalidationBox;
class URetainerBox;
class UTextBlock;

/**
 * Lightweight widget responsible for projecting combat text into screen space
 * with optional invalidation and retainer support.
 */
UCLASS()
class SKALD_API UW_FloatingText : public UUserWidget {
  GENERATED_BODY()

public:
  UW_FloatingText(const FObjectInitializer &ObjectInitializer);

  /** Prepare the floater for display with the specified owning player. */
  void InitializeFloater(APlayerController *InOwningPlayer);

  /** Reset the floater to a dormant state so it can be pooled. */
  void ResetForPool();

  /** Update the label displayed on the floater. */
  void SetText(const FText &InText);

  /** Apply a colour tint to the floater text. */
  void SetColorAndOpacity(const FLinearColor &InColor);

  /** Set the overall opacity of the floater. */
  void SetFloaterOpacity(float InOpacity);

  /** Uniformly scale the floater widget. */
  void SetFloaterScale(float InScale);

  /** Apply stylistic tweaks for hits or misses. */
  void SetTagStyle(bool bMissTag);

  /**
   * Project the supplied world location to the screen, applying the provided
   * offset. Returns false when the position cannot be projected (typically when
   * occluded or behind the camera).
   */
  bool UpdateProjection(const FVector &WorldLocation,
                        const FVector2D &ScreenOffset, float ClampMargin);

private:
  void ApplyInvalidation();

  /** Cached owning player used for world-to-screen projection. */
  TWeakObjectPtr<APlayerController> CachedPlayer;

  /** World position of the floater from the last update. */
  FVector CachedWorldLocation = FVector::ZeroVector;

  /** Additional screen-space offset to apply after projection. */
  FVector2D CachedScreenOffset = FVector2D::ZeroVector;

  /** Text element bound from the widget blueprint. */
  UPROPERTY(meta = (BindWidget))
  UTextBlock *FloatingText = nullptr;

  /** Optional invalidation panel wrapping the widget tree. */
  UPROPERTY(meta = (BindWidgetOptional))
  UInvalidationBox *InvalidationPanel = nullptr;

  /** Optional retainer box wrapping the widget tree. */
  UPROPERTY(meta = (BindWidgetOptional))
  URetainerBox *Retainer = nullptr;

  /** Whether the floater is currently considered active. */
  bool bActive = false;

  /** Cached copy of the default font so outlines can be restored. */
  FSlateFontInfo DefaultFont;
  bool bHasDefaultFont = false;
};

