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

struct FCachedPose
{
    TArray<FTransform> Bones;
    FBlendedHeapCurve Curves;
    UE::Anim::FHeapAttributeContainer Attributes;
    uint16 BoneSerial = 0;

    void CopyFrom(const FPoseContext& Source)
    {
        Bones = Source.Pose.GetBones();
        Curves.CopyFrom(Source.Curve);
        Attributes.CopyFrom(Source.CustomAttributes);
        BoneSerial = Source.Pose.GetBoneContainer().GetSerialNumber();
    }

    void CopyFrom(const FCachedPose& Source)
    {
        Bones = Source.Bones;
        Curves.CopyFrom(Source.Curves);
        Attributes.CopyFrom(Source.Attributes);
        BoneSerial = Source.BoneSerial;
    }

    bool Matches(const FPoseContext& Output) const
    {
        return Bones.Num() == Output.Pose.GetNumBones()
            && BoneSerial == Output.Pose.GetBoneContainer().GetSerialNumber();
    }

    void ApplyTo(FPoseContext& Output) const
    {
        Output.Pose.CopyBonesFrom(Bones);
        Output.Curve.CopyFrom(Curves);
        Output.CustomAttributes.CopyFrom(Attributes);
    }
};

struct FClipPlayback
{
    TObjectPtr<UAnimSequence> Sequence;
    TObjectPtr<UMirrorDataTable> MirrorTable;
    float Start = 0.0f;
    float Length = 0.0f;
    float Rate = 1.0f;
    float Time = 0.0f;
    bool bLoop = false;
    bool bUseTraversalClock = false;

    void Advance(float DeltaSeconds)
    {
        if (!bUseTraversalClock && Length > 0.0f)
        {
            FAnimationRuntime::AdvanceTime(bLoop, Rate * DeltaSeconds, Time, Length);
        }
    }

    bool Sample(FPoseContext& Output, float TraversalProgress) const
    {
        const USkeleton* Skeleton = Output.Pose.GetBoneContainer().GetSkeletonAsset();
        if (!Sequence || !Skeleton || Sequence->GetSkeleton() != Skeleton || Sequence->IsValidAdditive())
        {
            return false;
        }

        FAnimationPoseData PoseData(Output);
        const float SampleTime = Start + (bUseTraversalClock ? TraversalProgress * Length : Time);
        const FAnimExtractContext ExtractionContext(SampleTime, false, {}, bLoop);
        Sequence->GetAnimationPose(PoseData, ExtractionContext);
        return true;
    }
};

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

void PlantFoot(FCompactPose& Pose, FName FootBone, const FVector& ComponentTarget, float Weight)
{
    const FBoneContainer& Bones = Pose.GetBoneContainer();
    const FCompactPoseBoneIndex Foot = Bones.MakeCompactPoseIndex(
        FMeshPoseBoneIndex(Bones.GetPoseBoneIndexForBoneName(FootBone)));
    const FCompactPoseBoneIndex Pelvis = Bones.MakeCompactPoseIndex(
        FMeshPoseBoneIndex(Bones.GetPoseBoneIndexForBoneName(PelvisBone)));
    if (Foot.GetInt() == INDEX_NONE || Pelvis.GetInt() == INDEX_NONE)
    {
        return;
    }

    const FVector FootLocation = GetComponentSpaceBone(Pose, Foot).GetLocation();
    const FVector Correction = (ComponentTarget - FootLocation).GetClampedToMaxSize(MaxFootCorrection) * Weight;
    const FTransform PelvisParent = GetComponentSpaceBone(Pose, Pose.GetParentBoneIndex(Pelvis));
    Pose[Pelvis].AddToTranslation(PelvisParent.InverseTransformVector(Correction));
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

        SetPlayback(Sequence, bLoop, PlaybackRate, BlendTime, FVector2D(0.0, 1.0), nullptr, false);
    }

    void SetTraversalClip(UAnimSequence* Sequence, bool bLoop, float BlendTime, FVector2D Section,
        UMirrorDataTable* MirrorTable)
    {
        if (!Current.Sequence && !Sequence)
        {
            return;
        }
        SetPlayback(Sequence, bLoop, 1.0f, BlendTime, Section, MirrorTable, !bLoop && Sequence != nullptr);
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
        if (BlendElapsed < BlendDuration)
        {
            if (bBlendFromLiveClip)
            {
                Previous.Advance(DeltaSeconds);
            }
            BlendElapsed = FMath::Min(BlendElapsed + DeltaSeconds, BlendDuration);
        }
    }

    virtual bool Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode) override
    {
        if (!Output.Pose.IsValid() || Output.Pose.GetNumBones() == 0)
        {
            return false;
        }

        const bool bTraversalPose = Current.Sample(Output, Frame.Progress);
        if (bTraversalPose)
        {
            CurrentMirror.Apply(Output, Current.MirrorTable.Get());
            // Capsule motion drives traversal. A source clip can contain an offset even on frame zero.
            const FCompactPoseBoneIndex RootBone(0);
            Output.Pose[RootBone] = Output.Pose.GetRefPose(RootBone);
        }
        else if (InRootNode)
        {
            EvaluateAnimationNode_WithRoot(Output, InRootNode);
        }
        else
        {
            Output.ResetToRefPose();
        }

        const bool bHasFootTarget = bTraversalPose && !Frame.FootBone.IsNone() && Frame.FootWeight > 0.0f;
        const FVector ComponentFootTarget = bHasFootTarget
            ? GetComponentTransform().InverseTransformPosition(Frame.FootTarget)
            : FVector::ZeroVector;

        float CurrentWeight = 1.0f;
        if (BlendElapsed < BlendDuration)
        {
            FPoseContext PreviousPose(Output);
            bool bHasPreviousPose = false;
            if (bBlendFromLiveClip && Previous.Sample(PreviousPose, 0.0f))
            {
                PreviousMirror.Apply(PreviousPose, Previous.MirrorTable.Get());
                const FCompactPoseBoneIndex RootBone(0);
                PreviousPose.Pose[RootBone] = PreviousPose.Pose.GetRefPose(RootBone);
                if (bHasFootTarget && bPreviousWasTraversalPose)
                {
                    PlantFoot(PreviousPose.Pose, Frame.FootBone, ComponentFootTarget, Frame.FootWeight);
                }
                bHasPreviousPose = true;
            }
            else if (!bBlendFromLiveClip && BlendFrom.Matches(Output))
            {
                BlendFrom.ApplyTo(PreviousPose);
                bHasPreviousPose = true;
            }

            if (bHasPreviousPose)
            {
                FAnimationPoseData OutputPoseData(Output);
                const FAnimationPoseData PreviousPoseData(PreviousPose);
                const float Alpha = FMath::Clamp(BlendElapsed / BlendDuration, 0.0f, 1.0f);
                CurrentWeight = bBlendFromLiveClip
                    ? FMath::SmoothStep(0.0f, 1.0f, Alpha)
                    : 1.0f - FMath::Square(1.0f - Alpha);
                FAnimationRuntime::BlendTwoPosesTogetherInPlace(OutputPoseData, PreviousPoseData, CurrentWeight);
            }
        }

        if (bHasFootTarget)
        {
            const float PlantWeight = bPreviousWasTraversalPose ? Frame.FootWeight : Frame.FootWeight * CurrentWeight;
            PlantFoot(Output.Pose, Frame.FootBone, ComponentFootTarget, PlantWeight);
        }

        LastTraversalProgress = Frame.Progress;
        LastTraversalDuration = Frame.Duration;
        LastOutput.CopyFrom(Output);
        return true;
    }

    virtual void AddReferencedObjects(UAnimInstance* InAnimInstance, FReferenceCollector& Collector) override
    {
        FAnimInstanceProxy::AddReferencedObjects(InAnimInstance, Collector);
        Collector.AddReferencedObject(Current.Sequence);
        Collector.AddReferencedObject(Current.MirrorTable);
        Collector.AddReferencedObject(Previous.Sequence);
        Collector.AddReferencedObject(Previous.MirrorTable);
    }

private:
    void SetPlayback(UAnimSequence* Sequence, bool bLoop, float Rate, float BlendTime,
        const FVector2D& Section, UMirrorDataTable* MirrorTable, bool bUseTraversalClock)
    {
        const bool bWasBlending = BlendElapsed < BlendDuration;
        const bool bHadTraversalPose = Current.Sequence != nullptr;
        BlendDuration = FMath::IsFinite(BlendTime) ? FMath::Max(BlendTime, 0.0f) : 0.0f;
        BlendElapsed = 0.0f;
        bPreviousWasTraversalPose = bHadTraversalPose;
        bBlendFromLiveClip = false;

        if (BlendDuration > 0.0f)
        {
            if (bHadTraversalPose && !bWasBlending)
            {
                Previous = Current;
                if (Previous.bUseTraversalClock)
                {
                    Previous.Time = FMath::Clamp(LastTraversalProgress, 0.0f, 1.0f) * Previous.Length;
                    Previous.Rate = LastTraversalDuration > KINDA_SMALL_NUMBER
                        ? Previous.Length / LastTraversalDuration
                        : 1.0f;
                    Previous.bUseTraversalClock = false;
                }
                PreviousMirror.Reset();
                bBlendFromLiveClip = true;
            }
            else
            {
                Previous = {};
                // Match the displayed pose when interrupting an active fade or transitioning from ground locomotion.
                BlendFrom.CopyFrom(LastOutput);
            }
        }
        else
        {
            Previous = {};
        }

        if (Current.MirrorTable != MirrorTable)
        {
            CurrentMirror.Reset();
        }
        const float Duration = Sequence ? Sequence->GetPlayLength() : 0.0f;
        const double Start = FMath::IsFinite(Section.X) ? FMath::Clamp(Section.X, 0.0, 1.0) : 0.0;
        const double End = FMath::IsFinite(Section.Y) ? FMath::Clamp(Section.Y, Start, 1.0) : 1.0;
        Current.Sequence = Sequence;
        Current.MirrorTable = MirrorTable;
        Current.Start = Duration * static_cast<float>(Start);
        Current.Length = Duration * static_cast<float>(End - Start);
        Current.Rate = Rate;
        Current.Time = Rate < 0.0f ? Current.Length : 0.0f;
        Current.bLoop = bLoop;
        Current.bUseTraversalClock = bUseTraversalClock;
    }

    FClipPlayback Current;
    FClipPlayback Previous;
    FCachedPose LastOutput;
    FCachedPose BlendFrom;
    FPlumPoleAnimationFrame Frame;
    FMirrorCache CurrentMirror;
    FMirrorCache PreviousMirror;
    float BlendDuration = 0.0f;
    float BlendElapsed = 0.0f;
    float LastTraversalProgress = 0.0f;
    float LastTraversalDuration = 0.0f;
    bool bBlendFromLiveClip = false;
    bool bPreviousWasTraversalPose = false;
};
}

void UPlumPoleAnimInstance::SetClip(UAnimSequence* Sequence, bool bLoop, float Rate, float BlendTime)
{
    GetProxyOnGameThread<FPlumPoleAnimInstanceProxy>().SetClip(Sequence, bLoop, Rate, BlendTime);
}

void UPlumPoleAnimInstance::SetTraversalClip(UAnimSequence* Sequence, bool bLoop, float BlendTime,
    FVector2D Section, UMirrorDataTable* MirrorTable)
{
    GetProxyOnGameThread<FPlumPoleAnimInstanceProxy>().SetTraversalClip(
        Sequence, bLoop, BlendTime, Section, MirrorTable);
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
