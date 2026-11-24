#include "PlumPoleActor.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

APlumPoleActor::APlumPoleActor()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

    Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("PoleCollision"));
    Collision->SetupAttachment(RootComponent);
    Collision->SetCollisionProfileName(TEXT("BlockAll"));
    Collision->SetGenerateOverlapEvents(false);

    PoleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PoleMesh"));
    PoleMesh->SetupAttachment(RootComponent);
    PoleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    CapMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TopMesh"));
    CapMesh->SetupAttachment(RootComponent);
    CapMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    PoleMesh->SetStaticMesh(Cylinder.Object);
    CapMesh->SetStaticMesh(Cylinder.Object);
}

void APlumPoleActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    const float CapHeight = 12.0f;
    const float ShaftHeight = Height - CapHeight;
    Collision->SetBoxExtent(FVector(Radius * 0.72f, Radius * 0.72f, Height * 0.5f));
    Collision->SetRelativeLocation(FVector(0.0f, 0.0f, Height * 0.5f));
    PoleMesh->SetRelativeLocation(FVector(0.0f, 0.0f, ShaftHeight * 0.5f));
    PoleMesh->SetRelativeScale3D(FVector(Radius / 60.0f, Radius / 60.0f, ShaftHeight / 100.0f));
    CapMesh->SetRelativeLocation(FVector(0.0f, 0.0f, Height - CapHeight * 0.5f));
    CapMesh->SetRelativeScale3D(FVector(Radius / 50.0f, Radius / 50.0f, CapHeight / 100.0f));
}

FVector APlumPoleActor::GetLandingLocation() const
{
    return GetActorTransform().TransformPosition(FVector(0.0f, 0.0f, Height));
}

bool APlumPoleActor::IsAvailableFor(const AActor* Traverser) const
{
    return bTraversalEnabled && (!Occupant.IsValid() || Occupant.Get() == Traverser);
}

bool APlumPoleActor::TryReserve(AActor* Traverser)
{
    if (!IsValid(Traverser) || !IsAvailableFor(Traverser))
    {
        return false;
    }
    Occupant = Traverser;
    return true;
}

void APlumPoleActor::Release(AActor* Traverser)
{
    if (Occupant.Get() == Traverser)
    {
        Occupant.Reset();
    }
}
