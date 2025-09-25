#pragma once

#include "Blueprint/UserWidget.h"
#include "GridBattleManager.h"
#include "FighterSelectionWidget.generated.h"

class UButton;
class UScrollBox;
class UTextBlock;
class UFighterSelectionWidget; // forward declare for entry widget

/**
 * Entry widget representing a single fighter option in the list.
 * Visual setup is expected to be done in Blueprint.
 */
UCLASS(BlueprintType, Blueprintable)
class SKALD_API UFighterEntryWidget : public UUserWidget {
  GENERATED_BODY()

public:
  virtual void NativeConstruct() override;

  /** Initialise the entry with fighter data and parent widget. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Fighter")
  void Init(const FFighterDefinition &InFighter,
            UFighterSelectionWidget *InOwner);

  /** Button bound from the blueprint used to choose this fighter. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UButton *SelectButton;

  /** Button bound from the blueprint used to remove this fighter. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UButton *RemoveButton;

  /** Name text bound from the blueprint. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *NameText;

  /** Strength display text. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *StrengthText;

  /** Defence display text. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *DefenceText;

  /** Health display text. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *HealthText;

  /** Attack range display text. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *AttackRangeText;

  /** Attack damage display text. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *AttackDamageText;

  /** Attack dice display text. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *AttackDiceText;

  /** Movement display text. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *MovementText;

  /** Army cost display text. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *CostText;

  /** Fighter definition represented by this entry. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Fighter")
  FFighterDefinition Fighter;

private:
  /** Callback for SelectButton. */
  UFUNCTION()
  void HandleClicked();

  /** Callback for RemoveButton. */
  UFUNCTION()
  void HandleRemoveClicked();

  /** Owning selection widget. */
  UPROPERTY()
  UFighterSelectionWidget *Owner;
};

/** Delegate fired when the player locks in their fighter selections. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLockedIn);

/** Delegate fired when the player selects a fighter. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFighterChosen,
                                            const FFighterDefinition &,
                                            Fighter);

/**
 * Widget allowing a player to choose a set of fighters within a cost budget.
 */
UCLASS(BlueprintType, Blueprintable)
class SKALD_API UFighterSelectionWidget : public UUserWidget {
  GENERATED_BODY()

public:
  virtual bool Initialize() override;
  virtual void NativeConstruct() override;
  virtual void NativePreConstruct() override;

  /** Faction of the player owning this selection widget. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Fighter")
  ESkaldFaction PlayerFaction = ESkaldFaction::None;

  /** Fighters that can be chosen. */
  // Make these spawn-exposed so they can be passed at CreateWidget time:
  UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ExposeOnSpawn=true), Category = "Skald|Fighter")
  TArray<FFighterDefinition> AvailableFighters;

  /** Fighters chosen by the player. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Fighter")
  TArray<FFighterDefinition> ChosenFighters;

  /** Maximum total army cost allowed. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ExposeOnSpawn=true), Category = "Skald|Fighter")
  int32 MaxCost = 0;

  /** Current cost of chosen fighters. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Fighter")
  int32 CurrentCost = 0;

  /** Scroll box used to list available fighters. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UScrollBox *FighterList;

  /** Text displaying the current/maximum cost. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
  UTextBlock *CostDisplayText;

  /** Blueprint class used for each fighter entry. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Fighter")
  TSubclassOf<UFighterEntryWidget> FighterEntryClass;

  /** Delegate fired when Lock In is triggered. */
  UPROPERTY(BlueprintAssignable, Category = "Skald|Fighter|Events")
  FOnLockedIn OnLockedIn;

  /** Delegate fired when a fighter is chosen. */
  UPROPERTY(BlueprintAssignable, Category = "Skald|Fighter|Events")
  FOnFighterChosen OnFighterChosen;

  /** Attempt to choose a fighter, returns true if successful. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Fighter")
  bool ChooseFighter(const FFighterDefinition &Fighter);

  /** Attempt to remove a fighter, returns true if removed. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Fighter")
  bool RemoveFighter(const FFighterDefinition &Fighter);

  /** Lock in the current selection. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Fighter")
  void LockIn();

  /** Enable or disable the Lock In button. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Fighter")
  void SetLockInButtonEnabled(bool bEnabled);

  // Ensure this is PUBLIC (not protected)
  /** Check whether a fighter can be afforded with remaining cost. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Skald|Fighter")
  bool CanAfford(const FFighterDefinition& Fighter) const;

  // Add a setter that repopulates when data arrives later:
  UFUNCTION(BlueprintCallable, Category="Skald|Fighter")
  void SetAvailableFighters(const TArray<FFighterDefinition>& InFighters);
  
public:
  /** Populate the fighter list scroll box. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Fighter")
  void PopulateFighterList();

  /** Update cost display text from current/max cost. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Fighter")
  void UpdateCostDisplay();

private:
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget, AllowPrivateAccess = "true"))
  UButton *LockInButton = nullptr;

  UFUNCTION()
  void HandleLockInClicked();

  void GatherSelectedFighters(TArray<FFighterDefinition> &OutFighters) const;

  void RefreshEntryAffordability();
};
