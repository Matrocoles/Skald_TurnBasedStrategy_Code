#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DiceRollConfig.h"
#include "SkaldDiceD6.generated.h"

class UStaticMeshComponent;
class UAudioComponent;
class UMaterialInstanceDynamic;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSkaldDiceSettled, ASkaldDiceD6*, Dice, int32, FaceValue);

UCLASS()
class SKALDDICE_API ASkaldDiceD6 : public AActor
{
    GENERATED_BODY()

public:
    ASkaldDiceD6();

    virtual void Tick(float DeltaSeconds) override;
    virtual void BeginPlay() override;

    void InitialiseFromConfig(const UDiceRollConfig* InConfig);
    void SetOwningRollId(const FGuid& InRollId) { OwningRollId = InRollId; }
    FGuid GetOwningRollId() const { return OwningRollId; }
    void SetDesiredFaceValue(int32 InDesiredValue);
    void ApplyTint(const FLinearColor& Tint, FName TintParameterName);
    void LaunchDie(const FVector& Position, const FRotator& Rotation, const FVector& LinearImpulse, const FVector& AngularImpulse);
    void ForceFaceValue(int32 TargetValue, bool bBroadcastResult = true);
    int32 SampleFaceValue() const;

    bool HasSettled() const { return bSettled; }
    int32 GetResolvedValue() const { return ResolvedValue; }

    UPROPERTY(BlueprintAssignable, Category = "Dice")
    FOnSkaldDiceSettled OnDiceSettled;

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dice")
    TObjectPtr<UStaticMeshComponent> DiceMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dice")
    TObjectPtr<UAudioComponent> ImpactAudio;

    UPROPERTY(EditAnywhere, Category = "Dice")
    float SettleTimer = 0.f;

    bool bSettled = false;
    int32 ResolvedValue = 1;
    int32 DesiredValue = INDEX_NONE;
    bool bBroadcastSettlement = false;

    UPROPERTY()
    const UDiceRollConfig* CachedConfig;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterials;

    FGuid OwningRollId;

private:
    void UpdateSettleState(float DeltaSeconds);
    int32 ResolveFaceValue() const;
    FVector GetFaceWorldNormal(const FSkaldDiceFaceMapping& Mapping) const;
    void EnsureDynamicMaterialsCreated();
    void ValidateFaceMappings();

    bool bHasValidatedFaceMappings = false;
};
