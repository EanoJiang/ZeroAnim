#pragma once

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

UCLASS(ClassGroup = (Movement), meta = (BlueprintSpawnableComponent))
class ZEROANIM_API UPlumPoleTraversalComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPlumPoleTraversalComponent();

    /** Returns true when traversal consumes the jump. */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    bool TryTraverse();

    /** Request a forward flip to a reachable pole. Can be buffered during entry or a hop. */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    bool TryPoleFlip();

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsPoleFlip() const { return TraversalState == EPlumPoleTraversalState::Hopping && ArcStyle == EArcStyle::PoleFlip; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsChainedPoleHop() const { return TraversalState == EPlumPoleTraversalState::Hopping && ArcStyle == EArcStyle::ChainHop; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool HasCompletedChainHop() const
    {
        return TraversalState == EPlumPoleTraversalState::Perched && bLastTraversalCompleted && ArcStyle == EArcStyle::ChainHop;
    }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    UAnimSequence* GetPoleFlipAnimation() const { return IsPoleFlip() ? ActiveFlipAnimation.Get() : PoleFlipAnimation.Get(); }

    /** Optional world-space input from a custom movement action. Pass zero to clear it. */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole")
    void SetTraversalInputDirection(FVector WorldDirection);

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    EPlumPoleTraversalState GetTraversalState() const { return TraversalState; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    APlumPoleActor* GetCurrentPole() const;

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    APlumPoleActor* GetTargetPole() const;

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    float GetTraversalDuration() const { return TraversalDuration; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    float GetTraversalElapsedTime() const { return TraversalElapsed; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    float GetTraversalProgress() const;

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    float GetTraversalTakeoffProgress() const { return ActiveTiming.TakeoffProgress; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    float GetTraversalContactProgress() const { return ActiveTiming.ContactProgress; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool WasLastTraversalCompleted() const { return bLastTraversalCompleted; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsShortDismount() const { return TraversalState == EPlumPoleTraversalState::Dismounting && ArcStyle == EArcStyle::ShortDismount; }

    /** Pole is the destination for Entering/Hopping, and the departure pole for Dismounting. */
    UPROPERTY(BlueprintAssignable, Category = "Plum Pole")
    FPlumPoleTraversalStateChanged OnTraversalStateChanged;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    enum class EArcStyle : uint8 { Normal, ChainHop, PoleFlip, ShortDismount };

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Targeting", meta = (ClampMin = "0"))
    float MaxHopDistance = 480.0f;

    /** Horizontal center-to-center distance above which the regular jump uses a forward flip. */
    UPROPERTY(EditAnywhere, Category = "Plum Pole|Targeting", meta = (ClampMin = "0"))
    float ForwardFlipDistance = 350.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Targeting", meta = (ClampMin = "0"))
    float MaxPoleRise = 210.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Targeting", meta = (ClampMin = "0"))
    float MaxPoleDrop = 220.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Targeting", meta = (ClampMin = "0", ClampMax = "1"))
    float EntryAlignment = 0.2f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Targeting", meta = (ClampMin = "0", ClampMax = "1"))
    float HopAlignment = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Targeting", meta = (ClampMin = "0"))
    float InputMemorySeconds = 0.22f;

    /** Continuously hop between poles while movement is held. An explicit jump is required to dismount. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plum Pole|Input", meta = (AllowPrivateAccess = "true"))
    bool bAutoHopWithMovementInput = false;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Input", meta = (ClampMin = "0.01", ClampMax = "1", EditCondition = "bAutoHopWithMovementInput"))
    float AutoHopInputThreshold = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Timing", meta = (ClampMin = "0.1"))
    float EnterDuration = 0.94f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Timing", meta = (ClampMin = "0.1"))
    float HopDuration = 1.40f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Timing", meta = (ClampMin = "0.1"))
    float ChainHopDuration = 156.0f / 120.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Timing", meta = (ClampMin = "0.1"))
    float DismountDuration = 1.24f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Timing")
    FPlumPoleTraversalTiming EnterTiming;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Timing")
    FPlumPoleTraversalTiming HopTiming;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Timing")
    FPlumPoleTraversalTiming ChainHopTiming;

    /** In-place right-foot departure with PlumPoleSourceTravelForward and PlumPoleSourceTravelUp in cm. */
    UPROPERTY(EditAnywhere, Category = "Plum Pole|Forward Flip")
    TObjectPtr<UAnimSequence> PoleFlipAnimation;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Forward Flip")
    FPlumPoleTraversalTiming PoleFlipTiming;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Forward Flip", meta = (ClampMin = "0.1"))
    float PoleFlipPlayRate = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Timing")
    FPlumPoleTraversalTiming DismountTiming;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Timing", meta = (ClampMin = "0"))
    float JumpBufferSeconds = 0.32f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Trajectory", meta = (ClampMin = "0"))
    float HopHeight = 27.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Dismount", meta = (ClampMin = "0"))
    float DismountDistance = 360.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Dismount", meta = (ClampMin = "0"))
    float DismountMaxRise = 75.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Dismount", meta = (ClampMin = "0"))
    float DismountMaxDrop = 475.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Dismount")
    bool bFaceAwayFromDismount = true;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Dismount", meta = (ClampMin = "0"))
    float ShortDismountDistance = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Dismount", meta = (ClampMin = "0"))
    float ShortDismountMaxHeightDifference = 62.0f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Dismount", meta = (ClampMin = "0.1"))
    float ShortDismountDuration = 0.52f;

    UPROPERTY(EditAnywhere, Category = "Plum Pole|Dismount")
    FPlumPoleTraversalTiming ShortDismountTiming;

    void RememberMovementInput();
    FVector GetTravelDirection() const;
    FVector GetAutoHopDirection() const;
    bool TryStartFromGround(const FVector& Direction);
    bool ApplyPoleInput(bool bFlipRequested);
    bool TryStartFromPerch(const FVector& Direction, EArcStyle RequestedStyle, bool bAllowDismount = true);
    bool PreparePoleFlip();
    FVector2D SamplePoleFlipTravel(float Progress) const;
    APlumPoleActor* FindPole(const FVector& Direction, bool bEntering, FVector& OutCarryVelocity,
        EArcStyle RequestedStyle, bool bCanFlip, EArcStyle& OutStyle) const;
    bool FindDismountLocation(const FVector& Direction, FVector& OutLocation, bool& bOutShortDismount) const;
    FVector GetPoleCapsuleLocation(const APlumPoleActor& Pole) const;
    bool IsReservedAnchor(const APlumPoleActor* Pole, const FVector& Location) const;
    FPlumPoleTraversalTiming GetTiming(EPlumPoleTraversalState ArcState, EArcStyle Style) const;
    float GetDuration(EPlumPoleTraversalState ArcState, EArcStyle Style) const;
    FVector GetArcLocation(const FVector& Start, const FVector& End, EPlumPoleTraversalState ArcState,
        const FVector& CarryVelocity, const FPlumPoleTraversalTiming& Timing, float Duration, float Progress,
        EArcStyle Style) const;
    bool CanFollowArc(const FVector& Destination, EPlumPoleTraversalState ArcState, const FVector& CarryVelocity,
        EArcStyle Style = EArcStyle::Normal) const;
    bool SweepCapsule(const FVector& From, const FVector& To, FHitResult& Hit) const;
    bool StartArc(EPlumPoleTraversalState ArcState, const FVector& Destination, APlumPoleActor* DestinationPole,
        const FVector& CarryVelocity = FVector::ZeroVector, EArcStyle Style = EArcStyle::Normal,
        bool bFromCompletedPoleArc = false);
    void AdvanceArc(float DeltaTime);
    bool TryChainFromPoleArc(float OverflowDelta);
    void CompletePoleArc();
    void FinishDismount();
    void InterruptArc();
    void ReturnToCharacterMovement(const FVector& Velocity = FVector::ZeroVector, bool bLanded = false);
    void ChangeState(EPlumPoleTraversalState NewState, APlumPoleActor* Pole, bool bForceBroadcast = false);

    UPROPERTY(Transient)
    TObjectPtr<ACharacter> Character;

    UPROPERTY(Transient)
    TObjectPtr<UCharacterMovementComponent> CharacterMovement;

    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> ActiveFlipAnimation;

    TWeakObjectPtr<APlumPoleActor> CurrentPole;
    TWeakObjectPtr<APlumPoleActor> TargetPole;
    FVector2D FlipReleaseTravel = FVector2D::ZeroVector;
    FVector2D FlipContactTravel = FVector2D::ZeroVector;
    EArcStyle ArcStyle = EArcStyle::Normal;
    EArcStyle BufferedStyle = EArcStyle::Normal;
    EPlumPoleTraversalState TraversalState = EPlumPoleTraversalState::Grounded;
    FVector LastMoveDirection = FVector::ZeroVector;
    FVector ExplicitDirection = FVector::ZeroVector;
    float ExplicitInputMagnitude = 0.0f;
    uint64 ExplicitInputFrame = MAX_uint64;
    double LastMoveInputTime = -1.0;
    double ExplicitDirectionTime = -1.0;
    FVector ArcStart = FVector::ZeroVector;
    FVector ArcEnd = FVector::ZeroVector;
    FVector ArcCarryVelocity = FVector::ZeroVector;
    FVector BufferedDirection = FVector::ZeroVector;
    FPlumPoleTraversalTiming ActiveTiming;
    double BufferedJumpExpires = -1.0;
    float ArcStartYaw = 0.0f;
    float ArcYawDelta = 0.0f;
    float TraversalElapsed = 0.0f;
    float TraversalDuration = 0.0f;
    bool bArcActive = false;
    bool bMovementLocked = false;
    bool bLastTraversalCompleted = false;
    bool bJustLandedFromHop = false;
};
