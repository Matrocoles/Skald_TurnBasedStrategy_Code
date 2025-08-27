#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "SaveGameWidget.generated.h"

class UButton;
class UTextBlock;
class ULobbyMenuWidget;
/**
 * Simple save game menu listing a few save slots.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API USaveGameWidget : public UUserWidget {
  GENERATED_BODY()

public:
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UButton *Slot0Button;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UTextBlock *Slot0Text;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UButton *Slot1Button;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UTextBlock *Slot1Text;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UButton *Slot2Button;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UTextBlock *Slot2Text;

  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UButton *MainMenuButton;

  void SetLobbyMenu(ULobbyMenuWidget *InMenu) { LobbyMenu = InMenu; }

protected:
  virtual void NativeConstruct() override;
  virtual void NativeDestruct() override;

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

  void UpdateSlotDisplay(int32 SlotIndex, UButton *SlotButton,
                         UTextBlock *SlotText);

  UPROPERTY()
  TWeakObjectPtr<ULobbyMenuWidget> LobbyMenu;
};
