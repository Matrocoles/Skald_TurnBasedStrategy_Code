#include "FighterPawn.h"
#include "Blueprint/UserWidget.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Engine/CollisionProfile.h"
#include "EngineUtils.h"
#include "GridBattleManager.h"
#include "GridOverlayComponent.h"
#include "Net/UnrealNetwork.h"
#include "Skald_GameInstance.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr int32 ActionsPerActivation = 2;
}

AFighterPawn::AFighterPawn() {
  PrimaryActorTick.bCanEverTick = false;

  bReplicates = true;
  SetReplicateMovement(true);

  CollisionComponent =
      CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionComponent"));
  CollisionComponent->InitCapsuleSize(40.f, 88.f);
  CollisionComponent->SetCollisionProfileName(
      UCollisionProfile::Pawn_ProfileName);
  CollisionComponent->SetCanEverAffectNavigation(false);
  RootComponent = CollisionComponent;

  DisplayMesh =
      CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayMesh"));
  DisplayMesh->SetupAttachment(CollisionComponent);
  DisplayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

  HealthWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthWidget"));
  HealthWidget->SetupAttachment(DisplayMesh);
  HealthWidget->SetTwoSided(true);
  HealthWidget->SetRelativeLocation(FVector(0.f, 0.f, 400.f));
  HealthWidget->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
  HealthWidget->SetRelativeScale3D(FVector(1.f));

  static ConstructorHelpers::FClassFinder<UUserWidget> HealthWidgetFinder(
      TEXT("/Game/Blueprints/UI/WBP_FighterHealth"));
  if (HealthWidgetFinder.Succeeded()) {
    HealthWidgetTemplate = HealthWidgetFinder.Class;
  }

  static ConstructorHelpers::FClassFinder<UUserWidget> DamageWidgetFinder(
      TEXT("/Game/Blueprints/UI/WBP_DamageFloat"));
  if (DamageWidgetFinder.Succeeded()) {
    DamageFloatWidgetTemplate = DamageWidgetFinder.Class;
  }

  static ConstructorHelpers::FClassFinder<UUserWidget> MissWidgetFinder(
      TEXT("/Game/Blueprints/UI/WBP_Misses"));
  if (MissWidgetFinder.Succeeded()) {
    MissWidgetTemplate = MissWidgetFinder.Class;
  }

  ActionsRemaining = 0;
  bHasActivatedThisRound = false;
  bIsCurrentlyActive = false;
  CurrentCell = FIntPoint::ZeroValue;

  UpdateMeshOffset();
}

void AFighterPawn::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  DOREPLIFETIME(AFighterPawn, Stats);
  DOREPLIFETIME(AFighterPawn, bIsAttacker);
  DOREPLIFETIME(AFighterPawn, ActionsRemaining);
  DOREPLIFETIME(AFighterPawn, bHasActivatedThisRound);
  DOREPLIFETIME(AFighterPawn, bIsCurrentlyActive);
  DOREPLIFETIME(AFighterPawn, CurrentCell);
}

void AFighterPawn::OnConstruction(const FTransform &Transform) {
  Super::OnConstruction(Transform);
  UpdateMeshOffset();
}

void AFighterPawn::BeginPlay() {
  Super::BeginPlay();

  if (HealthWidget && HealthWidgetTemplate) {
    HealthWidget->SetWidgetClass(HealthWidgetTemplate);
  }

  OnHealthChanged.AddDynamic(this, &AFighterPawn::UpdateHealthDisplay);
  OnHealthChanged.Broadcast(Stats.Health);

  BroadcastActionsRemaining();

  UpdateMeshOffset();

  if (UGridOverlayComponent *Grid = GetGrid()) {
    CurrentCell = Grid->WorldToGrid(GetActorLocation());
    FVector AlignedLocation = Grid->GridToWorld(CurrentCell);
    AlignedLocation.Z += GetSimpleCollisionHalfHeight();
    SetActorLocation(AlignedLocation);
    Grid->SetOccupied(CurrentCell, true);
  }

  if (UWorld *World = GetWorld()) {
    if (USkaldGameInstance *GI =
            Cast<USkaldGameInstance>(World->GetGameInstance())) {
      if (GI->GridBattleManager) {
        GI->GridBattleManager->RegisterFighter(this, bIsAttacker);
      }
    }
  }
}

void AFighterPawn::BeginActivation() {
  if (!IsAlive()) {
    return;
  }

  ActionsRemaining = ActionsPerActivation;
  bIsCurrentlyActive = true;
  bHasActivatedThisRound = true;

  BroadcastActionsRemaining();
}

void AFighterPawn::ResetActivationState() {
  ActionsRemaining = 0;
  bHasActivatedThisRound = false;
  bIsCurrentlyActive = false;

  BroadcastActionsRemaining();
}

void AFighterPawn::FinishActivation() {
  ActionsRemaining = 0;
  bIsCurrentlyActive = false;

  BroadcastActionsRemaining();
}

UGridOverlayComponent *AFighterPawn::GetGrid() {
  if (!CachedGrid) {
    if (UWorld *World = GetWorld()) {
      for (TActorIterator<AActor> It(World); It; ++It) {
        CachedGrid = It->FindComponentByClass<UGridOverlayComponent>();
        if (CachedGrid) {
          break;
        }
      }
    }
  }
  return CachedGrid;
}

FIntPoint AFighterPawn::GetCurrentCell() const { return CurrentCell; }

UUserWidget *AFighterPawn::GetDamageWidgetFromPool() {
  for (UUserWidget *Widget : DamageWidgetPool) {
    if (Widget && !Widget->IsInViewport()) {
      return Widget;
    }
  }
  if (DamageFloatWidgetTemplate) {
    if (UWorld *World = GetWorld()) {
      if (UUserWidget *NewWidget =
              CreateWidget<UUserWidget>(World, DamageFloatWidgetTemplate)) {
        DamageWidgetPool.Add(NewWidget);
        return NewWidget;
      }
    }
  }
  return nullptr;
}

UUserWidget *AFighterPawn::GetMissWidgetFromPool() {
  for (UUserWidget *Widget : MissWidgetPool) {
    if (Widget && !Widget->IsInViewport()) {
      return Widget;
    }
  }
  if (MissWidgetTemplate) {
    if (UWorld *World = GetWorld()) {
      if (UUserWidget *NewWidget =
              CreateWidget<UUserWidget>(World, MissWidgetTemplate)) {
        MissWidgetPool.Add(NewWidget);
        return NewWidget;
      }
    }
  }
  return nullptr;
}

void AFighterPawn::MoveToCell(FIntPoint TargetCell) {
  if (!bIsCurrentlyActive || ActionsRemaining <= 0) {
    return;
  }
  const int32 Distance = FMath::Abs(TargetCell.X - CurrentCell.X) +
                         FMath::Abs(TargetCell.Y - CurrentCell.Y);
  if (Distance > Stats.Movement) {
    return;
  }
  UGridOverlayComponent *Grid = GetGrid();
  if (Grid && Grid->IsObscured(TargetCell)) {
    return;
  }

  if (Grid) {
    Grid->SetOccupied(CurrentCell, false);
  }

  CurrentCell = TargetCell;
  FVector NewLocation = GetActorLocation();
  if (Grid) {
    NewLocation = Grid->GridToWorld(TargetCell);
    NewLocation.Z += GetSimpleCollisionHalfHeight();
  }
  FaceTowardsLocation(NewLocation);
  SetActorLocation(NewLocation);
  ActionsRemaining = FMath::Max(0, ActionsRemaining - 1);

  BroadcastActionsRemaining();

  if (Grid) {
    Grid->SetOccupied(TargetCell, true);
    Grid->ClearHighlights();
  }
}

void AFighterPawn::PerformAttack(AFighterPawn *Target) {
  if (!bIsCurrentlyActive || ActionsRemaining <= 0 || !Target ||
      !Target->IsAlive()) {
    return;
  }

  if (PendingAttackTarget.IsValid() || PendingAttackRolls.Num() > 0) {
    if (UWorld *World = GetWorld()) {
      if (USkaldGameInstance *GameInstance =
              Cast<USkaldGameInstance>(World->GetGameInstance())) {
        if (UGridBattleManager *BattleManager =
                GameInstance->GridBattleManager) {
          BattleManager->ReportAttackRejected(
              this, Target,
              NSLOCTEXT("SkaldBattle", "AttackInProgress", "Attack in progress."));
        }
      }
    }
    return;
  }

  const int32 Distance = FMath::Abs(Target->CurrentCell.X - CurrentCell.X) +
                         FMath::Abs(Target->CurrentCell.Y - CurrentCell.Y);
  if (Distance > Stats.AttackRange) {
    return;
  }

  UGridOverlayComponent *Grid = GetGrid();
  if (Grid && !Grid->HasLineOfSight(CurrentCell, Target->CurrentCell)) {
    return;
  }

  FRandomStream *RandomStream = nullptr;
  if (UWorld *World = GetWorld()) {
    if (USkaldGameInstance *GameInstance =
            Cast<USkaldGameInstance>(World->GetGameInstance())) {
      RandomStream = &GameInstance->CombatRandomStream;
    }
  }
  if (!RandomStream) {
    return;
  }

  const int32 RequiredRoll =
      Stats.Strength > Target->Stats.Defence
          ? 3
          : (Stats.Strength < Target->Stats.Defence ? 5 : 4);

  TArray<FQueuedAttackRoll> QueuedRolls;
  QueuedRolls.Reserve(Stats.AttackDice);

  int32 SimulatedHealth = Target->Stats.Health;
  for (int32 i = 0; i < Stats.AttackDice && SimulatedHealth > 0; ++i) {
    const int32 Roll = RandomStream->RandRange(1, 6);
    int32 DamageThisDie = 0;

    if (Roll == 6) {
      DamageThisDie = Stats.AttackDamage + Stats.CriticalBonusDamage; // crit
    } else if (Roll >= RequiredRoll) {
      DamageThisDie = Stats.AttackDamage;
    }

    if (DamageThisDie > 0) {
      SimulatedHealth = FMath::Max(0, SimulatedHealth - DamageThisDie);
    }

    FQueuedAttackRoll &NewRoll = QueuedRolls.Emplace_GetRef();
    NewRoll.RollValue = Roll;
    NewRoll.Damage = DamageThisDie;
    NewRoll.bHit = DamageThisDie > 0;
  }

  StartQueuedAttack(Target, MoveTemp(QueuedRolls));

  ActionsRemaining = FMath::Max(0, ActionsRemaining - 1);

  BroadcastActionsRemaining();

  FaceTowardsLocation(Target->GetActorLocation());

  if (Grid) {
    Grid->ClearHighlights();
  }
}

void AFighterPawn::StartQueuedAttack(AFighterPawn *Target,
                                     TArray<FQueuedAttackRoll> &&Rolls) {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AttackRollTimerHandle);
  }

  PendingAttackRolls = MoveTemp(Rolls);
  PendingAttackRollIndex = 0;
  PendingAttackTarget = Target;
  bPendingAttackTargetDied = false;
  bHasProcessedPendingRoll = false;

  if (PendingAttackRolls.Num() == 0) {
    FinalizeQueuedAttack();
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().SetTimer(AttackRollTimerHandle, this,
                                      &AFighterPawn::ResolveNextAttackRoll, 1.f,
                                      false);
  } else {
    ResolveNextAttackRoll();
  }
}

void AFighterPawn::ResolveNextAttackRoll() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AttackRollTimerHandle);
  }

  if (!PendingAttackRolls.IsValidIndex(PendingAttackRollIndex)) {
    FinalizeQueuedAttack();
    return;
  }

  AFighterPawn *Target = PendingAttackTarget.Get();
  if (!Target) {
    FinalizeQueuedAttack();
    return;
  }

  const FQueuedAttackRoll Roll = PendingAttackRolls[PendingAttackRollIndex];
  bHasProcessedPendingRoll = true;

  if (USkaldGameInstance *GI = Cast<USkaldGameInstance>(GetGameInstance())) {
    if (UGridBattleManager *BattleManager = GI->GridBattleManager) {
      BattleManager->ReportAttackRoll(this, Target, Roll.RollValue, Roll.bHit,
                                      Roll.Damage);
    }
  }

  if (Roll.bHit) {
    Target->Stats.Health =
        FMath::Max(0, Target->Stats.Health - Roll.Damage);
    if (Target->Stats.Health <= 0) {
      bPendingAttackTargetDied = true;
    }

    if (UUserWidget *DamageWidget = GetDamageWidgetFromPool()) {
      if (UTextBlock *Text = Cast<UTextBlock>(
              DamageWidget->GetWidgetFromName(TEXT("DamageText")))) {
        Text->SetText(FText::AsNumber(Roll.Damage));
      }
      DamageWidget->AddToViewport();
      if (UWorld *WorldPtr = GetWorld()) {
        FTimerHandle Timer;
        WorldPtr->GetTimerManager().SetTimer(
            Timer, FTimerDelegate::CreateLambda([DamageWidget]() {
              DamageWidget->RemoveFromParent();
            }),
            1.f, false);
      }
    }
  } else {
    if (UUserWidget *MissWidget = GetMissWidgetFromPool()) {
      if (UTextBlock *MissText =
              Cast<UTextBlock>(MissWidget->GetWidgetFromName(TEXT("Missed")))) {
        MissText->SetText(NSLOCTEXT("Skald", "BattleAttackMiss", "Missed"));
      }
      MissWidget->AddToViewport();
      if (UWorld *WorldPtr = GetWorld()) {
        FTimerHandle Timer;
        WorldPtr->GetTimerManager().SetTimer(
            Timer, FTimerDelegate::CreateLambda([MissWidget]() {
              MissWidget->RemoveFromParent();
            }),
            1.f, false);
      }
    }
  }

  Target->OnHealthChanged.Broadcast(Target->Stats.Health);

  ++PendingAttackRollIndex;

  const bool bHasMoreRolls =
      PendingAttackRolls.IsValidIndex(PendingAttackRollIndex);
  const bool bTargetAlive = Target->IsAlive();

  if (bHasMoreRolls && bTargetAlive) {
    if (UWorld *WorldPtr = GetWorld()) {
      WorldPtr->GetTimerManager().SetTimer(
          AttackRollTimerHandle, this, &AFighterPawn::ResolveNextAttackRoll,
          1.f, false);
    } else {
      ResolveNextAttackRoll();
    }
  } else {
    FinalizeQueuedAttack();
  }
}

void AFighterPawn::FinalizeQueuedAttack() {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AttackRollTimerHandle);
  }

  AFighterPawn *Target = PendingAttackTarget.Get();
  if (Target) {
    if (!bPendingAttackTargetDied && !bHasProcessedPendingRoll) {
      Target->OnHealthChanged.Broadcast(Target->Stats.Health);
    }
    if (!Target->IsAlive() && !Target->IsActorBeingDestroyed()) {
      Target->Destroy();
    }
  }

  PendingAttackRolls.Reset();
  PendingAttackRollIndex = 0;
  PendingAttackTarget = nullptr;
  bPendingAttackTargetDied = false;
  bHasProcessedPendingRoll = false;
}

void AFighterPawn::UpdateMeshOffset() {
  if (DisplayMesh && CollisionComponent) {
    const float HalfHeight = CollisionComponent->GetUnscaledCapsuleHalfHeight();
    DisplayMesh->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight));
  }
}

bool AFighterPawn::IsAlive() const { return Stats.Health > 0; }

void AFighterPawn::UpdateHealthDisplay(int32 NewHealth) {
  if (!HealthWidget) {
    return;
  }
  if (UUserWidget *Widget = HealthWidget->GetUserWidgetObject()) {
    if (UTextBlock *Text =
            Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("HealthText")))) {
      Text->SetText(FText::AsNumber(NewHealth));
    }
  }
}

void AFighterPawn::Destroyed() {
  FinishActivation();
  if (UGridOverlayComponent *Grid = GetGrid()) {
    Grid->SetOccupied(CurrentCell, false);
  }
  if (UWorld *World = GetWorld()) {
    if (USkaldGameInstance *GI =
            Cast<USkaldGameInstance>(World->GetGameInstance())) {
      if (GI->GridBattleManager) {
        GI->GridBattleManager->UnregisterFighter(this);
      }
    }
  }
  CurrentCell = FIntPoint::ZeroValue;
  Super::Destroyed();
}

void AFighterPawn::OnRep_Stats(const FFighterStats &OldStats) {
  if (Stats.Health != OldStats.Health) {
    const bool bHadListeners = OnHealthChanged.IsBound();
    OnHealthChanged.Broadcast(Stats.Health);
    if (!bHadListeners) {
      UpdateHealthDisplay(Stats.Health);
    }
  } else if (!OnHealthChanged.IsBound()) {
    UpdateHealthDisplay(Stats.Health);
  }
}

void AFighterPawn::OnRep_ActionsRemaining() { BroadcastActionsRemaining(); }

void AFighterPawn::BroadcastActionsRemaining() {
  OnActionsChanged.Broadcast(ActionsRemaining);
}

void AFighterPawn::FaceTowardsLocation(const FVector &TargetLocation) {
  FVector Direction = TargetLocation - GetActorLocation();
  Direction.Z = 0.f;
  if (!Direction.IsNearlyZero()) {
    const FRotator LookRotation = Direction.Rotation();
    SetActorRotation(FRotator(0.f, LookRotation.Yaw, 0.f));
  }
}
