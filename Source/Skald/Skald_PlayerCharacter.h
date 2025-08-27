#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"

class AWorldMap;
class ATerritory;
class ASkaldGameMode;
class ASkaldGameState;
class USkaldGameInstance;

#include "Skald_PlayerCharacter.generated.h"

UCLASS(Blueprintable, BlueprintType)
class SKALD_API ASkald_PlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
        /** Sets default values for this character's properties */
        ASkald_PlayerCharacter();

protected:
        /** Called when the game starts or when spawned */
        virtual void BeginPlay() override;

        /** Reference to the world map actor for selection and movement */
        UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Selection", meta=(ExposeOnSpawn=true))
        AWorldMap* WorldMap;

        /** Currently selected territory, if any */
        UPROPERTY(BlueprintReadOnly, Category="Selection")
        ATerritory* CurrentSelection;

        /** Cached references to global game objects for blueprint use */
        UPROPERTY(BlueprintReadOnly, Category="Game")
        ASkaldGameMode* CachedGameMode;

        UPROPERTY(BlueprintReadOnly, Category="Game")
        ASkaldGameState* CachedGameState;

        UPROPERTY(BlueprintReadOnly, Category="Game")
        USkaldGameInstance* CachedGameInstance;

public:
        /** Called every frame */
        virtual void Tick(float DeltaTime) override;

        /** Called to bind functionality to input */
        virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

        /** Handle forward/backward movement input */
        UFUNCTION(BlueprintCallable, Category="Input")
        void MoveForward(float Value);

        /** Handle right/left movement input */
        UFUNCTION(BlueprintCallable, Category="Input")
        void MoveRight(float Value);

        /** Handle selection action */
        UFUNCTION(BlueprintCallable, Category="Input")
        void Select();

        /** Ability triggers */
        UFUNCTION(BlueprintCallable, Category="Abilities")
        void AbilityOne();

        UFUNCTION(BlueprintCallable, Category="Abilities")
        void AbilityTwo();

        UFUNCTION(BlueprintCallable, Category="Abilities")
        void AbilityThree();
};
