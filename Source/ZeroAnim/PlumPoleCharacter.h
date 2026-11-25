#pragma once

#include "AlphaBlend.h"
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "PlumPoleInteractPromptWidget.h"
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
struct FInputActionValue;
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

    UFUNCTION(BlueprintPure, Category = "Plum Pole")
    bool IsPoleAbilityActive() const;

    UFUNCTION(BlueprintPure, Category = "Plum Pole|Animation")
    UAnimSequence* GetSequenceForTimeline(EPlumPoleTimeline Timeline) const;

    UFUNCTION(BlueprintPure, Category = "Plum Pole|UI")
    bool IsUsingGamepad() const { return bUsingGamepad; }

    UFUNCTION(BlueprintCallable, Category = "Plum Pole|UI")
    void SetUsingGamepadForPrompt(bool bInUsingGamepad);

    UFUNCTION(BlueprintPure, Category = "Plum Pole|UI")
    bool IsInteractPromptVisible() const { return PromptAlpha > 0.05f && PromptTargetPole.IsValid(); }

    UFUNCTION(BlueprintPure, Category = "Plum Pole|UI")
    APlumPoleActor* GetInteractPromptPole() const { return PromptTargetPole.Get(); }

    UFUNCTION(BlueprintPure, Category = "Plum Pole|UI")
    EPlumPoleInteractUIState GetInteractPromptUIState() const { return CurrentPromptUIState; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole|UI")
    UPlumPoleInteractPromptWidget* GetInteractPromptWidget() const;

    UFUNCTION(BlueprintCallable, Category = "Plum Pole|UI")
    void SetInteractPromptInCombat(bool bInCombat);

    UFUNCTION(BlueprintCallable, Category = "Plum Pole|UI")
    void SetInteractPromptGamepadStyle(EPlumPoleGamepadIconStyle InStyle);

    /** Select playback for an exact PBZ timeline (Dash, Jump_L/R, Idle_L/R, JumpToPlatform_L/R, JumpOff). */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Plum Pole|Animation")
    FPlumPolePlayback ChooseTimelinePlayback(EPlumPoleTimeline Timeline, float StartTimeSeconds) const;

    /** Legacy input response hook kept for Blueprint compatibility. */
    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Plum Pole|Input")
    EPlumPoleInputResponse SelectPoleInputResponse(EPlumPoleTraversalState State, bool bFlipRequested, bool bCanChainFromGround) const;

    /** Legacy candidate scorer hook kept for Blueprint compatibility (unused by PBZ 1:1 selector). */
    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Plum Pole|Targeting")
    float ScorePoleCandidate(float DirectionAlignment, float HorizontalDistance, float HeightDelta, float SearchRadius) const;

    /** Legacy state-based animation selector kept for Blueprint compatibility. */
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

    /** Axis2D movement action (default /Game/Input/Actions/IA_Move) routed to PBZ OnMove2D. */
    UPROPERTY(EditDefaultsOnly, Category = "Plum Pole|Input")
    TObjectPtr<UInputAction> PoleMoveInputAction;

    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> PoleInputOwner;
    TSharedPtr<FPlumPoleInputDeviceProcessor> InputDeviceProcessor;
    void RegisterInputDeviceDetector();
    void UnregisterInputDeviceDetector();
    void RemovePoleInputContext();
    void HandlePoleFlipInput();
    void HandlePoleMoveInput(const FInputActionValue& Value);
    void ResolveActiveCameraComponents();
    void ReconcileCameraSpringArmOwner(USpringArmComponent* ActiveArm);
    void RestoreAppliedCameraAdditive();
    void UpdatePlumPoleCamera(float DeltaSeconds);
    void UpdateInteractPrompt(float DeltaSeconds);

    UFUNCTION()
    void OnTimelineChanged(EPlumPoleTimeline PreviousTimeline, EPlumPoleTimeline NewTimeline,
        float StartTimeSeconds, float PreviousAnimTimeSeconds, float OutgoingRate, bool bNaturalAutoBlendOut);

    UFUNCTION()
    void OnCameraAdditiveChanged(bool bEnablePoleCamera);

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

    /** Vertical offset (cm) above pole StandPoint (crown) for Screen-space PBZ prompt placement. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI", meta = (AllowPrivateAccess = "true"))
    float PolePromptCrownOffsetZ = 18.0f;

    /** Outer PBZ awareness radius (cm) showing the hollow white Virtual ring before entering 500 cm InputAction range. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
    float InteractPromptVirtualRadius = 900.0f;

    /** Full horizontal view cone (deg) around camera forward for outer Virtual awareness (0 = 360 deg). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI", meta = (AllowPrivateAccess = "true", ClampMin = "0", ClampMax = "360"))
    float InteractPromptVirtualViewAngle = 150.0f;

    /** Drive VirtualScreen / InputActionScreen directional arrow when tracked pole projects outside viewport safe margin. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI", meta = (AllowPrivateAccess = "true"))
    bool bEnableOffscreenPromptArrow = true;

    /** Safe pixel margin from viewport edges before clamping to *Screen directional arrow state. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
    float InteractPromptScreenMargin = 56.0f;

    // Legacy World-space prompt properties retained for BP_SoulThirdPerson serialization compatibility
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Legacy UI", meta = (AllowPrivateAccess = "true"))
    float InteractPromptHeightBelowCrown = 34.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Legacy UI", meta = (AllowPrivateAccess = "true", ClampMin = "0.05"))
    float InteractPromptBaseScale = 0.21f;

    // --- PBZ Exact Additive Camera Parameters (GA_StandingOnPole NBCamera) ---
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|PBZ Camera", meta = (AllowPrivateAccess = "true"))
    bool bUsePlumPoleCameraBlend = true;

    /** PBZ StandingOnPoleDistance additive arm length (+300.0 cm). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|PBZ Camera", meta = (AllowPrivateAccess = "true"))
    float PoleAdditiveArmLength = 300.0f;

    /** PBZ StandingOnPoleSocketOffset additive socket offset (0, +100, 0). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|PBZ Camera", meta = (AllowPrivateAccess = "true"))
    FVector PoleAdditiveSocketOffset = FVector(0.0, 100.0, 0.0);

    /** PBZ finite camera additive blend duration (0.5 s). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|PBZ Camera", meta = (AllowPrivateAccess = "true", ClampMin = "0.01"))
    float PoleCameraBlendDuration = 0.5f;

    // --- Legacy camera properties retained for BP_SoulThirdPerson serialization compatibility (not applied on poles) ---
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Legacy Camera", meta = (AllowPrivateAccess = "true", ClampMin = "100"))
    float GroundCameraArmLength = 340.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Legacy Camera", meta = (AllowPrivateAccess = "true"))
    FVector GroundCameraSocketOffset = FVector(0.0, 38.0, 44.0);

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Legacy Camera", meta = (AllowPrivateAccess = "true"))
    FVector GroundCameraTargetOffset = FVector(0.0, 0.0, 24.0);

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Legacy Camera", meta = (AllowPrivateAccess = "true", ClampMin = "40", ClampMax = "110"))
    float GroundCameraFOV = 68.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Legacy Camera", meta = (AllowPrivateAccess = "true", ClampMin = "150"))
    float PoleCameraArmLength = 640.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Legacy Camera", meta = (AllowPrivateAccess = "true"))
    FVector PoleCameraSocketOffset = FVector(0.0, 138.0, 44.0);

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Legacy Camera", meta = (AllowPrivateAccess = "true"))
    FVector PoleCameraTargetOffset = FVector(0.0, 0.0, 24.0);

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Legacy Camera", meta = (AllowPrivateAccess = "true", ClampMin = "40", ClampMax = "110"))
    float PoleCameraFOV = 68.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Legacy Camera", meta = (AllowPrivateAccess = "true", ClampMin = "0.5"))
    float CameraBlendSpeed = 4.5f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Legacy Camera", meta = (AllowPrivateAccess = "true", ClampMin = "-60", ClampMax = "20"))
    float InitialCameraPitch = -15.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|Legacy Camera", meta = (AllowPrivateAccess = "true", ClampMin = "-25", ClampMax = "15"))
    float PoleCameraPitchOffset = 0.0f;

    // --- PBZ Timeline Animation Sequences ---
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|PBZ Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> DashSequence;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|PBZ Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> JumpLSequence;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|PBZ Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> JumpRSequence;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|PBZ Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> IdleLSequence;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|PBZ Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> IdleRSequence;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|PBZ Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> JumpToPlatformLSequence;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|PBZ Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> JumpToPlatformRSequence;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|PBZ Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> JumpOffSequence;

    // --- Legacy animation properties retained for BP_SoulThirdPerson compatibility ---
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

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> ExitPoleLeft;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> ShortExitFromRight;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UAnimSequence> ShortExitFromLeft;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UMirrorDataTable> ExitMirrorTable;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0", ClampMax = "1"))
    FVector2D EnterSection = FVector2D(0.0, 1.310 / 1.983333);

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

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Plum Pole|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
    float PoleSoleHeight = 0.75f;

    UPROPERTY(Transient)
    TObjectPtr<USpringArmComponent> CachedCameraBoom;

    UPROPERTY(Transient)
    TObjectPtr<UCameraComponent> CachedFollowCamera;

    // Finite-duration 0.5 s PBZ additive camera state (bound to the specific SpringArm that received the additive offset)
    TWeakObjectPtr<USpringArmComponent> AppliedCameraSpringArm;
    float AppliedAdditiveArmLength = 0.0f;
    FVector AppliedAdditiveSocketOffset = FVector::ZeroVector;
    float CameraBlendStartArm = 0.0f;
    float CameraBlendTargetArm = 0.0f;
    FVector CameraBlendStartSocket = FVector::ZeroVector;
    FVector CameraBlendTargetSocket = FVector::ZeroVector;
    float CameraBlendElapsed = 0.0f;
    bool bCameraBlending = false;

    TWeakObjectPtr<APlumPoleActor> PromptTargetPole;
    EPlumPoleInteractUIState CurrentPromptUIState = EPlumPoleInteractUIState::None;
    FVector LastPromptDisplayWorldLocation = FVector::ZeroVector;
    FVector2D LastValidScreenEdgeDir = FVector2D(0.0, 1.0);
    bool bLastPromptWasScreenEdge = false;
    float PromptAlpha = 0.0f;
    bool bUsingGamepad = false;
    bool bInteractPromptInCombat = false;
    EPlumPoleGamepadIconStyle InteractPromptGamepadStyle = EPlumPoleGamepadIconStyle::Xbox;
};
