#pragma once

#include "AlphaBlend.h"
#include "Animation/AnimInstance.h"
#include "PlumPoleTraversalComponent.h"
#include "PlumPoleAnimInstance.generated.h"

class UAnimSequence;
class UMirrorDataTable;

struct FPlumPoleAnimationFrame
{
    float SourceTimeSeconds = 0.0f;
    float ActiveRate = 1.0f;
    float Progress = 0.0f;
    float Duration = 0.0f;
    bool bPoleAbilityActive = false;
    bool bUseSourceTimeClock = false;
};

UCLASS(Transient, Blueprintable)
class ZEROANIM_API UPlumPoleAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    /** PBZ CharacterState.Animation.DisableLegIK: true throughout active pole ability, false when Grounded. */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Plum Pole|Animation")
    bool bDisableLegIK = false;

    /** Convenience alpha (0.0 on poles, 1.0 on ground) wired directly to CR_Mannequin_FootIK Alpha in ABP_PlumPoleManny. */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "Plum Pole|Animation")
    float LegIKAlpha = 1.0f;

    /** Play a full-pose clip. Reusing the current clip updates its loop and rate without rewinding it. */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole|Animation")
    void SetClip(UAnimSequence* Sequence, bool bLoop, float Rate, float BlendTime);

    /**
     * Play a PBZ timeline sequence driven directly by source animation time in seconds.
     * Passing nullptr blends smoothly back to the Anim Blueprint ground pose.
     * Interrupted outgoing layers drop to 1.000 s/s; uninterrupted JumpToPlatform auto-blend-out maintains 1.025 s/s.
     */
    void SetTimelineClip(
        UAnimSequence* Sequence,
        bool bLoop,
        float StartTimeSeconds,
        float BlendInTime,
        EAlphaBlendOption BlendInOption,
        float OutgoingBlendTime,
        EAlphaBlendOption OutgoingBlendOption,
        float PreviousSourceTimeSeconds,
        float OutgoingRate,
        bool bForceDropOutgoingRate = false,
        UMirrorDataTable* MirrorTable = nullptr,
        float FirstUpdateDeltaOverride = -1.0f);

    /** Legacy segment playback API retained for compatibility. */
    void SetTraversalClip(UAnimSequence* Sequence, bool bLoop, float BlendTime,
        FVector2D Section, UMirrorDataTable* MirrorTable,
        EAlphaBlendOption BlendOption = EAlphaBlendOption::HermiteCubic,
        EPlumPoleTimeCurve TimeCurve = EPlumPoleTimeCurve::Linear,
        float PreviousTraversalProgress = -1.0f,
        float OutgoingRate = 1.0f);

    /** Number of live pose layers (active + blending-out outgoing clips) for runtime diagnostics. */
    UFUNCTION(BlueprintPure, Category = "Plum Pole|Animation")
    int32 GetActiveLayerCount() const;

protected:
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};
