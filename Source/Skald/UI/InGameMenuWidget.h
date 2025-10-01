#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/SlateWrapperTypes.h"

#include "InGameMenuWidget.generated.h"

class UButton;
class UUserWidget;
class USaveGameWidget;
class ULoadGameWidget;
class USettingsWidget;
/**
 * Lightweight in-game pause/menu widget surfaced from the player controller.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API UInGameMenuWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UInGameMenuWidget(const FObjectInitializer& ObjectInitializer);

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta=(BindWidgetOptional))
    UButton* SaveGameButton;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta=(BindWidgetOptional))
    UButton* LoadGameButton;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta=(BindWidgetOptional))
    UButton* SettingsButton;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta=(BindWidgetOptional))
    UButton* MainMenuButton;

    /** Clear bookkeeping for submenus that have been closed. */
    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void HandleSubMenuClosed(UUserWidget* ClosedWidget);

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeOnVisibilityChanged(ESlateVisibility InVisibility) override;

private:
    ESlateVisibility CachedVisibility = ESlateVisibility::Collapsed;
    void EnsureLayout();
    void HandleVisibilityChange(ESlateVisibility NewVisibility);

    UFUNCTION()
    void HandleSaveGameClicked();

    UFUNCTION()
    void HandleLoadGameClicked();

    UFUNCTION()
    void HandleSettingsClicked();

    UFUNCTION()
    void HandleMainMenuClicked();

    void ShowChildWidget(TSubclassOf<UUserWidget> WidgetClass);

    UPROPERTY(EditDefaultsOnly, Category="Skald|Widgets")
    TSubclassOf<USaveGameWidget> SaveGameWidgetClass;

    UPROPERTY(EditDefaultsOnly, Category="Skald|Widgets")
    TSubclassOf<ULoadGameWidget> LoadGameWidgetClass;

    UPROPERTY(EditDefaultsOnly, Category="Skald|Widgets")
    TSubclassOf<USettingsWidget> SettingsWidgetClass;

    UPROPERTY(Transient)
    TWeakObjectPtr<UUserWidget> ActiveChildWidget;
};
