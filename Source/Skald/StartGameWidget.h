#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "StartGameWidget.generated.h"

class ULobbyMenuWidget;
class UButton;
class APlayerController;
class UEditableTextBox;
class UComboBoxString;
class USpinBox;

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

  /** Button to start the multiplayer flow. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UButton *MultiplayerButton;

  /** Button to host a multiplayer session. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UButton *HostButton;

  /** Button to join an existing multiplayer session. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UButton *JoinButton;

  /** Entry box for the server address when joining. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UEditableTextBox *JoinAddressBox;

  /** Button to return to the lobby menu. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UButton *MainMenuButton;

  /** Entry box for the player's display name. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UEditableTextBox *DisplayNameBox;

  /** Combo box used to choose the player's faction. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UComboBoxString *FactionComboBox;

  /** Spin box determining how many AI opponents to spawn. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  USpinBox *AICountSpinBox;

  /** Button to confirm singleplayer settings. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|Widgets",
            meta = (BindWidgetOptional))
  UButton *LockInButton;

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
  void OnHost();

  UFUNCTION(BlueprintCallable, Category = "Skald|Widgets")
  void OnJoin();

  UFUNCTION(BlueprintCallable, Category = "Skald|Widgets")
  void OnMainMenu();

  UFUNCTION(BlueprintCallable, Category = "Skald|Widgets")
  void OnLockIn();

  void StartGame(bool bMultiplayer, bool bHost);

private:
  /** Reference back to the owning lobby menu so it can be restored. */
  UPROPERTY()
  TWeakObjectPtr<ULobbyMenuWidget> OwningLobbyMenu;
};
