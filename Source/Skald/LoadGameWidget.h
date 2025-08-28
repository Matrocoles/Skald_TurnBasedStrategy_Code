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

public:
    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta=(BindWidgetOptional))
    UButton* Slot0Button;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta=(BindWidgetOptional))
    UButton* Slot1Button;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta=(BindWidgetOptional))
    UButton* Slot2Button;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta=(BindWidgetOptional))
    UButton* MainMenuButton;

    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void SetLobbyMenu(ULobbyMenuWidget* InMenu);

protected:
    virtual void NativeConstruct() override;

    UFUNCTION(BlueprintCallable)
    void OnLoadSlot0();

    UFUNCTION(BlueprintCallable)
    void OnLoadSlot1();

    UFUNCTION(BlueprintCallable)
    void OnLoadSlot2();

    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void OnMainMenu();

private:
    /** Shared implementation for the individual load slot handlers. */
    void HandleLoadSlot(int32 SlotIndex);

    UPROPERTY()
    TWeakObjectPtr<ULobbyMenuWidget> LobbyMenu;
};

