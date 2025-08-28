#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SettingsWidget.generated.h"

class ULobbyMenuWidget;
class UButton;
/**
 * Basic settings menu allowing to apply current user settings.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API USettingsWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta = (BindWidgetOptional))
    UButton* ApplyButton;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta = (BindWidgetOptional))
    UButton* MainMenuButton;

    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void SetLobbyMenu(ULobbyMenuWidget* InMenu);

protected:
    virtual void NativeConstruct() override;

    UFUNCTION(BlueprintCallable)
    void OnApply();

    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void OnMainMenu();

private:
    UPROPERTY()
    TWeakObjectPtr<ULobbyMenuWidget> LobbyMenu;
};

