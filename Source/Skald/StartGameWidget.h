#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SkaldTypes.h"
#include "StartGameWidget.generated.h"

class UEditableTextBox;
class UComboBoxString;
class ULobbyMenuWidget;
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
    UPROPERTY(meta = (BindWidget))
    UEditableTextBox* DisplayNameBox;

    /** Combo box to choose a faction. */
    UPROPERTY(meta = (BindWidget))
    UComboBoxString* FactionComboBox;

    /** Reference back to the owning lobby menu so it can be restored. */
    UPROPERTY()
    TWeakObjectPtr<ULobbyMenuWidget> OwningLobbyMenu;

    /** Button to start singleplayer. */
    UPROPERTY(meta = (BindWidget))
    UButton* SingleplayerButton;

    /** Button to start multiplayer. */
    UPROPERTY(meta = (BindWidget))
    UButton* MultiplayerButton;

    /** Button to return to the lobby menu. */
    UPROPERTY(meta = (BindWidgetOptional))
    UButton* MainMenuButton;

    UFUNCTION()
    void OnSingleplayer();

    UFUNCTION()
    void OnMultiplayer();

    UFUNCTION()
    void OnMainMenu();

    void StartGame(bool bMultiplayer);

public:
    /** Record the lobby menu that spawned this widget so we can unhide it later. */
    void SetLobbyMenu(ULobbyMenuWidget* InMenu) { OwningLobbyMenu = InMenu; }
};

