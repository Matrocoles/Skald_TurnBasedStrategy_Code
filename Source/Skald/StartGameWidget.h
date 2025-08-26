#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SkaldTypes.h"
#include "StartGameWidget.generated.h"

class UEditableTextBox;
class UComboBoxString;
class ULobbyMenuWidget;

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
    UEditableTextBox* DisplayNameInput;

    /** Combo box to choose a faction. */
    UPROPERTY()
    UComboBoxString* FactionCombo;

    UPROPERTY()
    TWeakObjectPtr<ULobbyMenuWidget> LobbyMenu;

    UFUNCTION()
    void OnSingleplayer();

    UFUNCTION()
    void OnMultiplayer();

    UFUNCTION()
    void OnMainMenu();

    void StartGame(bool bMultiplayer);

public:
    void SetLobbyMenu(ULobbyMenuWidget* InMenu) { LobbyMenu = InMenu; }
};

