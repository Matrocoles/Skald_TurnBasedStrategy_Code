#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "LockedInFighterEntryWidget.generated.h"

class UButton;
class UImage;
class UProgressBar;
class UTextBlock;
class UWidget;
class AFighterPawn;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLockedInFighterEntryClicked,
                                           AFighterPawn *, Fighter);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLockedInFighterEntryRemoved,
                                           AFighterPawn *, Fighter);

/**
 * Entry widget representing a locked-in fighter within the battle HUD list.
 */
UCLASS(BlueprintType, Blueprintable)
class SKALD_API ULockedInFighterEntryWidget : public UUserWidget {
  GENERATED_BODY()

public:
  ULockedInFighterEntryWidget(const FObjectInitializer &ObjectInitializer);

  virtual void NativeConstruct() override;
  virtual void NativeDestruct() override;

  /** Assign the fighter represented by this entry. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  void SetFighter(AFighterPawn *InFighter);

  /** Retrieve the fighter represented by this entry. */
  UFUNCTION(BlueprintPure, Category = "Skald|Battle")
  AFighterPawn *GetFighter() const { return BoundFighter.Get(); }

  /** Update all bound widgets from the fighter's current data. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  void RefreshFromFighter();

  /** Refreshes the visual state based on the fighter's activation status. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  void RefreshTurnState();

  /** Apply the selected highlight state. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  void SetIsSelected(bool bSelected);

  /** Apply the active highlight state. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  void SetIsActive(bool bActive);

  /** Reset all bindings and clear display data. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Battle")
  void ResetEntry();

  /** Fired when the entry is clicked. */
  UPROPERTY(BlueprintAssignable, Category = "Skald|Battle|Events")
  FLockedInFighterEntryClicked OnEntryClicked;

  /** Fired when the entry should be removed (e.g. fighter destroyed). */
  UPROPERTY(BlueprintAssignable, Category = "Skald|Battle|Events")
  FLockedInFighterEntryRemoved OnEntryRemoved;

protected:
  /** Handle click input from the bound button. */
  UFUNCTION()
  void HandleEntryButtonClicked();

  /** Respond to health updates from the fighter. */
  UFUNCTION()
  void HandleFighterHealthChanged(int32 NewHealth);

  /** Respond to action updates from the fighter. */
  UFUNCTION()
  void HandleFighterActionsChanged(int32 NewActionsRemaining);

  /** Handle the fighter being destroyed. */
  UFUNCTION()
  void HandleFighterDestroyed(AActor *DestroyedActor);

  /** Apply tint and opacity changes based on selection/turn states. */
  void ApplyVisualState();

  /** Update the health widgets with the provided values. */
  void UpdateHealthDisplay(int32 CurrentHealth, int32 MaxHealth);

  /** Optional button used for click input. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UButton *ClickButton;

  /** Image displaying the fighter portrait. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UImage *PortraitImage;

  /** Text displaying the fighter's display name. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *NameText;

  /** Progress bar visualising the fighter's health. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UProgressBar *HealthProgress;

  /** Text displaying the health values. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *HealthText;

  /** Optional widget toggled when selected. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UWidget *SelectedHighlight;

  /** Optional widget toggled when active. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UWidget *ActiveHighlight;

  /** Tint applied under normal circumstances. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Battle")
  FLinearColor NormalTint = FLinearColor::White;

  /** Tint applied when the entry is selected. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Battle")
  FLinearColor SelectedTint = FLinearColor(1.f, 1.f, 1.f, 1.f);

  /** Tint applied when the fighter is currently active. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Battle")
  FLinearColor ActiveTint = FLinearColor(1.f, 1.f, 1.f, 1.f);

  /** Opacity used when the fighter has ended its turn. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Battle",
            meta = (ClampMin = "0.0", ClampMax = "1.0"))
  float SpentOpacity = 0.45f;

private:
  void BindToFighter(AFighterPawn *Fighter);
  void UnbindFromFighter();

  bool bIsSelected = false;
  bool bIsActive = false;
  bool bIsTurnSpent = false;

  TWeakObjectPtr<AFighterPawn> BoundFighter;

  int32 CachedMaxHealth = 1;
};

