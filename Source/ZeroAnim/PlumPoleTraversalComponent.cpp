#include "PlumPoleTraversalComponent.h"

#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/RichCurve.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PlumPoleActor.h"
#include "PlumPoleCharacter.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr float LandingFloorClearance = 2.0f;
constexpr float ZeroDirTolerance = 0.0001f;
const FName FlipForwardCurve(TEXT("PlumPoleSourceTravelForward"));
const FName FlipUpCurve(TEXT("PlumPoleSourceTravelUp"));

struct FPlumPoleTimelineSpec
{
    float DefaultStart = 0.0f;
    float TimelineLength = 1.0f;
    float MontageLength = 1.0f;
    bool bLooping = false;
    bool bStopAbility = false;
    bool bAutoBlendOut = true;
    float BlendInTime = 0.25f;
    EAlphaBlendOption BlendInOption = EAlphaBlendOption::HermiteCubic;
    float BlendOutTime = 0.25f;
    EAlphaBlendOption BlendOutOption = EAlphaBlendOption::HermiteCubic;

    // Action phase times (source animation seconds, <0 means not present)
    float PreJumpStart = -1.0f;
    float PreJumpEnd = -1.0f;
    float ActTime = -1.0f;
    float ComboTime = -1.0f;
    float CancelTime = -1.0f;
    float PhaseClearTime = -1.0f;
    float CancelByMoveStart = -1.0f;
    float CancelByMoveEnd = -1.0f;
    float CamResetTime = -1.0f;
    float AutoIdleTime = -1.0f;
    EPlumPoleTimeline AutoIdleTarget = EPlumPoleTimeline::None;

    // GAA_MoveMode window (MOVE_Flying + Ignore ECC_WorldDynamic -> restore + MOVE_Falling)
    float MoveModeStart = -1.0f;
    float MoveModeEnd = -1.0f;

    // SkewWarp Stage 1 (rotation-only) and Stage 2 (translation + rotation)
    float WarpStage1Start = -1.0f;
    float WarpStage1End = -1.0f;
    float WarpStage1RotMult = 1.0f;
    float WarpStage2Start = -1.0f;
    float WarpStage2End = -1.0f;
    EAlphaBlendOption WarpStage2Easing = EAlphaBlendOption::Linear;
    float WarpStage2RotMult = 1.0f;
    float WarpMaxRotRate = 90.0f;

    // JumpOff Gravity & RM_Input windows
    float GravityStart = -1.0f;
    float GravityEnd = -1.0f;
    float GravityScale = 0.5f;
    float RMInputStart = -1.0f;
    float RMInputEnd = -1.0f;
    float RMInputOffsetYaw = 180.0f;
    float RMInputRate = 720.0f;
};

const FPlumPoleTimelineSpec& GetTimelineSpec(EPlumPoleTimeline Timeline)
{
    static const FPlumPoleTimelineSpec EmptySpec{};

    static const FPlumPoleTimelineSpec DashSpec = []()
    {
        FPlumPoleTimelineSpec S;
        S.DefaultStart = 0.0f;
        S.TimelineLength = 1.966667f;
        S.MontageLength = 1.966667f;
        S.BlendInTime = 0.25f;
        S.BlendInOption = EAlphaBlendOption::HermiteCubic;
        S.BlendOutTime = 0.25f;
        S.BlendOutOption = EAlphaBlendOption::HermiteCubic;
        S.bAutoBlendOut = true;
        S.ActTime = 0.451629f;
        S.ComboTime = 1.108522f;
        S.PhaseClearTime = 1.310774f;
        S.AutoIdleTime = 1.325300f;
        S.AutoIdleTarget = EPlumPoleTimeline::Idle_L;
        S.MoveModeStart = 0.235649f;
        S.MoveModeEnd = 1.044753f;
        S.WarpStage2Start = 0.243271f;
        S.WarpStage2End = 1.092892f;
        S.WarpStage2Easing = EAlphaBlendOption::Linear;
        S.WarpStage2RotMult = 0.3f;
        S.WarpMaxRotRate = 90.0f;
        return S;
    }();

    static const FPlumPoleTimelineSpec JumpLSpec = []()
    {
        FPlumPoleTimelineSpec S;
        S.DefaultStart = 0.20f;
        S.TimelineLength = 2.166667f;
        S.MontageLength = 2.166667f;
        S.BlendInTime = 0.15f;
        S.BlendInOption = EAlphaBlendOption::CircularIn;
        S.BlendOutTime = 0.25f;
        S.BlendOutOption = EAlphaBlendOption::HermiteCubic;
        S.bAutoBlendOut = true;
        S.PreJumpStart = 0.194053f;
        S.PreJumpEnd = 0.499834f;
        S.ActTime = 0.513342f;
        S.ComboTime = 1.200703f;
        S.PhaseClearTime = 1.631298f;
        S.AutoIdleTime = 1.640069f;
        S.AutoIdleTarget = EPlumPoleTimeline::Idle_R;
        S.MoveModeStart = 0.513629f;
        S.MoveModeEnd = 1.249791f;
        S.WarpStage1Start = 0.000000f;
        S.WarpStage1End = 0.499159f;
        S.WarpStage1RotMult = 1.0f;
        S.WarpStage2Start = 0.513629f;
        S.WarpStage2End = 1.249176f;
        S.WarpStage2Easing = EAlphaBlendOption::CircularIn;
        S.WarpStage2RotMult = 0.5f;
        S.WarpMaxRotRate = 90.0f;
        return S;
    }();

    static const FPlumPoleTimelineSpec JumpRSpec = []()
    {
        FPlumPoleTimelineSpec S = JumpLSpec;
        S.TimelineLength = 2.0f;
        S.ComboTime = 1.200127f;
        S.WarpStage2End = 1.2491765f;
        S.AutoIdleTarget = EPlumPoleTimeline::Idle_L;
        return S;
    }();

    static const FPlumPoleTimelineSpec IdleLSpec = []()
    {
        FPlumPoleTimelineSpec S;
        S.DefaultStart = 0.0f;
        S.TimelineLength = 6.2f;
        S.MontageLength = 6.7f;
        S.bLooping = true;
        S.bAutoBlendOut = false;
        S.BlendInTime = 0.50f;
        S.BlendInOption = EAlphaBlendOption::HermiteCubic;
        S.BlendOutTime = 0.25f;
        S.BlendOutOption = EAlphaBlendOption::HermiteCubic;
        return S;
    }();

    static const FPlumPoleTimelineSpec IdleRSpec = []()
    {
        FPlumPoleTimelineSpec S = IdleLSpec;
        S.TimelineLength = 6.1f;
        S.MontageLength = 6.6f;
        return S;
    }();

    static const FPlumPoleTimelineSpec JumpToPlatformLSpec = []()
    {
        FPlumPoleTimelineSpec S;
        S.DefaultStart = 0.20f;
        S.TimelineLength = 1.916667f;
        S.MontageLength = 1.916667f;
        S.bStopAbility = true;
        S.bAutoBlendOut = true;
        S.BlendInTime = 0.15f;
        S.BlendInOption = EAlphaBlendOption::CircularIn;
        S.BlendOutTime = 0.30f;
        S.BlendOutOption = EAlphaBlendOption::CircularIn;
        S.ActTime = 0.511672f;
        S.ComboTime = 1.152248f;
        S.CancelTime = 1.258885f;
        S.CancelByMoveStart = 1.262088f;
        S.CancelByMoveEnd = 1.916667f;
        S.CamResetTime = 0.898739f;
        S.MoveModeStart = 0.513629f;
        S.MoveModeEnd = 1.249791f;
        S.WarpStage1Start = 0.000000f;
        S.WarpStage1End = 0.499159f;
        S.WarpStage1RotMult = 1.0f;
        S.WarpStage2Start = 0.513629f;
        S.WarpStage2End = 1.249176f;
        S.WarpStage2Easing = EAlphaBlendOption::CircularIn;
        S.WarpStage2RotMult = 0.5f;
        S.WarpMaxRotRate = 90.0f;
        return S;
    }();

    static const FPlumPoleTimelineSpec JumpToPlatformRSpec = []()
    {
        FPlumPoleTimelineSpec S = JumpToPlatformLSpec;
        S.CancelTime = 1.260598f;
        S.CancelByMoveStart = 1.263578f;
        S.CancelByMoveEnd = 1.916667f;
        return S;
    }();

    static const FPlumPoleTimelineSpec JumpOffSpec = []()
    {
        FPlumPoleTimelineSpec S;
        S.DefaultStart = 0.0f;
        S.TimelineLength = 2.533333f;
        S.MontageLength = 2.666667f;
        S.bStopAbility = true;
        S.bAutoBlendOut = true;
        S.BlendInTime = 0.25f;
        S.BlendInOption = EAlphaBlendOption::HermiteCubic;
        S.BlendOutTime = 0.25f;
        S.BlendOutOption = EAlphaBlendOption::HermiteCubic;
        S.ActTime = 0.196933f;
        S.ComboTime = 1.320476f;
        S.CancelTime = 1.519630f;
        S.CancelByMoveStart = 1.525488f;
        S.CancelByMoveEnd = 2.666667f;
        S.CamResetTime = 0.8987392f;
        S.GravityStart = 0.305805f;
        S.GravityEnd = 1.044568f;
        S.GravityScale = 0.5f;
        S.RMInputStart = 0.206297f;
        S.RMInputEnd = 1.342442f;
        S.RMInputOffsetYaw = 180.0f;
        S.RMInputRate = 720.0f;
        return S;
    }();

    switch (Timeline)
    {
    case EPlumPoleTimeline::Dash: return DashSpec;
    case EPlumPoleTimeline::Jump_L: return JumpLSpec;
    case EPlumPoleTimeline::Jump_R: return JumpRSpec;
    case EPlumPoleTimeline::Idle_L: return IdleLSpec;
    case EPlumPoleTimeline::Idle_R: return IdleRSpec;
    case EPlumPoleTimeline::JumpToPlatform_L: return JumpToPlatformLSpec;
    case EPlumPoleTimeline::JumpToPlatform_R: return JumpToPlatformRSpec;
    case EPlumPoleTimeline::JumpOff: return JumpOffSpec;
    default: return EmptySpec;
    }
}

void AddRichCurveKey(FRichCurve& Curve, float Time, float Value, ERichCurveInterpMode InterpMode, float Tangent)
{
    const FKeyHandle Handle = Curve.AddKey(Time, Value);
    FRichCurveKey& Key = Curve.GetKey(Handle);
    Key.InterpMode = InterpMode;
    Key.TangentMode = RCTM_User;
    Key.TangentWeightMode = RCTWM_WeightedNone;
    Key.ArriveTangent = Tangent;
    Key.LeaveTangent = Tangent;
}

const FRichCurve& GetDashRateCurve()
{
    static const FRichCurve Curve = []()
    {
        FRichCurve C;
        C.PreInfinityExtrap = RCCE_Constant;
        C.PostInfinityExtrap = RCCE_Constant;
        C.DefaultValue = 1.0f;
        // PBZ GA_StandingOnPole exports[231] Dash Rate curve
        AddRichCurveKey(C, 0.0f, 1.6f, RCIM_Cubic, 0.0f);
        AddRichCurveKey(C, 1.3f, 1.025f, RCIM_Linear, 0.0f);
        return C;
    }();
    return Curve;
}

const FRichCurve& GetJumpRateCurve()
{
    static const FRichCurve Curve = []()
    {
        FRichCurve C;
        C.PreInfinityExtrap = RCCE_Constant;
        C.PostInfinityExtrap = RCCE_Constant;
        C.DefaultValue = 2.0f;
        // PBZ GA_StandingOnPole exports[236, 237, 240, 241] Jump_L/R & JumpToPlatform_L/R Rate curve
        AddRichCurveKey(C, 0.0f, 2.0f, RCIM_Cubic, 0.0f);
        AddRichCurveKey(C, 0.3f, 2.25f, RCIM_Cubic, 1.75f);
        AddRichCurveKey(C, 0.5f, 2.875f, RCIM_Cubic, -0.4166667f);
        AddRichCurveKey(C, 0.9f, 2.0f, RCIM_Cubic, -1.85f);
        AddRichCurveKey(C, 1.5f, 1.025f, RCIM_Linear, 0.0f);
        return C;
    }();
    return Curve;
}

const FRichCurve& GetJumpOffRateCurve()
{
    static const FRichCurve Curve = []()
    {
        FRichCurve C;
        C.PreInfinityExtrap = RCCE_Constant;
        C.PostInfinityExtrap = RCCE_Constant;
        C.DefaultValue = 1.3f;
        // PBZ GA_StandingOnPole exports[238] JumpOff Rate curve (constant 1.3 before 0.3, 1.0 after 1.3)
        AddRichCurveKey(C, 0.3f, 1.3f, RCIM_Cubic, 0.0f);
        AddRichCurveKey(C, 0.9f, 1.0f, RCIM_Cubic, -0.3f);
        AddRichCurveKey(C, 1.3f, 1.0f, RCIM_Linear, 0.0f);
        return C;
    }();
    return Curve;
}
} // namespace

float EvaluatePlumPoleRateCurve(EPlumPoleTimeline Timeline, float SourceTimeSeconds)
{
    switch (Timeline)
    {
    case EPlumPoleTimeline::Dash:
        return FMath::Max(GetDashRateCurve().Eval(SourceTimeSeconds), 0.05f);
    case EPlumPoleTimeline::Jump_L:
    case EPlumPoleTimeline::Jump_R:
    case EPlumPoleTimeline::JumpToPlatform_L:
    case EPlumPoleTimeline::JumpToPlatform_R:
        return FMath::Max(GetJumpRateCurve().Eval(SourceTimeSeconds), 0.05f);
    case EPlumPoleTimeline::JumpOff:
        return FMath::Max(GetJumpOffRateCurve().Eval(SourceTimeSeconds), 0.05f);
    case EPlumPoleTimeline::Idle_L:
    case EPlumPoleTimeline::Idle_R:
    case EPlumPoleTimeline::None:
    default:
        return 1.0f;
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

    const EPlumPoleTimeline Timeline = Curve == EPlumPoleTimeCurve::ForwardFlip
        ? EPlumPoleTimeline::Dash
        : (Curve == EPlumPoleTimeCurve::JumpOff ? EPlumPoleTimeline::JumpOff : EPlumPoleTimeline::Jump_L);

    constexpr int32 Steps = 64;
    float CumulativeTimes[Steps + 1];
    CumulativeTimes[0] = 0.0f;
    const float Ds = (EndSeconds - StartSeconds) / static_cast<float>(Steps);
    for (int32 i = 1; i <= Steps; ++i)
    {
        const float MidS = StartSeconds + (static_cast<float>(i) - 0.5f) * Ds;
        const float Rate = EvaluatePlumPoleRateCurve(Timeline, MidS);
        CumulativeTimes[i] = CumulativeTimes[i - 1] + Ds / Rate;
    }

    const float TargetTime = CumulativeTimes[Steps] * ClampedProgress;
    for (int32 i = 0; i < Steps; ++i)
    {
        if (TargetTime <= CumulativeTimes[i + 1])
        {
            const float SegSpan = FMath::Max(CumulativeTimes[i + 1] - CumulativeTimes[i], KINDA_SMALL_NUMBER);
            const float Alpha = (TargetTime - CumulativeTimes[i]) / SegSpan;
            return StartSeconds + (static_cast<float>(i) + Alpha) * Ds;
        }
    }
    return EndSeconds;
}

APlumPoleActor* UPlumPoleTraversalComponent::SelectPoleActor(
    const FVector& Origin,
    const FVector& FacingDirection,
    float ViewAngleDegrees,
    float MaxRange,
    bool bUse2dDistance,
    bool bPreferFarthest,
    const TArray<APlumPoleActor*>& Candidates,
    const AActor* IgnoreActor)
{
    // Exact 1:1 implementation of PBZAbilityBlueprintLibrary::GetNearestActor (0x146752c70)
    // and GetFarthestActor (0x146751020), verified directly against PBZ.exe disassembly:
    //   CosThreshold = cosf(ViewAngleDegrees * (PI / 360.0f))  [full cone angle -> 90 deg = +/-45 deg]
    const bool bCheckMaxRange = MaxRange > 0.0f;
    const float MaxRangeSq = bCheckMaxRange ? (MaxRange * MaxRange) : 0.0f;

    FVector Dir = FacingDirection;
    if (bUse2dDistance)
    {
        Dir.Z = 0.0;
    }

    bool bCheckAngle = ViewAngleDegrees > 0.0f;
    if (bCheckAngle)
    {
        const bool bDirNonZero = FMath::Abs(Dir.X) > ZeroDirTolerance
            || FMath::Abs(Dir.Y) > ZeroDirTolerance
            || FMath::Abs(Dir.Z) > ZeroDirTolerance;
        if (!bDirNonZero)
        {
            bCheckAngle = false;
        }
    }

    const float CosThreshold = bCheckAngle
        ? FMath::Cos(FMath::DegreesToRadians(ViewAngleDegrees * 0.5f))
        : -1.0f;
    const FVector NormDir = bCheckAngle ? Dir.GetSafeNormal() : FVector::ZeroVector;

    APlumPoleActor* BestActor = nullptr;
    float BestDistSq = bPreferFarthest ? -1.0f : FLT_MAX;

    for (APlumPoleActor* Candidate : Candidates)
    {
        if (!IsValid(Candidate) || Candidate == IgnoreActor)
        {
            continue;
        }

        FVector Delta = Candidate->GetActorLocation() - Origin;
        if (bUse2dDistance)
        {
            Delta.Z = 0.0;
        }

        const float DistSq = static_cast<float>(Delta.SizeSquared());
        if (bCheckMaxRange && DistSq > MaxRangeSq)
        {
            continue;
        }

        if (bCheckAngle)
        {
            const bool bDeltaNonZero = FMath::Abs(Delta.X) > ZeroDirTolerance
                || FMath::Abs(Delta.Y) > ZeroDirTolerance
                || FMath::Abs(Delta.Z) > ZeroDirTolerance;
            if (bDeltaNonZero)
            {
                const FVector DeltaDir = Delta.GetSafeNormal();
                const double Dot = FVector::DotProduct(NormDir, DeltaDir);
                if (Dot < static_cast<double>(CosThreshold))
                {
                    continue;
                }
            }
        }

        if (bPreferFarthest)
        {
            if (DistSq > BestDistSq)
            {
                BestDistSq = DistSq;
                BestActor = Candidate;
            }
        }
        else
        {
            if (DistSq < BestDistSq)
            {
                BestDistSq = DistSq;
                BestActor = Candidate;
            }
        }
    }

    return BestActor;
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
    static ConstructorHelpers::FObjectFinder<UAnimSequence> DashClip(
        TEXT("/Game/PlumPole/Animations/SourceTiming/AS_Manny_Hun_LionHead_Dodge_F_R.AS_Manny_Hun_LionHead_Dodge_F_R"));
    PoleFlipAnimation = DashClip.Object;
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
    if (bPoleAbilityActive)
    {
        ExitGravityScaleWindow();
        ExitMoveModeWindow();
        ExitPoleCmcScope();
        SetCameraAdditiveDesired(false);
    }
    if (CurrentStandingPole.IsValid())
    {
        CurrentStandingPole->Release(Character);
    }
    if (LastRequestedTargetPole.IsValid())
    {
        LastRequestedTargetPole->Release(Character);
    }
    if (ActiveTaskTargetPole.IsValid())
    {
        ActiveTaskTargetPole->Release(Character);
    }
    CurrentStandingPole.Reset();
    LastRequestedTargetPole.Reset();
    ActiveTaskTargetPole.Reset();
    ProtectVolumes.Reset();
    Super::EndPlay(EndPlayReason);
}

void UPlumPoleTraversalComponent::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!IsValid(Character) || !IsValid(CharacterMovement))
    {
        return;
    }

    CurrentFrameMoveInput = Character->GetPendingMovementInputVector();
    if (CurrentFrameMoveInput.IsNearlyZero(ZeroDirTolerance))
    {
        CurrentFrameMoveInput = Character->GetLastMovementInputVector();
    }

    if (bPoleAbilityActive && ActiveTimeline != EPlumPoleTimeline::None)
    {
        AdvanceActiveTimeline(DeltaTime);
    }

    // Consume 1-shot per-tick directional caches so they remain live regardless of PC vs Component tick order
    // without leaking stale input onto subsequent frames after release.
    bHasPendingMove2D = false;
    bHasPendingExplicitInput = false;
}

void UPlumPoleTraversalComponent::AddJumpOutProtectOverlap(const AActor* Volume)
{
    if (IsValid(Volume))
    {
        ProtectVolumes.Add(Volume);
    }
}

void UPlumPoleTraversalComponent::RemoveJumpOutProtectOverlap(const AActor* Volume)
{
    ProtectVolumes.Remove(Volume);
}

void UPlumPoleTraversalComponent::SetTraversalInputDirection(FVector WorldDirection)
{
    ExplicitDirection = WorldDirection.GetSafeNormal2D();
    ExplicitInputFrame = GFrameCounter;
    bHasPendingExplicitInput = !ExplicitDirection.IsNearlyZero(ZeroDirTolerance);
}

bool UPlumPoleTraversalComponent::LeftOrRight() const
{
    // PBZ GA_StandingOnPole.LeftOrRight(): true (Left) iff Idle_L, Jump_R, or Dash.
    return ActiveTimeline == EPlumPoleTimeline::Idle_L
        || ActiveTimeline == EPlumPoleTimeline::Jump_R
        || ActiveTimeline == EPlumPoleTimeline::Dash;
}

bool UPlumPoleTraversalComponent::IsExitTimeline() const
{
    return ActiveTimeline == EPlumPoleTimeline::JumpOff
        || ActiveTimeline == EPlumPoleTimeline::JumpToPlatform_L
        || ActiveTimeline == EPlumPoleTimeline::JumpToPlatform_R;
}

bool UPlumPoleTraversalComponent::IsIdleTimeline() const
{
    return ActiveTimeline == EPlumPoleTimeline::Idle_L
        || ActiveTimeline == EPlumPoleTimeline::Idle_R;
}

bool UPlumPoleTraversalComponent::HasPreJumpTag() const
{
    if (ActiveTimeline != EPlumPoleTimeline::Jump_L && ActiveTimeline != EPlumPoleTimeline::Jump_R)
    {
        return false;
    }
    const FPlumPoleTimelineSpec& Spec = GetTimelineSpec(ActiveTimeline);
    return ActiveAnimTime >= Spec.PreJumpStart && ActiveAnimTime <= Spec.PreJumpEnd;
}

bool UPlumPoleTraversalComponent::CanMove() const
{
    // PBZ GA_StandingOnPole.CanMove(): !X && (I || C || (K && !H))
    if (!bPoleAbilityActive || IsExitTimeline())
    {
        return false;
    }
    return IsIdleTimeline()
        || ActionPhase == EPlumPoleActionPhase::Cancel
        || (ActionPhase == EPlumPoleActionPhase::Combo && !bIsHit);
}

bool UPlumPoleTraversalComponent::CanDash() const
{
    // PBZ GA_StandingOnPole.CanDash(): !X && (I || C || K)
    if (!bPoleAbilityActive || IsExitTimeline())
    {
        return false;
    }
    return IsIdleTimeline()
        || ActionPhase == EPlumPoleActionPhase::Cancel
        || ActionPhase == EPlumPoleActionPhase::Combo;
}

bool UPlumPoleTraversalComponent::IsPlayingJumpOrDashTimeline() const
{
    return ActiveTimeline == EPlumPoleTimeline::Dash
        || ActiveTimeline == EPlumPoleTimeline::Jump_L
        || ActiveTimeline == EPlumPoleTimeline::Jump_R;
}

bool UPlumPoleTraversalComponent::IsValidOwnedPole(const APlumPoleActor* Pole) const
{
    return IsValid(Pole)
        && IsValid(Character)
        && !Pole->IsExitPole()
        && Pole->IsActivated()
        && Pole->CanPlayerJump()
        && Pole->GetOccupant() == Character.Get();
}

bool UPlumPoleTraversalComponent::IsWithinPoleArrivalTolerance(const APlumPoleActor* Pole) const
{
    if (!IsValid(Pole) || !IsValid(Character))
    {
        return false;
    }
    const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
    const float CapsuleHalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.0f;
    const FVector ActorLoc = Character->GetActorLocation();
    const FVector StandLoc = Pole->GetLandingLocation();
    const double DistXY = FVector::Dist2D(ActorLoc, StandLoc);
    const double FeetZ = ActorLoc.Z - static_cast<double>(CapsuleHalfHeight);
    const double DeltaZ = FMath::Abs(FeetZ - StandLoc.Z);
    return DistXY <= 90.0 && DeltaZ <= 130.0;
}

bool UPlumPoleTraversalComponent::HasSafePoleArrivalAndSupport(const APlumPoleActor* Pole) const
{
    if (!IsValidOwnedPole(Pole) || !IsValid(CharacterMovement))
    {
        return false;
    }
    if (!IsWithinPoleArrivalTolerance(Pole))
    {
        return false;
    }

    FFindFloorResult FloorResult;
    CharacterMovement->FindFloor(Character->GetActorLocation(), FloorResult, /*bCanUseCachedLocation=*/false);
    if (!FloorResult.IsWalkableFloor())
    {
        return false;
    }

    const FVector StandLoc = Pole->GetLandingLocation();
    const double SupportDeltaZ = FMath::Abs(static_cast<double>(FloorResult.HitResult.ImpactPoint.Z) - StandLoc.Z);
    return SupportDeltaZ <= 60.0;
}

APlumPoleActor* UPlumPoleTraversalComponent::GetCurrentPole() const
{
    return CurrentStandingPole.Get();
}

APlumPoleActor* UPlumPoleTraversalComponent::GetTargetPole() const
{
    return ActiveTaskTargetPole.IsValid() ? ActiveTaskTargetPole.Get() : LastRequestedTargetPole.Get();
}

float UPlumPoleTraversalComponent::GetTraversalProgress() const
{
    if (!bPoleAbilityActive || ActiveTimeline == EPlumPoleTimeline::None)
    {
        return 0.0f;
    }
    const FPlumPoleTimelineSpec& Spec = GetTimelineSpec(ActiveTimeline);
    const float Denom = FMath::Max(Spec.MontageLength, KINDA_SMALL_NUMBER);
    return FMath::Clamp(ActiveAnimTime / Denom, 0.0f, 1.0f);
}

void UPlumPoleTraversalComponent::GatherCandidatePoles(bool bIncludeExitPoles, TArray<APlumPoleActor*>& OutCandidates) const
{
    OutCandidates.Reset();
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    for (TActorIterator<APlumPoleActor> It(World); It; ++It)
    {
        APlumPoleActor* Candidate = *It;
        if (!IsValid(Candidate) || !Candidate->IsAvailableFor(Character.Get()))
        {
            continue;
        }
        if (!bIncludeExitPoles && Candidate->IsExitPole())
        {
            continue;
        }
        OutCandidates.Add(Candidate);
    }
}

FVector UPlumPoleTraversalComponent::GetGroundEntryDirection() const
{
    // PBZ BP_LionDance_Area.JumpAction@350-604:
    //   !GetLastMovementInputVector().IsNearlyZero(0.0001) ? LastMovementInputVector : GetActorForwardVector()
    const FVector LastMoveInput = Character->GetLastMovementInputVector();
    if (!LastMoveInput.IsNearlyZero(ZeroDirTolerance))
    {
        return LastMoveInput;
    }
    return Character->GetActorForwardVector();
}

FVector UPlumPoleTraversalComponent::GetLiveMoveWorldDirection() const
{
    if (bHasPendingMove2D
        && Move2DInputFrame <= GFrameCounter
        && (GFrameCounter - Move2DInputFrame) <= 1
        && !Move2DWorldDirection.IsNearlyZero(ZeroDirTolerance))
    {
        return Move2DWorldDirection;
    }
    if (bHasPendingExplicitInput
        && ExplicitInputFrame <= GFrameCounter
        && (GFrameCounter - ExplicitInputFrame) <= 1
        && !ExplicitDirection.IsNearlyZero(ZeroDirTolerance))
    {
        return ExplicitDirection;
    }
    if (!CurrentFrameMoveInput.IsNearlyZero(ZeroDirTolerance))
    {
        return CurrentFrameMoveInput;
    }
    const FVector Pending = Character->GetPendingMovementInputVector();
    if (!Pending.IsNearlyZero(ZeroDirTolerance))
    {
        return Pending;
    }
    return FVector::ZeroVector;
}

FVector UPlumPoleTraversalComponent::GetDashDirection(const FVector& PreferredDirection) const
{
    // PBZ GA_StandingOnPole.OnDash@471-822:
    //   !PreferredDirection.IsZero() ? PreferredDirection
    //   : (!CMC->GetLastInputVector().IsZero() ? CMC->GetLastInputVector() : ControlRotation.Vector())
    if (!PreferredDirection.IsZero())
    {
        return PreferredDirection;
    }
    const FVector LastInput = CharacterMovement->GetLastInputVector();
    if (!LastInput.IsZero())
    {
        return LastInput;
    }
    return Character->GetControlRotation().Vector();
}

APlumPoleActor* UPlumPoleTraversalComponent::FindEnterablePole() const
{
    if (bPoleAbilityActive || !IsValid(Character) || !IsValid(CharacterMovement) || !CharacterMovement->IsMovingOnGround())
    {
        return nullptr;
    }

    TArray<APlumPoleActor*> Candidates;
    GatherCandidatePoles(/*bIncludeExitPoles=*/false, Candidates);
    const FVector FacingDir = GetGroundEntryDirection();
    return SelectPoleActor(
        Character->GetActorLocation(),
        FacingDir,
        SelectorViewAngle,
        GroundEntryMaxRange,
        /*bUse2dDistance=*/true,
        /*bPreferFarthest=*/false,
        Candidates,
        nullptr);
}

APlumPoleActor* UPlumPoleTraversalComponent::FindVirtualAwarenessPole(
    float MaxRange, float ViewAngleDegrees, FVector ReferenceForward) const
{
    if (bPoleAbilityActive
        || TraversalState != EPlumPoleTraversalState::Grounded
        || !IsValid(Character)
        || !IsValid(CharacterMovement)
        || !CharacterMovement->IsMovingOnGround()
        || MaxRange <= KINDA_SMALL_NUMBER)
    {
        return nullptr;
    }

    TArray<APlumPoleActor*> Candidates;
    GatherCandidatePoles(/*bIncludeExitPoles=*/false, Candidates);
    FVector FacingDir = ReferenceForward.GetSafeNormal2D();
    if (FacingDir.IsNearlyZero(ZeroDirTolerance))
    {
        FacingDir = Character->GetActorForwardVector().GetSafeNormal2D();
    }
    return SelectPoleActor(
        Character->GetActorLocation(),
        FacingDir,
        ViewAngleDegrees,
        MaxRange,
        /*bUse2dDistance=*/true,
        /*bPreferFarthest=*/false,
        Candidates,
        nullptr);
}

bool UPlumPoleTraversalComponent::TryTraverse()
{
    if (!IsValid(Character) || !IsValid(CharacterMovement))
    {
        return false;
    }

    if (!bPoleAbilityActive)
    {
        return TryGroundEntry();
    }

    // While pole ability is active, IA_Jump is owned by the pole ability (attempts JumpOff when allowed, never ground-jumps).
    TryActiveJumpOff();
    return true;
}

bool UPlumPoleTraversalComponent::TryPoleFlip()
{
    return OnDash(FVector::ZeroVector);
}

bool UPlumPoleTraversalComponent::TryGroundEntry()
{
    if (bPoleAbilityActive || !IsValid(Character) || !IsValid(CharacterMovement) || !CharacterMovement->IsMovingOnGround())
    {
        return false;
    }

    APlumPoleActor* TargetPole = FindEnterablePole();
    if (!TargetPole)
    {
        return false;
    }

    ActivatePoleAbility();
    if (!MoveToPole(TargetPole, EPlumPoleTimeline::Dash, -1.0f, /*bFromGroundEntry=*/true))
    {
        EndPoleAbility(/*bInterruptedByMove=*/false);
        return false;
    }
    return true;
}

bool UPlumPoleTraversalComponent::TryActiveJumpOff()
{
    // PBZ GA_StandingOnPole IA_Jump: CurrentStandingPole.AllowJumpOut && !LionDancingPoleProtect && CanMove() -> JumpOff(-1.0)
    if (!bPoleAbilityActive || !CanMove() || IsInJumpOutProtectVolume())
    {
        return false;
    }

    APlumPoleActor* StandingPole = CurrentStandingPole.Get();
    if (!IsValid(StandingPole) || !StandingPole->CanAllowJumpOut())
    {
        return false;
    }

    bDynamicTargetRefreshActive = false;
    bBufferedDash = false;
    StartTimeline(EPlumPoleTimeline::JumpOff, -1.0f, EPlumPoleTraversalState::Dismounting,
        StandingPole, /*bInterrupted=*/true, /*bChained=*/false);
    return true;
}

bool UPlumPoleTraversalComponent::OnMove2D(FVector2D InputAxis2D)
{
    if (!IsValid(Character))
    {
        return false;
    }
    if (InputAxis2D.IsNearlyZero(ZeroDirTolerance))
    {
        return false;
    }

    // Match PBZ GreaterGreater_VectorRotator: rotate (Forward, Right, 0) by full ControlRotation.
    // Note ZeroAnim IA_Move maps W/S -> Y (Forward) and D/A -> X (Right).
    const FVector LocalDir(InputAxis2D.Y, InputAxis2D.X, 0.0);
    const FVector WorldDir = Character->GetControlRotation().RotateVector(LocalDir);
    Move2DInputFrame = GFrameCounter;
    Move2DWorldDirection = WorldDir;
    bHasPendingMove2D = true;

    if (!bPoleAbilityActive)
    {
        return false;
    }

    // Check CancelByMoveInput immediately on exit timelines as well.
    const FPlumPoleTimelineSpec& Spec = GetTimelineSpec(ActiveTimeline);
    if (Spec.CancelByMoveStart >= 0.0f && ActiveAnimTime >= Spec.CancelByMoveStart && ActiveAnimTime <= Spec.CancelByMoveEnd)
    {
        EndPoleAbility(/*bInterruptedByMove=*/true);
        return true;
    }

    return OnMove(WorldDir);
}

bool UPlumPoleTraversalComponent::OnMove(FVector InputWorldDir)
{
    if (!bPoleAbilityActive || !IsValid(Character) || !IsValid(CharacterMovement))
    {
        return false;
    }
    if (InputWorldDir.IsNearlyZero(ZeroDirTolerance) || !CanMove())
    {
        return false;
    }

    // Determine effective standing pole (when chaining at Combo, ActiveTaskTargetPole is the pole we are launching from).
    APlumPoleActor* IgnorePole = (ActiveTaskTargetPole.IsValid() && ActionPhase == EPlumPoleActionPhase::Combo)
        ? ActiveTaskTargetPole.Get()
        : CurrentStandingPole.Get();

    TArray<APlumPoleActor*> Candidates;
    GatherCandidatePoles(/*bIncludeExitPoles=*/true, Candidates);

    APlumPoleActor* NearPole = SelectPoleActor(
        Character->GetActorLocation(),
        InputWorldDir,
        SelectorViewAngle,
        NearHopMaxRange,
        /*bUse2dDistance=*/true,
        /*bPreferFarthest=*/false,
        Candidates,
        IgnorePole);

    if (NearPole)
    {
        const bool bChaining = IsPlayingJumpOrDashTimeline();
        const EPlumPoleTimeline NextTimeline = LeftOrRight() ? EPlumPoleTimeline::Jump_L : EPlumPoleTimeline::Jump_R;
        const float RequestedStart = bChaining ? 0.32f : -1.0f;
        return MoveToPole(NearPole, NextTimeline, RequestedStart);
    }

    // PBZ OnMove@855->1531->1715: no candidate within 350 cm -> fall back to OnDash(InputWorldDir)!
    return OnDash(InputWorldDir);
}

bool UPlumPoleTraversalComponent::OnDash(FVector PreferredDirection)
{
    if (!bPoleAbilityActive || !IsValid(Character) || !IsValid(CharacterMovement))
    {
        return false;
    }

    const bool bInPreJump = HasPreJumpTag();
    if (!bInPreJump && !CanDash())
    {
        // PBZ OnDash@1220-1400: only OnDash writes Buffered during Action.Phase.Act!
        if (!IsExitTimeline() && HasActPhase())
        {
            bBufferedDash = true;
            return true;
        }
        return false;
    }

    const FVector DashDir = GetDashDirection(PreferredDirection);
    APlumPoleActor* IgnorePole = (!bInPreJump && ActiveTaskTargetPole.IsValid() && ActionPhase == EPlumPoleActionPhase::Combo)
        ? ActiveTaskTargetPole.Get()
        : CurrentStandingPole.Get();

    TArray<APlumPoleActor*> Candidates;
    GatherCandidatePoles(/*bIncludeExitPoles=*/true, Candidates);

    APlumPoleActor* FarPole = SelectPoleActor(
        Character->GetActorLocation(),
        DashDir,
        SelectorViewAngle,
        FarDashMaxRange,
        /*bUse2dDistance=*/true,
        /*bPreferFarthest=*/true,
        Candidates,
        IgnorePole);

    if (!FarPole)
    {
        return false;
    }

    // PBZ OnDash@1098-1141: PreJump cancellation is committed transactionally inside MoveToPole
    // AFTER confirming the new target succeeds so a failed TryReserve leaves the existing task intact.
    const bool bChaining = IsPlayingJumpOrDashTimeline();
    const float RequestedStart = bChaining ? 0.20f : -1.0f;
    return MoveToPole(FarPole, EPlumPoleTimeline::Dash, RequestedStart, /*bFromGroundEntry=*/false, bInPreJump);
}

void UPlumPoleTraversalComponent::ActivatePoleAbility()
{
    if (bPoleAbilityActive)
    {
        return;
    }

    bPoleAbilityActive = true;
    bIsHit = false;
    bBufferedDash = false;
    bLastTraversalCompleted = false;
    bLastHopWasChained = false;
    ActionPhase = EPlumPoleActionPhase::None;
    LastTargetLocation = Character->GetActorLocation();

    Character->StopJumping();
    CharacterMovement->StopMovementImmediately();
    EnterPoleCmcScope();
    SetCameraAdditiveDesired(true);
}

void UPlumPoleTraversalComponent::EndPoleAbility(bool bInterruptedByMove)
{
    if (!bPoleAbilityActive)
    {
        return;
    }

    const EPlumPoleTimeline LeavingTimeline = ActiveTimeline;
    const float LeavingAnimTime = ActiveAnimTime;
    const bool bWasNaturalBlendOut = bNaturalBlendOutTriggered;

    ExitGravityScaleWindow();
    ExitMoveModeWindow();
    ExitPoleCmcScope();
    SetCameraAdditiveDesired(false);
    if (IsValid(CharacterMovement) && CharacterMovement->MovementMode == MOVE_Flying)
    {
        CharacterMovement->SetMovementMode(MOVE_Falling);
    }

    if (CurrentStandingPole.IsValid())
    {
        CurrentStandingPole->Release(Character);
    }
    if (LastRequestedTargetPole.IsValid())
    {
        LastRequestedTargetPole->Release(Character);
    }
    if (ActiveTaskTargetPole.IsValid())
    {
        ActiveTaskTargetPole->Release(Character);
    }

    CurrentStandingPole.Reset();
    LastRequestedTargetPole.Reset();
    ActiveTaskTargetPole.Reset();

    PreviousTraversalProgress = GetTraversalProgress();
    bPoleAbilityActive = false;
    bBufferedDash = false;
    bDynamicTargetRefreshActive = false;
    bActiveTimelineChained = false;
    bCamResetTriggeredForTimeline = false;
    bNaturalBlendOutTriggered = false;
    ActionPhase = EPlumPoleActionPhase::None;
    ActiveTimeline = EPlumPoleTimeline::None;
    ActiveAnimTime = 0.0f;
    ActiveRate = 1.0f;
    TraversalDuration = 0.0f;
    CachedRMInputDirection = FVector::ZeroVector;

    // If interrupted early by CancelByMoveInput (or if natural blend-out hasn't fired yet), drop outgoing rate to 1.000 s/s.
    // If natural auto-blend-out already completed uninterrupted, AnimInstance already blended out cleanly at 1.025 s/s!
    if (bInterruptedByMove || !bWasNaturalBlendOut)
    {
        OnTimelineChanged.Broadcast(LeavingTimeline, EPlumPoleTimeline::None, 0.0f, LeavingAnimTime, 1.0f, false);
    }
    PendingTransitionRemainingDelta = -1.0f;

    ChangeState(EPlumPoleTraversalState::Grounded, nullptr);
}

bool UPlumPoleTraversalComponent::MoveToPole(
    APlumPoleActor* TargetPole,
    EPlumPoleTimeline RequestedTimeline,
    float RequestedStartTimeSeconds,
    bool bFromGroundEntry,
    bool bPreJumpCancel)
{
    if (!IsValid(TargetPole))
    {
        return false;
    }

    const bool bChainedFromJump = !bFromGroundEntry && !bPreJumpCancel && IsPlayingJumpOrDashTimeline();

    // --- PBZ Exit Pole branch FIRST (BP_LionDancing_Pole_Exit_C, MoveToPole@8588-9239) ---
    // Must run BEFORE mutating or releasing normal pole pointers!
    if (TargetPole->IsExitPole())
    {
        if (bChainedFromJump && ActiveTaskTargetPole.IsValid())
        {
            if (!IsValidOwnedPole(ActiveTaskTargetPole.Get()) || !IsWithinPoleArrivalTolerance(ActiveTaskTargetPole.Get()))
            {
                return false;
            }
        }

        bDynamicTargetRefreshActive = false;
        bBufferedDash = false;

        if (bPreJumpCancel)
        {
            if (LastRequestedTargetPole.IsValid() && LastRequestedTargetPole != CurrentStandingPole)
            {
                LastRequestedTargetPole->Release(Character);
            }
            if (ActiveTaskTargetPole.IsValid() && ActiveTaskTargetPole != CurrentStandingPole)
            {
                ActiveTaskTargetPole->Release(Character);
            }
            LastRequestedTargetPole = CurrentStandingPole;
            ActiveTaskTargetPole.Reset();
        }

        // Keep CurrentStandingPole & LastRequestedTargetPole UNCHANGED for normal/chained exit per PBZ MoveToPole@8588-9239!
        WarpOriginLocation = Character->GetActorLocation();
        const FVector ExitStand = TargetPole->GetLandingLocation();
        const double DeltaZ = ExitStand.Z - LastTargetLocation.Z;
        CalcWarpingTarget(TargetPole);

        const EPlumPoleTimeline ExitTimeline = (FMath::Abs(DeltaZ) < static_cast<double>(ExitPlatformDeltaZThreshold))
            ? (LeftOrRight() ? EPlumPoleTimeline::JumpToPlatform_L : EPlumPoleTimeline::JumpToPlatform_R)
            : EPlumPoleTimeline::JumpOff;

        // Pass through RequestedStartTimeSeconds (-1.0 from Idle -> default; 0.32 from OnMove chain; 0.20 from OnDash chain)!
        StartTimeline(ExitTimeline, RequestedStartTimeSeconds, EPlumPoleTraversalState::Dismounting,
            CurrentStandingPole.Get(), /*bInterrupted=*/true, bChainedFromJump);
        return true;
    }

    // --- Normal Pole branch (transactional: validate & TryReserve BEFORE mutating/releasing existing task state!) ---
    if (bChainedFromJump && ActiveTaskTargetPole.IsValid())
    {
        if (!IsValidOwnedPole(ActiveTaskTargetPole.Get()) || !IsWithinPoleArrivalTolerance(ActiveTaskTargetPole.Get()))
        {
            return false;
        }
    }

    if (!TargetPole->TryReserve(Character))
    {
        return false;
    }

    bDynamicTargetRefreshActive = false;
    bBufferedDash = false;

    if (bPreJumpCancel)
    {
        if (LastRequestedTargetPole.IsValid() && LastRequestedTargetPole != CurrentStandingPole && LastRequestedTargetPole != TargetPole)
        {
            LastRequestedTargetPole->Release(Character);
        }
        if (ActiveTaskTargetPole.IsValid() && ActiveTaskTargetPole != CurrentStandingPole && ActiveTaskTargetPole != TargetPole)
        {
            ActiveTaskTargetPole->Release(Character);
        }
        ActiveTaskTargetPole.Reset();
    }
    else if (bChainedFromJump && ActiveTaskTargetPole.IsValid() && ActiveTaskTargetPole != CurrentStandingPole)
    {
        if (CurrentStandingPole.IsValid() && CurrentStandingPole != TargetPole)
        {
            CurrentStandingPole->Release(Character);
        }
        CurrentStandingPole = ActiveTaskTargetPole;
        ActiveTaskTargetPole.Reset();
        bLastTraversalCompleted = true;
    }

    if (LastRequestedTargetPole.IsValid() && LastRequestedTargetPole != CurrentStandingPole && LastRequestedTargetPole != TargetPole)
    {
        LastRequestedTargetPole->Release(Character);
    }
    LastRequestedTargetPole = TargetPole;
    ActiveTaskTargetPole = TargetPole;

    WarpOriginLocation = Character->GetActorLocation();
    CalcWarpingTarget(TargetPole);
    bDynamicTargetRefreshActive = true;

    const EPlumPoleTraversalState NextState = bFromGroundEntry
        ? EPlumPoleTraversalState::Entering
        : EPlumPoleTraversalState::Hopping;

    StartTimeline(RequestedTimeline, RequestedStartTimeSeconds, NextState, TargetPole,
        /*bInterrupted=*/!bFromGroundEntry, bChainedFromJump);
    return true;
}

void UPlumPoleTraversalComponent::StartTimeline(
    EPlumPoleTimeline NewTimeline,
    float RequestedStartTimeSeconds,
    EPlumPoleTraversalState NewState,
    APlumPoleActor* StatePole,
    bool bInterrupted,
    bool bChained)
{
    const EPlumPoleTimeline PrevTimeline = ActiveTimeline;
    const float PrevAnimTime = ActiveAnimTime;

    // Cleanly close any active motion/gravity windows from the previous timeline before starting the new one.
    ExitGravityScaleWindow();
    ExitMoveModeWindow();

    const FPlumPoleTimelineSpec& Spec = GetTimelineSpec(NewTimeline);
    const float ResolvedStart = RequestedStartTimeSeconds >= 0.0f ? RequestedStartTimeSeconds : Spec.DefaultStart;

    PreviousTraversalProgress = GetTraversalProgress();
    ActiveTimeline = NewTimeline;
    ActiveAnimTime = ResolvedStart;
    ActiveRate = EvaluatePlumPoleRateCurve(NewTimeline, ActiveAnimTime);
    TraversalDuration = Spec.TimelineLength;
    ActionPhase = EPlumPoleActionPhase::None;
    bActiveTimelineChained = bChained;
    bCamResetTriggeredForTimeline = false;
    bNaturalBlendOutTriggered = false;
    CachedRMInputDirection = FVector::ZeroVector;

    WarpOriginLocation = Character->GetActorLocation();
    WarpStage2StartLocation = WarpOriginLocation;
    bWarpStage1Initialized = false;
    bWarpStage2Initialized = false;

    // If starting directly inside a MoveMode or Gravity window (e.g., high StartTimeSeconds passthrough), enter immediately.
    if (Spec.MoveModeStart >= 0.0f && ActiveAnimTime >= Spec.MoveModeStart && ActiveAnimTime < Spec.MoveModeEnd)
    {
        EnterMoveModeWindow();
    }
    if (Spec.GravityStart >= 0.0f && ActiveAnimTime >= Spec.GravityStart && ActiveAnimTime < Spec.GravityEnd)
    {
        EnterGravityScaleWindow(Spec.GravityScale);
    }
    if (Spec.ActTime >= 0.0f && ActiveAnimTime >= Spec.ActTime)
    {
        ActionPhase = EPlumPoleActionPhase::Act;
    }

    // Any interrupted timeline drops immediately to 1.000 s/s during its blend-out (matching PBZ V6).
    const float OutgoingRate = 1.0f;
    OnTimelineChanged.Broadcast(PrevTimeline, NewTimeline, ResolvedStart, PrevAnimTime, OutgoingRate, false);
    ChangeState(NewState, StatePole, /*bForceBroadcast=*/true);
}

void UPlumPoleTraversalComponent::CalcWarpingTarget(const APlumPoleActor* TargetPole)
{
    if (!IsValid(TargetPole) || !IsValid(Character))
    {
        return;
    }

    const FVector TargetStand = TargetPole->GetLandingLocation();
    LastTargetLocation = TargetStand;
    WarpLandLocation = TargetStand;

    const FVector ToTarget2D = (TargetStand - WarpOriginLocation).GetSafeNormal2D();
    if (!ToTarget2D.IsNearlyZero(ZeroDirTolerance))
    {
        WarpLandRotation = ToTarget2D.Rotation();
    }
    else
    {
        WarpLandRotation = FRotator(0.0, Character->GetActorRotation().Yaw, 0.0);
    }
}

void UPlumPoleTraversalComponent::AdvanceActiveTimeline(float DeltaTime)
{
    if (DeltaTime <= 0.0f)
    {
        return;
    }

    constexpr int32 MaxSubSteps = 12;
    constexpr float BoundaryEps = 1e-6f;
    float RemainingWallDt = DeltaTime;
    bool bEnteredGravityThisFrame = false;
    float GravityActiveWallDtThisFrame = 0.0f;

    for (int32 SubStep = 0; SubStep < MaxSubSteps && RemainingWallDt > KINDA_SMALL_NUMBER; ++SubStep)
    {
        if (!bPoleAbilityActive || ActiveTimeline == EPlumPoleTimeline::None)
        {
            break;
        }

        const FPlumPoleTimelineSpec& Spec = GetTimelineSpec(ActiveTimeline);

        if (Spec.bLooping)
        {
            // P0-01: Verify perched pole is still valid, activated, owned, within tolerance, and has walkable support.
            if (!HasSafePoleArrivalAndSupport(CurrentStandingPole.Get()))
            {
                EndPoleAbility(/*bInterruptedByMove=*/false);
                return;
            }

            float NewTime = ActiveAnimTime + RemainingWallDt;
            float LoopLen = Spec.MontageLength;
            if (const APlumPoleCharacter* PoleChar = Cast<APlumPoleCharacter>(Character))
            {
                if (const UAnimSequence* ActiveSeq = PoleChar->GetSequenceForTimeline(ActiveTimeline))
                {
                    LoopLen = FMath::Max(ActiveSeq->GetPlayLength(), 0.1f);
                }
            }
            if (LoopLen > KINDA_SMALL_NUMBER)
            {
                NewTime = FMath::Fmod(NewTime, LoopLen);
            }
            ActiveAnimTime = NewTime;
            ActiveRate = 1.0f;

            // While Idle_L/R, evaluate live held movement input immediately.
            const FVector LiveDir = GetLiveMoveWorldDirection();
            if (!LiveDir.IsNearlyZero(ZeroDirTolerance))
            {
                OnMove(LiveDir);
            }
            break;
        }

        // P0-01: For non-exit pole flights, abort cleanly if the target pole was destroyed, deactivated, or lost ownership.
        if (IsPlayingJumpOrDashTimeline())
        {
            if (!IsValidOwnedPole(ActiveTaskTargetPole.Get()))
            {
                EndPoleAbility(/*bInterruptedByMove=*/false);
                return;
            }
        }

        // 1) Dynamic target refresh (PBZ CalcWarpingTarget active until Combo | Cancel).
        if (bDynamicTargetRefreshActive && ActiveTaskTargetPole.IsValid())
        {
            if (ActionPhase == EPlumPoleActionPhase::Combo || ActionPhase == EPlumPoleActionPhase::Cancel)
            {
                bDynamicTargetRefreshActive = false;
            }
            else
            {
                CalcWarpingTarget(ActiveTaskTargetPole.Get());
            }
        }

        // 2) Compute full RK2 step target for RemainingWallDt.
        const float PrevTime = ActiveAnimTime;
        const float RateStart = EvaluatePlumPoleRateCurve(ActiveTimeline, PrevTime);
        const float MidTime = PrevTime + 0.5f * RateStart * RemainingWallDt;
        const float RateMid = EvaluatePlumPoleRateCurve(ActiveTimeline, MidTime);
        const float FullNewTime = PrevTime + RateMid * RemainingWallDt;

        // 3) Find earliest chronological event threshold in (PrevTime, FullNewTime] so multi-threshold frames
        // execute motion, MoveMode collision restore, GravityScale, CameraReset, Combo input, and AutoIdle in exact order.
        float StepEndTime = FullNewTime;
        bool bSplitAtEvent = false;
        auto ConsiderEvent = [&](float EventTime)
        {
            if (EventTime >= 0.0f && PrevTime < EventTime && FullNewTime >= EventTime && EventTime < StepEndTime)
            {
                StepEndTime = EventTime;
                bSplitAtEvent = true;
            }
        };

        ConsiderEvent(Spec.MoveModeStart);
        ConsiderEvent(Spec.MoveModeEnd);
        ConsiderEvent(Spec.GravityStart);
        ConsiderEvent(Spec.GravityEnd);
        if (!bCamResetTriggeredForTimeline)
        {
            ConsiderEvent(Spec.CamResetTime);
        }
        ConsiderEvent(Spec.ActTime);
        ConsiderEvent(Spec.ComboTime);
        ConsiderEvent(Spec.CancelTime);
        ConsiderEvent(Spec.PhaseClearTime);
        ConsiderEvent(Spec.CancelByMoveStart);

        const float AutoBlendOutStart = (Spec.bStopAbility && Spec.bAutoBlendOut)
            ? FMath::Max(0.0f, Spec.MontageLength - Spec.BlendOutTime)
            : -1.0f;
        if (!bNaturalBlendOutTriggered && AutoBlendOutStart >= 0.0f)
        {
            ConsiderEvent(AutoBlendOutStart);
        }
        ConsiderEvent(Spec.AutoIdleTime);
        if (Spec.bStopAbility)
        {
            ConsiderEvent(Spec.TimelineLength);
        }

        float StepDt = RemainingWallDt;
        if (bSplitAtEvent)
        {
            const float TotalSourceSpan = FMath::Max(FullNewTime - PrevTime, KINDA_SMALL_NUMBER);
            StepDt = RemainingWallDt * FMath::Clamp((StepEndTime - PrevTime) / TotalSourceSpan, 0.0f, 1.0f);
        }
        const float PostStepRemainingDt = FMath::Max(0.0f, RemainingWallDt - StepDt);

        ActiveAnimTime = StepEndTime;
        ActiveRate = EvaluatePlumPoleRateCurve(ActiveTimeline, StepEndTime);

        // 4) Execute motion for [PrevTime, StepEndTime] BEFORE restoring MoveMode collision or switching timelines at StepEndTime!
        if (ActiveTimeline == EPlumPoleTimeline::JumpOff)
        {
            ApplyJumpOffMotion(PrevTime, StepEndTime, StepDt);
        }
        else
        {
            ApplySkewWarpMotion(PrevTime, StepEndTime, StepDt);
        }

        if (bGravityOverrideActive)
        {
            GravityActiveWallDtThisFrame += StepDt;
        }

        auto Reached = [&](float Threshold) -> bool
        {
            return Threshold >= 0.0f && PrevTime < Threshold && (StepEndTime + BoundaryEps) >= Threshold;
        };

        // 5) GAA_MoveMode window transitions (MoveModeEnd restores ECC_WorldDynamic AFTER warp motion reached MoveModeEnd).
        if (Reached(Spec.MoveModeStart) && !bMoveModeWindowActive && StepEndTime < Spec.MoveModeEnd)
        {
            EnterMoveModeWindow();
        }
        if (bMoveModeWindowActive && Spec.MoveModeEnd >= 0.0f && (StepEndTime + BoundaryEps) >= Spec.MoveModeEnd)
        {
            ExitMoveModeWindow();
        }

        // 6) GravityScale window (JumpOff: 0.5 during 0.305805 .. 1.044568 s).
        if (Reached(Spec.GravityStart) && !bGravityOverrideActive && StepEndTime < Spec.GravityEnd)
        {
            EnterGravityScaleWindow(Spec.GravityScale);
            bEnteredGravityThisFrame = true;
        }
        if (bGravityOverrideActive && Spec.GravityEnd >= 0.0f && (StepEndTime + BoundaryEps) >= Spec.GravityEnd)
        {
            // If a large DeltaTime both entered and exited the gravity window within this single tick,
            // CMC already integrated full 1.0x gravity earlier this frame; compensate the reduced-gravity sub-step.
            if (bEnteredGravityThisFrame && GravityActiveWallDtThisFrame > 0.0f
                && CharacterMovement->MovementMode == MOVE_Falling)
            {
                const float GravityZ = CharacterMovement->GetGravityZ();
                CharacterMovement->Velocity.Z -= (1.0f - Spec.GravityScale) * GravityZ * GravityActiveWallDtThisFrame;
            }
            ExitGravityScaleWindow();
            bEnteredGravityThisFrame = false;
            GravityActiveWallDtThisFrame = 0.0f;
        }

        // 7) Camera additive reset at exact 0.898739 s (JumpToPlatform_L/R) / 0.8987392 s (JumpOff).
        if (Spec.CamResetTime >= 0.0f && !bCamResetTriggeredForTimeline && (StepEndTime + BoundaryEps) >= Spec.CamResetTime)
        {
            bCamResetTriggeredForTimeline = true;
            SetCameraAdditiveDesired(false);
        }

        // 8) Action Phase transitions.
        if (Reached(Spec.ActTime))
        {
            ActionPhase = EPlumPoleActionPhase::Act;
        }
        if (Reached(Spec.ComboTime))
        {
            ActionPhase = EPlumPoleActionPhase::Combo;
            bDynamicTargetRefreshActive = false;

            // PBZ GA_StandingOnPole.ExecuteUbergraph@1564-1574, 2046, 2086:
            // Process buffered OnDash AND live directional OnMove at Combo BEFORE PhaseClear/AutoIdle!
            PendingTransitionRemainingDelta = PostStepRemainingDt;
            if (bBufferedDash)
            {
                bBufferedDash = false;
                if (OnDash(FVector::ZeroVector))
                {
                    PendingTransitionRemainingDelta = -1.0f;
                    RemainingWallDt = PostStepRemainingDt;
                    continue;
                }
            }
            if (CanMove())
            {
                const FVector LiveDir = GetLiveMoveWorldDirection();
                if (!LiveDir.IsNearlyZero(ZeroDirTolerance) && OnMove(LiveDir))
                {
                    PendingTransitionRemainingDelta = -1.0f;
                    RemainingWallDt = PostStepRemainingDt;
                    continue;
                }
            }
            PendingTransitionRemainingDelta = -1.0f;
        }
        if (Reached(Spec.CancelTime))
        {
            ActionPhase = EPlumPoleActionPhase::Cancel;
            bDynamicTargetRefreshActive = false;
        }
        if (Reached(Spec.PhaseClearTime))
        {
            ActionPhase = EPlumPoleActionPhase::None;
        }

        // 9) CancelByMoveInput interval overlap test so large DeltaTime cannot skip the cancel window!
        if (Spec.CancelByMoveStart >= 0.0f && StepEndTime >= Spec.CancelByMoveStart && PrevTime <= Spec.CancelByMoveEnd)
        {
            const FVector LiveDir = GetLiveMoveWorldDirection();
            if (!LiveDir.IsNearlyZero(ZeroDirTolerance))
            {
                PendingTransitionRemainingDelta = PostStepRemainingDt;
                EndPoleAbility(/*bInterruptedByMove=*/true);
                return;
            }
        }

        // 10) Natural AutoBlendOut for uninterrupted exit timelines (e.g., JumpToPlatform_L/R 1.6167..1.9167 s at 1.025 s/s).
        if (AutoBlendOutStart >= 0.0f && !bNaturalBlendOutTriggered && (StepEndTime + BoundaryEps) >= AutoBlendOutStart)
        {
            bNaturalBlendOutTriggered = true;
            PendingTransitionRemainingDelta = PostStepRemainingDt;
            const float TailRate = EvaluatePlumPoleRateCurve(ActiveTimeline, StepEndTime);
            OnTimelineChanged.Broadcast(ActiveTimeline, EPlumPoleTimeline::None, 0.0f, StepEndTime, TailRate, true);
            PendingTransitionRemainingDelta = -1.0f;
        }

        // 11) Auto-Idle transition (Dash -> Idle_L at 1.325300 s, Jump_L -> Idle_R / Jump_R -> Idle_L at 1.640069 s).
        if (Spec.AutoIdleTime >= 0.0f && (StepEndTime + BoundaryEps) >= Spec.AutoIdleTime)
        {
            // P0-01: Verify target pole is valid, activated, owned, within safe capsule tolerance, and has walkable support!
            APlumPoleActor* LandedPole = ActiveTaskTargetPole.IsValid() ? ActiveTaskTargetPole.Get() : CurrentStandingPole.Get();
            if (!HasSafePoleArrivalAndSupport(LandedPole))
            {
                PendingTransitionRemainingDelta = PostStepRemainingDt;
                EndPoleAbility(/*bInterruptedByMove=*/false);
                return;
            }

            bLastTraversalCompleted = true;
            bLastHopWasChained = bActiveTimelineChained;
            if (ActiveTaskTargetPole.IsValid())
            {
                if (CurrentStandingPole.IsValid() && CurrentStandingPole != ActiveTaskTargetPole)
                {
                    CurrentStandingPole->Release(Character);
                }
                CurrentStandingPole = ActiveTaskTargetPole;
                ActiveTaskTargetPole.Reset();
            }
            PendingTransitionRemainingDelta = PostStepRemainingDt;
            StartTimeline(Spec.AutoIdleTarget, 0.0f, EPlumPoleTraversalState::Perched,
                CurrentStandingPole.Get(), /*bInterrupted=*/true, /*bChained=*/false);
            PendingTransitionRemainingDelta = -1.0f;
            RemainingWallDt = PostStepRemainingDt;
            continue;
        }

        // 12) Natural end of bStopAbility exit timelines (JumpToPlatform_L/R at 1.916667 s, JumpOff at 2.533333 s).
        if (Spec.bStopAbility && (StepEndTime + BoundaryEps) >= Spec.TimelineLength)
        {
            PendingTransitionRemainingDelta = PostStepRemainingDt;
            EndPoleAbility(/*bInterruptedByMove=*/false);
            return;
        }

        // 13) Live directional input on active Combo/Cancel sub-steps.
        if (CanMove())
        {
            const FVector LiveDir = GetLiveMoveWorldDirection();
            if (!LiveDir.IsNearlyZero(ZeroDirTolerance))
            {
                PendingTransitionRemainingDelta = PostStepRemainingDt;
                const bool bTransitioned = OnMove(LiveDir);
                PendingTransitionRemainingDelta = -1.0f;
                if (bTransitioned)
                {
                    RemainingWallDt = PostStepRemainingDt;
                    continue;
                }
            }
        }

        RemainingWallDt = PostStepRemainingDt;
    }
}

void UPlumPoleTraversalComponent::ApplySkewWarpMotion(float PrevTime, float NewTime, float DeltaTime)
{
    const FPlumPoleTimelineSpec& Spec = GetTimelineSpec(ActiveTimeline);
    const APlumPoleCharacter* PoleChar = Cast<APlumPoleCharacter>(Character);
    const UAnimSequence* ActiveSeq = PoleChar ? PoleChar->GetSequenceForTimeline(ActiveTimeline) : nullptr;

    const FQuat TargetQuat = FRotator(0.0, WarpLandRotation.Yaw, 0.0).Quaternion();

    // --- Stage 1: Rotation-only SkewWarp (Jump_L/R & JumpToPlatform_L/R: 0.0 .. 0.499159 s) ---
    if (Spec.WarpStage1Start >= 0.0f && NewTime > Spec.WarpStage1Start && PrevTime < Spec.WarpStage1End)
    {
        const float StepStart = FMath::Max(PrevTime, Spec.WarpStage1Start);
        const float StepEnd = FMath::Min(NewTime, Spec.WarpStage1End);
        if (!bWarpStage1Initialized)
        {
            WarpStage1ActualStart = StepStart;
            bWarpStage1Initialized = true;
        }
        const float RotFinish = WarpStage1ActualStart + (Spec.WarpStage1End - WarpStage1ActualStart) * Spec.WarpStage1RotMult;
        if (StepStart < RotFinish)
        {
            const float RotEnd = FMath::Min(StepEnd, RotFinish);
            const float Remaining = FMath::Max(RotFinish - StepStart, RotEnd - StepStart);
            const float Alpha = FMath::Clamp((RotEnd - StepStart) / FMath::Max(Remaining, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
            const FQuat NextQuat = FQuat::Slerp(Character->GetActorQuat(), TargetQuat, Alpha);
            Character->SetActorRotation(NextQuat);
        }
    }

    const float ClipEnd = ActiveSeq ? FMath::Min(Spec.MontageLength, ActiveSeq->GetPlayLength()) : Spec.MontageLength;
    const float RemainingEnd = (Spec.AutoIdleTime > Spec.WarpStage2End)
        ? FMath::Min(Spec.AutoIdleTime, ClipEnd)
        : ClipEnd;

    // --- Stage 2: Translation + Rotation SkewWarp (bSubtractRemainingRootMotion = true, bIgnoreZAxis = false) ---
    if (Spec.WarpStage2Start >= 0.0f && NewTime > Spec.WarpStage2Start && PrevTime < Spec.WarpStage2End)
    {
        const float StepStart = FMath::Max(PrevTime, Spec.WarpStage2Start);
        const float StepEnd = FMath::Min(NewTime, Spec.WarpStage2End);
        if (!bWarpStage2Initialized)
        {
            WarpStage2StartLocation = Character->GetActorLocation();
            WarpStage2ActualStart = StepStart;
            bWarpStage2Initialized = true;
        }

        // Rotation warp (RotMult = 0.3 for Dash, 0.5 for Jump/Platform)
        const float RotFinish = WarpStage2ActualStart + (Spec.WarpStage2End - WarpStage2ActualStart) * Spec.WarpStage2RotMult;
        if (StepStart < RotFinish)
        {
            const float RotEnd = FMath::Min(StepEnd, RotFinish);
            const float Remaining = FMath::Max(RotFinish - StepStart, RotEnd - StepStart);
            const float Alpha = FMath::Clamp((RotEnd - StepStart) / FMath::Max(Remaining, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
            const FQuat NextQuat = FQuat::Slerp(Character->GetActorQuat(), TargetQuat, Alpha);
            Character->SetActorRotation(NextQuat);
        }

        // Target Actor center: feet at WarpLandLocation (+LandingFloorClearance)
        const float CapsuleHalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const FVector FinalActorTarget = WarpLandLocation + FVector(0.0, 0.0, CapsuleHalfHeight + LandingFloorClearance);

        // bSubtractRemainingRootMotion = true, bIgnoreZAxis = false:
        // Subtract exact remaining RootMotion (XY, Z) after WarpStage2End with zero arbitrary XY clamping.
        FVector WarpEndActorTarget = FinalActorTarget;
        const bool bHasRootMotion = ActiveClipHasRootMotion(WarpStage2ActualStart, Spec.WarpStage2End);
        if (bHasRootMotion && ActiveSeq && RemainingEnd > Spec.WarpStage2End)
        {
            const FVector RemainingLocal = SampleActiveClipRootInActorSpace(RemainingEnd)
                - SampleActiveClipRootInActorSpace(Spec.WarpStage2End);
            const FVector RemainingWorld = TargetQuat.RotateVector(RemainingLocal);
            WarpEndActorTarget -= RemainingWorld;
        }

        // Authentic SkewWarp translation: preserve the animation's full 3D root-motion displacement
        // (lateral curve + Z arc) and add the residual offset [Target - (Start + TotalWindowAnimRM)]
        // modulated by PBZ's exact AddTranslationEasingFunc (CircularIn for Jump/Platform, Linear for Dash).
        FVector TotalWindowAnimRM = FVector::ZeroVector;
        FVector AnimRMNow = FVector::ZeroVector;

        if (bHasRootMotion && ActiveSeq)
        {
            const FVector RootStart = SampleActiveClipRootInActorSpace(WarpStage2ActualStart);
            const FVector RootEnd = SampleActiveClipRootInActorSpace(Spec.WarpStage2End);
            const FVector RootNow = SampleActiveClipRootInActorSpace(StepEnd);
            TotalWindowAnimRM = TargetQuat.RotateVector(RootEnd - RootStart);
            AnimRMNow = TargetQuat.RotateVector(RootNow - RootStart);
        }
        else if (ActiveSeq && ActiveSeq->HasCurveData(FlipForwardCurve, false) && ActiveSeq->HasCurveData(FlipUpCurve, false))
        {
            const FVector2D StartC = SampleCurveTravelFallback(ActiveSeq, WarpStage2ActualStart);
            const FVector2D EndC = SampleCurveTravelFallback(ActiveSeq, Spec.WarpStage2End);
            const FVector2D NowC = SampleCurveTravelFallback(ActiveSeq, StepEnd);
            TotalWindowAnimRM = TargetQuat.RotateVector(FVector(EndC.X - StartC.X, 0.0, EndC.Y - StartC.Y));
            AnimRMNow = TargetQuat.RotateVector(FVector(NowC.X - StartC.X, 0.0, NowC.Y - StartC.Y));
        }

        const float WindowSpan = FMath::Max(Spec.WarpStage2End - WarpStage2ActualStart, KINDA_SMALL_NUMBER);
        const float TimeU = FMath::Clamp((StepEnd - WarpStage2ActualStart) / WindowSpan, 0.0f, 1.0f);
        const double EaseAlpha = static_cast<double>(FAlphaBlend::AlphaToBlendOption(TimeU, Spec.WarpStage2Easing));

        // True SkewWarp decomposition:
        // - XY: skew along-axis travel monotonically to TargetXY while preserving 100% of the clip's orthogonal lateral curve
        //   (zero overshoot / moonwalk when TargetDist < AnimDist, smooth scaling when TargetDist > AnimDist).
        // - Z (bIgnoreZAxis = false): preserve the clip's full Z arc + modulate net elevation residual via AddTranslationEasingFunc.
        const FVector TargetDelta = WarpEndActorTarget - WarpStage2StartLocation;
        const FVector TotalXY(TotalWindowAnimRM.X, TotalWindowAnimRM.Y, 0.0);
        const FVector CurXY(AnimRMNow.X, AnimRMNow.Y, 0.0);
        const FVector TargetXY(TargetDelta.X, TargetDelta.Y, 0.0);

        FVector DesiredXY;
        const double TotalLenSq2D = TotalXY.SizeSquared();
        if (TotalLenSq2D > 1.0)
        {
            const double ForwardProgress = FVector::DotProduct(CurXY, TotalXY) / TotalLenSq2D;
            const FVector LateralCurveXY = CurXY - TotalXY * ForwardProgress;
            DesiredXY = FVector(WarpStage2StartLocation.X, WarpStage2StartLocation.Y, 0.0)
                + TargetXY * ForwardProgress + LateralCurveXY;
        }
        else
        {
            DesiredXY = FVector(WarpStage2StartLocation.X, WarpStage2StartLocation.Y, 0.0) + TargetXY * EaseAlpha;
        }

        const double ResidualZ = TargetDelta.Z - TotalWindowAnimRM.Z;
        const double DesiredZ = WarpStage2StartLocation.Z + AnimRMNow.Z + ResidualZ * EaseAlpha;
        const FVector DesiredLocation(DesiredXY.X, DesiredXY.Y, DesiredZ);

        const FVector MoveDelta = DesiredLocation - Character->GetActorLocation();
        if (!MoveDelta.IsNearlyZero(ZeroDirTolerance))
        {
            FHitResult Hit;
            const FQuat Rot = Character->GetActorQuat();
            CharacterMovement->SafeMoveUpdatedComponent(MoveDelta, Rot, true, Hit);
            if (Hit.bBlockingHit && Hit.Time < 1.0f)
            {
                const FVector SlideDelta = FVector::VectorPlaneProject(MoveDelta * (1.0f - Hit.Time), Hit.Normal);
                if (!SlideDelta.IsNearlyZero(ZeroDirTolerance))
                {
                    CharacterMovement->SafeMoveUpdatedComponent(SlideDelta, Rot, true, Hit);
                }
            }
        }
    }

    // --- Post-warp remaining root motion (e.g., JumpToPlatform_L/R post-landing step; preserves full XY + Z) ---
    if (Spec.WarpStage2End >= 0.0f && NewTime > Spec.WarpStage2End && ActiveSeq)
    {
        const float PostStart = FMath::Max(PrevTime, Spec.WarpStage2End);
        const float PostEnd = FMath::Min(NewTime, RemainingEnd);
        if (PostEnd > PostStart)
        {
            const FVector DeltaLocal = SampleActiveClipRootInActorSpace(PostEnd) - SampleActiveClipRootInActorSpace(PostStart);
            if (!DeltaLocal.IsNearlyZero(ZeroDirTolerance))
            {
                const FQuat Rot = Character->GetActorQuat();
                const FVector WorldDelta = Rot.RotateVector(DeltaLocal);
                FHitResult Hit;
                CharacterMovement->SafeMoveUpdatedComponent(WorldDelta, Rot, true, Hit);
                if (Hit.bBlockingHit && Hit.Time < 1.0f)
                {
                    const FVector SlideDelta = FVector::VectorPlaneProject(WorldDelta * (1.0f - Hit.Time), Hit.Normal);
                    if (!SlideDelta.IsNearlyZero(ZeroDirTolerance))
                    {
                        CharacterMovement->SafeMoveUpdatedComponent(SlideDelta, Rot, true, Hit);
                    }
                }
            }
        }
    }
}

void UPlumPoleTraversalComponent::ApplyJumpOffMotion(float PrevTime, float NewTime, float DeltaTime)
{
    const FPlumPoleTimelineSpec& Spec = GetTimelineSpec(EPlumPoleTimeline::JumpOff);

    // 1) RM_Input window (0.206297 .. 1.342442 s): OffsetYaw = 180 deg, RotationRate = 720 deg/s, ResetOnEmptyInput = false.
    if (NewTime > Spec.RMInputStart && PrevTime < Spec.RMInputEnd)
    {
        const float OverlapStart = FMath::Max(PrevTime, Spec.RMInputStart);
        const float OverlapEnd = FMath::Min(NewTime, Spec.RMInputEnd);
        const float StepSpan = NewTime - PrevTime;
        const float OverlapRatio = (StepSpan > KINDA_SMALL_NUMBER)
            ? FMath::Clamp((OverlapEnd - OverlapStart) / StepSpan, 0.0f, 1.0f)
            : 1.0f;
        const float RotDeltaTime = DeltaTime * OverlapRatio;

        const FVector LiveDir = GetLiveMoveWorldDirection();
        if (!LiveDir.IsNearlyZero(ZeroDirTolerance))
        {
            CachedRMInputDirection = LiveDir;
        }
        if (!CachedRMInputDirection.IsNearlyZero(ZeroDirTolerance) && RotDeltaTime > 0.0f)
        {
            const double DesiredYaw = FRotator::NormalizeAxis(
                CachedRMInputDirection.Rotation().Yaw + static_cast<double>(Spec.RMInputOffsetYaw));
            const double CurrentYaw = FRotator::NormalizeAxis(Character->GetActorRotation().Yaw);
            const double NextYaw = FMath::FixedTurn(CurrentYaw, DesiredYaw, static_cast<double>(Spec.RMInputRate * RotDeltaTime));
            Character->SetActorRotation(FRotator(0.0, NextYaw, 0.0));
        }
    }

    // 2) Horizontal backflip root motion from AS_Manny_Hun_LionHead_JumpOut_R while gravity governs Z.
    FVector DeltaLocal = SampleActiveClipRootInActorSpace(NewTime) - SampleActiveClipRootInActorSpace(PrevTime);
    DeltaLocal.Z = 0.0;
    if (!DeltaLocal.IsNearlyZero(ZeroDirTolerance))
    {
        const FQuat Rot = Character->GetActorQuat();
        const FVector WorldDelta = Rot.RotateVector(DeltaLocal);
        FHitResult Hit;
        CharacterMovement->SafeMoveUpdatedComponent(WorldDelta, Rot, true, Hit);
        if (Hit.bBlockingHit && Hit.Time < 1.0f)
        {
            const FVector SlideDelta = FVector::VectorPlaneProject(WorldDelta * (1.0f - Hit.Time), Hit.Normal);
            if (!SlideDelta.IsNearlyZero(ZeroDirTolerance))
            {
                CharacterMovement->SafeMoveUpdatedComponent(SlideDelta, Rot, true, Hit);
            }
        }

        // Transition promptly to MOVE_Falling once the backflip clears the pole rim.
        if (CharacterMovement->MovementMode == MOVE_Walking)
        {
            FFindFloorResult FloorResult;
            CharacterMovement->FindFloor(Character->GetActorLocation(), FloorResult, false);
            if (!FloorResult.IsWalkableFloor())
            {
                CharacterMovement->SetMovementMode(MOVE_Falling);
            }
        }
    }
}

FVector UPlumPoleTraversalComponent::SampleActiveClipRootInActorSpace(float SourceTimeSeconds) const
{
    const APlumPoleCharacter* PoleChar = Cast<APlumPoleCharacter>(Character);
    const UAnimSequence* ActiveSeq = PoleChar ? PoleChar->GetSequenceForTimeline(ActiveTimeline) : nullptr;
    if (!ActiveSeq || !Character || !Character->GetMesh())
    {
        return FVector::ZeroVector;
    }

    const double ClampedTime = FMath::Clamp(static_cast<double>(SourceTimeSeconds), 0.0, static_cast<double>(ActiveSeq->GetPlayLength()));
    const FTransform RootXf = ActiveSeq->ExtractRootTrackTransform(FAnimExtractContext(ClampedTime), nullptr);
    return Character->GetMesh()->GetRelativeTransform().TransformVectorNoScale(RootXf.GetTranslation());
}

bool UPlumPoleTraversalComponent::ActiveClipHasRootMotion(float StartTime, float EndTime) const
{
    const FVector Delta = SampleActiveClipRootInActorSpace(EndTime) - SampleActiveClipRootInActorSpace(StartTime);
    return Delta.SizeSquared2D() > 1.0;
}

FVector2D UPlumPoleTraversalComponent::SampleCurveTravelFallback(const UAnimSequence* Seq, float SourceTimeSeconds) const
{
    if (!Seq)
    {
        return FVector2D::ZeroVector;
    }
    const double ClampedTime = FMath::Clamp(static_cast<double>(SourceTimeSeconds), 0.0, static_cast<double>(Seq->GetPlayLength()));
    const FAnimExtractContext Context(ClampedTime);
    return FVector2D(
        Seq->EvaluateCurveData(FlipForwardCurve, Context),
        Seq->EvaluateCurveData(FlipUpCurve, Context));
}

void UPlumPoleTraversalComponent::EnterPoleCmcScope()
{
    if (bPoleCmcScopeActive || !IsValid(CharacterMovement))
    {
        return;
    }

    SavedMaxWalkSpeed = CharacterMovement->MaxWalkSpeed;
    SavedMaxAcceleration = CharacterMovement->MaxAcceleration;
    SavedBrakingFrictionFactor = CharacterMovement->BrakingFrictionFactor;
    SavedbUseSeparateBrakingFriction = CharacterMovement->bUseSeparateBrakingFriction;
    SavedBrakingFriction = CharacterMovement->BrakingFriction;
    SavedGroundFriction = CharacterMovement->GroundFriction;
    SavedBrakingDecelerationWalking = CharacterMovement->BrakingDecelerationWalking;
    SavedRotationRate = CharacterMovement->RotationRate;
    SavedbOrientRotationToMovement = CharacterMovement->bOrientRotationToMovement;

    // Exact PBZ GA_StandingOnPole.K2_ActivateAbilityFromEvent movement parameter overrides.
    CharacterMovement->MaxWalkSpeed = 0.0f;
    CharacterMovement->MaxAcceleration = 0.0f;
    CharacterMovement->BrakingFrictionFactor = 2.0f;
    CharacterMovement->bUseSeparateBrakingFriction = true;
    CharacterMovement->BrakingFriction = 0.0f;
    CharacterMovement->GroundFriction = 8.0f;
    CharacterMovement->BrakingDecelerationWalking = 2000.0f;
    CharacterMovement->RotationRate.Yaw = 500.0;
    CharacterMovement->bOrientRotationToMovement = false;
    bPoleCmcScopeActive = true;
}

void UPlumPoleTraversalComponent::ExitPoleCmcScope()
{
    if (!bPoleCmcScopeActive || !IsValid(CharacterMovement))
    {
        return;
    }

    CharacterMovement->MaxWalkSpeed = SavedMaxWalkSpeed;
    CharacterMovement->MaxAcceleration = SavedMaxAcceleration;
    CharacterMovement->BrakingFrictionFactor = SavedBrakingFrictionFactor;
    CharacterMovement->bUseSeparateBrakingFriction = SavedbUseSeparateBrakingFriction;
    CharacterMovement->BrakingFriction = SavedBrakingFriction;
    CharacterMovement->GroundFriction = SavedGroundFriction;
    CharacterMovement->BrakingDecelerationWalking = SavedBrakingDecelerationWalking;
    CharacterMovement->RotationRate = SavedRotationRate;
    CharacterMovement->bOrientRotationToMovement = SavedbOrientRotationToMovement;
    bPoleCmcScopeActive = false;
}

void UPlumPoleTraversalComponent::EnterMoveModeWindow()
{
    if (bMoveModeWindowActive || !IsValid(Character) || !IsValid(CharacterMovement))
    {
        return;
    }

    if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
    {
        SavedWorldDynamicResponse = Capsule->GetCollisionResponseToChannel(ECC_WorldDynamic);
        Capsule->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
    }
    CharacterMovement->SetMovementMode(MOVE_Flying);
    CharacterMovement->Velocity = FVector::ZeroVector;
    bMoveModeWindowActive = true;
}

void UPlumPoleTraversalComponent::ExitMoveModeWindow()
{
    if (!bMoveModeWindowActive || !IsValid(Character) || !IsValid(CharacterMovement))
    {
        return;
    }

    if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
    {
        Capsule->SetCollisionResponseToChannel(ECC_WorldDynamic, SavedWorldDynamicResponse);
    }
    CharacterMovement->SetMovementMode(MOVE_Falling);
    bMoveModeWindowActive = false;
}

void UPlumPoleTraversalComponent::EnterGravityScaleWindow(float Scale)
{
    if (bGravityOverrideActive || !IsValid(CharacterMovement))
    {
        return;
    }
    SavedGravityScale = CharacterMovement->GravityScale;
    CharacterMovement->GravityScale = Scale;
    bGravityOverrideActive = true;
}

void UPlumPoleTraversalComponent::ExitGravityScaleWindow()
{
    if (!bGravityOverrideActive || !IsValid(CharacterMovement))
    {
        return;
    }
    CharacterMovement->GravityScale = SavedGravityScale;
    bGravityOverrideActive = false;
}

void UPlumPoleTraversalComponent::SetCameraAdditiveDesired(bool bDesired)
{
    if (bCameraAdditiveDesired != bDesired)
    {
        bCameraAdditiveDesired = bDesired;
        OnCameraAdditiveChanged.Broadcast(bDesired);
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
