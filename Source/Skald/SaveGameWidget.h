#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SaveGameWidget.generated.h"

class UButton;
class ULobbyMenuWidget;
/**
 * Simple save game menu listing a few save slots.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API USaveGameWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    UPROPERTY(meta=(BindWidget))
    UButton* Slot0Button;

    UPROPERTY(meta=(BindWidget))
    UButton* Slot1Button;

    UPROPERTY(meta=(BindWidget))
    UButton* Slot2Button;

    UPROPERTY(meta=(BindWidget))
    UButton* MainMenuButton;

    UFUNCTION(BlueprintCallable)
    void OnSaveSlot0();

    UFUNCTION(BlueprintCallable)
    void OnSaveSlot1();

    UFUNCTION(BlueprintCallable)
    void OnSaveSlot2();

    UFUNCTION()
    void OnMainMenu();

private:
    /** Shared implementation for the individual save slot handlers. */
    void HandleSaveSlot(int32 SlotIndex);

    UPROPERTY()
    TWeakObjectPtr<ULobbyMenuWidget> LobbyMenu;

public:
    void SetLobbyMenu(ULobbyMenuWidget* InMenu) { LobbyMenu = InMenu; }
};

