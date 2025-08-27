#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SkaldTypes.h"
#include "Components/ComboBoxString.h"
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

    /** Button to confirm the player's selections before starting. */
    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta = (BindWidgetOptional))
    UButton* LockInButton;

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

    UFUNCTION()
    void OnSingleplayer();

    UFUNCTION()
    void OnMultiplayer();

    UFUNCTION()
    void OnMainMenu();

    UFUNCTION()
    void OnLockIn();

    UFUNCTION()
    void OnDisplayNameChanged(const FText& Text);

    UFUNCTION()
    void OnFactionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    void StartGame(bool bMultiplayer);

    void ValidateSelections();

    void RefreshFactionOptions();

    UFUNCTION()
    void HandleFactionsUpdated();

private:
    /** Reference back to the owning lobby menu so it can be restored. */
    UPROPERTY()
    TWeakObjectPtr<ULobbyMenuWidget> OwningLobbyMenu;
};

