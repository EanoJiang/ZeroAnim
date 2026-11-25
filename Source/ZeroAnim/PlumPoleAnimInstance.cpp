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
const FName PelvisBone(TEXT("pelvis"));
constexpr double MaxFootCorrection = 45.0;
constexpr int32 MaxOutgoingLayers = 6;
constexpr float DepartureOffsetDecaySeconds = 0.14f;

FTransform GetComponentSpaceBone(const FCompactPose& Pose, FCompactPoseBoneIndex Bone)
{
    FTransform ComponentSpace = FTransform::Identity;
    while (Bone.GetInt() != INDEX_NONE)
    {
        ComponentSpace = ComponentSpace * Pose[Bone];
        Bone = Pose.GetParentBoneIndex(Bone);
    }
    return ComponentSpace;
}

void ApplyPelvisComponentOffset(FCompactPose& Pose, const FVector& ComponentOffset)
{
    if (ComponentOffset.IsNearlyZero())
    {
        return;
    }
    const FBoneContainer& Bones = Pose.GetBoneContainer();
    const FCompactPoseBoneIndex Pelvis = Bones.MakeCompactPoseIndex(
        FMeshPoseBoneIndex(Bones.GetPoseBoneIndexForBoneName(PelvisBone)));
    if (Pelvis.GetInt() == INDEX_NONE)
    {
        return;
    }
    const FTransform PelvisParent = GetComponentSpaceBone(Pose, Pose.GetParentBoneIndex(Pelvis));
    Pose[Pelvis].AddToTranslation(PelvisParent.InverseTransformVector(ComponentOffset));
}

FVector PlantFoot(FCompactPose& Pose, FName FootBone, const FVector& ComponentTarget, float Weight)
{
    const FBoneContainer& Bones = Pose.GetBoneContainer();
    const FCompactPoseBoneIndex Foot = Bones.MakeCompactPoseIndex(
        FMeshPoseBoneIndex(Bones.GetPoseBoneIndexForBoneName(FootBone)));
    const FCompactPoseBoneIndex Pelvis = Bones.MakeCompactPoseIndex(
        FMeshPoseBoneIndex(Bones.GetPoseBoneIndexForBoneName(PelvisBone)));
    if (Foot.GetInt() == INDEX_NONE || Pelvis.GetInt() == INDEX_NONE)
    {
        return FVector::ZeroVector;
    }

    const FVector FootLocation = GetComponentSpaceBone(Pose, Foot).GetLocation();
    const FVector Correction = (ComponentTarget - FootLocation).GetClampedToMaxSize(MaxFootCorrection) * Weight;
    const FTransform PelvisParent = GetComponentSpaceBone(Pose, Pose.GetParentBoneIndex(Pelvis));
    Pose[Pelvis].AddToTranslation(PelvisParent.InverseTransformVector(Correction));
    return Correction;
}

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
    EPlumPoleTimeCurve TimeCurve = EPlumPoleTimeCurve::Linear;
    bool bLoop = false;
    bool bUseTraversalClock = false;
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

    float GetSampleTime(float TraversalProgress) const
    {
        if (bUseTraversalClock)
        {
            return FMath::Clamp(
                EvaluatePlumPoleTimeCurve(TimeCurve, SectionStart, SectionEnd, TraversalProgress),
                0.0f, FullLength);
        }
        return FMath::Clamp(Time, 0.0f, FullLength);
    }

    void Advance(float DeltaSeconds)
    {
        Blend.Update(DeltaSeconds);
        if (!bUseTraversalClock && Sequence && FullLength > 0.0f)
        {
            if (bLoop)
            {
                FAnimationRuntime::AdvanceTime(true, Rate * DeltaSeconds, Time, FullLength);
                bHoldingEndPose = false;
            }
            else if (!bHoldingEndPose)
            {
                Time += Rate * DeltaSeconds;
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

    bool Sample(FPoseContext& Output, float TraversalProgress)
    {
        const USkeleton* Skeleton = Output.Pose.GetBoneContainer().GetSkeletonAsset();
        if (!Sequence || !Skeleton || Sequence->GetSkeleton() != Skeleton || Sequence->IsValidAdditive())
        {
            return false;
        }

        FAnimationPoseData PoseData(Output);
        const float SampleTime = GetSampleTime(TraversalProgress);
        const FAnimExtractContext ExtractionContext(SampleTime, false, {}, bLoop);
        Sequence->GetAnimationPose(PoseData, ExtractionContext);
        MirrorCache.Apply(Output, MirrorTable.Get());

        // Capsule motion drives traversal. A source clip can contain root translation even on frame zero.
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
        if (Current.Sequence == Sequence && !Current.bUseTraversalClock && !Current.MirrorTable)
        {
            Current.bLoop = bLoop;
            Current.Rate = PlaybackRate;
            return;
        }

        SetPlayback(Sequence, bLoop, PlaybackRate, BlendTime, FVector2D(0.0, 1.0), nullptr,
            false, EAlphaBlendOption::HermiteCubic, EPlumPoleTimeCurve::Linear, -1.0f, 1.0f);
    }

    void SetTraversalClip(UAnimSequence* Sequence, bool bLoop, float BlendTime, FVector2D Section,
        UMirrorDataTable* MirrorTable, EAlphaBlendOption BlendOption, EPlumPoleTimeCurve TimeCurve,
        float PreviousTraversalProgress, float OutgoingRate)
    {
        if (!Current.Sequence && !Sequence)
        {
            return;
        }
        SetPlayback(Sequence, bLoop, 1.0f, BlendTime, Section, MirrorTable,
            !bLoop && Sequence != nullptr, BlendOption, TimeCurve, PreviousTraversalProgress, OutgoingRate);
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
        if (Frame.bDecayFootOffset && FootOffsetDecayRemaining > 0.0f)
        {
            FootOffsetDecayRemaining = FMath::Max(0.0f, FootOffsetDecayRemaining - DeltaSeconds);
            if (FootOffsetDecayRemaining <= KINDA_SMALL_NUMBER)
            {
                CachedFootOffset = FVector::ZeroVector;
            }
        }
    }

    virtual bool Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode) override
    {
        if (!Output.Pose.IsValid() || Output.Pose.GetNumBones() == 0)
        {
            return false;
        }

        const bool bHasFootTarget = !Frame.FootBone.IsNone() && Frame.FootWeight > 0.0f;
        const FVector ComponentFootTarget = bHasFootTarget
            ? GetComponentTransform().InverseTransformPosition(Frame.FootTarget)
            : FVector::ZeroVector;

        auto ApplyFootOffset = [&](FCompactPose& Pose, float TraversalShare)
        {
            if (TraversalShare <= KINDA_SMALL_NUMBER)
            {
                CachedFootOffset = FVector::ZeroVector;
                FootOffsetDecayRemaining = 0.0f;
                return;
            }
            if (bHasFootTarget)
            {
                CachedFootOffset = PlantFoot(Pose, Frame.FootBone, ComponentFootTarget, Frame.FootWeight * TraversalShare);
                FootOffsetDecayRemaining = DepartureOffsetDecaySeconds;
            }
            else if (Frame.bDecayFootOffset && FootOffsetDecayRemaining > 0.0f)
            {
                const float DecayAlpha = FMath::SmoothStep(0.0f, 1.0f, FootOffsetDecayRemaining / DepartureOffsetDecaySeconds);
                ApplyPelvisComponentOffset(Pose, CachedFootOffset * (DecayAlpha * TraversalShare));
            }
            else
            {
                CachedFootOffset = FVector::ZeroVector;
                FootOffsetDecayRemaining = 0.0f;
            }
        };

        // Fast path: no active crossfade layers.
        if (Outgoing.IsEmpty())
        {
            const bool bTraversalPose = Current.Sample(Output, Frame.Progress);
            if (!bTraversalPose)
            {
                EvaluateBasePose(Output, InRootNode);
                ApplyFootOffset(Output.Pose, 0.0f);
            }
            else
            {
                ApplyFootOffset(Output.Pose, 1.0f);
            }
            LastTraversalProgress = Frame.Progress;
            return true;
        }

        // Multi-instance blend path (matches PBZ V6 2- and 3-instance Montage overlap).
        float GroundWeight = 0.0f;
        float TraversalWeight = 0.0f;
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

        auto ProcessLayer = [&](FClipLayer& Layer, float TraversalProgress)
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
            if (Layer.Sample(LayerPose, TraversalProgress))
            {
                AccumulatePose(LayerPose, Weight);
                TraversalWeight += Weight;
            }
            else
            {
                GroundWeight += Weight;
            }
        };

        // Evaluate oldest-to-newest outgoing layers, then the active layer.
        for (FClipLayer& Layer : Outgoing)
        {
            ProcessLayer(Layer, 0.0f);
        }
        ProcessLayer(Current, Frame.Progress);

        // Evaluate the Anim Blueprint base pose at most once per frame when entering or exiting ground locomotion.
        if (GroundWeight > KINDA_SMALL_NUMBER || !bHasAccum)
        {
            FPoseContext GroundPose(Output);
            EvaluateBasePose(GroundPose, InRootNode);
            AccumulatePose(GroundPose, GroundWeight > KINDA_SMALL_NUMBER ? GroundWeight : 1.0f);
        }

        // Apply foot anchoring ONCE to the final blended pose (prevents double-application during multi-layer blends).
        const float TraversalShare = AccumWeight > KINDA_SMALL_NUMBER
            ? FMath::Clamp(TraversalWeight / AccumWeight, 0.0f, 1.0f)
            : 0.0f;
        ApplyFootOffset(Output.Pose, TraversalShare);

        LastTraversalProgress = Frame.Progress;
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

    void SetPlayback(UAnimSequence* Sequence, bool bLoop, float Rate, float BlendTime,
        const FVector2D& Section, UMirrorDataTable* MirrorTable, bool bUseTraversalClock,
        EAlphaBlendOption BlendOption, EPlumPoleTimeCurve TimeCurve,
        float PreviousTraversalProgress, float OutgoingRate)
    {
        const float SafeBlendTime = FMath::IsFinite(BlendTime) ? FMath::Max(BlendTime, 0.0f) : 0.0f;

        if (SafeBlendTime > 0.0f)
        {
            const float OldWeight = Current.GetWeight();
            if (OldWeight > KINDA_SMALL_NUMBER)
            {
                FClipLayer Leaving = MoveTemp(Current);
                if (Leaving.bUseTraversalClock)
                {
                    // PBZ V6: outgoing Montage instances continue advancing into their clip tail from the exact
                    // handoff position (1.000 s/s generally, 1.025 s/s for JumpOut blend-out) and hold end pose.
                    const float HandoffProgress = PreviousTraversalProgress >= 0.0f
                        ? FMath::Clamp(PreviousTraversalProgress, 0.0f, 1.0f)
                        : LastTraversalProgress;
                    Leaving.Time = Leaving.GetSampleTime(HandoffProgress);
                    Leaving.Rate = FMath::IsFinite(OutgoingRate) && OutgoingRate > 0.0f ? OutgoingRate : 1.0f;
                    Leaving.bUseTraversalClock = false;
                    Leaving.bHoldingEndPose = !Leaving.bLoop && Leaving.Time >= Leaving.FullLength;
                }
                Leaving.Blend.SetBlendOption(BlendOption);
                Leaving.Blend.SetBlendTime(SafeBlendTime);
                Leaving.Blend.SetValueRange(OldWeight, 0.0f);
                Leaving.Blend.Reset();

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
        const double StartFrac = FMath::IsFinite(Section.X) ? FMath::Clamp(Section.X, 0.0, 1.0) : 0.0;
        const double EndFrac = FMath::IsFinite(Section.Y) ? FMath::Clamp(Section.Y, StartFrac, 1.0) : 1.0;

        Current = FClipLayer();
        Current.Sequence = Sequence;
        Current.MirrorTable = MirrorTable;
        Current.FullLength = Duration;
        Current.SectionStart = Duration * static_cast<float>(StartFrac);
        Current.SectionEnd = Duration * static_cast<float>(EndFrac);
        Current.Rate = Rate;
        Current.Time = Rate < 0.0f ? Current.SectionEnd : Current.SectionStart;
        Current.TimeCurve = TimeCurve;
        Current.bLoop = bLoop;
        Current.bUseTraversalClock = bUseTraversalClock;
        Current.bHoldingEndPose = false;

        if (SafeBlendTime > 0.0f)
        {
            Current.Blend.SetBlendOption(BlendOption);
            Current.Blend.SetBlendTime(SafeBlendTime);
            Current.Blend.SetValueRange(0.0f, 1.0f);
            Current.Blend.Reset();
        }
        else
        {
            Current.Blend.SetBlendOption(BlendOption);
            Current.Blend.SetBlendTime(0.0f);
            Current.Blend.SetValueRange(1.0f, 1.0f);
            Current.Blend.SetAlpha(1.0f);
        }
    }

    FClipLayer Current;
    TArray<FClipLayer> Outgoing;
    FPlumPoleAnimationFrame Frame;
    FVector CachedFootOffset = FVector::ZeroVector;
    float FootOffsetDecayRemaining = 0.0f;
    float LastTraversalProgress = 0.0f;
};
}

void UPlumPoleAnimInstance::SetClip(UAnimSequence* Sequence, bool bLoop, float Rate, float BlendTime)
{
    GetProxyOnGameThread<FPlumPoleAnimInstanceProxy>().SetClip(Sequence, bLoop, Rate, BlendTime);
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
}

FAnimInstanceProxy* UPlumPoleAnimInstance::CreateAnimInstanceProxy()
{
    return new FPlumPoleAnimInstanceProxy(this);
}

void UPlumPoleAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
    delete InProxy;
}
