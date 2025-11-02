#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DiceRollConfig.generated.h"

class ADiceRollArena;
class ASkaldDiceD6;

USTRUCT(BlueprintType)
struct SKALDDICE_API FSkaldDiceFaceMapping
{
    GENERATED_BODY()

    FSkaldDiceFaceMapping()
        : UpNormal(FVector::UpVector)
        , FaceValue(1)
    {
    }

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice")
    FVector UpNormal;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice")
    int32 FaceValue;
};

UCLASS(BlueprintType)
class SKALDDICE_API UDiceRollConfig : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Timing")
    float RollDurationMin = 0.75f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Timing")
    float RollDurationMax = 1.5f;

    /** Seconds to wait after a roll completes before cleaning up spawned dice. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Timing")
    float DiceCleanupDelay = 0.6f;

    /** Seconds to wait after a roll completes before cleaning up the arena. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Timing")
    float ArenaCleanupDelay = 0.75f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Physics")
    FVector2D SpawnImpulseRange = FVector2D(100.0f, 300.0f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Physics")
    FVector2D AngularImpulseRange = FVector2D(100.0f, 300.0f);

    /** Height offset applied when spawning dice above the arena bounds. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Physics")
    float SpawnHeightOffset = 60.f;

    /** Horizontal spread (in Unreal units) used when scattering dice spawn positions. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Physics")
    float SpawnSpread = 80.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Arena")
    FBox ArenaBounds = FBox(FVector(-100.f, -100.f, 0.f), FVector(100.f, 100.f, 200.f));

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Settle")
    float SettleVelocityThreshold = 2.5f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Settle")
    float SettleAngularThreshold = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Settle")
    float SettleHoldTime = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Tint")
    FLinearColor PlayerTint = FLinearColor(0.0f, 0.45f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Tint")
    FLinearColor EnemyTint = FLinearColor(1.0f, 0.1f, 0.1f);

    /** Optional material parameter name applied to dice meshes when tinting. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Tint")
    FName DiceTintParameter = TEXT("TintColor");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Determinism")
    bool bDeterministicSeed = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Determinism", meta = (EditCondition = "bDeterministicSeed"))
    int32 Seed = 1337;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|UI")
    bool bMirrorToUIThumbnail = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|UI")
    int32 ThumbnailFPS = 30;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Fallbacks")
    bool bUseSpriteOnly = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Faces")
    TArray<FSkaldDiceFaceMapping> FaceLookup;

    /** Actor class used when spawning the transient dice arena. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Classes")
    TSubclassOf<ADiceRollArena> ArenaClass;

    /** Dice class spawned for player-aligned dice. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Classes")
    TSubclassOf<ASkaldDiceD6> PlayerDiceClass;

    /** Dice class spawned for enemy-aligned dice (defaults to player dice if unset). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dice|Classes")
    TSubclassOf<ASkaldDiceD6> EnemyDiceClass;
};
