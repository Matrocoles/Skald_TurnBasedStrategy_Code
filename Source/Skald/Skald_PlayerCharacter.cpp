#include "Skald_PlayerCharacter.h"
#include "Skald.h"
#include "WorldMap.h"
#include "Territory.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "Skald_GameMode.h"
#include "Skald_GameState.h"
#include "Skald_GameInstance.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

// Sets default values
ASkald_PlayerCharacter::ASkald_PlayerCharacter()
{
        PrimaryActorTick.bCanEverTick = true;

        bUseControllerRotationYaw = true;
        bUseControllerRotationPitch = true;

        WorldMap = nullptr;
        CurrentSelection = nullptr;

        CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
        CameraBoom->SetupAttachment(RootComponent);
        CameraBoom->bUsePawnControlRotation = true;

        FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
        FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
        FollowCamera->bUsePawnControlRotation = false;
}

// Called when the game starts or when spawned
void ASkald_PlayerCharacter::BeginPlay()
{
        Super::BeginPlay();

        // Cache reference to the world map if one exists in the level
        WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(GetWorld(), AWorldMap::StaticClass()));

        CachedGameMode = GetWorld()->GetAuthGameMode<ASkaldGameMode>();
        CachedGameState = GetWorld()->GetGameState<ASkaldGameState>();
        CachedGameInstance = GetGameInstance<USkaldGameInstance>();
}

// Called every frame
void ASkald_PlayerCharacter::Tick(float DeltaTime)
{
        Super::Tick(DeltaTime);

        // Example tick behavior: keep track of selection validity
        if (!IsValid(CurrentSelection))
        {
                CurrentSelection = nullptr;
        }
}

// Called to bind functionality to input
void ASkald_PlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
        Super::SetupPlayerInputComponent(PlayerInputComponent);

        check(PlayerInputComponent);

        PlayerInputComponent->BindAxis("MoveForward", this, &ASkald_PlayerCharacter::MoveForward);
        PlayerInputComponent->BindAxis("MoveRight", this, &ASkald_PlayerCharacter::MoveRight);
       PlayerInputComponent->BindAxis("MoveUp", this, &ASkald_PlayerCharacter::MoveUp);
        PlayerInputComponent->BindAxis("Turn", this, &ASkald_PlayerCharacter::Turn);
        PlayerInputComponent->BindAxis("LookUp", this, &ASkald_PlayerCharacter::LookUp);

        PlayerInputComponent->BindAction("Select", IE_Pressed, this, &ASkald_PlayerCharacter::Select);
        PlayerInputComponent->BindAction("Ability1", IE_Pressed, this, &ASkald_PlayerCharacter::AbilityOne);
        PlayerInputComponent->BindAction("Ability2", IE_Pressed, this, &ASkald_PlayerCharacter::AbilityTwo);
        PlayerInputComponent->BindAction("Ability3", IE_Pressed, this, &ASkald_PlayerCharacter::AbilityThree);
}

void ASkald_PlayerCharacter::MoveForward(float Value)
{
        if (Value != 0.0f)
        {
                AddMovementInput(GetActorForwardVector(), Value);
        }
}

void ASkald_PlayerCharacter::MoveRight(float Value)
{
        if (Value != 0.0f)
        {
                AddMovementInput(GetActorRightVector(), Value);
        }
}

void ASkald_PlayerCharacter::MoveUp(float Value)
{
        if (Value != 0.0f)
        {
                // Move strictly along world Z to ensure SpaceBar and LeftControl
                // translate vertically regardless of character rotation
                AddMovementInput(FVector::UpVector, Value);
        }
}

void ASkald_PlayerCharacter::Turn(float Value)
{
        if (Value != 0.0f)
        {
                AddControllerYawInput(Value);
        }
}

void ASkald_PlayerCharacter::LookUp(float Value)
{
        if (Value != 0.0f)
        {
                AddControllerPitchInput(Value);
        }
}

void ASkald_PlayerCharacter::Select()
{
        APlayerController* PlayerController = Cast<APlayerController>(Controller);
        if (!PlayerController)
        {
                return;
        }

        FHitResult Hit;
        if (PlayerController->GetHitResultUnderCursor(ECC_Visibility, false, Hit))
        {
                if (ATerritory* Territory = Cast<ATerritory>(Hit.GetActor()))
                {
                        Territory->Select();
                        CurrentSelection = Territory;
                }
        }
}

void ASkald_PlayerCharacter::AbilityOne()
{
        if (CurrentSelection)
        {
                UE_LOG(LogSkald, Log, TEXT("%s used Ability One on %s"), *GetName(), *CurrentSelection->GetName());
        }
}

void ASkald_PlayerCharacter::AbilityTwo()
{
        if (CurrentSelection)
        {
                UE_LOG(LogSkald, Log, TEXT("%s used Ability Two"), *GetName());
        }
}

void ASkald_PlayerCharacter::AbilityThree()
{
        UE_LOG(LogSkald, Log, TEXT("%s used Ability Three"), *GetName());
}

