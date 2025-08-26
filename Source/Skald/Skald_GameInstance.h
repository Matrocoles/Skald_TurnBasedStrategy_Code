#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "SkaldTypes.h"
#include "Skald_GameInstance.generated.h"

/** Game instance storing player selections from the lobby. */
UCLASS()
class SKALD_API USkaldGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    /** Player chosen display name. */
    UPROPERTY(BlueprintReadWrite, Category="Player")
    FString DisplayName;

    /** Selected faction for this player. */
    UPROPERTY(BlueprintReadWrite, Category="Player")
    ESkaldFaction Faction;

    /** Whether the game was started in multiplayer mode. */
    UPROPERTY(BlueprintReadWrite, Category="Player")
    bool bIsMultiplayer;

    /** Factions that have already been selected by players or AI. */
    UPROPERTY(BlueprintReadWrite, Category="Player")
    TArray<ESkaldFaction> TakenFactions;
};

