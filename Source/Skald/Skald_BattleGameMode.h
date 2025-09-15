#pragma once

#include "CoreMinimal.h"
#include "Skald_GameMode.h"
#include "Skald_BattleGameMode.generated.h"

/** GameMode dedicated to resolving grid-based battles. */
UCLASS()
class SKALD_API ASkald_BattleGameMode : public ASkaldGameMode {
  GENERATED_BODY()

protected:
  virtual void BeginPlay() override;
  virtual void TryInitializeWorldAndStart() override;
};

