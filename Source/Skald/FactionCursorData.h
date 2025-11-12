#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SkaldTypes.h"

#include "FactionCursorData.generated.h"

class UTexture2D;
class USoundBase;
class UNiagaraSystem;

USTRUCT(BlueprintType)
struct FFactionCursorDefinition
{
  GENERATED_BODY()

  /** Optional hardware cursor texture for the faction. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cursor")
  TSoftObjectPtr<UTexture2D> CursorTexture;

  /** Optional sound played when hovering interactive UI. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cursor|Audio")
  TSoftObjectPtr<USoundBase> HoverSound;

  /** Optional sound played when clicking interactive UI. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cursor|Audio")
  TSoftObjectPtr<USoundBase> ClickSound;

  /** Optional Niagara system spawned to trail the cursor. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cursor|VFX")
  TSoftObjectPtr<UNiagaraSystem> CursorTrailFX;

  /** Hotspot of the cursor texture in pixels. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cursor|Meta")
  FVector2D CursorHotspot = FVector2D(0.f, 0.f);
};

/** Data asset configuring per-faction cursor visuals, audio and FX. */
UCLASS(BlueprintType)
class UFactionCursorData : public UDataAsset
{
  GENERATED_BODY()

public:
  /** Mapping between faction identifiers and cursor configuration. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cursors")
  TMap<ESkaldFaction, FFactionCursorDefinition> FactionCursors;
};

