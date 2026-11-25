#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlumPoleActor.generated.h"

class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class ZEROANIM_API APlumPoleActor : public AActor
{
    GENERATED_BODY()

public:
    static constexpr float ExitStandPointOffsetZ = 0.1268f;

    APlumPoleActor();

    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    FVector GetLandingLocation() const;

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    USceneComponent* GetStandPoint() const { return StandPoint.Get(); }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    USceneComponent* GetTopHeight() const { return TopHeight.Get(); }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    float GetRadius() const { return Radius; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    float GetHeight() const { return Height; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    float GetApproachRadius() const { return ApproachRadius; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsActivated() const { return bActivated && bTraversalEnabled; }

    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    void SetActivated(bool bInActivated);

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool CanPlayerJump() const { return bPlayerCanJump; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool CanAllowJumpOut() const { return bAllowJumpOut; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsExitPole() const { return bIsExitPole; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsOccupied() const { return Occupant.IsValid(); }

    /** True when this pole can be selected by a new traversal query (PBZ FilterOccupiedPoles + Activated + PlayerCanJump). */
    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsSelectableForTraversal() const;

    /** A character reserves the destination during its approach and holds it while perched. */
    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsAvailableFor(const AActor* Traverser) const;

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    AActor* GetOccupant() const { return Occupant.Get(); }

    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    bool TryReserve(AActor* Traverser);

    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    void Release(AActor* Traverser);

    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    void ForceRelease();

private:
    void ApplyPoleConfiguration();

    UPROPERTY(VisibleAnywhere, Category = "Plum Pole")
    TObjectPtr<UBoxComponent> Collision;

    UPROPERTY(VisibleAnywhere, Category = "Plum Pole")
    TObjectPtr<UStaticMeshComponent> PoleMesh;

    UPROPERTY(VisibleAnywhere, Category = "Plum Pole")
    TObjectPtr<UStaticMeshComponent> CapMesh;

    /** Warp target anchor at pole crown (normal pole) or ground +0.1268 cm (exit pole). Tagged "WarpTarget". */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Plum Pole", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USceneComponent> StandPoint;

    /** Top height marker attached under Root, matching PBZ BP_LionDancing_Pole_Base SCS. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Plum Pole", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USceneComponent> TopHeight;

    UPROPERTY(EditAnywhere, Category = "Plum Pole", meta = (ClampMin = "20"))
    float Height = 145.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole", meta = (ClampMin = "10"))
    float Radius = 34.0f;

    /** Legacy per-pole range hint (kept at PBZ ground-entry 500 cm default). */
    UPROPERTY(EditAnywhere, Category = "Plum Pole", meta = (ClampMin = "0"))
    float ApproachRadius = 500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plum Pole|PBZ", meta = (AllowPrivateAccess = "true"))
    bool bActivated = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plum Pole|PBZ", meta = (AllowPrivateAccess = "true"))
    bool bPlayerCanJump = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plum Pole|PBZ", meta = (AllowPrivateAccess = "true"))
    bool bAllowJumpOut = true;

    /** Mirrors BP_LionDancing_Pole_Exit_C: hidden in game, no blocking collision, StandPoint flush at ground +0.1268 cm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plum Pole|PBZ", meta = (AllowPrivateAccess = "true"))
    bool bIsExitPole = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plum Pole", meta = (AllowPrivateAccess = "true"))
    bool bTraversalEnabled = true;

    TWeakObjectPtr<AActor> Occupant;
};
