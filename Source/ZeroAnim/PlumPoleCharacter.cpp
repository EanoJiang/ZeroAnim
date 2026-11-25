#include "PlumPoleCharacter.h"

#include "Animation/AnimSequence.h"
#include "Animation/MirrorDataTable.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "PlumPoleActor.h"
#include "PlumPoleAnimInstance.h"
#include "PlumPoleInteractPromptWidget.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr float FootPlantWindow = 0.08f;
constexpr float EntryFootPlantWindow = 0.08f;
const FName LeftSupportBone(TEXT("ball_l"));
const FName RightSupportBone(TEXT("ball_r"));
}

APlumPoleCharacter::APlumPoleCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    PlumPoleTraversal = CreateDefaultSubobject<UPlumPoleTraversalComponent>(TEXT("PlumPoleTraversal"));

    InteractPromptComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("InteractPromptComponent"));
    InteractPromptComponent->SetupAttachment(RootComponent);
    InteractPromptComponent->SetWidgetSpace(EWidgetSpace::World);
    InteractPromptComponent->SetGeometryMode(EWidgetGeometryMode::Plane);
    InteractPromptComponent->SetDrawSize(FVector2D(280.0f, 140.0f));
    InteractPromptComponent->SetPivot(FVector2D(0.5f, 0.5f));
    InteractPromptComponent->SetBlendMode(EWidgetBlendMode::Transparent);
    InteractPromptComponent->SetTwoSided(true);
    InteractPromptComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    InteractPromptComponent->SetCastShadow(false);
    InteractPromptComponent->bAffectDistanceFieldLighting = false;
    InteractPromptComponent->SetUsingAbsoluteLocation(true);
    InteractPromptComponent->SetUsingAbsoluteRotation(true);
    InteractPromptComponent->SetUsingAbsoluteScale(true);
    InteractPromptComponent->SetVisibility(false);
    InteractPromptWidgetClass = UPlumPoleInteractPromptWidget::StaticClass();

    static ConstructorHelpers::FObjectFinder<UAnimSequence> EnterFlip(TEXT("/Game/PlumPole/Animations/AS_Manny_PlumPole_PoleFlipFromRight.AS_Manny_PlumPole_PoleFlipFromRight"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> HopRight(TEXT("/Game/PlumPole/Animations/AS_Manny_PlumPole_HopFromRight.AS_Manny_PlumPole_HopFromRight"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> HopLeft(TEXT("/Game/PlumPole/Animations/AS_Manny_PlumPole_HopFromLeft.AS_Manny_PlumPole_HopFromLeft"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> JumpOutRight(TEXT("/Game/PlumPole/Animations/SourceTiming/AS_Manny_Hun_LionHead_JumpOut_R_02.AS_Manny_Hun_LionHead_JumpOut_R_02"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> JumpOutLeft(TEXT("/Game/PlumPole/Animations/SourceTiming/AS_Manny_Hun_LionHead_JumpOut_L_02.AS_Manny_Hun_LionHead_JumpOut_L_02"));
    EnterPole = EnterFlip.Object;
    ChainHopFromRight = HopRight.Object;
    ChainHopFromLeft = HopLeft.Object;
    ExitPole = JumpOutRight.Object;
    ExitPoleLeft = JumpOutLeft.Object;
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
        // On poles, directional input drives hops, far flips, and dismount; Jump is reserved for entering from ground.
        return bFlipRequested ? EPlumPoleInputResponse::PoleFlip : EPlumPoleInputResponse::Consume;
    case EPlumPoleTraversalState::Entering:
    case EPlumPoleTraversalState::Hopping:
        return bFlipRequested ? EPlumPoleInputResponse::BufferRequest : EPlumPoleInputResponse::Consume;
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

    ResolveActiveCameraComponents();
    if (bUsePlumPoleCameraBlend && CachedCameraBoom && CachedFollowCamera)
    {
        CachedCameraBoom->TargetArmLength = GroundCameraArmLength;
        CachedCameraBoom->SocketOffset = GroundCameraSocketOffset;
        CachedCameraBoom->TargetOffset = GroundCameraTargetOffset;
        CachedCameraBoom->bEnableCameraLag = true;
        CachedCameraBoom->CameraLagSpeed = 11.0f;
        CachedCameraBoom->CameraLagMaxDistance = 75.0f;
        CachedCameraBoom->bEnableCameraRotationLag = true;
        CachedCameraBoom->CameraRotationLagSpeed = 14.0f;
        CachedFollowCamera->SetFieldOfView(GroundCameraFOV);
    }

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (PC->IsLocalPlayerController())
        {
            FRotator ControlRot = PC->GetControlRotation();
            if (FMath::IsNearlyZero(ControlRot.Pitch, 1.0f))
            {
                ControlRot.Pitch = InitialCameraPitch;
                PC->SetControlRotation(ControlRot);
            }
            RegisterInputDeviceDetector();
        }
    }

    if (InteractPromptComponent)
    {
        UClass* WidgetCls = InteractPromptWidgetClass
            ? InteractPromptWidgetClass.Get()
            : UPlumPoleInteractPromptWidget::StaticClass();
        InteractPromptComponent->SetWidgetClass(WidgetCls);
        InteractPromptComponent->InitWidget();
        if (UPlumPoleInteractPromptWidget* Prompt = Cast<UPlumPoleInteractPromptWidget>(InteractPromptComponent->GetUserWidgetObject()))
        {
            Prompt->SetUsingGamepad(bUsingGamepad);
            Prompt->SetPromptAlpha(0.0f);
        }
        InteractPromptComponent->SetVisibility(false);
    }
}

void APlumPoleCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdatePlumPoleCamera(DeltaSeconds);
    UpdateInteractPrompt(DeltaSeconds);
}

void APlumPoleCharacter::SetUsingGamepadForPrompt(bool bInUsingGamepad)
{
    if (bUsingGamepad == bInUsingGamepad)
    {
        return;
    }
    bUsingGamepad = bInUsingGamepad;
    if (InteractPromptComponent)
    {
        if (UPlumPoleInteractPromptWidget* Prompt = Cast<UPlumPoleInteractPromptWidget>(InteractPromptComponent->GetUserWidgetObject()))
        {
            Prompt->SetUsingGamepad(bUsingGamepad);
        }
    }
}

void APlumPoleCharacter::ResolveActiveCameraComponents()
{
    CachedCameraBoom = nullptr;
    CachedFollowCamera = nullptr;

    TInlineComponentArray<USpringArmComponent*> SpringArms;
    GetComponents(SpringArms);
    for (USpringArmComponent* Arm : SpringArms)
    {
        if (!IsValid(Arm) || !Arm->IsRegistered() || Arm->IsBeingDestroyed() || Arm->GetAttachParent() == nullptr)
        {
            continue;
        }
        if (Arm->GetName().StartsWith(TEXT("TRASH_")))
        {
            continue;
        }
        if (!CachedCameraBoom || (Arm->IsActive() && !CachedCameraBoom->IsActive()))
        {
            CachedCameraBoom = Arm;
        }
    }

    TInlineComponentArray<UCameraComponent*> Cameras;
    GetComponents(Cameras);
    for (UCameraComponent* Cam : Cameras)
    {
        if (!IsValid(Cam) || !Cam->IsRegistered() || Cam->IsBeingDestroyed())
        {
            continue;
        }
        if (Cam->GetName().StartsWith(TEXT("TRASH_")))
        {
            continue;
        }
        const bool bAttachedToBoom = CachedCameraBoom && Cam->GetAttachParent() == CachedCameraBoom;
        const bool bPrevAttachedToBoom = CachedCameraBoom && CachedFollowCamera && CachedFollowCamera->GetAttachParent() == CachedCameraBoom;
        if (!CachedFollowCamera
            || (bAttachedToBoom && !bPrevAttachedToBoom)
            || (bAttachedToBoom == bPrevAttachedToBoom && Cam->IsActive() && !CachedFollowCamera->IsActive()))
        {
            CachedFollowCamera = Cam;
        }
    }
}

void APlumPoleCharacter::UpdatePlumPoleCamera(float DeltaSeconds)
{
    if (!bUsePlumPoleCameraBlend)
    {
        return;
    }
    if (!IsValid(CachedCameraBoom) || !CachedCameraBoom->IsRegistered() || CachedCameraBoom->GetAttachParent() == nullptr
        || !IsValid(CachedFollowCamera) || !CachedFollowCamera->IsRegistered())
    {
        ResolveActiveCameraComponents();
    }
    if (!CachedCameraBoom || !CachedFollowCamera)
    {
        return;
    }

    const EPlumPoleTraversalState State = PlumPoleTraversal->GetTraversalState();
    const bool bOnPole = State == EPlumPoleTraversalState::Entering
        || State == EPlumPoleTraversalState::Perched
        || State == EPlumPoleTraversalState::Hopping;

    const float TargetArm = bOnPole ? PoleCameraArmLength : GroundCameraArmLength;
    const FVector TargetSocket = bOnPole ? PoleCameraSocketOffset : GroundCameraSocketOffset;
    const FVector TargetOff = bOnPole ? PoleCameraTargetOffset : GroundCameraTargetOffset;
    const float TargetFOV = bOnPole ? PoleCameraFOV : GroundCameraFOV;

    CachedCameraBoom->TargetArmLength = FMath::FInterpTo(CachedCameraBoom->TargetArmLength, TargetArm, DeltaSeconds, CameraBlendSpeed);
    CachedCameraBoom->SocketOffset = FMath::VInterpTo(CachedCameraBoom->SocketOffset, TargetSocket, DeltaSeconds, CameraBlendSpeed);
    CachedCameraBoom->TargetOffset = FMath::VInterpTo(CachedCameraBoom->TargetOffset, TargetOff, DeltaSeconds, CameraBlendSpeed);
    CachedFollowCamera->SetFieldOfView(FMath::FInterpTo(CachedFollowCamera->FieldOfView, TargetFOV, DeltaSeconds, CameraBlendSpeed));

    const float TargetPitchOffset = bOnPole ? PoleCameraPitchOffset : 0.0f;
    const float NextPitchOffset = FMath::FInterpTo(AppliedPolePitchOffset, TargetPitchOffset, DeltaSeconds, CameraBlendSpeed);
    const float DeltaPitch = NextPitchOffset - AppliedPolePitchOffset;
    AppliedPolePitchOffset = NextPitchOffset;
    if (!FMath::IsNearlyZero(DeltaPitch, 1e-4f))
    {
        if (APlayerController* PC = Cast<APlayerController>(GetController()))
        {
            if (PC->IsLocalPlayerController())
            {
                FRotator ControlRot = PC->GetControlRotation();
                const double NormalizedPitch = FRotator::NormalizeAxis(ControlRot.Pitch);
                ControlRot.Pitch = FMath::Clamp(NormalizedPitch + static_cast<double>(DeltaPitch), -55.0, 20.0);
                PC->SetControlRotation(ControlRot);
            }
        }
    }
}

void APlumPoleCharacter::UpdateInteractPrompt(float DeltaSeconds)
{
    if (!InteractPromptComponent)
    {
        return;
    }

    APlumPoleActor* TargetEnterPole = bShowInteractPrompt ? PlumPoleTraversal->FindEnterablePole() : nullptr;
    if (TargetEnterPole)
    {
        PromptTargetPole = TargetEnterPole;
    }

    const float TargetAlpha = TargetEnterPole ? 1.0f : 0.0f;
    PromptAlpha = FMath::FInterpConstantTo(PromptAlpha, TargetAlpha, DeltaSeconds, TargetAlpha > PromptAlpha ? 10.0f : 12.0f);

    if (PromptAlpha <= KINDA_SMALL_NUMBER || !PromptTargetPole.IsValid())
    {
        if (InteractPromptComponent->IsVisible())
        {
            InteractPromptComponent->SetVisibility(false);
        }
        if (!TargetEnterPole)
        {
            PromptTargetPole.Reset();
        }
        return;
    }

    FVector CamLocation = GetActorLocation() - GetActorForwardVector() * 340.0f + FVector::UpVector * 140.0f;
    if (const APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (PC->PlayerCameraManager)
        {
            CamLocation = PC->PlayerCameraManager->GetCameraLocation();
        }
    }
    else if (CachedFollowCamera)
    {
        CamLocation = CachedFollowCamera->GetComponentLocation();
    }

    const APlumPoleActor* Pole = PromptTargetPole.Get();
    const FVector Anchor = Pole->GetLandingLocation() - FVector::UpVector * InteractPromptHeightBelowCrown;
    const FVector ToCam2D = (CamLocation - Anchor).GetSafeNormal2D();
    const FVector WidgetLoc = Anchor + ToCam2D * (Pole->GetRadius() + 14.0f);
    const FVector ToCam = (CamLocation - WidgetLoc).GetSafeNormal();
    const FRotator FacingRot = ToCam.IsNearlyZero()
        ? FRotator::ZeroRotator
        : FRotationMatrix::MakeFromX(ToCam).Rotator();

    const float CamDist = FVector::Dist(CamLocation, WidgetLoc);
    const float Scale = InteractPromptBaseScale * FMath::Clamp(CamDist / 360.0f, 0.72f, 1.35f);

    InteractPromptComponent->SetWorldLocationAndRotation(WidgetLoc, FacingRot);
    InteractPromptComponent->SetWorldScale3D(FVector(Scale));
    InteractPromptComponent->SetTintColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, PromptAlpha));
    if (UPlumPoleInteractPromptWidget* Prompt = Cast<UPlumPoleInteractPromptWidget>(InteractPromptComponent->GetUserWidgetObject()))
    {
        Prompt->SetUsingGamepad(bUsingGamepad);
        Prompt->SetPromptAlpha(PromptAlpha);
    }
    if (!InteractPromptComponent->IsVisible())
    {
        InteractPromptComponent->SetVisibility(true);
    }
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
        if (Frame.Progress <= Takeoff)
        {
            FootPole = PlumPoleTraversal->GetCurrentPole();
            Frame.FootWeight = 1.0f;
        }
        else if (ArrivalWeight > 0.0f)
        {
            FootPole = PlumPoleTraversal->GetTargetPole();
            Frame.FootWeight = ArrivalWeight;
            bRightFoot = !bRightFootSupport;
        }
        else
        {
            // Decay the frozen component-space planted offset smoothly instead of pulling back toward the receding pole.
            Frame.bDecayFootOffset = true;
        }
        break;
    case EPlumPoleTraversalState::Dismounting:
        if (Frame.Progress <= Takeoff)
        {
            FootPole = PlumPoleTraversal->GetCurrentPole();
            Frame.FootWeight = 1.0f;
        }
        else
        {
            Frame.bDecayFootOffset = true;
        }
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
        bRightFootSupport = bEnterLandsOnRightFoot;
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
        const float PrevProgress = PlumPoleTraversal->GetPreviousTraversalProgress();
        const float OutgoingRate = (PreviousState == EPlumPoleTraversalState::Dismounting
            && NewState == EPlumPoleTraversalState::Grounded) ? 1.025f : 1.0f;
        Player->SetTraversalClip(ActivePoleAnimation.Sequence, ActivePoleAnimation.bLoop, ActivePoleAnimation.BlendTime,
            ActivePoleAnimation.Section, ActivePoleAnimation.MirrorTable,
            ActivePoleAnimation.BlendOption, ActivePoleAnimation.TimeCurve, PrevProgress, OutgoingRate);
    }
}

FPlumPolePlayback APlumPoleCharacter::SelectPoleAnimation_Implementation(
    EPlumPoleTraversalState State, EPlumPoleTraversalState PreviousState, bool bRightSupport, bool bPoleFlip,
    bool bShortExit, bool bChainedHop) const
{
    FPlumPolePlayback Clip;
    Clip.BlendTime = TraversalBlendTime;
    Clip.BlendOption = EAlphaBlendOption::CircularIn;
    switch (State)
    {
    case EPlumPoleTraversalState::Grounded:
        Clip.BlendTime = GroundBlendTime;
        Clip.BlendOption = EAlphaBlendOption::CircularIn;
        break;
    case EPlumPoleTraversalState::Entering:
        Clip.Sequence = EnterPole;
        Clip.Section = EnterSection;
        Clip.BlendTime = PoleFlipBlendTime;
        Clip.BlendOption = EAlphaBlendOption::HermiteCubic;
        Clip.TimeCurve = EPlumPoleTimeCurve::ForwardFlip;
        break;
    case EPlumPoleTraversalState::Perched:
        Clip.Sequence = bRightSupport ? PerchRight : PerchLeft;
        Clip.bLoop = true;
        Clip.BlendTime = PreviousState == EPlumPoleTraversalState::Entering ? EntrySettleBlendTime :
            PlumPoleTraversal->HasCompletedChainHop() ? ChainHopSettleBlendTime : PerchBlendTime;
        Clip.BlendOption = EAlphaBlendOption::HermiteCubic;
        Clip.TimeCurve = EPlumPoleTimeCurve::Linear;
        break;
    case EPlumPoleTraversalState::Hopping:
        if (bPoleFlip)
        {
            Clip.Sequence = PlumPoleTraversal->GetPoleFlipAnimation();
            Clip.Section = PoleFlipSection;
            Clip.BlendTime = PoleFlipBlendTime;
            Clip.BlendOption = EAlphaBlendOption::HermiteCubic;
            Clip.TimeCurve = EPlumPoleTimeCurve::ForwardFlip;
            Clip.MirrorTable = bRightSupport ? nullptr : ExitMirrorTable.Get();
        }
        else if (bChainedHop)
        {
            Clip.Sequence = bRightSupport ? ChainHopFromRight : ChainHopFromLeft;
            Clip.Section = ChainHopSection;
            Clip.BlendTime = TraversalBlendTime;
            Clip.BlendOption = EAlphaBlendOption::CircularIn;
            Clip.TimeCurve = EPlumPoleTimeCurve::NearHop;
        }
        else
        {
            Clip.Sequence = bRightSupport ? HopFromRight : HopFromLeft;
            Clip.Section = HopSection;
            Clip.BlendTime = TraversalBlendTime;
            Clip.BlendOption = EAlphaBlendOption::CircularIn;
            Clip.TimeCurve = EPlumPoleTimeCurve::NearHop;
        }
        break;
    case EPlumPoleTraversalState::Dismounting:
        if (PreviousState != EPlumPoleTraversalState::Perched)
        {
            Clip.BlendTime = GroundBlendTime;
            Clip.BlendOption = EAlphaBlendOption::CircularIn;
        }
        else if (bShortExit)
        {
            Clip.Sequence = bRightSupport ? ShortExitFromRight : ShortExitFromLeft;
            Clip.BlendTime = TraversalBlendTime;
            Clip.BlendOption = EAlphaBlendOption::CircularIn;
            Clip.TimeCurve = EPlumPoleTimeCurve::Linear;
        }
        else if (!bRightSupport && ExitPoleLeft)
        {
            Clip.Sequence = ExitPoleLeft;
            Clip.Section = ExitSection;
            Clip.BlendTime = TraversalBlendTime;
            Clip.BlendOption = EAlphaBlendOption::CircularIn;
            Clip.TimeCurve = EPlumPoleTimeCurve::NearHop;
        }
        else
        {
            Clip.Sequence = ExitPole;
            Clip.Section = ExitSection;
            Clip.BlendTime = TraversalBlendTime;
            Clip.BlendOption = EAlphaBlendOption::CircularIn;
            Clip.TimeCurve = EPlumPoleTimeCurve::NearHop;
            Clip.MirrorTable = bRightSupport ? nullptr : ExitMirrorTable.Get();
        }
        break;
    }
    return Clip;
}
