#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlumPoleProtectVolume.generated.h"

class UBoxComponent;
class UPrimitiveComponent;
class UPlumPoleTraversalComponent;

/**
 * Trigger volume analogous to PBZ BP_PoleJumpOutProtectVolume.
 * While overlapped, increments the ref-counted LionDancingPoleProtect state on UPlumPoleTraversalComponent,
 * preventing active IA_Jump (JumpOff) dismounts over hazards such as deep water.
 */
UCLASS(Blueprintable)
class ZEROANIM_API APlumPoleProtectVolume : public AActor
{
    GENERATED_BODY()

public:
    APlumPoleProtectVolume();

    virtual void OnConstruction(const FTransform& Transform) override;

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    UBoxComponent* GetProtectBounds() const { return ProtectBounds.Get(); }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void OnBoundsBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    UFUNCTION()
    void OnBoundsEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

    void RegisterActorOverlap(AActor* OtherActor);
    void UnregisterActorOverlap(AActor* OtherActor);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Plum Pole", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UBoxComponent> ProtectBounds;

    /** PBZ BP_PoleJumpOutProtectVolume BorderHeight = 300.0 cm -> half-height 150 cm. */
    UPROPERTY(EditAnywhere, Category = "Plum Pole", meta = (ClampMin = "10"))
    FVector BoxExtent = FVector(200.0f, 200.0f, 150.0f);

    TSet<TWeakObjectPtr<UPlumPoleTraversalComponent>> ActiveOverlaps;
};
