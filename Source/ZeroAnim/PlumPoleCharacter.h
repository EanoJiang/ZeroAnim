#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "PlumPoleTraversalComponent.h"
#include "PlumPoleCharacter.generated.h"

class UAnimSequence;
class UMirrorDataTable;
class UInputAction;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;
struct FPlumPoleAnimationFrame;

UENUM(BlueprintType)
enum class EPlumPoleInputResponse : uint8
{
    Ignore,
    Consume,
    Enter,
    HopOrDismount,
    PoleFlip,
    BufferRequest,
    ChainFromGround
};

/** Animation chosen when a pole traversal state starts. A null sequence resumes ground locomotion. */
USTRUCT(BlueprintType)
struct ZEROANIM_API FPlumPolePlayback
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
    TObjectPtr<UAnimSequence> Sequence;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
    bool bLoop = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0"))
    float BlendTime = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
    FVector2D Section = FVector2D(0.0, 1.0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
    TObjectPtr<UMirrorDataTable> MirrorTable;
};

UCLASS(Blueprintable)
class ZEROANIM_API APlumPoleCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    APlumPoleCharacter();

    virtual void Jump() override;
    FPlumPoleAnimationFrame GetTraversalAnimationFrame() const;

    /** Translate a jump or flip press into a traversal request. Movement validates the requested transition. */
    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Plum Pole|Input")
    EPlumPoleInputResponse SelectPoleInputResponse(EPlumPoleTraversalState State, bool bFlipRequested, bool bCanChainFromGround) const;

    /** Rank a reachable pole. A lower score gives that pole priority. HeightDelta is signed in cm. */
    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Plum Pole|Targeting")
    float ScorePoleCandidate(float DirectionAlignment, float HorizontalDistance, float HeightDelta, float SearchRadius) const;

    /** Called on the game thread for every state change, after updating the support foot. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Plum Pole|Animation")
    FPlumPolePlayback SelectPoleAnimation(EPlumPoleTraversalState State, EPlumPoleTraversalState PreviousState,
        bool bRightSupport, bool bPoleFlip, bool bShortExit, bool bChainedHop) const;

protected:
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void NotifyControllerChanged() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void BeginPlay() override;

private:
    UPROPERTY(EditDefaultsOnly, Category = "Plum Pole|Input")
    TObjectPtr<UInputMappingContext> PoleInputMappingContext;

    UPROPERTY(EditDefaultsOnly, Category = "Plum Pole|Input")
    TObjectPtr<UInputAction> PoleFlipInputAction;

    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> PoleInputOwner;
    void RemovePoleInputContext();
    void HandlePoleFlipInput();

    UFUNCTION()
    void OnTraversalChanged(
        EPlumPoleTraversalState PreviousState, EPlumPoleTraversalState NewState, APlumPoleActor* Pole);

    UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Plum Pole|Animation",
        meta = (AllowPrivateAccess = "true"))
    FPlumPolePlayback ActivePoleAnimation;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Plum Pole", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UPlumPoleTraversalComponent> PlumPoleTraversal;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> EnterPole;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> PerchRight;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> PerchLeft;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> HopFromRight;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> HopFromLeft;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> ChainHopFromRight;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> ChainHopFromLeft;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> ExitPole;

    /** Optional separate exit for a left-foot perch. The mirror table applies when this is unset. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> ExitPoleLeft;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> ShortExitFromRight;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> ShortExitFromLeft;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UMirrorDataTable> ExitMirrorTable;

    /** Normalized source clip ranges. Time in the range follows the traversal progress. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0", ClampMax = "1"))
    FVector2D EnterSection = FVector2D(0.0, 1.0);

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0", ClampMax = "1"))
    FVector2D HopSection = FVector2D(0.0, 1.0);

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0", ClampMax = "1"))
    FVector2D ExitSection = FVector2D(0.0, 1.0);

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
    float PerchBlendTime = 0.10f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
    float ChainHopSettleBlendTime = 0.10f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
    float EntrySettleBlendTime = 0.10f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
    float TraversalBlendTime = 0.12f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
    float GroundBlendTime = 0.18f;

    /** Height of Manny's ball bone above the visible boot sole at the pole contact. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
    float PoleSoleHeight = 0.75f;

    bool bRightFootSupport = true;
};
