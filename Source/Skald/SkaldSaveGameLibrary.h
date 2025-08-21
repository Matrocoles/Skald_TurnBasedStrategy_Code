#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SkaldSaveGameLibrary.generated.h"

class USkaldSaveGame;

/**
 * Blueprint-callable helpers for saving and loading Skald game data.
 */
UCLASS(BlueprintType)
class SKALD_API USkaldSaveGameLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Save the provided game object to the given slot. */
    UFUNCTION(BlueprintCallable, Category="Skald|SaveGame")
    static bool SaveSkaldGame(USkaldSaveGame* SaveGameObject, const FString& SlotName, int32 UserIndex);

    /** Load a game object from the given slot. */
    UFUNCTION(BlueprintCallable, Category="Skald|SaveGame")
    static USkaldSaveGame* LoadSkaldGame(const FString& SlotName, int32 UserIndex);
};

