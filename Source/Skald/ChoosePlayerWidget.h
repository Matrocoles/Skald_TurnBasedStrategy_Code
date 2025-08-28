#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SkaldTypes.h"
#include "ChoosePlayerWidget.generated.h"

class UEditableTextBox;
class UComboBoxString;
class UButton;
namespace ESelectInfo
{
    enum Type;
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPlayerLockedIn);

/**
 * Widget allowing a player to choose a display name and faction.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API UChoosePlayerWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;

    /** Handle the player locking in their choices. */
    UFUNCTION(BlueprintCallable)
    void OnLockIn();

    /** Name entry box bound from the widget blueprint. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidget))
    UEditableTextBox* DisplayNameBox;

    /** Faction selection combo box bound from the widget blueprint. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidget))
    UComboBoxString* FactionComboBox;

    /** Button used to lock in the selection. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidget))
    UButton* LockInButton;

    /** Delegate fired when the player locks in their selection. */
    UPROPERTY(BlueprintAssignable, Category="Skald|Widgets|Events")
    FPlayerLockedIn OnPlayerLockedIn;

protected:
    /** Text box change handler. */
    UFUNCTION()
    void HandleDisplayNameChanged(const FText& Text);

    /** Faction combo selection handler. */
    UFUNCTION()
    void HandleFactionSelected(FString SelectedItem, ESelectInfo::Type SelectionType);

    /** Enable lock in button when prerequisites are met. */
    UFUNCTION()
    void UpdateLockInEnabled();
};

