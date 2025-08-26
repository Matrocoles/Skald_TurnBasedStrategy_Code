#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SkaldTypes.h"
#include "StartGameWidget.generated.h"

class UEditableTextBox;
class UComboBoxString;

/**
 * Menu shown after pressing Start Game, to choose single or multiplayer.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API UStartGameWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    /** Entry box for the player's display name. */
    UPROPERTY()
    UEditableTextBox* DisplayNameBox;

    /** Combo box to choose a faction. */
    UPROPERTY()
    UComboBoxString* FactionComboBox;

    UFUNCTION()
    void OnSingleplayer();

    UFUNCTION()
    void OnMultiplayer();

    void StartGame(bool bMultiplayer);
};

