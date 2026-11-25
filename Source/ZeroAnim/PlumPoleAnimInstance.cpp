#include "PlumPoleAnimInstance.h"

#include "Animation/AnimationPoseData.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AttributesRuntime.h"
#include "Animation/MirrorDataTable.h"
#include "AnimationRuntime.h"
#include "PlumPoleCharacter.h"
#include "UObject/UObjectGlobals.h"

namespace
{
constexpr int32 MaxOutgoingLayers = 6;

struct FMirrorCache
{
    TCustomBoneIndexArray<FCompactPoseBoneIndex, FCompactPoseBoneIndex> Indices;
    TCustomBoneIndexArray<FQuat, FCompactPoseBoneIndex> Rotations;
    uint16 BoneSerial = 0;

    void Reset()
    {
        Indices.Reset();
        Rotations.Reset();
        BoneSerial = 0;
    }

    void Apply(FPoseContext& Output, const UMirrorDataTable* Table)
    {
        const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
        if (!Table || Table->Skeleton != Bones.GetSkeletonAsset())
        {
            return;
        }

        if (Indices.IsEmpty() || BoneSerial != Bones.GetSerialNumber())
        {
            Table->FillCompactPoseAndComponentRefRotations(Bones, Indices, Rotations);
            BoneSerial = Bones.GetSerialNumber();
        }
        FAnimationRuntime::MirrorPose(Output.Pose, Table->MirrorAxis, Indices, Rotations);
        UE::Anim::Attributes::MirrorAttributes(Output.CustomAttributes, *Table, Indices);
    }
};

struct FClipLayer
{
    TObjectPtr<UAnimSequence> Sequence;
    TObjectPtr<UMirrorDataTable> MirrorTable;
    FMirrorCache MirrorCache;
    FAlphaBlend Blend;
    float SectionStart = 0.0f;
    float SectionEnd = 0.0f;
    float FullLength = 0.0f;
    float Rate = 1.0f;
    float Time = 0.0f;
    float FirstUpdateDeltaOverride = -1.0f;
    EPlumPoleTimeCurve TimeCurve = EPlumPoleTimeCurve::Linear;
    bool bLoop = false;
    bool bUseSourceTimeClock = false;
    bool bUseLegacyTraversalClock = false;
    bool bHoldingEndPose = false;

    FClipLayer()
    {
        Blend.SetBlendOption(EAlphaBlendOption::HermiteCubic);
        Blend.SetBlendTime(0.0f);
        Blend.SetValueRange(1.0f, 1.0f);
        Blend.SetAlpha(1.0f);
    }

    float GetWeight() const
    {
        return FMath::Max(Blend.GetBlendedValue(), 0.0f);
    }

    float GetSampleTime(const FPlumPoleAnimationFrame& Frame) const
    {
        if (bUseSourceTimeClock && Frame.bUseSourceTimeClock)
        {
            return FMath::Clamp(Frame.SourceTimeSeconds, 0.0f, FullLength);
        }
        if (bUseLegacyTraversalClock)
        {
            return FMath::Clamp(
                EvaluatePlumPoleTimeCurve(TimeCurve, SectionStart, SectionEnd, Frame.Progress),
                0.0f, FullLength);
        }
        return FMath::Clamp(Time, 0.0f, FullLength);
    }

    void Advance(float DeltaSeconds)
    {
        float EffectiveDelta = DeltaSeconds;
        if (FirstUpdateDeltaOverride >= 0.0f)
        {
            EffectiveDelta = FMath::Clamp(FirstUpdateDeltaOverride, 0.0f, DeltaSeconds);
            FirstUpdateDeltaOverride = -1.0f;
        }
        Blend.Update(EffectiveDelta);
        if (!bUseSourceTimeClock && !bUseLegacyTraversalClock && Sequence && FullLength > 0.0f)
        {
            if (bLoop)
            {
                FAnimationRuntime::AdvanceTime(true, Rate * EffectiveDelta, Time, FullLength);
                bHoldingEndPose = false;
            }
            else if (!bHoldingEndPose)
            {
                Time += Rate * EffectiveDelta;
                if (Time >= FullLength)
                {
                    Time = FullLength;
                    bHoldingEndPose = true;
                }
                else if (Time <= 0.0f && Rate < 0.0f)
                {
                    Time = 0.0f;
                    bHoldingEndPose = true;
                }
            }
        }
    }

    bool Sample(FPoseContext& Output, const FPlumPoleAnimationFrame& Frame)
    {
        const USkeleton* Skeleton = Output.Pose.GetBoneContainer().GetSkeletonAsset();
        if (!Sequence || !Skeleton || Sequence->GetSkeleton() != Skeleton || Sequence->IsValidAdditive())
        {
            return false;
        }

        FAnimationPoseData PoseData(Output);
        const float SampleTime = GetSampleTime(Frame);
        const FAnimExtractContext ExtractionContext(SampleTime, false, {}, bLoop);
        Sequence->GetAnimationPose(PoseData, ExtractionContext);
        MirrorCache.Apply(Output, MirrorTable.Get());

        // Capsule motion drives traversal; strip raw root bone translation while preserving local pelvis motion.
        const FCompactPoseBoneIndex RootBone(0);
        Output.Pose[RootBone] = Output.Pose.GetRefPose(RootBone);
        return true;
    }
};

class FPlumPoleAnimInstanceProxy final : public FAnimInstanceProxy
{
public:
    explicit FPlumPoleAnimInstanceProxy(UAnimInstance* Instance)
        : FAnimInstanceProxy(Instance)
    {
    }

    void SetClip(UAnimSequence* Sequence, bool bLoop, float Rate, float BlendTime)
    {
        const float PlaybackRate = FMath::IsFinite(Rate) ? Rate : 1.0f;
        if (Current.Sequence == Sequence && !Current.bUseSourceTimeClock && !Current.bUseLegacyTraversalClock && !Current.MirrorTable)
        {
            Current.bLoop = bLoop;
            Current.Rate = PlaybackRate;
            return;
        }

        SetTimelineClip(Sequence, bLoop, 0.0f, BlendTime, EAlphaBlendOption::HermiteCubic,
            BlendTime, EAlphaBlendOption::HermiteCubic, LastSourceTimeSeconds, 1.0f,
            false, nullptr, /*bDirectSourceClock=*/false, PlaybackRate);
    }

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
        bool bForceDropOutgoingRate,
        UMirrorDataTable* MirrorTable,
        bool bDirectSourceClock = true,
        float DirectRate = 1.0f,
        float InFirstUpdateDeltaOverride = -1.0f)
    {
        if (bForceDropOutgoingRate)
        {
            for (FClipLayer& Layer : Outgoing)
            {
                Layer.Rate = 1.0f;
            }
        }

        if (!Current.Sequence && !Sequence)
        {
            return;
        }

        const float SafeBlendIn = FMath::IsFinite(BlendInTime) ? FMath::Max(BlendInTime, 0.0f) : 0.0f;
        const float SafeBlendOut = FMath::IsFinite(OutgoingBlendTime) ? FMath::Max(OutgoingBlendTime, 0.0f) : SafeBlendIn;

        if (SafeBlendOut > 0.0f)
        {
            const float OldWeight = Current.GetWeight();
            if (OldWeight > KINDA_SMALL_NUMBER)
            {
                FClipLayer Leaving = MoveTemp(Current);
                if (Leaving.bUseSourceTimeClock)
                {
                    const float HandoffTime = PreviousSourceTimeSeconds >= 0.0f
                        ? PreviousSourceTimeSeconds
                        : LastSourceTimeSeconds;
                    Leaving.Time = FMath::Clamp(HandoffTime, 0.0f, Leaving.FullLength);
                    Leaving.Rate = (FMath::IsFinite(OutgoingRate) && OutgoingRate > 0.0f) ? OutgoingRate : 1.0f;
                    Leaving.bUseSourceTimeClock = false;
                    Leaving.bUseLegacyTraversalClock = false;
                    Leaving.bHoldingEndPose = !Leaving.bLoop && Leaving.Time >= Leaving.FullLength;
                }
                else if (Leaving.bUseLegacyTraversalClock)
                {
                    Leaving.Time = FMath::Clamp(Leaving.GetSampleTime(Frame), 0.0f, Leaving.FullLength);
                    Leaving.Rate = (FMath::IsFinite(OutgoingRate) && OutgoingRate > 0.0f) ? OutgoingRate : 1.0f;
                    Leaving.bUseLegacyTraversalClock = false;
                    Leaving.bHoldingEndPose = !Leaving.bLoop && Leaving.Time >= Leaving.FullLength;
                }
                else if (Leaving.Sequence)
                {
                    Leaving.Rate = (FMath::IsFinite(OutgoingRate) && OutgoingRate > 0.0f) ? OutgoingRate : 1.0f;
                }

                Leaving.Blend.SetBlendOption(OutgoingBlendOption);
                Leaving.Blend.SetBlendTime(SafeBlendOut);
                Leaving.Blend.SetValueRange(OldWeight, 0.0f);
                Leaving.Blend.Reset();
                Leaving.FirstUpdateDeltaOverride = InFirstUpdateDeltaOverride;

                if (Outgoing.Num() >= MaxOutgoingLayers)
                {
                    int32 LowestIdx = 0;
                    float LowestWeight = Outgoing[0].GetWeight();
                    for (int32 i = 1; i < Outgoing.Num(); ++i)
                    {
                        const float W = Outgoing[i].GetWeight();
                        if (W < LowestWeight)
                        {
                            LowestWeight = W;
                            LowestIdx = i;
                        }
                    }
                    Outgoing.RemoveAt(LowestIdx);
                }
                Outgoing.Add(MoveTemp(Leaving));
            }
        }
        else
        {
            Outgoing.Reset();
        }

        const float Duration = Sequence ? Sequence->GetPlayLength() : 0.0f;
        Current = FClipLayer();
        Current.Sequence = Sequence;
        Current.MirrorTable = MirrorTable;
        Current.FullLength = Duration;
        Current.SectionStart = 0.0f;
        Current.SectionEnd = Duration;
        Current.Rate = DirectRate;
        Current.Time = FMath::Clamp(StartTimeSeconds, 0.0f, Duration);
        Current.bLoop = bLoop;
        Current.bUseSourceTimeClock = bDirectSourceClock && (Sequence != nullptr);
        Current.bUseLegacyTraversalClock = false;
        Current.bHoldingEndPose = false;
        Current.FirstUpdateDeltaOverride = InFirstUpdateDeltaOverride;

        if (SafeBlendIn > 0.0f)
        {
            Current.Blend.SetBlendOption(BlendInOption);
            Current.Blend.SetBlendTime(SafeBlendIn);
            Current.Blend.SetValueRange(0.0f, 1.0f);
            Current.Blend.Reset();
        }
        else
        {
            Current.Blend.SetBlendOption(BlendInOption);
            Current.Blend.SetBlendTime(0.0f);
            Current.Blend.SetValueRange(1.0f, 1.0f);
            Current.Blend.SetAlpha(1.0f);
        }
    }

    void SetTraversalClip(UAnimSequence* Sequence, bool bLoop, float BlendTime, FVector2D Section,
        UMirrorDataTable* MirrorTable, EAlphaBlendOption BlendOption, EPlumPoleTimeCurve TimeCurve,
        float PreviousTraversalProgress, float OutgoingRate)
    {
        const float Duration = Sequence ? Sequence->GetPlayLength() : 0.0f;
        const float StartSec = Duration * static_cast<float>(FMath::Clamp(Section.X, 0.0, 1.0));
        SetTimelineClip(Sequence, bLoop, StartSec, BlendTime, BlendOption,
            BlendTime, BlendOption, LastSourceTimeSeconds, OutgoingRate, false, MirrorTable,
            /*bDirectSourceClock=*/false, 1.0f);
        if (Sequence && !bLoop)
        {
            Current.SectionStart = StartSec;
            Current.SectionEnd = Duration * static_cast<float>(FMath::Clamp(Section.Y, Section.X, 1.0));
            Current.TimeCurve = TimeCurve;
            Current.bUseLegacyTraversalClock = true;
        }
    }

    int32 GetActiveLayerCount() const
    {
        int32 Count = Current.GetWeight() > KINDA_SMALL_NUMBER ? 1 : 0;
        for (const FClipLayer& Layer : Outgoing)
        {
            if (Layer.GetWeight() > KINDA_SMALL_NUMBER)
            {
                ++Count;
            }
        }
        return Count;
    }

protected:
    virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override
    {
        FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
        Frame = {};
        if (const APlumPoleCharacter* Character = Cast<APlumPoleCharacter>(InAnimInstance->GetOwningActor()))
        {
            Frame = Character->GetTraversalAnimationFrame();
        }
        if (UPlumPoleAnimInstance* PoleAnim = Cast<UPlumPoleAnimInstance>(InAnimInstance))
        {
            PoleAnim->bDisableLegIK = Frame.bPoleAbilityActive;
            PoleAnim->LegIKAlpha = Frame.bPoleAbilityActive ? 0.0f : 1.0f;
        }
    }

    virtual void Update(float DeltaSeconds) override
    {
        Current.Advance(DeltaSeconds);
        for (int32 Index = Outgoing.Num() - 1; Index >= 0; --Index)
        {
            Outgoing[Index].Advance(DeltaSeconds);
            if (Outgoing[Index].Blend.IsComplete() && Outgoing[Index].GetWeight() <= KINDA_SMALL_NUMBER)
            {
                Outgoing.RemoveAt(Index);
            }
        }
    }

    virtual bool Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode) override
    {
        if (!Output.Pose.IsValid() || Output.Pose.GetNumBones() == 0)
        {
            return false;
        }

        // PBZ Disables LegIK on poles (CharacterState.Animation.DisableLegIK):
        // Zero custom PlantFoot / pelvis-offset overrides are applied here; CR_Mannequin_FootIK is gated via LegIKAlpha.

        if (Outgoing.IsEmpty())
        {
            const bool bTraversalPose = Current.Sample(Output, Frame);
            if (!bTraversalPose)
            {
                EvaluateBasePose(Output, InRootNode);
            }
            LastSourceTimeSeconds = Frame.SourceTimeSeconds;
            return true;
        }

        float GroundWeight = 0.0f;
        float AccumWeight = 0.0f;
        bool bHasAccum = false;

        auto AccumulatePose = [&](FPoseContext& LayerPose, float LayerWeight)
        {
            if (LayerWeight <= KINDA_SMALL_NUMBER)
            {
                return;
            }
            if (!bHasAccum)
            {
                Output.Pose.CopyBonesFrom(LayerPose.Pose);
                Output.Curve.CopyFrom(LayerPose.Curve);
                Output.CustomAttributes.CopyFrom(LayerPose.CustomAttributes);
                AccumWeight = LayerWeight;
                bHasAccum = true;
            }
            else
            {
                FAnimationPoseData OutputData(Output);
                const FAnimationPoseData LayerData(LayerPose);
                const float OutputShare = AccumWeight / (AccumWeight + LayerWeight);
                FAnimationRuntime::BlendTwoPosesTogetherInPlace(OutputData, LayerData, OutputShare);
                AccumWeight += LayerWeight;
            }
        };

        auto ProcessLayer = [&](FClipLayer& Layer, const FPlumPoleAnimationFrame& LayerFrame)
        {
            const float Weight = Layer.GetWeight();
            if (Weight <= KINDA_SMALL_NUMBER)
            {
                return;
            }
            if (!Layer.Sequence)
            {
                GroundWeight += Weight;
                return;
            }
            FPoseContext LayerPose(Output);
            if (Layer.Sample(LayerPose, LayerFrame))
            {
                AccumulatePose(LayerPose, Weight);
            }
            else
            {
                GroundWeight += Weight;
            }
        };

        const FPlumPoleAnimationFrame OutgoingFrame{};
        for (FClipLayer& Layer : Outgoing)
        {
            ProcessLayer(Layer, OutgoingFrame);
        }
        ProcessLayer(Current, Frame);

        if (GroundWeight > KINDA_SMALL_NUMBER || !bHasAccum)
        {
            FPoseContext GroundPose(Output);
            EvaluateBasePose(GroundPose, InRootNode);
            AccumulatePose(GroundPose, GroundWeight > KINDA_SMALL_NUMBER ? GroundWeight : 1.0f);
        }

        LastSourceTimeSeconds = Frame.SourceTimeSeconds;
        return true;
    }

    virtual void AddReferencedObjects(UAnimInstance* InAnimInstance, FReferenceCollector& Collector) override
    {
        FAnimInstanceProxy::AddReferencedObjects(InAnimInstance, Collector);
        Collector.AddReferencedObject(Current.Sequence);
        Collector.AddReferencedObject(Current.MirrorTable);
        for (FClipLayer& Layer : Outgoing)
        {
            Collector.AddReferencedObject(Layer.Sequence);
            Collector.AddReferencedObject(Layer.MirrorTable);
        }
    }

private:
    void EvaluateBasePose(FPoseContext& Target, FAnimNode_Base* InRootNode)
    {
        if (InRootNode)
        {
            EvaluateAnimationNode_WithRoot(Target, InRootNode);
        }
        else
        {
            Target.ResetToRefPose();
        }
    }

    FClipLayer Current;
    TArray<FClipLayer> Outgoing;
    FPlumPoleAnimationFrame Frame;
    float LastSourceTimeSeconds = 0.0f;
};
} // namespace

void UPlumPoleAnimInstance::SetClip(UAnimSequence* Sequence, bool bLoop, float Rate, float BlendTime)
{
    GetProxyOnGameThread<FPlumPoleAnimInstanceProxy>().SetClip(Sequence, bLoop, Rate, BlendTime);
}

void UPlumPoleAnimInstance::SetTimelineClip(
    UAnimSequence* Sequence,
    bool bLoop,
    float StartTimeSeconds,
    float BlendInTime,
    EAlphaBlendOption BlendInOption,
    float OutgoingBlendTime,
    EAlphaBlendOption OutgoingBlendOption,
    float PreviousSourceTimeSeconds,
    float OutgoingRate,
    bool bForceDropOutgoingRate,
    UMirrorDataTable* MirrorTable,
    float FirstUpdateDeltaOverride)
{
    GetProxyOnGameThread<FPlumPoleAnimInstanceProxy>().SetTimelineClip(
        Sequence, bLoop, StartTimeSeconds, BlendInTime, BlendInOption,
        OutgoingBlendTime, OutgoingBlendOption, PreviousSourceTimeSeconds, OutgoingRate,
        bForceDropOutgoingRate, MirrorTable, /*bDirectSourceClock=*/true, 1.0f, FirstUpdateDeltaOverride);
}

void UPlumPoleAnimInstance::SetTraversalClip(UAnimSequence* Sequence, bool bLoop, float BlendTime,
    FVector2D Section, UMirrorDataTable* MirrorTable, EAlphaBlendOption BlendOption,
    EPlumPoleTimeCurve TimeCurve, float PreviousTraversalProgress, float OutgoingRate)
{
    GetProxyOnGameThread<FPlumPoleAnimInstanceProxy>().SetTraversalClip(
        Sequence, bLoop, BlendTime, Section, MirrorTable, BlendOption, TimeCurve,
        PreviousTraversalProgress, OutgoingRate);
}

int32 UPlumPoleAnimInstance::GetActiveLayerCount() const
{
    return GetProxyOnGameThread<FPlumPoleAnimInstanceProxy>().GetActiveLayerCount();
}

void UPlumPoleAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    SetRootMotionMode(ERootMotionMode::NoRootMotionExtraction);
    bDisableLegIK = false;
    LegIKAlpha = 1.0f;
}

void UPlumPoleAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    if (const APlumPoleCharacter* Character = Cast<APlumPoleCharacter>(GetOwningActor()))
    {
        bDisableLegIK = Character->IsPoleAbilityActive();
        LegIKAlpha = bDisableLegIK ? 0.0f : 1.0f;
    }
    else
    {
        bDisableLegIK = false;
        LegIKAlpha = 1.0f;
    }
}

FAnimInstanceProxy* UPlumPoleAnimInstance::CreateAnimInstanceProxy()
{
    return new FPlumPoleAnimInstanceProxy(this);
}

void UPlumPoleAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
    delete InProxy;
}
