#include "PlumPoleTraversalComponent.h"

#include "Animation/AnimSequence.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PlumPoleActor.h"
#include "PlumPoleCharacter.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr float LandingClearance = 2.5f;
constexpr float AnchorTolerance = 6.0f;
constexpr int32 ArcSweepSegments = 24;
constexpr float DismountExitSlope = 0.30f;
const FName FlipForwardCurve(TEXT("PlumPoleSourceTravelForward"));
const FName FlipUpCurve(TEXT("PlumPoleSourceTravelUp"));

float GetFlightProgress(const FPlumPoleTraversalTiming& Timing, float Progress)
{
    return FMath::Clamp((Progress - Timing.TakeoffProgress) / (Timing.ContactProgress - Timing.TakeoffProgress), 0.0f, 1.0f);
}

float GetFlightBump(float Progress)
{
    return FMath::Square(4.0f * Progress * (1.0f - Progress));
}
}

UPlumPoleTraversalComponent::UPlumPoleTraversalComponent()
    : EnterTiming(0.11f, 0.76f, 0.89f)
    , HopTiming(0.343f, 0.56f, 0.58f)
    , ChainHopTiming(16.0f / 156.0f, 71.0f / 156.0f, 81.0f / 156.0f)
    , PoleFlipTiming(0.280954f / 1.983333f, 1.090758f / 1.983333f, 1.0f)
    , DismountTiming(0.10f, 0.52f, 0.76f)
    , ShortDismountTiming(0.28f, 0.76f, 0.94f)
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
    static ConstructorHelpers::FObjectFinder<UAnimSequence> Flip(TEXT("/Game/PlumPole/Animations/AS_Manny_PlumPole_PoleFlipFromRight.AS_Manny_PlumPole_PoleFlipFromRight"));
    PoleFlipAnimation = Flip.Object;
}

void UPlumPoleTraversalComponent::BeginPlay()
{
    Super::BeginPlay();

    Character = Cast<ACharacter>(GetOwner());
    if (Character)
    {
        CharacterMovement = Character->GetCharacterMovement();
    }
    if (CharacterMovement)
    {
        AddTickPrerequisiteComponent(CharacterMovement);
    }
    else
    {
        SetComponentTickEnabled(false);
    }
}

void UPlumPoleTraversalComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (CurrentPole.IsValid())
    {
        CurrentPole->Release(Character);
    }
    if (TargetPole.IsValid())
    {
        TargetPole->Release(Character);
    }
    if (EndPlayReason == EEndPlayReason::Destroyed && bMovementLocked && IsValid(Character) && !Character->IsActorBeingDestroyed())
    {
        ReturnToCharacterMovement();
    }
    Super::EndPlay(EndPlayReason);
}

void UPlumPoleTraversalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!IsValid(Character) || !IsValid(CharacterMovement))
    {
        return;
    }

    RememberMovementInput();

    if (bMovementLocked && CharacterMovement->MovementMode != MOVE_None)
    {
        CharacterMovement->DisableMovement();
    }

    if (bArcActive)
    {
        AdvanceArc(DeltaTime);
        return;
    }

    if (TraversalState == EPlumPoleTraversalState::Perched)
    {
        const bool bCanAutoChain = bJustLandedFromHop;
        bJustLandedFromHop = false;
        const APlumPoleActor* Pole = CurrentPole.Get();
        if (!IsReservedAnchor(Pole, Character->GetActorLocation()))
        {
            InterruptArc();
        }
        else if (GetWorld()->GetTimeSeconds() <= BufferedJumpExpires)
        {
            BufferedJumpExpires = -1.0;
            TryStartFromPerch(BufferedDirection, BufferedStyle, BufferedStyle != EArcStyle::PoleFlip);
        }
        else if (bAutoHopWithMovementInput)
        {
            const FVector Direction = GetAutoHopDirection();
            if (!Direction.IsNearlyZero())
            {
                TryStartFromPerch(Direction, bCanAutoChain ? EArcStyle::ChainHop : EArcStyle::Normal, false);
            }
        }
    }
    else if (TraversalState == EPlumPoleTraversalState::Dismounting && !CharacterMovement->IsFalling())
    {
        FinishDismount();
    }
}

bool UPlumPoleTraversalComponent::TryTraverse()
{
    return ApplyPoleInput(false);
}

bool UPlumPoleTraversalComponent::TryPoleFlip()
{
    return ApplyPoleInput(true);
}

bool UPlumPoleTraversalComponent::ApplyPoleInput(bool bFlipRequested)
{
    if (!IsValid(Character) || !IsValid(CharacterMovement))
    {
        return false;
    }
    const bool bCanChain = TraversalState == EPlumPoleTraversalState::Dismounting && !bMovementLocked &&
        CharacterMovement->IsMovingOnGround() && GetTraversalProgress() >= ActiveTiming.ChainProgress;
    const APlumPoleCharacter* Pawn = Cast<APlumPoleCharacter>(Character);
    const EPlumPoleInputResponse Response = Pawn ? Pawn->SelectPoleInputResponse(TraversalState, bFlipRequested, bCanChain) :
        GetDefault<APlumPoleCharacter>()->SelectPoleInputResponse(TraversalState, bFlipRequested, bCanChain);

    switch (Response)
    {
    case EPlumPoleInputResponse::Enter:
        return TraversalState == EPlumPoleTraversalState::Grounded && TryStartFromGround(GetTravelDirection());
    case EPlumPoleInputResponse::HopOrDismount:
    case EPlumPoleInputResponse::PoleFlip:
    {
        if (TraversalState != EPlumPoleTraversalState::Perched)
        {
            return false;
        }
        BufferedJumpExpires = -1.0;
        const bool bFlip = Response == EPlumPoleInputResponse::PoleFlip;
        const bool bStarted = TryStartFromPerch(GetTravelDirection(), bFlip ? EArcStyle::PoleFlip : EArcStyle::Normal);
        // A regular jump while balancing must not become a ground jump when no landing is reachable.
        return bStarted || !bFlip;
    }
    case EPlumPoleInputResponse::BufferRequest:
    {
        if ((TraversalState != EPlumPoleTraversalState::Entering && TraversalState != EPlumPoleTraversalState::Hopping) ||
            (bFlipRequested && !PoleFlipAnimation))
        {
            return false;
        }
        BufferedDirection = GetTravelDirection();
        const bool bUseChainHop = TraversalState == EPlumPoleTraversalState::Hopping && GetTraversalProgress() < 0.75f;
        BufferedStyle = bFlipRequested ? EArcStyle::PoleFlip :
            bUseChainHop ? EArcStyle::ChainHop : EArcStyle::Normal;
        // A second press during a hop stays queued until the landing can support another jump.
        const float TimeUntilChain = TraversalState == EPlumPoleTraversalState::Hopping
            ? TraversalDuration * ActiveTiming.ChainProgress - TraversalElapsed
            : 0.0f;
        BufferedJumpExpires = GetWorld()->GetTimeSeconds() + FMath::Max(TimeUntilChain, 0.0f) + JumpBufferSeconds;
        return true;
    }
    case EPlumPoleInputResponse::ChainFromGround:
        if (!bCanChain)
        {
            return false;
        }
        FinishDismount();
        return TryStartFromGround(GetTravelDirection());
    case EPlumPoleInputResponse::Consume:
        return true;
    case EPlumPoleInputResponse::Ignore:
        return false;
    }
    return false;
}

bool UPlumPoleTraversalComponent::TryStartFromGround(const FVector& Direction)
{
    if (!CharacterMovement->IsMovingOnGround())
    {
        return false;
    }

    FVector CarryVelocity;
    EArcStyle Style = EArcStyle::Normal;
    APlumPoleActor* Pole = FindPole(Direction, true, CarryVelocity, EArcStyle::Normal, false, Style);
    if (!Pole)
    {
        return false;
    }

    return StartArc(EPlumPoleTraversalState::Entering, GetPoleCapsuleLocation(*Pole), Pole, CarryVelocity);
}

bool UPlumPoleTraversalComponent::TryStartFromPerch(const FVector& Direction, EArcStyle RequestedStyle, bool bAllowDismount)
{
    if (!IsReservedAnchor(CurrentPole.Get(), Character->GetActorLocation()))
    {
        InterruptArc();
        return false;
    }
    const bool bCanFlip = PreparePoleFlip();
    FVector CarryVelocity;
    EArcStyle Style = RequestedStyle;
    if (APlumPoleActor* Pole = FindPole(Direction, false, CarryVelocity, RequestedStyle, bCanFlip, Style))
    {
        return StartArc(EPlumPoleTraversalState::Hopping, GetPoleCapsuleLocation(*Pole), Pole, CarryVelocity, Style);
    }

    FVector Landing;
    bool bShortExit = false;
    if (bAllowDismount && RequestedStyle != EArcStyle::PoleFlip && FindDismountLocation(Direction, Landing, bShortExit))
    {
        return StartArc(EPlumPoleTraversalState::Dismounting, Landing, nullptr, FVector::ZeroVector,
            bShortExit ? EArcStyle::ShortDismount : EArcStyle::Normal);
    }
    return false;
}

bool UPlumPoleTraversalComponent::PreparePoleFlip()
{
    ActiveFlipAnimation = PoleFlipAnimation;
    if (!ActiveFlipAnimation || !ActiveFlipAnimation->HasCurveData(FlipForwardCurve, false) ||
        !ActiveFlipAnimation->HasCurveData(FlipUpCurve, false))
    {
        return false;
    }
    const FPlumPoleTraversalTiming Timing = GetTiming(EPlumPoleTraversalState::Hopping, EArcStyle::PoleFlip);
    FlipReleaseTravel = SamplePoleFlipTravel(Timing.TakeoffProgress);
    FlipContactTravel = SamplePoleFlipTravel(Timing.ContactProgress);
    return !FMath::IsNearlyEqual(FlipContactTravel.X, FlipReleaseTravel.X);
}

FVector2D UPlumPoleTraversalComponent::SamplePoleFlipTravel(float Progress) const
{
    const FAnimExtractContext Context(static_cast<double>(Progress * ActiveFlipAnimation->GetPlayLength()));
    return FVector2D(ActiveFlipAnimation->EvaluateCurveData(FlipForwardCurve, Context),
        ActiveFlipAnimation->EvaluateCurveData(FlipUpCurve, Context));
}

void UPlumPoleTraversalComponent::SetTraversalInputDirection(FVector WorldDirection)
{
    ExplicitInputFrame = GFrameCounter;
    ExplicitInputMagnitude = WorldDirection.Size2D();
    ExplicitDirection = WorldDirection.GetSafeNormal2D();
    if (const UWorld* World = GetWorld())
    {
        ExplicitDirectionTime = World->GetTimeSeconds();
    }
}

APlumPoleActor* UPlumPoleTraversalComponent::GetCurrentPole() const
{
    return CurrentPole.Get();
}

APlumPoleActor* UPlumPoleTraversalComponent::GetTargetPole() const
{
    return TargetPole.Get();
}

float UPlumPoleTraversalComponent::GetTraversalProgress() const
{
    return TraversalDuration > 0.0f ? FMath::Clamp(TraversalElapsed / TraversalDuration, 0.0f, 1.0f) : 0.0f;
}

void UPlumPoleTraversalComponent::RememberMovementInput()
{
    const FVector Direction = Character->GetLastMovementInputVector().GetSafeNormal2D();
    if (!Direction.IsNearlyZero())
    {
        LastMoveDirection = Direction;
        LastMoveInputTime = GetWorld()->GetTimeSeconds();
    }
}

FVector UPlumPoleTraversalComponent::GetTravelDirection() const
{
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now - ExplicitDirectionTime <= InputMemorySeconds && !ExplicitDirection.IsNearlyZero())
    {
        return ExplicitDirection;
    }

    const FVector PendingDirection = Character->GetPendingMovementInputVector().GetSafeNormal2D();
    if (!PendingDirection.IsNearlyZero())
    {
        return PendingDirection;
    }
    if (Now - LastMoveInputTime <= InputMemorySeconds && !LastMoveDirection.IsNearlyZero())
    {
        return LastMoveDirection;
    }

    return FRotator(0.0, Character->GetControlRotation().Yaw, 0.0).Vector();
}

FVector UPlumPoleTraversalComponent::GetAutoHopDirection() const
{
    // Only use real movement from this update. Camera facing and buffered input cannot sustain a chain.
    if (ExplicitInputFrame == GFrameCounter && !ExplicitDirection.IsNearlyZero())
    {
        return ExplicitInputMagnitude >= AutoHopInputThreshold ? ExplicitDirection : FVector::ZeroVector;
    }
    FVector Movement = Character->GetPendingMovementInputVector();
    if (Movement.IsNearlyZero())
    {
        Movement = Character->GetLastMovementInputVector();
    }
    return Movement.SizeSquared2D() >= FMath::Square(AutoHopInputThreshold) ? Movement.GetSafeNormal2D() : FVector::ZeroVector;
}

APlumPoleActor* UPlumPoleTraversalComponent::FindPole(const FVector& Direction, bool bEntering, FVector& OutCarryVelocity,
    EArcStyle RequestedStyle, bool bCanFlip, EArcStyle& OutStyle) const
{
    const EPlumPoleTraversalState ArcState = bEntering ? EPlumPoleTraversalState::Entering : EPlumPoleTraversalState::Hopping;
    const FVector Origin = Character->GetActorLocation();
    const FVector GroundVelocity = bEntering ? FVector(CharacterMovement->Velocity.X, CharacterMovement->Velocity.Y, 0.0) : FVector::ZeroVector;
    APlumPoleActor* BestPole = nullptr;
    const APlumPoleCharacter* PoleCharacter = Cast<APlumPoleCharacter>(Character);
    double BestScore = UE_BIG_NUMBER;

    for (TActorIterator<APlumPoleActor> It(GetWorld()); It; ++It)
    {
        APlumPoleActor* Candidate = *It;
        if (Candidate == CurrentPole.Get() || !Candidate->IsAvailableFor(Character))
        {
            continue;
        }

        const FVector Destination = GetPoleCapsuleLocation(*Candidate);
        const FVector Delta = Destination - Origin;
        const double Distance = Delta.Size2D();
        const float MaxDistance = bEntering ? Candidate->GetApproachRadius() : MaxHopDistance;
        if (Distance <= 1.0 || Distance > MaxDistance || Delta.Z > MaxPoleRise || Delta.Z < -MaxPoleDrop)
        {
            continue;
        }

        const double Alignment = FVector::DotProduct(Direction, Delta.GetSafeNormal2D());
        if (Alignment < (bEntering ? EntryAlignment : HopAlignment))
        {
            continue;
        }

        const double Score = PoleCharacter
            ? PoleCharacter->ScorePoleCandidate(Alignment, Distance, Delta.Z, MaxDistance)
            : Distance + (1.0 - Alignment) * MaxDistance * 0.8 + FMath::Abs(Delta.Z) * 0.3;
        if (!FMath::IsFinite(Score) || Score >= BestScore)
        {
            continue;
        }

        const EArcStyle Style = !bEntering && Distance > ForwardFlipDistance ? EArcStyle::PoleFlip : RequestedStyle;
        if (Style == EArcStyle::PoleFlip && !bCanFlip)
        {
            continue;
        }

        // Preserve the approach speed when there is enough room to leave the floor.
        FVector CarryVelocity = GroundVelocity;
        if (!CanFollowArc(Destination, ArcState, CarryVelocity, Style))
        {
            if (CarryVelocity.IsNearlyZero() || !CanFollowArc(Destination, ArcState, FVector::ZeroVector, Style))
            {
                continue;
            }
            CarryVelocity = FVector::ZeroVector;
        }

        BestPole = Candidate;
        BestScore = Score;
        OutCarryVelocity = CarryVelocity;
        OutStyle = Style;
    }
    return BestPole;
}

bool UPlumPoleTraversalComponent::FindDismountLocation(const FVector& Direction, FVector& OutLocation, bool& bOutShortDismount) const
{
    const FVector Origin = Character->GetActorLocation();
    auto TryLanding = [&](float Distance, float MaxRise, float MaxDrop, bool bShortExit)
    {
        const FVector Center = Origin + Direction * Distance;
        const FVector From = Center + FVector::UpVector * (MaxRise + LandingClearance);
        const FVector To = Center - FVector::UpVector * MaxDrop;
        FHitResult GroundHit;
        if (!SweepCapsule(From, To, GroundHit) || !CharacterMovement->IsWalkable(GroundHit) || Cast<APlumPoleActor>(GroundHit.GetActor()))
        {
            return false;
        }

        const FVector Landing = GroundHit.Location + FVector::UpVector * LandingClearance;
        if (!CanFollowArc(Landing, EPlumPoleTraversalState::Dismounting, FVector::ZeroVector,
            bShortExit ? EArcStyle::ShortDismount : EArcStyle::Normal))
        {
            return false;
        }
        OutLocation = Landing;
        bOutShortDismount = bShortExit;
        return true;
    };

    static constexpr float ShortDistanceScales[] = {0.65f, 0.82f, 1.0f};
    if (ShortDismountDistance > 0.0f)
    {
        for (const float Scale : ShortDistanceScales)
        {
            if (TryLanding(ShortDismountDistance * Scale, ShortDismountMaxHeightDifference, ShortDismountMaxHeightDifference, true))
            {
                return true;
            }
        }
    }

    static constexpr float DistanceScales[] = {1.0f, 0.75f, 1.25f};
    for (const float Scale : DistanceScales)
    {
        if (TryLanding(DismountDistance * Scale, DismountMaxRise, DismountMaxDrop, false))
        {
            return true;
        }
    }
    return false;
}

FVector UPlumPoleTraversalComponent::GetPoleCapsuleLocation(const APlumPoleActor& Pole) const
{
    const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    return Pole.GetLandingLocation() + FVector::UpVector * (HalfHeight + LandingClearance);
}

bool UPlumPoleTraversalComponent::IsReservedAnchor(const APlumPoleActor* Pole, const FVector& Location) const
{
    return IsValid(Pole) && Pole->IsAvailableFor(Character) && Pole->GetOccupant() == Character &&
        Location.Equals(GetPoleCapsuleLocation(*Pole), AnchorTolerance);
}

FPlumPoleTraversalTiming UPlumPoleTraversalComponent::GetTiming(EPlumPoleTraversalState ArcState, EArcStyle Style) const
{
    FPlumPoleTraversalTiming Timing = ArcState == EPlumPoleTraversalState::Entering ? EnterTiming :
        Style == EArcStyle::PoleFlip ? PoleFlipTiming :
        Style == EArcStyle::ChainHop ? ChainHopTiming :
        ArcState == EPlumPoleTraversalState::Hopping ? HopTiming :
        Style == EArcStyle::ShortDismount ? ShortDismountTiming : DismountTiming;
    Timing.TakeoffProgress = FMath::Clamp(Timing.TakeoffProgress, 0.0f, 0.95f);
    Timing.ContactProgress = FMath::Clamp(Timing.ContactProgress, Timing.TakeoffProgress + 0.05f, 1.0f);
    Timing.ChainProgress = FMath::Clamp(Timing.ChainProgress, Timing.ContactProgress, 1.0f);
    return Timing;
}

float UPlumPoleTraversalComponent::GetDuration(EPlumPoleTraversalState ArcState, EArcStyle Style) const
{
    const float Duration = ArcState == EPlumPoleTraversalState::Entering ? EnterDuration :
        Style == EArcStyle::PoleFlip ? ActiveFlipAnimation->GetPlayLength() / FMath::Max(PoleFlipPlayRate, 0.1f) :
        Style == EArcStyle::ChainHop ? ChainHopDuration :
        ArcState == EPlumPoleTraversalState::Hopping ? HopDuration :
        Style == EArcStyle::ShortDismount ? ShortDismountDuration : DismountDuration;
    return FMath::Max(Duration, 0.1f);
}

FVector UPlumPoleTraversalComponent::GetArcLocation(const FVector& Start, const FVector& End, EPlumPoleTraversalState ArcState,
    const FVector& CarryVelocity, const FPlumPoleTraversalTiming& Timing, float Duration, float Progress, EArcStyle Style) const
{
    const FVector Delta = End - Start;
    const float Distance = static_cast<float>(Delta.Size2D());
    const float Elevation = static_cast<float>(Delta.Z);
    const float Flight = GetFlightProgress(Timing, Progress);
    const float Travel = FMath::SmoothStep(0.0f, 1.0f, Flight);
    switch (ArcState)
    {
    case EPlumPoleTraversalState::Entering:
    {
        // Close to the side of a post, rise first. Give a running approach room to carry across the floor.
        const float Lead = FMath::Lerp(0.49f, 0.10f, FMath::SmoothStep(66.0f, 175.0f, Distance));
        const float ToContact = FMath::Min(Progress / Timing.ContactProgress, 1.0f);
        const float VelocityWeight = Duration * Progress * FMath::Cube(1.0f - ToContact);
        FVector Position = FMath::Lerp(Start, End, FMath::SmoothStep(Lead, 1.0f, Flight)) + CarryVelocity * VelocityWeight;
        // Clear the leading toe over the rim before the landing pose plants it on the crown.
        const float Overshoot = FMath::Clamp(FMath::Abs(Elevation) * 0.23f + Distance * 0.03f, 16.0f, 43.0f);
        const float Crest = FMath::SmoothStep(0.17f, 0.61f, Flight) * (1.0f - FMath::SmoothStep(0.63f, 1.0f, Flight));
        Position.Z = FMath::Lerp(Start.Z, End.Z, FMath::SmoothStep(0.0f, 0.67f, Flight)) + Overshoot * Crest;
        return Position;
    }
    case EPlumPoleTraversalState::Hopping:
    {
        if (Style == EArcStyle::PoleFlip)
        {
            if (Progress <= Timing.TakeoffProgress)
            {
                return Start;
            }
            if (Progress >= Timing.ContactProgress)
            {
                return End;
            }
            const FVector2D Sample = SamplePoleFlipTravel(Progress);
            const double Along = FMath::Clamp((Sample.X - FlipReleaseTravel.X) / (FlipContactTravel.X - FlipReleaseTravel.X), 0.0, 1.0);
            // The pose has had its airborne rise removed. Restore that displacement once, above the line between crowns.
            const double Height = Sample.Y - FMath::Lerp(FlipReleaseTravel.Y, FlipContactTravel.Y, static_cast<double>(Flight));
            return FMath::Lerp(Start, End, Along) + FVector::UpVector * Height;
        }
        // The source pose already tucks the feet in flight. Reserve a larger lift for uneven posts.
        const float Height = HopHeight + FMath::Abs(Elevation) * 0.30f;
        const float VerticalTravel = Elevation >= 0.0f ? FMath::SmoothStep(0.0f, 0.82f, Flight) : FMath::SmoothStep(0.20f, 1.0f, Flight);
        // The ordinary clip drives the pelvis forward while the departure foot is still planted.
        const float WeightShiftStart = FMath::Max(0.0f, Timing.TakeoffProgress - (Timing.ContactProgress - Timing.TakeoffProgress) / 3.0f);
        const float HorizontalTravel = Style == EArcStyle::Normal
            ? FMath::SmoothStep(WeightShiftStart, Timing.ContactProgress, Progress)
            : Travel;
        FVector Position = FMath::Lerp(Start, End, HorizontalTravel);
        Position.Z = FMath::Lerp(Start.Z, End.Z, VerticalTravel) + Height * GetFlightBump(Flight);
        return Position;
    }
    default:
    {
        const bool bShortExit = Style == EArcStyle::ShortDismount;
        // Carry horizontal speed through contact so the recovery can roll into ground movement.
        const float ExitTravel = Travel - DismountExitSlope * FMath::Square(Flight) * (1.0f - Flight);
        const float Height = (bShortExit ? 15.0f : 29.0f) + FMath::Abs(Elevation) * 0.13f;
        const float DescentStart = bShortExit ? 0.22f : 0.45f;
        FVector Position = FMath::Lerp(Start, End, ExitTravel);
        Position.Z = FMath::Lerp(Start.Z, End.Z, FMath::SmoothStep(DescentStart, 1.0f, Flight)) + Height * GetFlightBump(Flight);
        return Position;
    }
    }
}

bool UPlumPoleTraversalComponent::CanFollowArc(const FVector& Destination, EPlumPoleTraversalState ArcState,
    const FVector& CarryVelocity, EArcStyle Style) const
{
    const FVector Origin = Character->GetActorLocation();
    const FPlumPoleTraversalTiming Timing = GetTiming(ArcState, Style);
    const float Duration = GetDuration(ArcState, Style);
    FVector Previous = Origin;
    for (int32 Step = 1; Step <= ArcSweepSegments; ++Step)
    {
        const float Progress = Timing.ContactProgress * Step / ArcSweepSegments;
        const FVector Next = GetArcLocation(Origin, Destination, ArcState, CarryVelocity, Timing, Duration, Progress, Style);
        FHitResult Hit;
        if (!Previous.Equals(Next, KINDA_SMALL_NUMBER) && SweepCapsule(Previous, Next, Hit))
        {
            return false;
        }
        Previous = Next;
    }
    return true;
}

bool UPlumPoleTraversalComponent::SweepCapsule(const FVector& From, const FVector& To, FHitResult& Hit) const
{
    const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PlumPoleTraversal), false, Character.Get());
    FCollisionResponseParams Responses;
    CharacterMovement->InitCollisionParams(Query, Responses);
    return GetWorld()->SweepSingleByChannel(Hit, From, To, Capsule->GetComponentQuat(), Capsule->GetCollisionObjectType(), Capsule->GetCollisionShape(), Query, Responses);
}

bool UPlumPoleTraversalComponent::StartArc(EPlumPoleTraversalState ArcState, const FVector& Destination, APlumPoleActor* DestinationPole,
    const FVector& CarryVelocity, EArcStyle Style, bool bFromCompletedPoleArc)
{
    if (DestinationPole && (!IsValid(DestinationPole) || !DestinationPole->TryReserve(Character)))
    {
        return false;
    }
    ArcStart = Character->GetActorLocation();
    ArcEnd = Destination;
    ArcCarryVelocity = CarryVelocity;
    const FVector Delta = ArcEnd - ArcStart;
    ArcStyle = Style;
    const FVector Facing = ArcState == EPlumPoleTraversalState::Dismounting && Style != EArcStyle::ShortDismount && bFaceAwayFromDismount ? -Delta : Delta;
    ArcStartYaw = static_cast<float>(Character->GetActorRotation().Yaw);
    ArcYawDelta = FMath::FindDeltaAngleDegrees(ArcStartYaw, static_cast<float>(Facing.Rotation().Yaw));
    ActiveTiming = GetTiming(ArcState, Style);
    TraversalDuration = GetDuration(ArcState, Style);
    TraversalElapsed = 0.0f;
    TargetPole = DestinationPole;
    bArcActive = true;
    bLastTraversalCompleted = bFromCompletedPoleArc;
    bJustLandedFromHop = false;
    BufferedJumpExpires = -1.0;

    if (!bMovementLocked)
    {
        Character->StopJumping();
        CharacterMovement->StopMovementImmediately();
        CharacterMovement->DisableMovement();
        bMovementLocked = true;
    }
    ChangeState(ArcState, DestinationPole ? DestinationPole : CurrentPole.Get(), bFromCompletedPoleArc);
    bLastTraversalCompleted = false;
    return true;
}

void UPlumPoleTraversalComponent::AdvanceArc(float DeltaTime)
{
    const bool bDismounting = TraversalState == EPlumPoleTraversalState::Dismounting;
    const bool bWindupOnPole = TraversalState != EPlumPoleTraversalState::Entering && GetTraversalProgress() < ActiveTiming.TakeoffProgress;
    if (bWindupOnPole && !IsReservedAnchor(CurrentPole.Get(), ArcStart))
    {
        InterruptArc();
        return;
    }
    if (!bDismounting && !IsReservedAnchor(TargetPole.Get(), ArcEnd))
    {
        InterruptArc();
        return;
    }

    const float PreviousElapsed = TraversalElapsed;
    const float EndTime = FMath::Min(TraversalElapsed + DeltaTime, TraversalDuration);
    const float AnimationProgress = EndTime / TraversalDuration;
    if (bMovementLocked)
    {
        const float TurnFraction = FMath::GetMappedRangeValueClamped(FVector2D(55.0f, 140.0f),
            FVector2D(0.32f, 0.70f), FMath::Abs(ArcYawDelta));
        // Set the flip's heading while the support foot is planted so the roll follows the flight path.
        const float TurnEnd = ArcStyle == EArcStyle::PoleFlip ? ActiveTiming.TakeoffProgress :
            FMath::Lerp(ActiveTiming.TakeoffProgress, ActiveTiming.ContactProgress, TurnFraction);
        const float Yaw = ArcStartYaw + ArcYawDelta * FMath::SmoothStep(0.0f, TurnEnd, AnimationProgress);
        Character->SetActorRotation(FRotator(0.0f, Yaw, 0.0f));

        const float EndProgress = FMath::Min(AnimationProgress, ActiveTiming.ContactProgress);
        float Progress = GetTraversalProgress();
        while (Progress < EndProgress)
        {
            Progress = FMath::Min(Progress + ActiveTiming.ContactProgress / ArcSweepSegments, EndProgress);
            const FVector Position = GetArcLocation(ArcStart, ArcEnd, TraversalState, ArcCarryVelocity,
                ActiveTiming, TraversalDuration, Progress, ArcStyle);
            if (Character->GetActorLocation().Equals(Position, KINDA_SMALL_NUMBER))
            {
                continue;
            }

            FHitResult Hit;
            Character->SetActorLocation(Position, true, &Hit);
            if (Hit.bBlockingHit || !Character->GetActorLocation().Equals(Position, LandingClearance))
            {
                InterruptArc();
                return;
            }
        }
    }
    if (GetTraversalProgress() <= ActiveTiming.TakeoffProgress && AnimationProgress >= ActiveTiming.TakeoffProgress && CurrentPole.IsValid())
    {
        CurrentPole->Release(Character);
    }
    TraversalElapsed = EndTime;

    if (!bDismounting)
    {
        const bool bHasBufferedJump = GetWorld()->GetTimeSeconds() <= BufferedJumpExpires;
        const bool bReachedEnd = TraversalElapsed >= TraversalDuration;
        if (bHasBufferedJump && AnimationProgress >= ActiveTiming.ChainProgress)
        {
            const float ChainTime = TraversalDuration * ActiveTiming.ChainProgress;
            const float OverflowDelta = PreviousElapsed < ChainTime
                ? FMath::Max(0.0f, EndTime - ChainTime)
                : DeltaTime;
            if (TryChainFromPoleArc(OverflowDelta))
            {
                return;
            }
            // No next target is reachable from this pole; clear the buffer and finish settling onto the current pole.
            BufferedJumpExpires = -1.0;
            if (!bReachedEnd)
            {
                return;
            }
        }
        if (bReachedEnd)
        {
            CompletePoleArc();
        }
        return;
    }

    if (bMovementLocked && AnimationProgress >= ActiveTiming.ContactProgress)
    {
        const FVector Landing = Character->GetActorLocation();
        FHitResult FloorHit;
        const bool bLanded = SweepCapsule(Landing, Landing - FVector::UpVector * AnchorTolerance, FloorHit) && CharacterMovement->IsWalkable(FloorHit);
        const FVector Delta = ArcEnd - ArcStart;
        const float FlightDuration = TraversalDuration * (ActiveTiming.ContactProgress - ActiveTiming.TakeoffProgress);
        const FVector Velocity = FVector(Delta.X, Delta.Y, 0.0) * (DismountExitSlope / FlightDuration);
        CurrentPole.Reset();
        bLastTraversalCompleted = bLanded;
        ReturnToCharacterMovement(Velocity, bLanded);
    }

    if (AnimationProgress >= ActiveTiming.ChainProgress && !bMovementLocked && !CharacterMovement->IsFalling())
    {
        FinishDismount();
    }
}

bool UPlumPoleTraversalComponent::TryChainFromPoleArc(float OverflowDelta)
{
    APlumPoleActor* LandedPole = TargetPole.Get();
    if (!IsReservedAnchor(LandedPole, Character->GetActorLocation()))
    {
        InterruptArc();
        return true;
    }

    const TWeakObjectPtr<APlumPoleActor> PreviousPole = CurrentPole;
    CurrentPole = LandedPole;
    TargetPole.Reset();

    const bool bAllowDismount = BufferedStyle != EArcStyle::PoleFlip;
    const bool bCanFlip = PreparePoleFlip();
    FVector CarryVelocity = FVector::ZeroVector;
    EArcStyle NextStyle = BufferedStyle;
    APlumPoleActor* NextPole = FindPole(BufferedDirection, false, CarryVelocity, BufferedStyle, bCanFlip, NextStyle);

    bool bStarted = false;
    if (NextPole)
    {
        bStarted = StartArc(EPlumPoleTraversalState::Hopping, GetPoleCapsuleLocation(*NextPole), NextPole,
            CarryVelocity, NextStyle, true);
    }
    else if (bAllowDismount)
    {
        FVector Landing;
        bool bShortExit = false;
        if (FindDismountLocation(BufferedDirection, Landing, bShortExit))
        {
            bStarted = StartArc(EPlumPoleTraversalState::Dismounting, Landing, nullptr, FVector::ZeroVector,
                bShortExit ? EArcStyle::ShortDismount : EArcStyle::Normal, true);
        }
    }

    if (!bStarted)
    {
        CurrentPole = PreviousPole;
        TargetPole = LandedPole;
        return false;
    }

    const float ClampedOverflow = FMath::Min(OverflowDelta, 0.03f);
    if (ClampedOverflow > KINDA_SMALL_NUMBER)
    {
        AdvanceArc(ClampedOverflow);
    }
    return true;
}

void UPlumPoleTraversalComponent::CompletePoleArc()
{
    if (!IsReservedAnchor(TargetPole.Get(), Character->GetActorLocation()))
    {
        InterruptArc();
        return;
    }

    bArcActive = false;
    bLastTraversalCompleted = true;
    bJustLandedFromHop = TraversalState == EPlumPoleTraversalState::Hopping;
    CurrentPole = TargetPole;
    TargetPole.Reset();
    ChangeState(EPlumPoleTraversalState::Perched, CurrentPole.Get());
}

void UPlumPoleTraversalComponent::FinishDismount()
{
    bArcActive = false;
    TraversalElapsed = 0.0f;
    TraversalDuration = 0.0f;
    ChangeState(EPlumPoleTraversalState::Grounded, nullptr);
}

void UPlumPoleTraversalComponent::InterruptArc()
{
    bArcActive = false;
    bLastTraversalCompleted = false;
    BufferedJumpExpires = -1.0;
    if (TargetPole.IsValid())
    {
        TargetPole->Release(Character);
    }
    TargetPole.Reset();
    APlumPoleActor* DeparturePole = CurrentPole.Get();
    const bool bDepartedFromPole = TraversalState == EPlumPoleTraversalState::Hopping || TraversalState == EPlumPoleTraversalState::Dismounting;
    if (bDepartedFromPole && DeparturePole && Character->GetActorLocation().Equals(ArcStart, AnchorTolerance) &&
        ArcStart.Equals(GetPoleCapsuleLocation(*DeparturePole), AnchorTolerance) && DeparturePole->TryReserve(Character))
    {
        FHitResult Hit;
        Character->SetActorLocationAndRotation(ArcStart, FRotator(0.0f, ArcStartYaw, 0.0f), true, &Hit);
        if (!Hit.bBlockingHit)
        {
            ChangeState(EPlumPoleTraversalState::Perched, DeparturePole);
            return;
        }
    }

    if (DeparturePole)
    {
        DeparturePole->Release(Character);
    }
    CurrentPole.Reset();
    ReturnToCharacterMovement();
    // A lost support or a blocked flight hands the pose back to the template falling state immediately.
    FinishDismount();
}

void UPlumPoleTraversalComponent::ReturnToCharacterMovement(const FVector& Velocity, bool bLanded)
{
    bMovementLocked = false;
    if (IsValid(CharacterMovement))
    {
        CharacterMovement->SetMovementMode(bLanded ? MOVE_Walking : MOVE_Falling);
        CharacterMovement->Velocity = Velocity.GetClampedToMaxSize(CharacterMovement->GetMaxSpeed());
        CharacterMovement->UpdateComponentVelocity();
    }
}

void UPlumPoleTraversalComponent::ChangeState(EPlumPoleTraversalState NewState, APlumPoleActor* Pole, bool bForceBroadcast)
{
    if (TraversalState != NewState || bForceBroadcast)
    {
        const EPlumPoleTraversalState PreviousState = TraversalState;
        TraversalState = NewState;
        OnTraversalStateChanged.Broadcast(PreviousState, NewState, Pole);
    }
}
