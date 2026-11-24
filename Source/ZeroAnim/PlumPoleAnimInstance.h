#pragma once

#include "Animation/AnimInstance.h"
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
        FVector2D Section, UMirrorDataTable* MirrorTable);

protected:
    virtual void NativeInitializeAnimation() override;
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};
