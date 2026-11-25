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

struct FTimeCurveTable
{
    TArray<float> Positions;
    TArray<float> CumulativeTimes;

    FTimeCurveTable(const float* InPos, const float* InRate, int32 Count)
    {
        Positions.Reserve(Count);
        CumulativeTimes.Reserve(Count);
        float T = 0.0f;
        for (int32 i = 0; i < Count; ++i)
        {
            if (i > 0)
            {
                const float AvgInvRate = 0.5f * (1.0f / FMath::Max(InRate[i - 1], 0.05f)
                    + 1.0f / FMath::Max(InRate[i], 0.05f));
                T += (InPos[i] - InPos[i - 1]) * AvgInvRate;
            }
            Positions.Add(InPos[i]);
            CumulativeTimes.Add(T);
        }
    }

    float EvalTimeAtPos(float Pos) const
    {
        if (Pos <= Positions[0])
        {
            return CumulativeTimes[0];
        }
        const int32 Last = Positions.Num() - 1;
        if (Pos >= Positions[Last])
        {
            return CumulativeTimes[Last] + (Pos - Positions[Last]);
        }
        for (int32 i = 0; i < Last; ++i)
        {
            if (Pos <= Positions[i + 1])
            {
                const float Alpha = (Pos - Positions[i]) / FMath::Max(Positions[i + 1] - Positions[i], KINDA_SMALL_NUMBER);
                return FMath::Lerp(CumulativeTimes[i], CumulativeTimes[i + 1], Alpha);
            }
        }
        return CumulativeTimes[Last];
    }

    float EvalPosAtTime(float Time) const
    {
        if (Time <= CumulativeTimes[0])
        {
            return Positions[0];
        }
        const int32 Last = CumulativeTimes.Num() - 1;
        if (Time >= CumulativeTimes[Last])
        {
            return Positions[Last] + (Time - CumulativeTimes[Last]);
        }
        for (int32 i = 0; i < Last; ++i)
        {
            if (Time <= CumulativeTimes[i + 1])
            {
                const float Alpha = (Time - CumulativeTimes[i]) / FMath::Max(CumulativeTimes[i + 1] - CumulativeTimes[i], KINDA_SMALL_NUMBER);
                return FMath::Lerp(Positions[i], Positions[i + 1], Alpha);
            }
        }
        return Positions[Last];
    }
};

const FTimeCurveTable& GetNearHopCurveTable()
{
    // PBZ V6 measured dPos/dGameTime shared by Battle_Walk_Fwd_L/R and JumpOut_L_02.
    static constexpr float Pos[] = {
        0.00f, 0.20f, 0.25f, 0.30f, 0.35f, 0.40f, 0.45f, 0.50f, 0.53f, 0.56f,
        0.60f, 0.65f, 0.70f, 0.75f, 0.80f, 0.85f, 0.90f, 0.95f, 1.00f, 1.05f,
        1.10f, 1.15f, 1.20f, 1.25f, 1.30f, 1.35f, 1.40f, 1.45f, 1.50f, 1.65f, 2.20f
    };
    static constexpr float Rate[] = {
        2.170f, 2.170f, 2.174f, 2.267f, 2.439f, 2.613f, 2.766f, 2.859f, 2.850f, 2.828f,
        2.732f, 2.648f, 2.520f, 2.389f, 2.233f, 2.121f, 2.006f, 1.917f, 1.806f, 1.710f,
        1.590f, 1.489f, 1.387f, 1.287f, 1.205f, 1.128f, 1.075f, 1.041f, 1.026f, 1.025f, 1.025f
    };
    static const FTimeCurveTable Table(Pos, Rate, UE_ARRAY_COUNT(Pos));
    return Table;
}

const FTimeCurveTable& GetForwardFlipCurveTable()
{
    // PBZ V6 measured dPos/dGameTime for Dodge_F_R (entry and far flip).
    static constexpr float Pos[] = {
        0.00f, 0.05f, 0.10f, 0.15f, 0.20f, 0.25f, 0.30f, 0.40f, 0.50f,
        0.60f, 0.70f, 0.80f, 0.90f, 1.00f, 1.10f, 1.20f, 1.30f, 2.00f
    };
    static constexpr float Rate[] = {
        1.599f, 1.597f, 1.591f, 1.580f, 1.564f, 1.544f, 1.526f, 1.473f, 1.415f,
        1.346f, 1.285f, 1.217f, 1.156f, 1.105f, 1.062f, 1.036f, 1.025f, 1.025f
    };
    static const FTimeCurveTable Table(Pos, Rate, UE_ARRAY_COUNT(Pos));
    return Table;
}
}

float EvaluatePlumPoleTimeCurve(
    EPlumPoleTimeCurve Curve, float StartSeconds, float EndSeconds, float NormalizedProgress)
{
    const float ClampedProgress = FMath::Clamp(NormalizedProgress, 0.0f, 1.0f);
    if (EndSeconds <= StartSeconds + KINDA_SMALL_NUMBER || Curve == EPlumPoleTimeCurve::Linear)
    {
        return FMath::Lerp(StartSeconds, EndSeconds, ClampedProgress);
    }

    const FTimeCurveTable& Table = Curve == EPlumPoleTimeCurve::ForwardFlip
        ? GetForwardFlipCurveTable()
        : GetNearHopCurveTable();
    const float T0 = Table.EvalTimeAtPos(StartSeconds);
    const float T1 = Table.EvalTimeAtPos(EndSeconds);
    return FMath::Clamp(Table.EvalPosAtTime(FMath::Lerp(T0, T1, ClampedProgress)), StartSeconds, EndSeconds);
}

UPlumPoleTraversalComponent::UPlumPoleTraversalComponent()
    : EnterTiming(0.1507f, 0.7965f, 0.8258f)
    , HopTiming(0.1768f, 0.5601f, 0.5601f)
    , ChainHopTiming(0.1218f, 0.5306f, 0.5306f)
    , PoleFlipTiming(0.1507f, 0.7965f, 0.8258f)
    , DismountTiming(0.1830f, 0.7550f, 1.0f)
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
        PreparePoleFlip();
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

    if (bMovementLocked && CharacterMovement->MovementMode != MOVE_None)
    {
        CharacterMovement->DisableMovement();
    }

    // CharacterMovement ticks before us (prerequisite) and consumes ControlInputVector into LastControlInputVector
    // each tick even in MOVE_None; reading pending-or-last gives exact live input without wiping it.
    CurrentFrameMoveInput = Character->GetPendingMovementInputVector();
    if (CurrentFrameMoveInput.IsNearlyZero())
    {
        CurrentFrameMoveInput = Character->GetLastMovementInputVector();
    }

    RememberMovementInput();

    if (bArcActive)
    {
        AdvanceArc(DeltaTime);
        return;
    }

    if (TraversalState == EPlumPoleTraversalState::Perched)
    {
        PerchElapsed += DeltaTime;
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
            if (Direction.IsNearlyZero())
            {
                bInputNeutralSincePerch = true;
            }
            else
            {
                const bool bSteeredAwayFromArrival = ArrivalDirection.IsNearlyZero()
                    || FVector::DotProduct(Direction, ArrivalDirection) < 0.5f;
                const bool bCanAutoDismount = bAutoDismountWithMovementInput
                    && (bInputNeutralSincePerch || bSteeredAwayFromArrival || PerchElapsed >= AutoDismountHoldDelay);
                TryStartFromPerch(Direction, bCanAutoChain ? EArcStyle::ChainHop : EArcStyle::Normal, bCanAutoDismount);
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
        const bool bUseChainHop = TraversalState == EPlumPoleTraversalState::Entering
            || (TraversalState == EPlumPoleTraversalState::Hopping && GetTraversalProgress() < 0.75f);
        BufferedStyle = bFlipRequested ? EArcStyle::PoleFlip :
            bUseChainHop ? EArcStyle::ChainHop : EArcStyle::Normal;
        // A second press during entry or a hop stays queued until the landing can support another jump.
        const float TimeUntilChain = TraversalDuration * ActiveTiming.ChainProgress - TraversalElapsed;
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

    PreparePoleFlip();
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
    const float FlipEnd = FMath::Min(PoleFlipSectionEndSeconds, ActiveFlipAnimation->GetPlayLength());
    const float SampleSeconds = EvaluatePlumPoleTimeCurve(
        EPlumPoleTimeCurve::ForwardFlip, 0.0f, FlipEnd, Progress);
    const FAnimExtractContext Context(static_cast<double>(SampleSeconds));
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

APlumPoleActor* UPlumPoleTraversalComponent::FindEnterablePole() const
{
    if (TraversalState != EPlumPoleTraversalState::Grounded || !IsValid(Character) || !IsValid(CharacterMovement)
        || !CharacterMovement->IsMovingOnGround())
    {
        return nullptr;
    }
    FVector CarryVelocity;
    EArcStyle Style = EArcStyle::Normal;
    return FindPole(GetTravelDirection(), true, CarryVelocity, EArcStyle::Normal, false, Style);
}

float UPlumPoleTraversalComponent::GetTraversalProgress() const
{
    return TraversalDuration > 0.0f ? FMath::Clamp(TraversalElapsed / TraversalDuration, 0.0f, 1.0f) : 0.0f;
}

void UPlumPoleTraversalComponent::RememberMovementInput()
{
    FVector Direction = CurrentFrameMoveInput.GetSafeNormal2D();
    if (Direction.IsNearlyZero())
    {
        Direction = Character->GetLastMovementInputVector().GetSafeNormal2D();
    }
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

    const FVector PendingDirection = CurrentFrameMoveInput.IsNearlyZero()
        ? Character->GetPendingMovementInputVector().GetSafeNormal2D()
        : CurrentFrameMoveInput.GetSafeNormal2D();
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
    // Only use live movement from this update. Camera facing and stale input cannot sustain a chain.
    if (ExplicitInputFrame == GFrameCounter && !ExplicitDirection.IsNearlyZero())
    {
        return ExplicitInputMagnitude >= AutoHopInputThreshold ? ExplicitDirection : FVector::ZeroVector;
    }
    FVector Movement = CurrentFrameMoveInput;
    if (Movement.IsNearlyZero() && !bMovementLocked)
    {
        Movement = Character->GetPendingMovementInputVector();
        if (Movement.IsNearlyZero())
        {
            Movement = Character->GetLastMovementInputVector();
        }
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
        Style == EArcStyle::PoleFlip ? PoleFlipDuration / FMath::Max(PoleFlipPlayRate, 0.1f) :
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
        // PBZ V6 ground entry uses Dodge_F_R: stay grounded during takeoff wind-up, then soar cleanly onto the crown.
        if (ActiveFlipAnimation && !FMath::IsNearlyEqual(FlipContactTravel.X, FlipReleaseTravel.X))
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
            const double RawAlong = FMath::Clamp(
                (Sample.X - FlipReleaseTravel.X) / (FlipContactTravel.X - FlipReleaseTravel.X), 0.0, 1.0);
            const double Along = FMath::Lerp(RawAlong, static_cast<double>(FMath::SmoothStep(0.06f, 1.0f, Flight)), 0.15);
            // Match PBZ V6 ID 13 Dodge_F_R ground-entry soar height (~235-240 cm pelvis above destination crown).
            const double FlipLift = FMath::Max(
                0.0, Sample.Y - FMath::Lerp(FlipReleaseTravel.Y, FlipContactTravel.Y, static_cast<double>(Flight)));
            FVector Position = FMath::Lerp(Start, End, Along);
            Position.Z = FMath::Lerp(Start.Z, End.Z, FMath::SmoothStep(0.0f, 0.78f, Flight)) + FlipLift;
            return Position;
        }
        // Fallback when no flip travel curve is configured.
        const float Lead = FMath::Lerp(0.49f, 0.10f, FMath::SmoothStep(66.0f, 175.0f, Distance));
        const float ToContact = FMath::Min(Progress / Timing.ContactProgress, 1.0f);
        const float VelocityWeight = Duration * Progress * FMath::Cube(1.0f - ToContact);
        FVector Position = FMath::Lerp(Start, End, FMath::SmoothStep(Lead, 1.0f, Flight)) + CarryVelocity * VelocityWeight;
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
        // PBZ V6 Battle_Walk_Fwd SkewWarp: slightly front-loaded XY matching the 2.8x->1.3x play rate,
        // and minimal capsule Z bump (+1.5 to +4 cm) since the pose itself already tucks the legs ~30 cm.
        const float NearHopTravel = FMath::SmoothStep(
            0.0f, 1.0f, FMath::Clamp(Flight + 0.12f * Flight * (1.0f - Flight), 0.0f, 1.0f));
        const float Height = HopHeight + FMath::Abs(Elevation) * 0.04f;
        const float VerticalTravel = Elevation >= 0.0f
            ? FMath::SmoothStep(0.0f, 0.94f, Flight)
            : FMath::SmoothStep(0.04f, 1.0f, Flight);
        FVector Position = FMath::Lerp(Start, End, NearHopTravel);
        Position.Z = FMath::Lerp(Start.Z, End.Z, VerticalTravel) + Height * GetFlightBump(Flight);
        return Position;
    }
    default:
    {
        const bool bShortExit = Style == EArcStyle::ShortDismount;
        // PBZ V6 JumpOut_L_02: low forward leap without upward backflip ballooning (+4 cm vs legacy +51 cm),
        // clearing the 34 cm pole cylinder + 34 cm capsule radius horizontally before descending smoothly to the floor.
        const float ForwardTravel = FMath::SmoothStep(
            0.0f, 1.0f, FMath::Clamp(Flight + 0.22f * Flight * (1.0f - Flight), 0.0f, 1.0f));
        const float ExitTravel = ForwardTravel - DismountExitSlope * FMath::Square(Flight) * (1.0f - Flight);
        const float Height = bShortExit ? 8.0f : 4.0f;
        const float DescentStart = bShortExit ? 0.20f : 0.36f;
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
    PreviousTraversalProgress = GetTraversalProgress();
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
        float YawAlpha = 0.0f;
        if (ArcStyle == EArcStyle::PoleFlip || TraversalState == EPlumPoleTraversalState::Entering)
        {
            // PBZ V6 Dodge_F_R SkewWarp [0.243, 1.093]: zero pre-takeoff spin, smooth ease-out airborne turn.
            const float TurnEnd = FMath::Max(ActiveTiming.TakeoffProgress + 0.05f, ActiveTiming.ContactProgress * 0.92f);
            const float U = FMath::Clamp(
                (AnimationProgress - ActiveTiming.TakeoffProgress) / (TurnEnd - ActiveTiming.TakeoffProgress), 0.0f, 1.0f);
            YawAlpha = 1.0f - FMath::Square(1.0f - U);
        }
        else
        {
            // PBZ V6 Battle_Walk_Fwd & JumpOut two-window SkewWarp: ~42% during wind-up, remaining ~58% across flight.
            const float TurnEnd = FMath::Max(ActiveTiming.TakeoffProgress, ActiveTiming.ContactProgress * 0.95f);
            const float U = FMath::Clamp(AnimationProgress / TurnEnd, 0.0f, 1.0f);
            YawAlpha = FMath::Lerp(FMath::SmoothStep(0.0f, 1.0f, U), 1.0f - FMath::Square(1.0f - U), 0.55f);
        }
        const float Yaw = ArcStartYaw + ArcYawDelta * YawAlpha;
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
        if (AnimationProgress >= ActiveTiming.ChainProgress)
        {
            const float ChainTime = TraversalDuration * ActiveTiming.ChainProgress;
            const float OverflowDelta = PreviousElapsed < ChainTime
                ? FMath::Max(0.0f, EndTime - ChainTime)
                : DeltaTime;

            if (bHasBufferedJump)
            {
                if (TryChainFromPoleArc(OverflowDelta, BufferedDirection, BufferedStyle, BufferedStyle != EArcStyle::PoleFlip))
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
            else if (bAutoHopWithMovementInput)
            {
                const FVector AutoDirection = GetAutoHopDirection();
                if (!AutoDirection.IsNearlyZero()
                    && TryChainFromPoleArc(OverflowDelta, AutoDirection, EArcStyle::ChainHop, false))
                {
                    return;
                }
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

    const bool bEarlyGroundBlend = AnimationProgress >= 0.96f && !GetAutoHopDirection().IsNearlyZero();
    if ((AnimationProgress >= ActiveTiming.ChainProgress || bEarlyGroundBlend) && !bMovementLocked && !CharacterMovement->IsFalling())
    {
        FinishDismount();
    }
}

bool UPlumPoleTraversalComponent::TryChainFromPoleArc(
    float OverflowDelta, const FVector& ChainDirection, EArcStyle ChainStyle, bool bAllowDismount)
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

    const bool bCanFlip = PreparePoleFlip();
    FVector CarryVelocity = FVector::ZeroVector;
    EArcStyle NextStyle = ChainStyle;
    APlumPoleActor* NextPole = FindPole(ChainDirection, false, CarryVelocity, ChainStyle, bCanFlip, NextStyle);

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
        if (FindDismountLocation(ChainDirection, Landing, bShortExit))
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

    PreviousTraversalProgress = GetTraversalProgress();
    ArrivalDirection = (ArcEnd - ArcStart).GetSafeNormal2D();
    PerchElapsed = 0.0f;
    bInputNeutralSincePerch = false;
    bArcActive = false;
    bLastTraversalCompleted = true;
    bJustLandedFromHop = TraversalState == EPlumPoleTraversalState::Hopping;
    CurrentPole = TargetPole;
    TargetPole.Reset();
    ChangeState(EPlumPoleTraversalState::Perched, CurrentPole.Get());
}

void UPlumPoleTraversalComponent::FinishDismount()
{
    PreviousTraversalProgress = GetTraversalProgress();
    bArcActive = false;
    TraversalElapsed = 0.0f;
    TraversalDuration = 0.0f;
    ChangeState(EPlumPoleTraversalState::Grounded, nullptr);
}

void UPlumPoleTraversalComponent::InterruptArc()
{
    PreviousTraversalProgress = GetTraversalProgress();
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
            PerchElapsed = 0.0f;
            bInputNeutralSincePerch = false;
            ArrivalDirection = FVector::ZeroVector;
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
