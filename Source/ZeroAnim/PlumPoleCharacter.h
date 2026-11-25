#pragma once

#include "AlphaBlend.h"
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "PlumPoleTraversalComponent.h"
#include "PlumPoleCharacter.generated.h"

class UAnimSequence;
class UCameraComponent;
class UMirrorDataTable;
class UInputAction;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;
class UPlumPoleInteractPromptWidget;
class USpringArmComponent;
class UWidgetComponent;
class FPlumPoleInputDeviceProcessor;
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
    EAlphaBlendOption BlendOption = EAlphaBlendOption::HermiteCubic;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
    EPlumPoleTimeCurve TimeCurve = EPlumPoleTimeCurve::Linear;
};

UCLASS(Blueprintable)
class ZEROANIM_API APlumPoleCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    APlumPoleCharacter();

    virtual void Jump() override;
    virtual void Tick(float DeltaSeconds) override;
    FPlumPoleAnimationFrame GetTraversalAnimationFrame() const;

    UFUNCTION(BlueprintPure, Category = "Plum Pole|UI")
    bool IsUsingGamepad() const { return bUsingGamepad; }

    UFUNCTION(BlueprintCallable, Category = "Plum Pole|UI")
    void SetUsingGamepadForPrompt(bool bInUsingGamepad);

    UFUNCTION(BlueprintPure, Category = "Plum Pole|UI")
    bool IsInteractPromptVisible() const { return PromptAlpha > 0.05f && PromptTargetPole.IsValid(); }

    UFUNCTION(BlueprintPure, Category = "Plum Pole|UI")
    APlumPoleActor* GetInteractPromptPole() const { return PromptTargetPole.Get(); }

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
    TSharedPtr<FPlumPoleInputDeviceProcessor> InputDeviceProcessor;
    void RegisterInputDeviceDetector();
    void UnregisterInputDeviceDetector();
    void RemovePoleInputContext();
    void HandlePoleFlipInput();
    void ResolveActiveCameraComponents();
    void UpdatePlumPoleCamera(float DeltaSeconds);
    void UpdateInteractPrompt(float DeltaSeconds);

    UFUNCTION()
    void OnTraversalChanged(
        EPlumPoleTraversalState PreviousState, EPlumPoleTraversalState NewState, APlumPoleActor* Pole);

    UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Plum Pole|Animation",
        meta = (AllowPrivateAccess = "true"))
    FPlumPolePlayback ActivePoleAnimation;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Plum Pole", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UPlumPoleTraversalComponent> PlumPoleTraversal;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Plum Pole|UI", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UWidgetComponent> InteractPromptComponent;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI", meta = (AllowPrivateAccess = "true"))
    TSubclassOf<UPlumPoleInteractPromptWidget> InteractPromptWidgetClass;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI", meta = (AllowPrivateAccess = "true"))
    bool bShowInteractPrompt = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI", meta = (AllowPrivateAccess = "true"))
    float InteractPromptHeightBelowCrown = 34.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI", meta = (AllowPrivateAccess = "true", ClampMin = "0.05"))
    float InteractPromptBaseScale = 0.21f;

    /** Smoothly blend the third-person camera between PBZ grounded over-the-shoulder view and elevated pole view. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Camera", meta = (AllowPrivateAccess = "true"))
    bool bUsePlumPoleCameraBlend = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Camera", meta = (AllowPrivateAccess = "true", ClampMin = "100"))
    float GroundCameraArmLength = 340.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Camera", meta = (AllowPrivateAccess = "true"))
    FVector GroundCameraSocketOffset = FVector(0.0, 38.0, 44.0);

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Camera", meta = (AllowPrivateAccess = "true"))
    FVector GroundCameraTargetOffset = FVector(0.0, 0.0, 24.0);

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Camera", meta = (AllowPrivateAccess = "true", ClampMin = "40", ClampMax = "110"))
    float GroundCameraFOV = 68.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Camera", meta = (AllowPrivateAccess = "true", ClampMin = "150"))
    float PoleCameraArmLength = 540.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Camera", meta = (AllowPrivateAccess = "true"))
    FVector PoleCameraSocketOffset = FVector(0.0, 0.0, 0.0);

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Camera", meta = (AllowPrivateAccess = "true"))
    FVector PoleCameraTargetOffset = FVector(0.0, 0.0, -10.0);

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Camera", meta = (AllowPrivateAccess = "true", ClampMin = "40", ClampMax = "110"))
    float PoleCameraFOV = 72.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Camera", meta = (AllowPrivateAccess = "true", ClampMin = "0.5"))
    float CameraBlendSpeed = 4.5f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Camera", meta = (AllowPrivateAccess = "true", ClampMin = "-60", ClampMax = "20"))
    float InitialCameraPitch = -15.0f;

    /** Subtle additive pitch bias (degrees, negative looks further down) smoothly applied while on poles and released on ground. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Camera", meta = (AllowPrivateAccess = "true", ClampMin = "-25", ClampMax = "15"))
    float PoleCameraPitchOffset = -5.0f;

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
    FVector2D EnterSection = FVector2D(0.0, 1.310 / 1.983333);

    /** PBZ Dodge_F_R ground entry lands on the left foot (false). Set true if using a right-foot entry clip. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true"))
    bool bEnterLandsOnRightFoot = false;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0", ClampMax = "1"))
    FVector2D HopSection = FVector2D(0.200 / 2.183333, 1.640 / 2.183333);

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0", ClampMax = "1"))
    FVector2D ChainHopSection = FVector2D(0.320 / 2.183333, 1.640 / 2.183333);

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0", ClampMax = "1"))
    FVector2D PoleFlipSection = FVector2D(0.0, 1.310 / 1.983333);

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0", ClampMax = "1"))
    FVector2D ExitSection = FVector2D(0.200 / 1.933333, 1.610 / 1.933333);

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
    float PerchBlendTime = 0.50f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
    float ChainHopSettleBlendTime = 0.50f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
    float EntrySettleBlendTime = 0.50f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
    float TraversalBlendTime = 0.15f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
    float PoleFlipBlendTime = 0.25f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
    float GroundBlendTime = 0.30f;

    /** Height of Manny's ball bone above the visible boot sole at the pole contact. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
    float PoleSoleHeight = 0.75f;

    UPROPERTY(Transient)
    TObjectPtr<USpringArmComponent> CachedCameraBoom;

    UPROPERTY(Transient)
    TObjectPtr<UCameraComponent> CachedFollowCamera;

    TWeakObjectPtr<APlumPoleActor> PromptTargetPole;
    float PromptAlpha = 0.0f;
    float AppliedPolePitchOffset = 0.0f;
    bool bUsingGamepad = false;
    bool bRightFootSupport = true;
};
