#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SkaldTypes.h"
#include "PlayerSetupWidget.generated.h"

class UEditableTextBox;
class UComboBoxString;

/**
 * Widget shown after selecting single or multiplayer that allows the
 * player to choose a display name and faction before entering the game.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API UPlayerSetupWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;

    /** Called when the player confirms their choices. */
    UFUNCTION(BlueprintCallable)
    void OnConfirm();

    /** Set the faction chosen from the UI. */
    UFUNCTION(BlueprintCallable)
    void OnFactionSelected(ESkaldFaction NewFaction);

    /** Optional binding to the player's name entry box. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UEditableTextBox* NameEntryBox;

    /** Optional binding to the faction selection combo box. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UComboBoxString* FactionSelectionBox;

    /** Display name entered by the player. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Setup")
    FString DisplayName;

    /** Currently selected faction. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Setup")
    ESkaldFaction SelectedFaction;

    /** Whether the game should start in multiplayer. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Setup")
    bool bMultiplayer;
};
