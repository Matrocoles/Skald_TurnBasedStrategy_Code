#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DiceRollArena.generated.h"

class UBoxComponent;
class USceneComponent;
class UPointLightComponent;
class UDecalComponent;
class UStaticMeshComponent;
class UDiceRollConfig;

UCLASS()
class SKALDDICE_API ADiceRollArena : public AActor
{
    GENERATED_BODY()

public:
    ADiceRollArena();

    void ConfigureArena(const UDiceRollConfig* Config);

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dice")
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dice")
    TObjectPtr<UBoxComponent> Bounds;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dice")
    TObjectPtr<UPointLightComponent> KeyLight;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dice")
    TObjectPtr<UDecalComponent> FloorVisual;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dice")
    TObjectPtr<UStaticMeshComponent> FloorMesh;
};
