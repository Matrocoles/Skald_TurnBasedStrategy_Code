#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LoadGameWidget.generated.h"

class UButton;
class ULobbyMenuWidget;
/**
 * Simple load game menu listing a few save slots.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API ULoadGameWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    UPROPERTY(meta=(BindWidget))
    UButton* Slot0Button;

    UPROPERTY(meta=(BindWidget))
    UButton* Slot1Button;

    UPROPERTY(meta=(BindWidget))
    UButton* Slot2Button;

    UPROPERTY(meta=(BindWidget))
    UButton* MainMenuButton;

    UFUNCTION(BlueprintCallable)
    void OnLoadSlot0();

    UFUNCTION(BlueprintCallable)
    void OnLoadSlot1();

    UFUNCTION(BlueprintCallable)
    void OnLoadSlot2();

    UFUNCTION()
    void OnMainMenu();

private:
    /** Shared implementation for the individual load slot handlers. */
    void HandleLoadSlot(int32 SlotIndex);

    UPROPERTY()
    TWeakObjectPtr<ULobbyMenuWidget> LobbyMenu;

public:
    void SetLobbyMenu(ULobbyMenuWidget* InMenu) { LobbyMenu = InMenu; }
};

