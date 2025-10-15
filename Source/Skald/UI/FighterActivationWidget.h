#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FighterActivationWidget.generated.h"

class UImage;
class UTexture2D;

UENUM(BlueprintType)
enum class EFighterActivationIndicatorState : uint8 {
  Hidden,
  Active,
  Spent
};

/**
 * Simple widget used to display a floating activation indicator above fighters.
 */
UCLASS()
class SKALD_API UFighterActivationWidget : public UUserWidget {
  GENERATED_BODY()

public:
  virtual TSharedRef<SWidget> RebuildWidget() override;
  virtual void NativeConstruct() override;

  /** Update the textures used for each state. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  void SetIconTextures(UTexture2D *ActiveTexture, UTexture2D *SpentTexture);

  /** Apply a new activation state to the widget. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  void SetActivationState(EFighterActivationIndicatorState NewState);

protected:
  /** Refresh the brush displayed by the image widget. */
  void RefreshBrush();

  /** Image that displays the activation state icon. */
  UPROPERTY(Transient)
  UImage *ActivationImage = nullptr;

  /** Texture displayed while the fighter is currently acting. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skald|Battle")
  TObjectPtr<UTexture2D> ActiveStateTexture;

  /** Texture displayed once the fighter has exhausted its actions. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skald|Battle")
  TObjectPtr<UTexture2D> SpentStateTexture;

  /** Tint applied to the icon while active. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skald|Battle")
  FLinearColor ActiveTint = FLinearColor::White;

  /** Tint applied when the fighter has ended its activation. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skald|Battle")
  FLinearColor SpentTint = FLinearColor::White;

  /** Cached state currently being represented. */
  EFighterActivationIndicatorState CurrentState =
      EFighterActivationIndicatorState::Hidden;
};
