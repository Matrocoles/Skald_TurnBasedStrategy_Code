#pragma once

#include "Blueprint/UserWidget.h"
#include "GridBattleManager.h"
#include "FighterSelectionWidget.generated.h"

class UButton;
class UScrollBox;
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
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UButton *SelectButton;

  /** Fighter definition represented by this entry. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Fighter")
  FFighterDefinition Fighter;

private:
  /** Callback for SelectButton. */
  UFUNCTION()
  void HandleClicked();

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
  virtual void NativeConstruct() override;

  /** Faction of the player owning this selection widget. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Fighter")
  ESkaldFaction PlayerFaction = ESkaldFaction::None;

  /** Fighters that can be chosen. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Fighter")
  TArray<FFighterDefinition> AvailableFighters;

  /** Fighters chosen by the player. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Fighter")
  TArray<FFighterDefinition> ChosenFighters;

  /** Maximum total army cost allowed. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skald|Fighter")
  int32 MaxCost = 0;

  /** Current cost of chosen fighters. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Fighter")
  int32 CurrentCost = 0;

  /** Scroll box used to list available fighters. */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UScrollBox *FighterList;

  /** Blueprint class used for each fighter entry. */
  UPROPERTY(EditAnywhere, Category = "Skald|Fighter")
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

  /** Lock in the current selection. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Fighter")
  void LockIn();

protected:
  /** Populate the fighter list scroll box. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Fighter")
  void PopulateFighterList();

  /** Check whether a fighter can be afforded with remaining cost. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Fighter")
  bool CanAfford(const FFighterDefinition &Fighter) const;
};
