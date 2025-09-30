#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Runtime/Launch/Resources/Version.h"

#include "InGameMenuWidget.generated.h"

class UButton;
class UUserWidget;
class USaveGameWidget;
class ULoadGameWidget;
class USettingsWidget;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
struct FVisibilityChangedEvent;
#endif

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
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
    virtual void NativeOnVisibilityChanged(const FVisibilityChangedEvent& VisibilityChangedEvent) override;
#else
    virtual void NativeOnVisibilityChanged(ESlateVisibility InVisibility) override;
#endif

private:
    void EnsureLayout();
    void HandleVisibilityChanged(ESlateVisibility NewVisibility);

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
