#pragma once

#include "AlphaBlend.h"
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlumPoleTraversalComponent.generated.h"

class ACharacter;
class APlumPoleActor;
class UAnimSequence;
class UCharacterMovementComponent;

UENUM(BlueprintType)
enum class EPlumPoleTraversalState : uint8
{
    Grounded,
    Entering,
    Perched,
    Hopping,
    Dismounting
};

UENUM(BlueprintType)
enum class EPlumPoleTimeCurve : uint8
{
    Linear,
    NearHop,
    ForwardFlip,
    JumpOff
};

/** Exact PBZ GA_StandingOnPole exploration timelines. */
UENUM(BlueprintType)
enum class EPlumPoleTimeline : uint8
{
    None,
    Dash,
    Jump_L,
    Jump_R,
    Idle_L,
    Idle_R,
    JumpToPlatform_L,
    JumpToPlatform_R,
    JumpOff
};

/** Exact PBZ CharacterState.Action.Phase.* states. */
UENUM(BlueprintType)
enum class EPlumPoleActionPhase : uint8
{
    None,
    Act,
    Combo,
    Cancel
};

/** Evaluate PBZ's exact static FRichCurve "Rate" at the given source animation time (seconds). */
ZEROANIM_API float EvaluatePlumPoleRateCurve(EPlumPoleTimeline Timeline, float SourceTimeSeconds);

/** Legacy helper mapping normalized progress [0, 1] into clip seconds via PBZ's FRichCurve Rate curves. */
ZEROANIM_API float EvaluatePlumPoleTimeCurve(
    EPlumPoleTimeCurve Curve, float StartSeconds, float EndSeconds, float NormalizedProgress);

USTRUCT(BlueprintType)
struct FPlumPoleTraversalTiming
{
    GENERATED_BODY()

    FPlumPoleTraversalTiming() = default;
    FPlumPoleTraversalTiming(float Takeoff, float Contact, float Chain)
        : TakeoffProgress(Takeoff), ContactProgress(Contact), ChainProgress(Chain)
    {
    }

    UPROPERTY(EditAnywhere, Category = "Timing", meta = (ClampMin = "0", ClampMax = "1"))
    float TakeoffProgress = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Timing", meta = (ClampMin = "0", ClampMax = "1"))
    float ContactProgress = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Timing", meta = (ClampMin = "0", ClampMax = "1"))
    float ChainProgress = 1.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FPlumPoleTraversalStateChanged,
    EPlumPoleTraversalState, PreviousState,
    EPlumPoleTraversalState, NewState,
    APlumPoleActor*, Pole);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_SixParams(
    FPlumPoleTimelineChanged,
    EPlumPoleTimeline, PreviousTimeline,
    EPlumPoleTimeline, NewTimeline,
    float, StartTimeSeconds,
    float, PreviousAnimTimeSeconds,
    float, OutgoingRate,
    bool, bNaturalAutoBlendOut);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FPlumPoleCameraAdditiveChanged,
    bool, bEnablePoleCamera);

UCLASS(ClassGroup = (Movement), meta = (BlueprintSpawnableComponent))
class ZEROANIM_API UPlumPoleTraversalComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPlumPoleTraversalComponent();

    /** Ground entry (JumpAction) when grounded, or active JumpOff dismount when on a pole. Returns true when consumed. */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    bool TryTraverse();

    /** PBZ OnDash entry point (IA_PoleFlip / Dodge / E / RightShoulder). */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    bool TryPoleFlip();

    /** PBZ OnMove with 2D movement axis (X=Right, Y=Forward), rotated by full ControlRotation. */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    bool OnMove2D(FVector2D InputAxis2D);

    /** PBZ OnMove with world-space direction: 350 cm nearest, falling back to OnDash(InputWorldDir) on miss. */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    bool OnMove(FVector InputWorldDir);

    /** PBZ OnDash with optional preferred world direction: 500 cm farthest, PreJump early cancel, Act-phase buffer. */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    bool OnDash(FVector PreferredDirection = FVector::ZeroVector);

    /** PBZ JumpAction from ground: 500 cm, 90 deg cone, 2D nearest, excludes exit poles. */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    bool TryGroundEntry();

    /** PBZ active IA_Jump on pole: requires CurrentStandingPole->AllowJumpOut && !Protect && CanMove(). */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    bool TryActiveJumpOff();

    /** Optional world-space input from a custom movement action. Pass zero to clear it. */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    void SetTraversalInputDirection(FVector WorldDirection);

    /** Ref-counted PBZ CharacterState.LionDancingPoleProtect overlap tracking. */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    void AddJumpOutProtectOverlap(const AActor* Volume);

    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    void RemoveJumpOutProtectOverlap(const AActor* Volume);

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsInJumpOutProtectVolume() const { return ProtectVolumes.Num() > 0; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    int32 GetJumpOutProtectOverlapCount() const { return ProtectVolumes.Num(); }

    /** PBZ GA_StandingOnPole.LeftOrRight(): true (Left) iff active timeline is Idle_L, Jump_R, or Dash. */
    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool LeftOrRight() const;

    /** PBZ GA_StandingOnPole.CanMove(): !IsExitTimeline && (IsIdle || HasCancel || (HasCombo && !IsHit)). */
    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool CanMove() const;

    /** PBZ GA_StandingOnPole.CanDash(): !IsExitTimeline && (IsIdle || HasCancel || HasCombo). */
    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool CanDash() const;

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsPoleAbilityActive() const { return bPoleAbilityActive; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsExitTimeline() const;

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsIdleTimeline() const;

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool HasPreJumpTag() const;

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool HasActPhase() const { return ActionPhase == EPlumPoleActionPhase::Act; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsBufferedDash() const { return bBufferedDash; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    EPlumPoleTimeline GetActiveTimeline() const { return ActiveTimeline; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    EPlumPoleActionPhase GetActiveActionPhase() const { return ActionPhase; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    float GetActiveAnimTime() const { return ActiveAnimTime; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    float GetActiveRate() const { return ActiveRate; }

    /** Remaining wall-clock delta in the current tick after a mid-frame timeline transition (<0 means full frame). */
    float GetPendingTransitionRemainingDelta() const { return PendingTransitionRemainingDelta; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsCameraAdditiveDesired() const { return bCameraAdditiveDesired; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsPoleFlip() const { return TraversalState == EPlumPoleTraversalState::Hopping && ActiveTimeline == EPlumPoleTimeline::Dash; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsChainedPoleHop() const { return bActiveTimelineChained && (ActiveTimeline == EPlumPoleTimeline::Jump_L || ActiveTimeline == EPlumPoleTimeline::Jump_R); }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool HasCompletedChainHop() const { return TraversalState == EPlumPoleTraversalState::Perched && bLastTraversalCompleted && bLastHopWasChained; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    UAnimSequence* GetPoleFlipAnimation() const { return PoleFlipAnimation.Get(); }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    EPlumPoleTraversalState GetTraversalState() const { return TraversalState; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    APlumPoleActor* GetCurrentPole() const;

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    APlumPoleActor* GetTargetPole() const;

    /** Returns the pole that pressing Jump from the ground will currently enter, or nullptr when unavailable. */
    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    APlumPoleActor* FindEnterablePole() const;

    /**
     * Returns the nearest activated, player-jumpable, unoccupied, non-exit pole within the outer PBZ awareness radius
     * while grounded (for driving the Virtual hollow-ring state prior to entering 500 cm InputAction range).
     */
    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    APlumPoleActor* FindVirtualAwarenessPole(float MaxRange, float ViewAngleDegrees, FVector ReferenceForward = FVector::ZeroVector) const;

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    float GetTraversalDuration() const { return TraversalDuration; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    float GetTraversalElapsedTime() const { return ActiveAnimTime; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    float GetTraversalProgress() const;

    float GetPreviousTraversalProgress() const { return PreviousTraversalProgress; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    float GetTraversalTakeoffProgress() const { return ActiveTiming.TakeoffProgress; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    float GetTraversalContactProgress() const { return ActiveTiming.ContactProgress; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool WasLastTraversalCompleted() const { return bLastTraversalCompleted; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsShortDismount() const { return false; }

    /** Exact PBZ selector helper matching PBZAbilityBlueprintLibrary::GetNearestActor / GetFarthestActor. */
    static APlumPoleActor* SelectPoleActor(
        const FVector& Origin,
        const FVector& FacingDirection,
        float ViewAngleDegrees,
        float MaxRange,
        bool bUse2dDistance,
        bool bPreferFarthest,
        const TArray<APlumPoleActor*>& Candidates,
        const AActor* IgnoreActor = nullptr);

    /** Pole is the destination for Entering/Hopping, and the departure pole for Dismounting. */
    UPROPERTY(BlueprintAssignable, Category = "Plum Pole")
    FPlumPoleTraversalStateChanged OnTraversalStateChanged;

    /** Fires whenever the active PBZ timeline starts, switches, or naturally auto-blends out. */
    UPROPERTY(BlueprintAssignable, Category = "Plum Pole")
    FPlumPoleTimelineChanged OnTimelineChanged;

    /** Fires true on pole ability activation, false at 0.898739 s of exit timelines or on ability end. */
    UPROPERTY(BlueprintAssignable, Category = "Plum Pole")
    FPlumPoleCameraAdditiveChanged OnCameraAdditiveChanged;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    // --- PBZ static targeting parameters ---
    UPROPERTY(EditAnywhere, Category = "Plum Pole|PBZ Targeting", meta = (ClampMin = "0"))
    float GroundEntryMaxRange = 500.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|PBZ Targeting", meta = (ClampMin = "0"))
    float NearHopMaxRange = 350.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|PBZ Targeting", meta = (ClampMin = "0"))
    float FarDashMaxRange = 500.0f;

    /** PBZ full cone ViewAngle in degrees (half-angle = 45 deg -> cos(45 deg) = 0.70710678). */
    UPROPERTY(EditAnywhere, Category = "Plum Pole|PBZ Targeting", meta = (ClampMin = "0", ClampMax = "360"))
    float SelectorViewAngle = 90.0f;

    /** PBZ exit pole vertical threshold: Abs(DeltaZ) < 200 cm -> JumpToPlatform_L/R, else -> JumpOff. */
    UPROPERTY(EditAnywhere, Category = "Plum Pole|PBZ Targeting", meta = (ClampMin = "0"))
    float ExitPlatformDeltaZThreshold = 200.0f;

    // --- Legacy serialized properties preserved for BP_SoulThirdPerson compatibility ---
    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0"))
    float MaxHopDistance = 500.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0"))
    float ForwardFlipDistance = 350.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0"))
    float MaxPoleRise = 500.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0"))
    float MaxPoleDrop = 500.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0", ClampMax = "1"))
    float EntryAlignment = 0.707107f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0", ClampMax = "1"))
    float HopAlignment = 0.707107f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0"))
    float InputMemorySeconds = 0.22f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plum Pole|Input", meta = (AllowPrivateAccess = "true"))
    bool bAutoHopWithMovementInput = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plum Pole|Input", meta = (AllowPrivateAccess = "true"))
    bool bAutoDismountWithMovementInput = true;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Input", meta = (ClampMin = "0.01", ClampMax = "1"))
    float AutoHopInputThreshold = 0.1f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Input", meta = (ClampMin = "0"))
    float AutoDismountHoldDelay = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0.1"))
    float EnterDuration = 1.022f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0.1"))
    float HopDuration = 0.862f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0.1"))
    float ChainHopDuration = 0.808f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0.1"))
    float DismountDuration = 0.833f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy")
    FPlumPoleTraversalTiming EnterTiming;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy")
    FPlumPoleTraversalTiming HopTiming;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy")
    FPlumPoleTraversalTiming ChainHopTiming;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Forward Flip")
    TObjectPtr<UAnimSequence> PoleFlipAnimation;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy")
    FPlumPoleTraversalTiming PoleFlipTiming;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0.1"))
    float PoleFlipDuration = 1.022f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0.1"))
    float PoleFlipSectionEndSeconds = 1.310f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0.1"))
    float PoleFlipPlayRate = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy")
    FPlumPoleTraversalTiming DismountTiming;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0"))
    float JumpBufferSeconds = 0.32f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0"))
    float HopHeight = 3.5f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0"))
    float DismountDistance = 220.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0"))
    float DismountMaxRise = 75.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0"))
    float DismountMaxDrop = 475.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy")
    bool bFaceAwayFromDismount = false;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0"))
    float ShortDismountDistance = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0"))
    float ShortDismountMaxHeightDifference = 62.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy", meta = (ClampMin = "0.1"))
    float ShortDismountDuration = 0.52f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Legacy")
    FPlumPoleTraversalTiming ShortDismountTiming;

    // --- Internal PBZ state machine helpers ---
    void GatherCandidatePoles(bool bIncludeExitPoles, TArray<APlumPoleActor*>& OutCandidates) const;
    FVector GetGroundEntryDirection() const;
    FVector GetLiveMoveWorldDirection() const;
    FVector GetDashDirection(const FVector& PreferredDirection) const;
    bool IsPlayingJumpOrDashTimeline() const;
    bool IsValidOwnedPole(const APlumPoleActor* Pole) const;
    bool IsWithinPoleArrivalTolerance(const APlumPoleActor* Pole) const;
    bool HasSafePoleArrivalAndSupport(const APlumPoleActor* Pole) const;

    void ActivatePoleAbility();
    void EndPoleAbility(bool bInterruptedByMove);
    bool MoveToPole(APlumPoleActor* TargetPole, EPlumPoleTimeline RequestedTimeline, float RequestedStartTimeSeconds,
        bool bFromGroundEntry = false, bool bPreJumpCancel = false);
    void StartTimeline(EPlumPoleTimeline NewTimeline, float RequestedStartTimeSeconds,
        EPlumPoleTraversalState NewState, APlumPoleActor* StatePole, bool bInterrupted, bool bChained);
    void CalcWarpingTarget(const APlumPoleActor* TargetPole);
    void AdvanceActiveTimeline(float DeltaTime);
    void ApplySkewWarpMotion(float PrevTime, float NewTime, float DeltaTime);
    void ApplyJumpOffMotion(float PrevTime, float NewTime, float DeltaTime);
    FVector SampleActiveClipRootInActorSpace(float SourceTimeSeconds) const;
    bool ActiveClipHasRootMotion(float StartTime, float EndTime) const;
    FVector2D SampleCurveTravelFallback(const UAnimSequence* Seq, float SourceTimeSeconds) const;

    void EnterPoleCmcScope();
    void ExitPoleCmcScope();
    void EnterMoveModeWindow();
    void ExitMoveModeWindow();
    void EnterGravityScaleWindow(float Scale);
    void ExitGravityScaleWindow();
    void SetCameraAdditiveDesired(bool bDesired);
    void ChangeState(EPlumPoleTraversalState NewState, APlumPoleActor* Pole, bool bForceBroadcast = false);

    UPROPERTY(Transient)
    TObjectPtr<ACharacter> Character;

    UPROPERTY(Transient)
    TObjectPtr<UCharacterMovementComponent> CharacterMovement;

    // Pole pointers matching PBZ GA_StandingOnPole semantics
    TWeakObjectPtr<APlumPoleActor> CurrentStandingPole;
    TWeakObjectPtr<APlumPoleActor> LastRequestedTargetPole;
    TWeakObjectPtr<APlumPoleActor> ActiveTaskTargetPole;
    TSet<TWeakObjectPtr<const AActor>> ProtectVolumes;

    // Core PBZ timeline & phase state
    EPlumPoleTraversalState TraversalState = EPlumPoleTraversalState::Grounded;
    EPlumPoleTimeline ActiveTimeline = EPlumPoleTimeline::None;
    EPlumPoleActionPhase ActionPhase = EPlumPoleActionPhase::None;
    float ActiveAnimTime = 0.0f;
    float ActiveRate = 1.0f;
    float TraversalDuration = 0.0f;
    float PreviousTraversalProgress = 0.0f;
    FPlumPoleTraversalTiming ActiveTiming;

    bool bPoleAbilityActive = false;
    bool bIsHit = false;
    bool bBufferedDash = false;
    bool bActiveTimelineChained = false;
    bool bLastTraversalCompleted = false;
    bool bLastHopWasChained = false;
    bool bCameraAdditiveDesired = false;
    bool bCamResetTriggeredForTimeline = false;
    bool bNaturalBlendOutTriggered = false;
    bool bDynamicTargetRefreshActive = false;

    // Warp target & motion state
    FVector LastTargetLocation = FVector::ZeroVector;
    FVector WarpLandLocation = FVector::ZeroVector;
    FRotator WarpLandRotation = FRotator::ZeroRotator;
    FVector WarpOriginLocation = FVector::ZeroVector;
    FVector WarpStage2StartLocation = FVector::ZeroVector;
    float WarpStage1ActualStart = 0.0f;
    float WarpStage2ActualStart = 0.0f;
    bool bWarpStage1Initialized = false;
    bool bWarpStage2Initialized = false;

    // Input tracking
    FVector CurrentFrameMoveInput = FVector::ZeroVector;
    FVector ExplicitDirection = FVector::ZeroVector;
    uint64 ExplicitInputFrame = MAX_uint64;
    bool bHasPendingExplicitInput = false;
    uint64 Move2DInputFrame = MAX_uint64;
    bool bHasPendingMove2D = false;
    FVector Move2DWorldDirection = FVector::ZeroVector;
    FVector CachedRMInputDirection = FVector::ZeroVector;
    float PendingTransitionRemainingDelta = -1.0f;

    // Saved CMC & collision state for PBZ scopes
    bool bPoleCmcScopeActive = false;
    float SavedMaxWalkSpeed = 600.0f;
    float SavedMaxAcceleration = 1500.0f;
    float SavedBrakingFrictionFactor = 1.0f;
    bool SavedbUseSeparateBrakingFriction = false;
    float SavedBrakingFriction = 0.0f;
    float SavedGroundFriction = 8.0f;
    float SavedBrakingDecelerationWalking = 2000.0f;
    FRotator SavedRotationRate = FRotator(0.0, 500.0, 0.0);
    bool SavedbOrientRotationToMovement = true;

    bool bMoveModeWindowActive = false;
    ECollisionResponse SavedWorldDynamicResponse = ECR_Block;

    bool bGravityOverrideActive = false;
    float SavedGravityScale = 1.0f;
};
