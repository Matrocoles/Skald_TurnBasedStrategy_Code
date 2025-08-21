#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LobbyGameMode.generated.h"

/**
 * Game mode used for the lobby map.
 */
UCLASS()
class SKALD_API ALobbyGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ALobbyGameMode();

    virtual void BeginPlay() override;

protected:
    UPROPERTY(EditDefaultsOnly, Category = "UI")
    TSubclassOf<class UUserWidget> LobbyWidgetClass;
};

