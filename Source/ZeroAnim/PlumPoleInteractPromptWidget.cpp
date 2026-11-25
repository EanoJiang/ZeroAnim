#include "PlumPoleInteractPromptWidget.h"

#include "Engine/Texture2D.h"
#include "Math/TransformCalculus2D.h"
#include "Rendering/SlateRenderTransform.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SInvalidationPanel.h"
#include "Widgets/SOverlay.h"

namespace
{
constexpr float PBZTickRate = 60000.0f;

constexpr float TicksToSec(int32 Ticks)
{
    return static_cast<float>(Ticks) / PBZTickRate;
}

/** Hermite cubic interpolation with zero end tangents (UE MovieScene InterpMode=2 / RCIM_Cubic). */
float EvalCubicSegment(float TimeSec, float StartSec, float EndSec, float StartVal, float EndVal)
{
    if (EndSec <= StartSec)
    {
        return TimeSec >= EndSec ? EndVal : StartVal;
    }
    if (TimeSec <= StartSec)
    {
        return StartVal;
    }
    if (TimeSec >= EndSec)
    {
        return EndVal;
    }
    const float U = (TimeSec - StartSec) / (EndSec - StartSec);
    return FMath::CubicInterp(StartVal, 0.0f, EndVal, 0.0f, U);
}

/** Exact 3-key PBZ VirtualToInputAction icon.Scale curve (ticks 0 -> 9088 -> 15088). */
float EvalVirtualToInputActionScale(float TimeSec, float StartScale)
{
    constexpr float T0 = TicksToSec(0);
    constexpr float T1 = TicksToSec(9088);
    constexpr float T2 = TicksToSec(15088);
    // Export 70: ArriveTangent = LeaveTangent = 3.313892e-05 per tick * 60000 ticks/sec
    constexpr float MidTangentPerSec = 3.313892e-05f * PBZTickRate;
    constexpr float EndScale = 1.5f;

    if (TimeSec <= T0)
    {
        return StartScale;
    }
    if (TimeSec >= T2)
    {
        return EndScale;
    }

    // Preserve exact PBZ mid value (1.4) when starting from standard 1.0, or stay monotonic on mid-flight interrupts.
    const float MidScale = FMath::Max(StartScale, 1.4f);
    if (TimeSec < T1)
    {
        const float Dt = T1 - T0;
        const float U = (TimeSec - T0) / Dt;
        return FMath::CubicInterp(StartScale, 0.0f, MidScale, MidTangentPerSec * Dt, U);
    }

    const float Dt = T2 - T1;
    const float U = (TimeSec - T1) / Dt;
    return FMath::CubicInterp(MidScale, MidTangentPerSec * Dt, EndScale, 0.0f, U);
}

struct FPBZLayerSteadyState
{
    float IconOpacity = 0.0f;
    float IconScale = 1.0f;
    float KeyOpacity = 0.0f;
    float ArrowOpacity = 0.0f;
};

FPBZLayerSteadyState GetSteadyState(EPlumPoleInteractUIState State)
{
    switch (State)
    {
    case EPlumPoleInteractUIState::Virtual:
        return { 1.0f, 1.0f, 0.0f, 0.0f };
    case EPlumPoleInteractUIState::VirtualScreen:
        return { 1.0f, 1.0f, 0.0f, 1.0f };
    case EPlumPoleInteractUIState::InputAction:
        return { 0.0f, 1.5f, 1.0f, 0.0f };
    case EPlumPoleInteractUIState::InputActionScreen:
        return { 0.0f, 1.5f, 1.0f, 1.0f };
    case EPlumPoleInteractUIState::None:
    default:
        return { 0.0f, 1.0f, 0.0f, 0.0f };
    }
}

struct FPBZTransitionSpec
{
    float DurationSec = 0.2f;
    float IconOpacityStartSec = 0.0f;
    float IconOpacityEndSec = 0.2f;
    float KeyOpacityStartSec = 0.0f;
    float KeyOpacityEndSec = 0.2f;
    float ArrowOpacityStartSec = 0.0f;
    float ArrowOpacityEndSec = 0.2f;
    float IconScaleStartSec = 0.0f;
    float IconScaleEndSec = 0.0f;
    bool bConstantIconOpacity = false;
    float ConstantIconOpacity = 0.0f;
    bool bConstantKeyOpacity = false;
    float ConstantKeyOpacity = 0.0f;
    bool bConstantArrowOpacity = false;
    float ConstantArrowOpacity = 0.0f;
    bool bUseVirtualToInputScaleCurve = false;
    bool bUseInputToVirtualScaleShrink = false;
};

/** Exact PBZ W_InteractionUI MovieScene durations and keyframe windows (verified against W_InteractionUI.json). */
FPBZTransitionSpec GetTransitionSpec(EPlumPoleInteractUIState From, EPlumPoleInteractUIState To)
{
    FPBZTransitionSpec Spec;

    if (From == EPlumPoleInteractUIState::None && To == EPlumPoleInteractUIState::Virtual)
    {
        // NoneToVirtual_INST [59]: UpperBound=11730, icon 0..11729, key/arrow constant 0
        Spec.DurationSec = TicksToSec(11730);
        Spec.IconOpacityStartSec = TicksToSec(0);
        Spec.IconOpacityEndSec = TicksToSec(11729);
        Spec.bConstantKeyOpacity = true;
        Spec.ConstantKeyOpacity = 0.0f;
        Spec.bConstantArrowOpacity = true;
        Spec.ConstantArrowOpacity = 0.0f;
        return Spec;
    }
    if (From == EPlumPoleInteractUIState::None && To == EPlumPoleInteractUIState::InputAction)
    {
        // NoneToInputAction_INST [57]: UpperBound=15136, key 0..15135, icon/arrow constant 0
        Spec.DurationSec = TicksToSec(15136);
        Spec.bConstantIconOpacity = true;
        Spec.ConstantIconOpacity = 0.0f;
        Spec.KeyOpacityStartSec = TicksToSec(0);
        Spec.KeyOpacityEndSec = TicksToSec(15135);
        Spec.bConstantArrowOpacity = true;
        Spec.ConstantArrowOpacity = 0.0f;
        return Spec;
    }
    if (From == EPlumPoleInteractUIState::None && To == EPlumPoleInteractUIState::VirtualScreen)
    {
        // NoneToVirtualScreen_INST [60]: UpperBound=12001, icon/arrow 0..11999, key constant 0
        Spec.DurationSec = TicksToSec(12001);
        Spec.IconOpacityEndSec = TicksToSec(11999);
        Spec.bConstantKeyOpacity = true;
        Spec.ConstantKeyOpacity = 0.0f;
        Spec.ArrowOpacityEndSec = TicksToSec(11999);
        return Spec;
    }
    if (From == EPlumPoleInteractUIState::None && To == EPlumPoleInteractUIState::InputActionScreen)
    {
        // NoneToInputActionScreen_INST [58]: UpperBound=12000, key/arrow 0..11999, icon constant 0
        Spec.DurationSec = TicksToSec(12000);
        Spec.bConstantIconOpacity = true;
        Spec.ConstantIconOpacity = 0.0f;
        Spec.KeyOpacityEndSec = TicksToSec(11999);
        Spec.ArrowOpacityEndSec = TicksToSec(11999);
        return Spec;
    }
    if (From == EPlumPoleInteractUIState::Virtual && To == EPlumPoleInteractUIState::InputAction)
    {
        // VirtualToInputAction_INST [65]: UpperBound=15089
        // icon.Scale: 1.0 -> 1.4 -> 1.5 at [0, 9088, 15088]
        // icon.Opacity: 1 -> 0 over [8000..14000]
        // InputAction.Opacity: 0 -> 1 over [0..15087], arrow constant 0
        Spec.DurationSec = TicksToSec(15089);
        Spec.IconOpacityStartSec = TicksToSec(8000);
        Spec.IconOpacityEndSec = TicksToSec(14000);
        Spec.KeyOpacityStartSec = TicksToSec(0);
        Spec.KeyOpacityEndSec = TicksToSec(15087);
        Spec.bConstantArrowOpacity = true;
        Spec.ConstantArrowOpacity = 0.0f;
        Spec.bUseVirtualToInputScaleCurve = true;
        return Spec;
    }
    if (From == EPlumPoleInteractUIState::InputAction && To == EPlumPoleInteractUIState::Virtual)
    {
        // InputActionToVirtual_INST [55]: UpperBound=12003
        // icon.Opacity: 0 -> 1 over [0..8002]
        // icon.Scale: 1.5 -> 1.0 over [4000..12002]
        // InputAction.Opacity: 1 -> 0 over [0..9000], arrow constant 0
        Spec.DurationSec = TicksToSec(12003);
        Spec.IconOpacityStartSec = TicksToSec(0);
        Spec.IconOpacityEndSec = TicksToSec(8002);
        Spec.KeyOpacityStartSec = TicksToSec(0);
        Spec.KeyOpacityEndSec = TicksToSec(9000);
        Spec.bConstantArrowOpacity = true;
        Spec.ConstantArrowOpacity = 0.0f;
        Spec.IconScaleStartSec = TicksToSec(4000);
        Spec.IconScaleEndSec = TicksToSec(12002);
        Spec.bUseInputToVirtualScaleShrink = true;
        return Spec;
    }
    if (From == EPlumPoleInteractUIState::Virtual && To == EPlumPoleInteractUIState::None)
    {
        // VirtualToNone_INST [67]: UpperBound=12001, icon 0..12000, key/arrow constant 0
        Spec.DurationSec = TicksToSec(12001);
        Spec.IconOpacityEndSec = TicksToSec(12000);
        Spec.bConstantKeyOpacity = true;
        Spec.ConstantKeyOpacity = 0.0f;
        Spec.bConstantArrowOpacity = true;
        Spec.ConstantArrowOpacity = 0.0f;
        return Spec;
    }
    if (From == EPlumPoleInteractUIState::InputAction && To == EPlumPoleInteractUIState::None)
    {
        // InputActionToNone_INST [54]: UpperBound=11918, key 0..11917, icon/arrow constant 0
        Spec.DurationSec = TicksToSec(11918);
        Spec.bConstantIconOpacity = true;
        Spec.ConstantIconOpacity = 0.0f;
        Spec.KeyOpacityEndSec = TicksToSec(11917);
        Spec.bConstantArrowOpacity = true;
        Spec.ConstantArrowOpacity = 0.0f;
        return Spec;
    }
    if (From == EPlumPoleInteractUIState::Virtual && To == EPlumPoleInteractUIState::VirtualScreen)
    {
        // VirtualToVirtualScreen_INST [68]: UpperBound=15000, arrow 0..14999 (0->1), icon constant 1, key constant 0
        Spec.DurationSec = TicksToSec(15000);
        Spec.bConstantIconOpacity = true;
        Spec.ConstantIconOpacity = 1.0f;
        Spec.bConstantKeyOpacity = true;
        Spec.ConstantKeyOpacity = 0.0f;
        Spec.ArrowOpacityEndSec = TicksToSec(14999);
        return Spec;
    }
    if (From == EPlumPoleInteractUIState::VirtualScreen && To == EPlumPoleInteractUIState::Virtual)
    {
        // VirtualScreenToVirtual_INST [64]: UpperBound=15000, icon constant 1, key constant 0,
        // Image_30 [386]/[310]: 0=1.0 -> 14999=0.0 smooth cubic fade
        Spec.DurationSec = TicksToSec(15000);
        Spec.bConstantIconOpacity = true;
        Spec.ConstantIconOpacity = 1.0f;
        Spec.bConstantKeyOpacity = true;
        Spec.ConstantKeyOpacity = 0.0f;
        Spec.ArrowOpacityEndSec = TicksToSec(14999);
        return Spec;
    }
    if (From == EPlumPoleInteractUIState::VirtualScreen && To == EPlumPoleInteractUIState::None)
    {
        // VirtualScreenToNone_INST [63]: UpperBound=15000, icon/arrow 0..14999, key constant 0
        Spec.DurationSec = TicksToSec(15000);
        Spec.IconOpacityEndSec = TicksToSec(14999);
        Spec.bConstantKeyOpacity = true;
        Spec.ConstantKeyOpacity = 0.0f;
        Spec.ArrowOpacityEndSec = TicksToSec(14999);
        return Spec;
    }
    if (From == EPlumPoleInteractUIState::InputAction && To == EPlumPoleInteractUIState::InputActionScreen)
    {
        // InputActionToInputActionScreen_INST [53]: UpperBound=15089, arrow 0..15088, icon constant 0, key constant 1
        Spec.DurationSec = TicksToSec(15089);
        Spec.bConstantIconOpacity = true;
        Spec.ConstantIconOpacity = 0.0f;
        Spec.bConstantKeyOpacity = true;
        Spec.ConstantKeyOpacity = 1.0f;
        Spec.ArrowOpacityEndSec = TicksToSec(15088);
        return Spec;
    }
    if (From == EPlumPoleInteractUIState::InputActionScreen && To == EPlumPoleInteractUIState::InputAction)
    {
        // InputActionScreenToInputAction_INST [49]: UpperBound=15088, arrow 0..15087, icon constant 0, key constant 1
        Spec.DurationSec = TicksToSec(15088);
        Spec.bConstantIconOpacity = true;
        Spec.ConstantIconOpacity = 0.0f;
        Spec.bConstantKeyOpacity = true;
        Spec.ConstantKeyOpacity = 1.0f;
        Spec.ArrowOpacityEndSec = TicksToSec(15087);
        return Spec;
    }
    if ((From == EPlumPoleInteractUIState::VirtualScreen && To == EPlumPoleInteractUIState::InputActionScreen)
        || (From == EPlumPoleInteractUIState::InputActionScreen && To == EPlumPoleInteractUIState::VirtualScreen))
    {
        // VirtualScreenToInpuActionScreen [61] & InputActionScreenToVirtualScreen [52]:
        // UpperBound=12000, icon/key 0..11999, Image_30 [301]/[265] tick-0 constant 1.0!
        Spec.DurationSec = TicksToSec(12000);
        Spec.IconOpacityEndSec = TicksToSec(11999);
        Spec.KeyOpacityEndSec = TicksToSec(11999);
        Spec.bConstantArrowOpacity = true;
        Spec.ConstantArrowOpacity = 1.0f;
        return Spec;
    }
    if (From == EPlumPoleInteractUIState::InputActionScreen && To == EPlumPoleInteractUIState::None)
    {
        // InputActionScreenToNone_INST [50]: UpperBound=12000, icon constant 0, key/arrow 0..11999
        Spec.DurationSec = TicksToSec(12000);
        Spec.bConstantIconOpacity = true;
        Spec.ConstantIconOpacity = 0.0f;
        Spec.KeyOpacityEndSec = TicksToSec(11999);
        Spec.ArrowOpacityEndSec = TicksToSec(11999);
        return Spec;
    }

    // Remaining *Screen* cross-transitions:
    // VirtualToInputActionScreen [66], VirtualScreenToInputAction [62] (Image_30 [305]: 0=1.0 -> 11999=0.0),
    // InputActionToVirtualScreen [56], InputActionScreenToVirtual [51]: UpperBound=12000, keys 0..11999
    Spec.DurationSec = TicksToSec(12000);
    Spec.IconOpacityEndSec = TicksToSec(11999);
    Spec.KeyOpacityEndSec = TicksToSec(11999);
    Spec.ArrowOpacityEndSec = TicksToSec(11999);
    return Spec;
}

void ConfigureTextureBrush(FSlateBrush& Brush, UTexture2D* Texture, const FVector2D& Size)
{
    Brush = FSlateBrush();
    Brush.SetImageSize(Size);
    Brush.Tiling = ESlateBrushTileType::NoTile;
    Brush.ImageType = ESlateBrushImageType::FullColor;
    if (Texture)
    {
        Brush.SetResourceObject(Texture);
        Brush.DrawAs = ESlateBrushDrawType::Image;
    }
    else
    {
        Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
    }
}
} // namespace

UPlumPoleInteractPromptWidget::UPlumPoleInteractPromptWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    static ConstructorHelpers::FObjectFinder<UTexture2D> RingAsset(
        TEXT("/Game/PlumPole/UI/Textures/T_PBZ_Interact_Ellipse.T_PBZ_Interact_Ellipse"));
    static ConstructorHelpers::FObjectFinder<UTexture2D> ArrowAsset(
        TEXT("/Game/PlumPole/UI/Textures/T_PBZ_Interact_Arrow.T_PBZ_Interact_Arrow"));
    static ConstructorHelpers::FObjectFinder<UTexture2D> SpaceAsset(
        TEXT("/Game/PlumPole/UI/Textures/T_PBZ_Key_PC_Space.T_PBZ_Key_PC_Space"));
    static ConstructorHelpers::FObjectFinder<UTexture2D> XboxAAsset(
        TEXT("/Game/PlumPole/UI/Textures/T_PBZ_Key_Xbox_A.T_PBZ_Key_Xbox_A"));
    static ConstructorHelpers::FObjectFinder<UTexture2D> PSCrossAsset(
        TEXT("/Game/PlumPole/UI/Textures/T_PBZ_Key_PS_Cross.T_PBZ_Key_PS_Cross"));

    if (RingAsset.Succeeded())
    {
        RingTexture = RingAsset.Object;
    }
    if (ArrowAsset.Succeeded())
    {
        ArrowTexture = ArrowAsset.Object;
    }
    if (SpaceAsset.Succeeded())
    {
        KeyboardSpaceTexture = SpaceAsset.Object;
    }
    if (XboxAAsset.Succeeded())
    {
        XboxATexture = XboxAAsset.Object;
    }
    if (PSCrossAsset.Succeeded())
    {
        PlayStationCrossTexture = PSCrossAsset.Object;
    }
}

TSharedRef<SWidget> UPlumPoleInteractPromptWidget::RebuildWidget()
{
    // Pure icon-driven PBZ W_InteractionUI hierarchy: InvalidationBox -> Overlay -> [icon, InputActionWidget, Image_30]
    TSharedRef<SInvalidationPanel> Root = SAssignNew(InvalidationPanel, SInvalidationPanel)
        .Visibility(EVisibility::HitTestInvisible)
        [
            SAssignNew(RootOverlay, SOverlay)
            .Visibility(EVisibility::HitTestInvisible)
            + SOverlay::Slot()
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            [
                SAssignNew(IconRingImage, SImage)
                .Visibility(EVisibility::HitTestInvisible)
                .Image(&RingBrush)
            ]
            + SOverlay::Slot()
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            [
                SAssignNew(KeyBadgeImage, SImage)
                .Visibility(EVisibility::HitTestInvisible)
                .Image(&KeyBadgeBrush)
            ]
            + SOverlay::Slot()
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            [
                SAssignNew(DirectionArrowImage, SImage)
                .Visibility(EVisibility::HitTestInvisible)
                .Image(&ArrowBrush)
            ]
        ];

    if (IconRingImage.IsValid())
    {
        IconRingImage->SetRenderTransformPivot(FVector2D(0.5, 0.5));
    }
    if (KeyBadgeImage.IsValid())
    {
        KeyBadgeImage->SetRenderTransformPivot(FVector2D(0.5, 0.5));
    }
    if (DirectionArrowImage.IsValid())
    {
        DirectionArrowImage->SetRenderTransformPivot(FVector2D(0.5, 0.5));
    }

    RefreshBrushes();
    ApplyVisualStateToSlate();
    return Root;
}

void UPlumPoleInteractPromptWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    InvalidationPanel.Reset();
    RootOverlay.Reset();
    IconRingImage.Reset();
    KeyBadgeImage.Reset();
    DirectionArrowImage.Reset();
}

void UPlumPoleInteractPromptWidget::NativePreConstruct()
{
    Super::NativePreConstruct();
    RefreshBrushes();
    if (IsDesignTime())
    {
        SnapToState(EPlumPoleInteractUIState::InputAction);
    }
    else
    {
        ApplyVisualStateToSlate();
    }
}

void UPlumPoleInteractPromptWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    AdvanceAnimation(InDeltaTime);
}

void UPlumPoleInteractPromptWidget::TransitionToState(EPlumPoleInteractUIState NewState)
{
    if (TargetState == NewState)
    {
        return;
    }

    const EPlumPoleInteractUIState PreviousState = TargetState;
    TransitionFromState = PreviousState;
    TargetState = NewState;

    StartIconOpacity = CurrentIconOpacity;
    StartIconScale = CurrentIconScale;
    StartKeyOpacity = CurrentKeyOpacity;
    StartArrowOpacity = CurrentArrowOpacity;

    // When entering InputActionToVirtual from a hidden ring, ensure ring begins at PBZ's 1.5x scale ready to shrink to 1.0x.
    if (TransitionFromState == EPlumPoleInteractUIState::InputAction
        && TargetState == EPlumPoleInteractUIState::Virtual
        && StartIconOpacity <= KINDA_SMALL_NUMBER)
    {
        StartIconScale = 1.5f;
    }
    else if (TransitionFromState == EPlumPoleInteractUIState::None
        && (TargetState == EPlumPoleInteractUIState::Virtual || TargetState == EPlumPoleInteractUIState::VirtualScreen))
    {
        StartIconScale = 1.0f;
    }

    const FPBZTransitionSpec Spec = GetTransitionSpec(TransitionFromState, TargetState);
    TransitionElapsed = 0.0f;
    TransitionDuration = Spec.DurationSec;
    bTransitionActive = (TransitionDuration > KINDA_SMALL_NUMBER);

    EvaluateTransitionAt(0.0f);
    ApplyVisualStateToSlate();
    OnInteractStateChanged(PreviousState, TargetState);
}

void UPlumPoleInteractPromptWidget::SnapToState(EPlumPoleInteractUIState NewState)
{
    const EPlumPoleInteractUIState PreviousState = TargetState;
    TransitionFromState = NewState;
    TargetState = NewState;
    bTransitionActive = false;
    TransitionElapsed = 0.0f;
    TransitionDuration = 0.0f;

    const FPBZLayerSteadyState Steady = GetSteadyState(NewState);
    CurrentIconOpacity = Steady.IconOpacity;
    CurrentIconScale = Steady.IconScale;
    CurrentKeyOpacity = Steady.KeyOpacity;
    CurrentArrowOpacity = Steady.ArrowOpacity;

    StartIconOpacity = CurrentIconOpacity;
    StartIconScale = CurrentIconScale;
    StartKeyOpacity = CurrentKeyOpacity;
    StartArrowOpacity = CurrentArrowOpacity;

    ApplyVisualStateToSlate();
    if (PreviousState != NewState)
    {
        OnInteractStateChanged(PreviousState, NewState);
    }
}

void UPlumPoleInteractPromptWidget::ClearWidget()
{
    SnapToState(EPlumPoleInteractUIState::None);
}

void UPlumPoleInteractPromptWidget::AdvanceAnimation(float DeltaSeconds)
{
    if (LastAdvanceFrame == GFrameCounter)
    {
        return;
    }
    LastAdvanceFrame = GFrameCounter;

    if (!bTransitionActive || DeltaSeconds <= 0.0f)
    {
        return;
    }

    TransitionElapsed = FMath::Min(TransitionElapsed + DeltaSeconds, TransitionDuration);
    EvaluateTransitionAt(TransitionElapsed);

    if (TransitionElapsed >= TransitionDuration)
    {
        bTransitionActive = false;
        const FPBZLayerSteadyState Steady = GetSteadyState(TargetState);
        CurrentIconOpacity = Steady.IconOpacity;
        CurrentIconScale = Steady.IconScale;
        CurrentKeyOpacity = Steady.KeyOpacity;
        CurrentArrowOpacity = Steady.ArrowOpacity;
    }

    ApplyVisualStateToSlate();
}

bool UPlumPoleInteractPromptWidget::IsVisuallyHidden() const
{
    return TargetState == EPlumPoleInteractUIState::None
        && !bTransitionActive
        && GetVisibleAlpha() <= KINDA_SMALL_NUMBER;
}

float UPlumPoleInteractPromptWidget::GetVisibleAlpha() const
{
    return FMath::Max3(CurrentIconOpacity, CurrentKeyOpacity, CurrentArrowOpacity) * PromptAlpha;
}

void UPlumPoleInteractPromptWidget::SetUsingGamepad(bool bInUsingGamepad)
{
    if (bUsingGamepad == bInUsingGamepad)
    {
        return;
    }
    bUsingGamepad = bInUsingGamepad;
    RefreshBrushes();
    ApplyVisualStateToSlate();
    OnInputDeviceUpdated(bUsingGamepad);
}

void UPlumPoleInteractPromptWidget::SetGamepadIconStyle(EPlumPoleGamepadIconStyle InStyle)
{
    if (GamepadIconStyle == InStyle)
    {
        return;
    }
    GamepadIconStyle = InStyle;
    RefreshBrushes();
    ApplyVisualStateToSlate();
}

void UPlumPoleInteractPromptWidget::SetInCombat(bool bInCombat)
{
    if (bInCombatState == bInCombat)
    {
        return;
    }
    bInCombatState = bInCombat;
    ApplyVisualStateToSlate();
}

void UPlumPoleInteractPromptWidget::SetScreenAngle(float AngleDeg)
{
    if (FMath::IsNearlyEqual(ScreenAngleDegrees, AngleDeg, 0.01f))
    {
        return;
    }
    ScreenAngleDegrees = AngleDeg;
    ApplyVisualStateToSlate();
}

void UPlumPoleInteractPromptWidget::SetPromptAlpha(float InAlpha)
{
    PromptAlpha = FMath::Clamp(InAlpha, 0.0f, 1.0f);
    SetRenderOpacity(PromptAlpha);
}

void UPlumPoleInteractPromptWidget::EvaluateTransitionAt(float ElapsedSeconds)
{
    const FPBZTransitionSpec Spec = GetTransitionSpec(TransitionFromState, TargetState);
    const FPBZLayerSteadyState TargetSteady = GetSteadyState(TargetState);

    CurrentIconOpacity = Spec.bConstantIconOpacity
        ? Spec.ConstantIconOpacity
        : EvalCubicSegment(ElapsedSeconds, Spec.IconOpacityStartSec, Spec.IconOpacityEndSec, StartIconOpacity, TargetSteady.IconOpacity);
    CurrentKeyOpacity = Spec.bConstantKeyOpacity
        ? Spec.ConstantKeyOpacity
        : EvalCubicSegment(ElapsedSeconds, Spec.KeyOpacityStartSec, Spec.KeyOpacityEndSec, StartKeyOpacity, TargetSteady.KeyOpacity);
    CurrentArrowOpacity = Spec.bConstantArrowOpacity
        ? Spec.ConstantArrowOpacity
        : EvalCubicSegment(ElapsedSeconds, Spec.ArrowOpacityStartSec, Spec.ArrowOpacityEndSec, StartArrowOpacity, TargetSteady.ArrowOpacity);

    if (Spec.bUseVirtualToInputScaleCurve)
    {
        CurrentIconScale = EvalVirtualToInputActionScale(ElapsedSeconds, StartIconScale);
    }
    else if (Spec.bUseInputToVirtualScaleShrink)
    {
        CurrentIconScale = EvalCubicSegment(
            ElapsedSeconds, Spec.IconScaleStartSec, Spec.IconScaleEndSec, StartIconScale, 1.0f);
    }
    else
    {
        // Keep visible ring at 1.0 scale during pure fade / *Screen transitions (matching PBZ MovieScenes).
        CurrentIconScale = (TargetSteady.IconOpacity > 0.0f || CurrentIconOpacity > KINDA_SMALL_NUMBER)
            ? 1.0f
            : TargetSteady.IconScale;
    }
}

void UPlumPoleInteractPromptWidget::RefreshBrushes()
{
    ConfigureTextureBrush(RingBrush, RingTexture.Get(), RingBaseSize);
    ConfigureTextureBrush(ArrowBrush, ArrowTexture.Get(), ArrowBaseSize);

    UTexture2D* ActiveKeyTexture = nullptr;
    FVector2D ActiveKeySize = KeyboardSpaceBaseSize;
    if (bUsingGamepad)
    {
        ActiveKeyTexture = (GamepadIconStyle == EPlumPoleGamepadIconStyle::PlayStation)
            ? PlayStationCrossTexture.Get()
            : XboxATexture.Get();
        ActiveKeySize = GamepadBadgeBaseSize;
    }
    else
    {
        ActiveKeyTexture = KeyboardSpaceTexture.Get();
        ActiveKeySize = KeyboardSpaceBaseSize;
    }
    ConfigureTextureBrush(KeyBadgeBrush, ActiveKeyTexture, ActiveKeySize);

    if (IconRingImage.IsValid())
    {
        IconRingImage->SetImage(&RingBrush);
        IconRingImage->InvalidateImage();
    }
    if (KeyBadgeImage.IsValid())
    {
        KeyBadgeImage->SetImage(&KeyBadgeBrush);
        KeyBadgeImage->InvalidateImage();
    }
    if (DirectionArrowImage.IsValid())
    {
        DirectionArrowImage->SetImage(&ArrowBrush);
        DirectionArrowImage->InvalidateImage();
    }
    if (InvalidationPanel.IsValid())
    {
        InvalidationPanel->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
    }
}

void UPlumPoleInteractPromptWidget::ApplyVisualStateToSlate()
{
    // Exact PBZ CheckInCombatState colors: red (1,0,0,1) in combat, pure white (1,1,1,1) otherwise.
    const FLinearColor RingArrowTint = bInCombatState
        ? FLinearColor(1.0f, 0.0f, 0.0f, 1.0f)
        : FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

    if (IconRingImage.IsValid())
    {
        IconRingImage->SetColorAndOpacity(RingArrowTint);
        IconRingImage->SetRenderOpacity(FMath::Clamp(CurrentIconOpacity, 0.0f, 1.0f));
        IconRingImage->SetRenderTransform(FSlateRenderTransform(FScale2D(CurrentIconScale, CurrentIconScale)));
    }

    if (KeyBadgeImage.IsValid())
    {
        KeyBadgeImage->SetColorAndOpacity(FLinearColor::White);
        KeyBadgeImage->SetRenderOpacity(FMath::Clamp(CurrentKeyOpacity, 0.0f, 1.0f));
        KeyBadgeImage->SetRenderTransform(
            FSlateRenderTransform(FScale2D(static_cast<float>(KeyBadgeRenderScale.X), static_cast<float>(KeyBadgeRenderScale.Y))));
    }

    if (DirectionArrowImage.IsValid())
    {
        DirectionArrowImage->SetColorAndOpacity(RingArrowTint);
        DirectionArrowImage->SetRenderOpacity(FMath::Clamp(CurrentArrowOpacity, 0.0f, 1.0f));
        DirectionArrowImage->SetRenderTransform(
            FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(ScreenAngleDegrees))));
    }

    SetRenderOpacity(PromptAlpha);

    if (InvalidationPanel.IsValid())
    {
        InvalidationPanel->SetCanCache(!bTransitionActive);
        InvalidationPanel->Invalidate(EInvalidateWidgetReason::Paint);
    }
}
