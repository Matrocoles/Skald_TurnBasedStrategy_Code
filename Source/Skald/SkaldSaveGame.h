#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "SkaldTypes.h"
#include "SkaldSaveGame.generated.h"

/**
 * Native save game object storing persistent game data.
 */
UCLASS(BlueprintType)
class SKALD_API USkaldSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    USkaldSaveGame();

    /** Name of the save slot. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    FString SaveName;

    /** Date the save was created. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    FDateTime SaveDate;

    /** Current turn number in the campaign. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    int32 TurnNumber = 0;

    /** Index of the player whose turn is active. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    int32 CurrentPlayerIndex = 0;

    /** Stored territory state data. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    TArray<FS_Territory> Territories;

    /** Stored player data. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    TArray<FPlayerSaveStruct> Players;

    /** Stored siege equipment data. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    TArray<FS_Siege> Sieges;

    /** Random seed used for procedural elements. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    int32 RandomSeed = 0;

    /** Camera view offset at the time of saving. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    FVector2D SavedViewOffset = FVector2D::ZeroVector;

    /** Camera zoom level at the time of saving. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    float SavedZoomAmount = 0.f;
};

