#include "PlumPoleActor.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
const FName WarpTargetComponentTag(TEXT("WarpTarget"));
}

APlumPoleActor::APlumPoleActor()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

    Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("PoleCollision"));
    Collision->SetupAttachment(RootComponent);
    // Use ECC_WorldDynamic object type so PBZ GAA_MoveMode (IgnoreChannels = [ECC_WorldDynamic])
    // lets the character fly cleanly over pole rims while blocking ground walking and standing on crowns.
    Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    Collision->SetGenerateOverlapEvents(false);

    PoleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PoleMesh"));
    PoleMesh->SetupAttachment(RootComponent);
    PoleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    CapMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TopMesh"));
    CapMesh->SetupAttachment(RootComponent);
    CapMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CapMesh->SetVisibility(false);
    CapMesh->SetHiddenInGame(true);

    StandPoint = CreateDefaultSubobject<USceneComponent>(TEXT("StandPoint"));
    StandPoint->SetupAttachment(RootComponent);
    StandPoint->ComponentTags.AddUnique(WarpTargetComponentTag);

    TopHeight = CreateDefaultSubobject<USceneComponent>(TEXT("TopHeight"));
    TopHeight->SetupAttachment(RootComponent);

    // GASP SM_Cylinder has bottom-center pivot (0, 0, 0), Radius = 50 cm, Height = 100 cm.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> GaspCylinder(
        TEXT("/Game/Levels/LevelPrototyping/Meshes/SM_Cylinder.SM_Cylinder"));
    if (GaspCylinder.Succeeded())
    {
        PoleMesh->SetStaticMesh(GaspCylinder.Object);
    }

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> GaspObstacleMat(
        TEXT("/Game/PlumPole/Materials/MI_PlumPole_GASP_Obstacle.MI_PlumPole_GASP_Obstacle"));
    if (GaspObstacleMat.Succeeded())
    {
        PoleMesh->SetMaterial(0, GaspObstacleMat.Object);
    }
    else
    {
        static ConstructorHelpers::FObjectFinder<UMaterialInterface> GaspGridGray(
            TEXT("/Game/Levels/LevelPrototyping/Materials/MI_Grid_Gray.MI_Grid_Gray"));
        if (GaspGridGray.Succeeded())
        {
            PoleMesh->SetMaterial(0, GaspGridGray.Object);
        }
    }
}

void APlumPoleActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    ApplyPoleConfiguration();
}

void APlumPoleActor::BeginPlay()
{
    Super::BeginPlay();
    ApplyPoleConfiguration();
}

void APlumPoleActor::ApplyPoleConfiguration()
{
    if (StandPoint)
    {
        StandPoint->ComponentTags.AddUnique(WarpTargetComponentTag);
    }

    if (bIsExitPole)
    {
        SetActorHiddenInGame(true);
        if (Collision)
        {
            Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Collision->SetBoxExtent(FVector(Radius * 0.5f, Radius * 0.5f, 10.0f));
            Collision->SetRelativeLocation(FVector(0.0f, 0.0f, 10.0f));
        }
        if (PoleMesh)
        {
            PoleMesh->SetVisibility(false);
            PoleMesh->SetHiddenInGame(true);
        }
        if (CapMesh)
        {
            CapMesh->SetVisibility(false);
            CapMesh->SetHiddenInGame(true);
        }
        if (StandPoint)
        {
            StandPoint->SetRelativeLocation(FVector(0.0f, 0.0f, ExitStandPointOffsetZ));
        }
        if (TopHeight)
        {
            TopHeight->SetRelativeLocation(FVector(0.0f, 0.0f, ExitStandPointOffsetZ));
        }
        return;
    }

    const bool bActive = IsActivated();
    SetActorHiddenInGame(!bActive);

    if (Collision)
    {
        Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
        Collision->SetCollisionEnabled(bActive ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
        Collision->SetBoxExtent(FVector(Radius * 0.72f, Radius * 0.72f, Height * 0.5f));
        Collision->SetRelativeLocation(FVector(0.0f, 0.0f, Height * 0.5f));
    }
    if (PoleMesh)
    {
        // GASP SM_Cylinder: bottom-center pivot (0, 0, 0), R = 50 cm, H = 100 cm.
        // Base sits flush at Z = 0, flat top crown sits exactly at Z = Height = StandPoint.Z.
        PoleMesh->SetVisibility(true);
        PoleMesh->SetHiddenInGame(!bActive);
        PoleMesh->SetRelativeLocation(FVector::ZeroVector);
        PoleMesh->SetRelativeScale3D(FVector(Radius / 50.0f, Radius / 50.0f, Height / 100.0f));
    }
    if (CapMesh)
    {
        CapMesh->SetVisibility(false);
        CapMesh->SetHiddenInGame(true);
    }
    if (StandPoint)
    {
        StandPoint->SetRelativeLocation(FVector(0.0f, 0.0f, Height));
    }
    if (TopHeight)
    {
        TopHeight->SetRelativeLocation(FVector(0.0f, 0.0f, Height));
    }
}

void APlumPoleActor::SetActivated(bool bInActivated)
{
    bActivated = bInActivated;
    ApplyPoleConfiguration();
}

FVector APlumPoleActor::GetLandingLocation() const
{
    if (StandPoint)
    {
        return StandPoint->GetComponentLocation();
    }
    const float OffsetZ = bIsExitPole ? ExitStandPointOffsetZ : Height;
    return GetActorTransform().TransformPosition(FVector(0.0f, 0.0f, OffsetZ));
}

bool APlumPoleActor::IsSelectableForTraversal() const
{
    return IsActivated() && bPlayerCanJump && !Occupant.IsValid();
}

bool APlumPoleActor::IsAvailableFor(const AActor* Traverser) const
{
    return IsActivated() && bPlayerCanJump && (!Occupant.IsValid() || Occupant.Get() == Traverser);
}

bool APlumPoleActor::TryReserve(AActor* Traverser)
{
    if (bIsExitPole || !IsValid(Traverser) || !IsAvailableFor(Traverser))
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

void APlumPoleActor::ForceRelease()
{
    Occupant.Reset();
}
