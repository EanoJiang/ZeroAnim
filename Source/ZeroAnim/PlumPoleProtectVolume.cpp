#include "PlumPoleProtectVolume.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "PlumPoleTraversalComponent.h"

namespace
{
bool IsPrimaryCharacterCapsule(const AActor* OtherActor, const UPrimitiveComponent* OtherComp)
{
    if (!IsValid(OtherActor) || !IsValid(OtherComp))
    {
        return false;
    }
    if (const ACharacter* Character = Cast<ACharacter>(OtherActor))
    {
        return OtherComp == Character->GetCapsuleComponent();
    }
    return OtherComp == OtherActor->GetRootComponent();
}
} // namespace

APlumPoleProtectVolume::APlumPoleProtectVolume()
{
    PrimaryActorTick.bCanEverTick = false;

    ProtectBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("ProtectBounds"));
    RootComponent = ProtectBounds;
    ProtectBounds->SetBoxExtent(BoxExtent);
    ProtectBounds->SetCollisionProfileName(TEXT("Trigger"));
    ProtectBounds->SetGenerateOverlapEvents(true);
    ProtectBounds->ShapeColor = FColor(255, 40, 40, 255);
}

void APlumPoleProtectVolume::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if (ProtectBounds)
    {
        ProtectBounds->SetBoxExtent(BoxExtent);
    }
}

void APlumPoleProtectVolume::BeginPlay()
{
    Super::BeginPlay();

    if (ProtectBounds)
    {
        ProtectBounds->SetBoxExtent(BoxExtent);
        ProtectBounds->OnComponentBeginOverlap.AddDynamic(this, &APlumPoleProtectVolume::OnBoundsBeginOverlap);
        ProtectBounds->OnComponentEndOverlap.AddDynamic(this, &APlumPoleProtectVolume::OnBoundsEndOverlap);

        TArray<UPrimitiveComponent*> InitialOverlaps;
        ProtectBounds->GetOverlappingComponents(InitialOverlaps);
        for (UPrimitiveComponent* Comp : InitialOverlaps)
        {
            if (Comp && IsPrimaryCharacterCapsule(Comp->GetOwner(), Comp))
            {
                RegisterActorOverlap(Comp->GetOwner());
            }
        }
    }
}

void APlumPoleProtectVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    for (const TWeakObjectPtr<UPlumPoleTraversalComponent>& WeakTraversal : ActiveOverlaps)
    {
        if (UPlumPoleTraversalComponent* Traversal = WeakTraversal.Get())
        {
            Traversal->RemoveJumpOutProtectOverlap(this);
        }
    }
    ActiveOverlaps.Reset();
    Super::EndPlay(EndPlayReason);
}

void APlumPoleProtectVolume::OnBoundsBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    if (!IsPrimaryCharacterCapsule(OtherActor, OtherComp))
    {
        return;
    }
    RegisterActorOverlap(OtherActor);
}

void APlumPoleProtectVolume::OnBoundsEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
    if (!IsPrimaryCharacterCapsule(OtherActor, OtherComp))
    {
        return;
    }
    UnregisterActorOverlap(OtherActor);
}

void APlumPoleProtectVolume::RegisterActorOverlap(AActor* OtherActor)
{
    if (!IsValid(OtherActor))
    {
        return;
    }
    if (UPlumPoleTraversalComponent* Traversal = OtherActor->FindComponentByClass<UPlumPoleTraversalComponent>())
    {
        if (!ActiveOverlaps.Contains(Traversal))
        {
            ActiveOverlaps.Add(Traversal);
            Traversal->AddJumpOutProtectOverlap(this);
        }
    }
}

void APlumPoleProtectVolume::UnregisterActorOverlap(AActor* OtherActor)
{
    if (!IsValid(OtherActor))
    {
        return;
    }
    if (UPlumPoleTraversalComponent* Traversal = OtherActor->FindComponentByClass<UPlumPoleTraversalComponent>())
    {
        if (ActiveOverlaps.Remove(Traversal) > 0)
        {
            Traversal->RemoveJumpOutProtectOverlap(this);
        }
    }
}
