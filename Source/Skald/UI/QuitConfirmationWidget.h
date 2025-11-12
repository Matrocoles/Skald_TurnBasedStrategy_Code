#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "QuitConfirmationWidget.generated.h"

class UButton;
class USettingsWidget;
class UTextBlock;

/**
 * Simple confirmation dialog that asks the player whether to quit the game.
 */
UCLASS()
class SKALD_API UQuitConfirmationWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UQuitConfirmationWidget(const FObjectInitializer& ObjectInitializer);

    /** Assign the settings widget that spawned this confirmation dialog. */
    void SetOwningSettings(USettingsWidget* InSettings);

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    void EnsureLayout();

    UFUNCTION()
    void HandleYesClicked();

    UFUNCTION()
    void HandleNoClicked();

    /** Optional widgets when using a Blueprint implementation. */
    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta=(BindWidgetOptional, AllowPrivateAccess="true"))
    UTextBlock* ConfirmationText;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta=(BindWidgetOptional, AllowPrivateAccess="true"))
    UButton* YesButton;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta=(BindWidgetOptional, AllowPrivateAccess="true"))
    UButton* NoButton;

    /** Owning settings widget so we can notify when the dialog is dismissed. */
    TWeakObjectPtr<USettingsWidget> OwningSettings;
};

