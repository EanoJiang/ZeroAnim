#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlumPoleActor.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class ZEROANIM_API APlumPoleActor : public AActor
{
    GENERATED_BODY()

public:
    APlumPoleActor();

    virtual void OnConstruction(const FTransform& Transform) override;

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    FVector GetLandingLocation() const;

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    float GetApproachRadius() const { return ApproachRadius; }

    /** A character reserves the destination during its approach and holds it while perched. */
    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsAvailableFor(const AActor* Traverser) const;

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    AActor* GetOccupant() const { return Occupant.Get(); }

    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    bool TryReserve(AActor* Traverser);

    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    void Release(AActor* Traverser);

private:
    UPROPERTY(VisibleAnywhere, Category = "Plum Pole")
    TObjectPtr<UBoxComponent> Collision;

    UPROPERTY(VisibleAnywhere, Category = "Plum Pole")
    TObjectPtr<UStaticMeshComponent> PoleMesh;

    UPROPERTY(VisibleAnywhere, Category = "Plum Pole")
    TObjectPtr<UStaticMeshComponent> CapMesh;

    UPROPERTY(EditAnywhere, Category = "Plum Pole", meta = (ClampMin = "20"))
    float Height = 145.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole", meta = (ClampMin = "10"))
    float Radius = 34.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole", meta = (ClampMin = "0"))
    float ApproachRadius = 340.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plum Pole", meta = (AllowPrivateAccess = "true"))
    bool bTraversalEnabled = true;

    TWeakObjectPtr<AActor> Occupant;
};
