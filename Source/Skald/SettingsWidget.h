#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SettingsWidget.generated.h"

class ULobbyMenuWidget;
/**
 * Basic settings menu allowing to apply current user settings.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API USettingsWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    UFUNCTION(BlueprintCallable)
    void OnApply();

    UFUNCTION()
    void OnMainMenu();

private:
    UPROPERTY()
    TWeakObjectPtr<ULobbyMenuWidget> LobbyMenu;

public:
    void SetLobbyMenu(ULobbyMenuWidget* InMenu) { LobbyMenu = InMenu; }
};

