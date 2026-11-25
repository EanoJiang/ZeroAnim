#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "PlumPoleInteractPromptWidget.generated.h"

class SImage;
class SInvalidationPanel;
class SOverlay;
class SWidget;
class UTexture2D;

/** Exact 5-state PBZ W_InteractionUI state machine. */
UENUM(BlueprintType)
enum class EPlumPoleInteractUIState : uint8
{
    None,
    Virtual,
    VirtualScreen,
    InputAction,
    InputActionScreen
};

/** Gamepad face-button icon style (Xbox A vs PlayStation Cross). */
UENUM(BlueprintType)
enum class EPlumPoleGamepadIconStyle : uint8
{
    Xbox,
    PlayStation
};

/**
 * 1:1 PBZ W_InteractionUI_SpecialMove / W_InteractionUI recreation:
 * Pure icon-driven screen-space widget inside InvalidationBox -> Overlay:
 *   - IconRing (Ellipse_147_2_, 20x20, morphing 1.0 -> 1.4 -> 1.5)
 *   - KeyBadge (Space / Xbox A / PS Cross, RenderScale 1.5x1.5)
 *   - DirectionArrow (T_InteractionArrow, 100x100, rotated by SetScreenAngle)
 * Implements all 20 PBZ MovieScene transitions at exact 60,000 tick/s keyframes.
 */
UCLASS(Blueprintable)
class ZEROANIM_API UPlumPoleInteractPromptWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UPlumPoleInteractPromptWidget(const FObjectInitializer& ObjectInitializer);

    /** Transition smoothly to TargetState using exact PBZ W_InteractionUI MovieScene keyframes. */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole|UI")
    void TransitionToState(EPlumPoleInteractUIState NewState);

    /** Immediately snap visual state without transition animation. */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole|UI")
    void SnapToState(EPlumPoleInteractUIState NewState);

    /** Matches PBZ W_InteractionUI::ClearWidget (resets all layers to 0 opacity, icon scale 1.0, state None). */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole|UI")
    void ClearWidget();

    /** Advance the active PBZ keyframe animation clock (deduplicated per frame with NativeTick). */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole|UI")
    void AdvanceAnimation(float DeltaSeconds);

    UFUNCTION(BlueprintPure, Category = "Plum Pole|UI")
    EPlumPoleInteractUIState GetTargetUIState() const { return TargetState; }

    UFUNCTION(BlueprintPure, Category = "Plum Pole|UI")
    bool IsTransitioning() const { return bTransitionActive; }

    /** True when steady in None and all visual layers have reached 0 opacity. */
    UFUNCTION(BlueprintPure, Category = "Plum Pole|UI")
    bool IsVisuallyHidden() const;

    /** Maximum current opacity across ring, key badge, and arrow (0..1). */
    UFUNCTION(BlueprintPure, Category = "Plum Pole|UI")
    float GetVisibleAlpha() const;

    UFUNCTION(BlueprintCallable, Category = "Plum Pole|UI")
    void SetUsingGamepad(bool bInUsingGamepad);

    UFUNCTION(BlueprintPure, Category = "Plum Pole|UI")
    bool IsUsingGamepad() const { return bUsingGamepad; }

    UFUNCTION(BlueprintCallable, Category = "Plum Pole|UI")
    void SetGamepadIconStyle(EPlumPoleGamepadIconStyle InStyle);

    UFUNCTION(BlueprintPure, Category = "Plum Pole|UI")
    EPlumPoleGamepadIconStyle GetGamepadIconStyle() const { return GamepadIconStyle; }

    /** Matches PBZ W_InteractionUI::CheckInCombatState: tints IconRing & DirectionArrow red (1,0,0,1) vs white (1,1,1,1). */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole|UI")
    void SetInCombat(bool bInCombat);

    UFUNCTION(BlueprintPure, Category = "Plum Pole|UI")
    bool IsInCombat() const { return bInCombatState; }

    /** Matches PBZ W_InteractionUI::SetScreenAngle: rotates DirectionArrow (Image_30) in degrees (+X = 0 deg, clockwise). */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole|UI")
    void SetScreenAngle(float AngleDeg);

    UFUNCTION(BlueprintPure, Category = "Plum Pole|UI")
    float GetScreenAngle() const { return ScreenAngleDegrees; }

    /** Overall widget opacity multiplier (default 1.0; kept for Blueprint compatibility). */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole|UI")
    void SetPromptAlpha(float InAlpha);

    UFUNCTION(BlueprintImplementableEvent, Category = "Plum Pole|UI")
    void OnInputDeviceUpdated(bool bGamepad);

    UFUNCTION(BlueprintImplementableEvent, Category = "Plum Pole|UI")
    void OnInteractStateChanged(EPlumPoleInteractUIState PreviousState, EPlumPoleInteractUIState NewState);

    // --- Editable PBZ Texture Assets (defaulted to /Game/PlumPole/UI/Textures/T_PBZ_*) ---
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI|PBZ Textures")
    TObjectPtr<UTexture2D> RingTexture;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI|PBZ Textures")
    TObjectPtr<UTexture2D> ArrowTexture;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI|PBZ Textures")
    TObjectPtr<UTexture2D> KeyboardSpaceTexture;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI|PBZ Textures")
    TObjectPtr<UTexture2D> XboxATexture;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI|PBZ Textures")
    TObjectPtr<UTexture2D> PlayStationCrossTexture;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI|PBZ Style")
    EPlumPoleGamepadIconStyle GamepadIconStyle = EPlumPoleGamepadIconStyle::Xbox;

    /** PBZ W_InteractionUI `icon` base brush size (20x20). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI|PBZ Layout")
    FVector2D RingBaseSize = FVector2D(20.0, 20.0);

    /** PBZ W_InteractionUI PC Space base brush size preserving exact 366:256 aspect ratio at height 32 (x1.5 = 68.625x48). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI|PBZ Layout")
    FVector2D KeyboardSpaceBaseSize = FVector2D(45.75, 32.0);

    /** PBZ W_InteractionUI Xbox/PS interact base brush size (32x32, x1.5 = 48x48). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI|PBZ Layout")
    FVector2D GamepadBadgeBaseSize = FVector2D(32.0, 32.0);

    /** PBZ W_InteractionUI `InputActionWidget_C_75` RenderTransform.Scale (1.5, 1.5). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI|PBZ Layout")
    FVector2D KeyBadgeRenderScale = FVector2D(1.5, 1.5);

    /** PBZ W_InteractionUI `Image_30` directional crescent arrow brush size (100x100). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Plum Pole|UI|PBZ Layout")
    FVector2D ArrowBaseSize = FVector2D(100.0, 100.0);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
    virtual void NativePreConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    void RefreshBrushes();
    void ApplyVisualStateToSlate();
    void EvaluateTransitionAt(float ElapsedSeconds);

    TSharedPtr<SInvalidationPanel> InvalidationPanel;
    TSharedPtr<SOverlay> RootOverlay;
    TSharedPtr<SImage> IconRingImage;
    TSharedPtr<SImage> KeyBadgeImage;
    TSharedPtr<SImage> DirectionArrowImage;

    FSlateBrush RingBrush;
    FSlateBrush KeyBadgeBrush;
    FSlateBrush ArrowBrush;

    EPlumPoleInteractUIState TransitionFromState = EPlumPoleInteractUIState::None;
    EPlumPoleInteractUIState TargetState = EPlumPoleInteractUIState::None;
    bool bTransitionActive = false;
    float TransitionElapsed = 0.0f;
    float TransitionDuration = 0.0f;

    // Captured start-of-transition values for glitch-free mid-flight interrupts
    float StartIconOpacity = 0.0f;
    float StartIconScale = 1.0f;
    float StartKeyOpacity = 0.0f;
    float StartArrowOpacity = 0.0f;

    // Live evaluated layer state
    float CurrentIconOpacity = 0.0f;
    float CurrentIconScale = 1.0f;
    float CurrentKeyOpacity = 0.0f;
    float CurrentArrowOpacity = 0.0f;

    float ScreenAngleDegrees = 0.0f;
    float PromptAlpha = 1.0f;
    bool bUsingGamepad = false;
    bool bInCombatState = false;
    uint64 LastAdvanceFrame = MAX_uint64;
};
