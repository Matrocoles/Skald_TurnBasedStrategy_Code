#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "LobbyPlayerController.generated.h"

class ULobbyMenuWidget;
class UTitleScreenWidget;

UCLASS()
class SKALD_API ALobbyPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    ALobbyPlayerController();
    virtual void BeginPlay() override;

protected:
    UPROPERTY(EditDefaultsOnly, Category="UI")
    TSubclassOf<ULobbyMenuWidget> LobbyWidgetClass;

    UPROPERTY(EditDefaultsOnly, Category="UI")
    TSubclassOf<UTitleScreenWidget> TitleScreenWidgetClass;

    UPROPERTY()
    ULobbyMenuWidget* LobbyWidgetInstance = nullptr;

    UPROPERTY()
    UTitleScreenWidget* TitleScreenWidgetInstance = nullptr;

    void InitLobbyUI();

    UFUNCTION()
    void HandleTitleScreenDismissed();
};
