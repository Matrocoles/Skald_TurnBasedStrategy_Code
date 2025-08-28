#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "StartGameWidget.generated.h"

class ULobbyMenuWidget;
class UButton;
class APlayerController;

/**
 * Menu shown after pressing Start Game, to choose single or multiplayer.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API UStartGameWidget : public UUserWidget {
  GENERATED_BODY()

public:
  /** Button to start singleplayer. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UButton *SingleplayerButton;

  /** Button to start multiplayer. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UButton *MultiplayerButton;

  /** Button to return to the lobby menu. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UButton *MainMenuButton;

  /** Record the lobby menu that spawned this widget so we can unhide it later. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Widgets")
  void SetLobbyMenu(ULobbyMenuWidget *InMenu);

  /** Shared helper to move the player controller to the gameplay map. */
  UFUNCTION(BlueprintCallable, Category = "Skald|Widgets")
  static void TravelToGameplayMap(APlayerController *PC, bool bMultiplayer);

protected:
  virtual void NativeConstruct() override;

  UFUNCTION(BlueprintCallable, Category = "Skald|Widgets")
  void OnSingleplayer();

  UFUNCTION(BlueprintCallable, Category = "Skald|Widgets")
  void OnMultiplayer();

  UFUNCTION(BlueprintCallable, Category = "Skald|Widgets")
  void OnMainMenu();

  void StartGame(bool bMultiplayer);

private:
  /** Reference back to the owning lobby menu so it can be restored. */
  UPROPERTY()
  TWeakObjectPtr<ULobbyMenuWidget> OwningLobbyMenu;
};
