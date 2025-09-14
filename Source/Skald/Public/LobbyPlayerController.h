#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "LobbyPlayerController.generated.h"

class ULobbyMenuWidget;

UCLASS()
class SKALD_API ALobbyPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;

protected:
    UPROPERTY(EditDefaultsOnly, Category="UI")
    TSubclassOf<UUserWidget> LobbyWidgetClass;

    UPROPERTY()
    ULobbyMenuWidget* LobbyWidgetInstance = nullptr;

    void InitLobbyUI();
};
