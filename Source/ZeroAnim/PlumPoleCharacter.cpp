#include "PlumPoleCharacter.h"

#include "Animation/AnimSequence.h"
#include "Animation/MirrorDataTable.h"
#include "Components/SkeletalMeshComponent.h"
#include "PlumPoleActor.h"
#include "PlumPoleAnimInstance.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr float FootReleaseWindow = 0.05f;
constexpr float FootPlantWindow = 0.08f;
constexpr float EntryFootPlantWindow = 0.045f;
const FName LeftSupportBone(TEXT("ball_l"));
const FName RightSupportBone(TEXT("ball_r"));
}

APlumPoleCharacter::APlumPoleCharacter()
{
    PlumPoleTraversal = CreateDefaultSubobject<UPlumPoleTraversalComponent>(TEXT("PlumPoleTraversal"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> ChainRight(TEXT("/Game/PlumPole/Animations/AS_Manny_PlumPole_ChainHopFromRight.AS_Manny_PlumPole_ChainHopFromRight"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> ChainLeft(TEXT("/Game/PlumPole/Animations/AS_Manny_PlumPole_ChainHopFromLeft.AS_Manny_PlumPole_ChainHopFromLeft"));
    ChainHopFromRight = ChainRight.Object;
    ChainHopFromLeft = ChainLeft.Object;
}

float APlumPoleCharacter::ScorePoleCandidate_Implementation(float DirectionAlignment, float HorizontalDistance,
    float HeightDelta, float SearchRadius) const
{
    return HorizontalDistance + (1.0f - DirectionAlignment) * SearchRadius * 0.8f + FMath::Abs(HeightDelta) * 0.3f;
}

EPlumPoleInputResponse APlumPoleCharacter::SelectPoleInputResponse_Implementation(EPlumPoleTraversalState State,
    bool bFlipRequested, bool bCanChainFromGround) const
{
    switch (State)
    {
    case EPlumPoleTraversalState::Grounded:
        return bFlipRequested ? EPlumPoleInputResponse::Ignore : EPlumPoleInputResponse::Enter;
    case EPlumPoleTraversalState::Perched:
        return bFlipRequested ? EPlumPoleInputResponse::PoleFlip : EPlumPoleInputResponse::HopOrDismount;
    case EPlumPoleTraversalState::Entering:
    case EPlumPoleTraversalState::Hopping:
        return EPlumPoleInputResponse::BufferRequest;
    case EPlumPoleTraversalState::Dismounting:
        if (bFlipRequested)
        {
            return EPlumPoleInputResponse::Ignore;
        }
        return bCanChainFromGround ? EPlumPoleInputResponse::ChainFromGround : EPlumPoleInputResponse::Consume;
    }
    return EPlumPoleInputResponse::Ignore;
}

void APlumPoleCharacter::BeginPlay()
{
    Super::BeginPlay();

    GetMesh()->AddTickPrerequisiteComponent(PlumPoleTraversal);
    PlumPoleTraversal->OnTraversalStateChanged.AddDynamic(this, &APlumPoleCharacter::OnTraversalChanged);
}

void APlumPoleCharacter::Jump()
{
    if (!PlumPoleTraversal->TryTraverse())
    {
        Super::Jump();
    }
}

FPlumPoleAnimationFrame APlumPoleCharacter::GetTraversalAnimationFrame() const
{
    FPlumPoleAnimationFrame Frame;
    Frame.Progress = PlumPoleTraversal->GetTraversalProgress();
    Frame.Duration = PlumPoleTraversal->GetTraversalDuration();
    const float Takeoff = PlumPoleTraversal->GetTraversalTakeoffProgress();
    const float Contact = PlumPoleTraversal->GetTraversalContactProgress();
    const EPlumPoleTraversalState State = PlumPoleTraversal->GetTraversalState();
    const float ReleaseWindow = State == EPlumPoleTraversalState::Hopping ? 0.04f : FootReleaseWindow;
    const float DepartureWeight = 1.0f - FMath::SmoothStep(Takeoff, Takeoff + ReleaseWindow, Frame.Progress);
    const float PlantWindow = State == EPlumPoleTraversalState::Entering ? EntryFootPlantWindow : FootPlantWindow;
    const float ArrivalWeight = FMath::SmoothStep(Contact - PlantWindow, Contact, Frame.Progress);
    const APlumPoleActor* FootPole = nullptr;
    bool bRightFoot = bRightFootSupport;

    switch (State)
    {
    case EPlumPoleTraversalState::Grounded:
        break;
    case EPlumPoleTraversalState::Entering:
        FootPole = PlumPoleTraversal->GetTargetPole();
        Frame.FootWeight = ArrivalWeight;
        break;
    case EPlumPoleTraversalState::Perched:
        FootPole = PlumPoleTraversal->GetCurrentPole();
        Frame.FootWeight = 1.0f;
        break;
    case EPlumPoleTraversalState::Hopping:
        if (DepartureWeight > 0.0f)
        {
            FootPole = PlumPoleTraversal->GetCurrentPole();
            Frame.FootWeight = DepartureWeight;
        }
        else
        {
            FootPole = PlumPoleTraversal->GetTargetPole();
            Frame.FootWeight = ArrivalWeight;
            bRightFoot = !bRightFootSupport;
        }
        break;
    case EPlumPoleTraversalState::Dismounting:
        FootPole = PlumPoleTraversal->GetCurrentPole();
        Frame.FootWeight = DepartureWeight;
        break;
    }

    if (FootPole)
    {
        Frame.FootBone = bRightFoot ? RightSupportBone : LeftSupportBone;
        Frame.FootTarget = FootPole->GetLandingLocation() + FVector::UpVector * PoleSoleHeight;
    }
    return Frame;
}

void APlumPoleCharacter::OnTraversalChanged(
    EPlumPoleTraversalState PreviousState, EPlumPoleTraversalState NewState, APlumPoleActor*)
{
    const bool bCompletedHop = PreviousState == EPlumPoleTraversalState::Hopping
        && PlumPoleTraversal->WasLastTraversalCompleted();
    if (NewState == EPlumPoleTraversalState::Entering)
    {
        bRightFootSupport = true;
    }
    else if (bCompletedHop && (NewState == EPlumPoleTraversalState::Perched
        || NewState == EPlumPoleTraversalState::Hopping
        || NewState == EPlumPoleTraversalState::Dismounting))
    {
        bRightFootSupport = !bRightFootSupport;
    }

    const EPlumPoleTraversalState SelectionPreviousState =
        (NewState == EPlumPoleTraversalState::Dismounting && PlumPoleTraversal->WasLastTraversalCompleted())
        ? EPlumPoleTraversalState::Perched
        : PreviousState;
    ActivePoleAnimation = SelectPoleAnimation(NewState, SelectionPreviousState, bRightFootSupport,
        PlumPoleTraversal->IsPoleFlip(), PlumPoleTraversal->IsShortDismount(),
        PlumPoleTraversal->IsChainedPoleHop());
    if (UPlumPoleAnimInstance* Player = Cast<UPlumPoleAnimInstance>(GetMesh()->GetAnimInstance()))
    {
        Player->SetTraversalClip(ActivePoleAnimation.Sequence, ActivePoleAnimation.bLoop, ActivePoleAnimation.BlendTime,
            ActivePoleAnimation.Section, ActivePoleAnimation.MirrorTable);
    }
}

FPlumPolePlayback APlumPoleCharacter::SelectPoleAnimation_Implementation(
    EPlumPoleTraversalState State, EPlumPoleTraversalState PreviousState, bool bRightSupport, bool bPoleFlip,
    bool bShortExit, bool bChainedHop) const
{
    FPlumPolePlayback Clip;
    Clip.BlendTime = TraversalBlendTime;
    switch (State)
    {
    case EPlumPoleTraversalState::Grounded:
        Clip.BlendTime = GroundBlendTime;
        break;
    case EPlumPoleTraversalState::Entering:
        Clip.Sequence = EnterPole;
        Clip.Section = EnterSection;
        break;
    case EPlumPoleTraversalState::Perched:
        Clip.Sequence = bRightSupport ? PerchRight : PerchLeft;
        Clip.bLoop = true;
        Clip.BlendTime = PreviousState == EPlumPoleTraversalState::Entering ? EntrySettleBlendTime :
            PlumPoleTraversal->HasCompletedChainHop() ? ChainHopSettleBlendTime : PerchBlendTime;
        break;
    case EPlumPoleTraversalState::Hopping:
        Clip.BlendTime = bChainedHop ? FMath::Min(0.075f, TraversalBlendTime) : TraversalBlendTime;
        if (bPoleFlip)
        {
            Clip.Sequence = PlumPoleTraversal->GetPoleFlipAnimation();
            Clip.MirrorTable = bRightSupport ? nullptr : ExitMirrorTable.Get();
        }
        else if (bChainedHop)
        {
            Clip.Sequence = bRightSupport ? ChainHopFromRight : ChainHopFromLeft;
        }
        else
        {
            Clip.Sequence = bRightSupport ? HopFromRight : HopFromLeft;
            Clip.Section = HopSection;
        }
        break;
    case EPlumPoleTraversalState::Dismounting:
        if (PreviousState != EPlumPoleTraversalState::Perched)
        {
            Clip.BlendTime = GroundBlendTime;
        }
        else if (bShortExit)
        {
            Clip.Sequence = bRightSupport ? ShortExitFromRight : ShortExitFromLeft;
        }
        else if (!bRightSupport && ExitPoleLeft)
        {
            Clip.Sequence = ExitPoleLeft;
            Clip.Section = ExitSection;
        }
        else
        {
            Clip.Sequence = ExitPole;
            Clip.Section = ExitSection;
            Clip.MirrorTable = bRightSupport ? nullptr : ExitMirrorTable.Get();
        }
        break;
    }
    return Clip;
}
