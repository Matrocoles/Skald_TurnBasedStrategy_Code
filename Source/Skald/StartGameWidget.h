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

public:
    /** Entry box for the player's display name. */
    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta = (BindWidgetOptional))
    UEditableTextBox* DisplayNameBox;

    /** Combo box to choose a faction. */
    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta = (BindWidgetOptional))
    UComboBoxString* FactionComboBox;

    /** Button to start singleplayer. */
    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta = (BindWidgetOptional))
    UButton* SingleplayerButton;

    /** Button to start multiplayer. */
    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta = (BindWidgetOptional))
    UButton* MultiplayerButton;

    /** Button to return to the lobby menu. */
    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta = (BindWidgetOptional))
    UButton* MainMenuButton;

    /** Record the lobby menu that spawned this widget so we can unhide it later. */
    void SetLobbyMenu(ULobbyMenuWidget* InMenu) { OwningLobbyMenu = InMenu; }

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    UFUNCTION()
    void OnSingleplayer();

    UFUNCTION()
    void OnMultiplayer();

    UFUNCTION()
    void OnMainMenu();

    void StartGame(bool bMultiplayer);

private:
    /** Reference back to the owning lobby menu so it can be restored. */
    UPROPERTY()
    TWeakObjectPtr<ULobbyMenuWidget> OwningLobbyMenu;
};

