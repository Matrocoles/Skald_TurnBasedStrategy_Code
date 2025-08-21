// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"

class AWorldMap;
class ATerritory;

#include "Skald_PlayerCharacter.generated.h"

UCLASS()
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
        UPROPERTY()
        AWorldMap* WorldMap;

        /** Currently selected territory, if any */
        UPROPERTY(BlueprintReadOnly, Category="Selection")
        ATerritory* CurrentSelection;

public:
        /** Called every frame */
        virtual void Tick(float DeltaTime) override;

        /** Called to bind functionality to input */
        virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

        /** Handle forward/backward movement input */
        void MoveForward(float Value);

        /** Handle right/left movement input */
        void MoveRight(float Value);

        /** Handle selection action */
        void Select();

        /** Ability triggers */
        void AbilityOne();
        void AbilityTwo();
        void AbilityThree();
};
