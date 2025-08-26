#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SkaldTypes.h"
#include "StartGameWidget.generated.h"

class UEditableTextBox;
class UComboBoxString;
class UButton;

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
    UPROPERTY(meta=(BindWidget))
    UEditableTextBox* DisplayNameBox;

    /** Combo box to choose a faction. */
    UPROPERTY(meta=(BindWidget))
    UComboBoxString* FactionComboBox;

    /** Button to start singleplayer. */
    UPROPERTY(meta=(BindWidget))
    UButton* SingleplayerButton;

    /** Button to start multiplayer. */
    UPROPERTY(meta=(BindWidget))
    UButton* MultiplayerButton;

    UFUNCTION()
    void OnSingleplayer();

    UFUNCTION()
    void OnMultiplayer();

    void StartGame(bool bMultiplayer);
};

