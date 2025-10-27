#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "LobbyPlayerController.generated.h"

class ULobbyMenuWidget;
class UTitleScreenWidget;
class ULobbySessionWidget;
class ALobbyGameMode;
class ALobbyGameState;

UCLASS()
class SKALD_API ALobbyPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    ALobbyPlayerController();
    virtual void BeginPlay() override;

    /** Called by UI to request a lobby player count change. */
    UFUNCTION(BlueprintCallable, Category = "Lobby")
    void RequestPlayerCount(int32 PlayerCount);

    /** Called by UI to request AI slot adjustments. */
    UFUNCTION(BlueprintCallable, Category = "Lobby")
    void RequestAICount(int32 AICount);

    /** Called by UI to toggle the local ready state. */
    UFUNCTION(BlueprintCallable, Category = "Lobby")
    void ToggleReadyState(bool bReady);

    /** Called by UI when the local player selects a faction. */
    UFUNCTION(BlueprintCallable, Category = "Lobby")
    void RequestFactionSelection(ESkaldFaction Faction);

    /** Called by UI when the local player updates their display name. */
    UFUNCTION(BlueprintCallable, Category = "Lobby")
    void RequestDisplayNameUpdate(const FString& DisplayName);

    /** Called by UI when the host presses Launch Game. */
    UFUNCTION(BlueprintCallable, Category = "Lobby")
    void RequestLaunch();

    /** Returns true when this local player occupies the authoritative lobby host slot. */
    UFUNCTION(BlueprintPure, Category = "Lobby")
    bool IsLocalPlayerLobbyHost() const;

protected:
    UPROPERTY(EditDefaultsOnly, Category="UI")
    TSubclassOf<ULobbyMenuWidget> LobbyWidgetClass;

    UPROPERTY(EditDefaultsOnly, Category="UI")
    TSubclassOf<UTitleScreenWidget> TitleScreenWidgetClass;

    UPROPERTY(EditDefaultsOnly, Category = "UI")
    TSubclassOf<ULobbySessionWidget> LobbySessionWidgetClass;

    UPROPERTY()
    ULobbyMenuWidget* LobbyWidgetInstance = nullptr;

    UPROPERTY()
    UTitleScreenWidget* TitleScreenWidgetInstance = nullptr;

    UPROPERTY()
    ULobbySessionWidget* LobbySessionWidgetInstance = nullptr;

    void InitLobbyUI();

    UFUNCTION()
    void HandleTitleScreenDismissed();

    UFUNCTION(Server, Reliable)
    void ServerSetPlayerCount(int32 PlayerCount);

    UFUNCTION(Server, Reliable)
    void ServerSetAICount(int32 AICount);

    UFUNCTION(Server, Reliable)
    void ServerSetReady(bool bReady);

    UFUNCTION(Server, Reliable)
    void ServerSetFaction(ESkaldFaction Faction);

    UFUNCTION(Server, Reliable)
    void ServerSetDisplayName(const FString& DisplayName);

    UFUNCTION(Server, Reliable)
    void ServerLaunchMatch();

    /** Server-side validation to ensure only the lobby host can modify lobby state. */
    bool IsLobbyHostOnServer() const;
};
