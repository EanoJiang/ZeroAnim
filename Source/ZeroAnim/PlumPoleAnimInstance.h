#pragma once

#include "AlphaBlend.h"
#include "Animation/AnimInstance.h"
#include "PlumPoleTraversalComponent.h"
#include "PlumPoleAnimInstance.generated.h"

class UAnimSequence;
class UMirrorDataTable;

struct FPlumPoleAnimationFrame
{
    FVector FootTarget = FVector::ZeroVector;
    FName FootBone = NAME_None;
    float FootWeight = 0.0f;
    float Progress = 0.0f;
    float Duration = 0.0f;
    bool bDecayFootOffset = false;
};

UCLASS(Transient, Blueprintable)
class ZEROANIM_API UPlumPoleAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    /** Play a full-pose clip. Reusing the current clip updates its loop and rate without rewinding it. */
    UFUNCTION(BlueprintCallable, Category = "Plum Pole|Animation")
    void SetClip(UAnimSequence* Sequence, bool bLoop, float Rate, float BlendTime);

    /** Play a segment on the traversal clock. A null sequence blends back to the Anim Blueprint. */
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
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};
