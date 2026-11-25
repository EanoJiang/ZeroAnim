#include "PlumPoleCharacter.h"

#include "Animation/AnimSequence.h"
#include "Animation/MirrorDataTable.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "PlumPoleActor.h"
#include "PlumPoleAnimInstance.h"
#include "PlumPoleInteractPromptWidget.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
void GetPBZBlendOut(EPlumPoleTimeline Timeline, float& OutBlendTime, EAlphaBlendOption& OutBlendOption)
{
    switch (Timeline)
    {
    case EPlumPoleTimeline::JumpToPlatform_L:
    case EPlumPoleTimeline::JumpToPlatform_R:
        OutBlendTime = 0.30f;
        OutBlendOption = EAlphaBlendOption::CircularIn;
        break;
    case EPlumPoleTimeline::Dash:
    case EPlumPoleTimeline::Jump_L:
    case EPlumPoleTimeline::Jump_R:
    case EPlumPoleTimeline::JumpOff:
    case EPlumPoleTimeline::Idle_L:
    case EPlumPoleTimeline::Idle_R:
    default:
        OutBlendTime = 0.25f;
        OutBlendOption = EAlphaBlendOption::HermiteCubic;
        break;
    }
}
} // namespace

APlumPoleCharacter::APlumPoleCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    PlumPoleTraversal = CreateDefaultSubobject<UPlumPoleTraversalComponent>(TEXT("PlumPoleTraversal"));

    InteractPromptComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("InteractPromptComponent"));
    InteractPromptComponent->SetupAttachment(RootComponent);
    InteractPromptComponent->SetWidgetSpace(EWidgetSpace::Screen);
    InteractPromptComponent->SetDrawAtDesiredSize(true);
    InteractPromptComponent->SetDrawSize(FVector2D(100.0f, 100.0f));
    InteractPromptComponent->SetPivot(FVector2D(0.5f, 0.5f));
    InteractPromptComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    InteractPromptComponent->SetCastShadow(false);
    InteractPromptComponent->bAffectDistanceFieldLighting = false;
    InteractPromptComponent->SetUsingAbsoluteLocation(true);
    InteractPromptComponent->SetUsingAbsoluteRotation(true);
    InteractPromptComponent->SetUsingAbsoluteScale(true);
    InteractPromptComponent->SetVisibility(false);
    InteractPromptWidgetClass = UPlumPoleInteractPromptWidget::StaticClass();

    static ConstructorHelpers::FObjectFinder<UInputAction> MoveAction(TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
    if (MoveAction.Succeeded())
    {
        PoleMoveInputAction = MoveAction.Object;
    }

    static ConstructorHelpers::FObjectFinder<UAnimSequence> DashClip(
        TEXT("/Game/PlumPole/Animations/SourceTiming/AS_Manny_Hun_LionHead_Dodge_F_R.AS_Manny_Hun_LionHead_Dodge_F_R"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> JumpLClip(
        TEXT("/Game/PlumPole/Animations/AS_Manny_PlumPole_HopFromLeft.AS_Manny_PlumPole_HopFromLeft"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> JumpRClip(
        TEXT("/Game/PlumPole/Animations/AS_Manny_PlumPole_HopFromRight.AS_Manny_PlumPole_HopFromRight"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> IdleLClip(
        TEXT("/Game/PlumPole/Animations/AS_Manny_PlumPole_PerchLeft.AS_Manny_PlumPole_PerchLeft"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> IdleRClip(
        TEXT("/Game/PlumPole/Animations/AS_Manny_PlumPole_PerchRight.AS_Manny_PlumPole_PerchRight"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> JumpToPlatformLClip(
        TEXT("/Game/PlumPole/Animations/SourceTiming/AS_Manny_Hun_LionHead_JumpOut_L_02.AS_Manny_Hun_LionHead_JumpOut_L_02"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> JumpToPlatformRClip(
        TEXT("/Game/PlumPole/Animations/SourceTiming/AS_Manny_Hun_LionHead_JumpOut_R_02.AS_Manny_Hun_LionHead_JumpOut_R_02"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> JumpOffClip(
        TEXT("/Game/PlumPole/Animations/SourceTiming/AS_Manny_Hun_LionHead_JumpOut_R.AS_Manny_Hun_LionHead_JumpOut_R"));

    DashSequence = DashClip.Object;
    JumpLSequence = JumpLClip.Object;
    JumpRSequence = JumpRClip.Object;
    IdleLSequence = IdleLClip.Object;
    IdleRSequence = IdleRClip.Object;
    JumpToPlatformLSequence = JumpToPlatformLClip.Object;
    JumpToPlatformRSequence = JumpToPlatformRClip.Object;
    JumpOffSequence = JumpOffClip.Object;

    // Keep legacy properties synced to matching assets
    EnterPole = DashClip.Object;
    PerchLeft = IdleLClip.Object;
    PerchRight = IdleRClip.Object;
    HopFromLeft = JumpLClip.Object;
    HopFromRight = JumpRClip.Object;
    ChainHopFromLeft = JumpLClip.Object;
    ChainHopFromRight = JumpRClip.Object;
    ExitPole = JumpToPlatformRClip.Object;
    ExitPoleLeft = JumpToPlatformLClip.Object;
}

bool APlumPoleCharacter::IsPoleAbilityActive() const
{
    return PlumPoleTraversal && PlumPoleTraversal->IsPoleAbilityActive();
}

UAnimSequence* APlumPoleCharacter::GetSequenceForTimeline(EPlumPoleTimeline Timeline) const
{
    switch (Timeline)
    {
    case EPlumPoleTimeline::Dash:
        return DashSequence ? DashSequence.Get() : EnterPole.Get();
    case EPlumPoleTimeline::Jump_L:
        return JumpLSequence ? JumpLSequence.Get() : HopFromLeft.Get();
    case EPlumPoleTimeline::Jump_R:
        return JumpRSequence ? JumpRSequence.Get() : HopFromRight.Get();
    case EPlumPoleTimeline::Idle_L:
        return IdleLSequence ? IdleLSequence.Get() : PerchLeft.Get();
    case EPlumPoleTimeline::Idle_R:
        return IdleRSequence ? IdleRSequence.Get() : PerchRight.Get();
    case EPlumPoleTimeline::JumpToPlatform_L:
        return JumpToPlatformLSequence ? JumpToPlatformLSequence.Get() : ExitPoleLeft.Get();
    case EPlumPoleTimeline::JumpToPlatform_R:
        return JumpToPlatformRSequence ? JumpToPlatformRSequence.Get() : ExitPole.Get();
    case EPlumPoleTimeline::JumpOff:
        return JumpOffSequence ? JumpOffSequence.Get() : ExitPole.Get();
    case EPlumPoleTimeline::None:
    default:
        return nullptr;
    }
}

FPlumPolePlayback APlumPoleCharacter::ChooseTimelinePlayback_Implementation(
    EPlumPoleTimeline Timeline, float StartTimeSeconds) const
{
    FPlumPolePlayback Clip;
    Clip.Sequence = GetSequenceForTimeline(Timeline);
    Clip.MirrorTable = nullptr;

    switch (Timeline)
    {
    case EPlumPoleTimeline::Dash:
        Clip.bLoop = false;
        Clip.BlendTime = 0.25f;
        Clip.BlendOption = EAlphaBlendOption::HermiteCubic;
        Clip.TimeCurve = EPlumPoleTimeCurve::ForwardFlip;
        break;
    case EPlumPoleTimeline::Jump_L:
    case EPlumPoleTimeline::Jump_R:
        Clip.bLoop = false;
        Clip.BlendTime = 0.15f;
        Clip.BlendOption = EAlphaBlendOption::CircularIn;
        Clip.TimeCurve = EPlumPoleTimeCurve::NearHop;
        break;
    case EPlumPoleTimeline::Idle_L:
    case EPlumPoleTimeline::Idle_R:
        Clip.bLoop = true;
        Clip.BlendTime = 0.50f;
        Clip.BlendOption = EAlphaBlendOption::HermiteCubic;
        Clip.TimeCurve = EPlumPoleTimeCurve::Linear;
        break;
    case EPlumPoleTimeline::JumpToPlatform_L:
    case EPlumPoleTimeline::JumpToPlatform_R:
        Clip.bLoop = false;
        Clip.BlendTime = 0.15f;
        Clip.BlendOption = EAlphaBlendOption::CircularIn;
        Clip.TimeCurve = EPlumPoleTimeCurve::NearHop;
        break;
    case EPlumPoleTimeline::JumpOff:
        Clip.bLoop = false;
        Clip.BlendTime = 0.25f;
        Clip.BlendOption = EAlphaBlendOption::HermiteCubic;
        Clip.TimeCurve = EPlumPoleTimeCurve::JumpOff;
        break;
    case EPlumPoleTimeline::None:
    default:
        Clip.Sequence = nullptr;
        Clip.bLoop = false;
        Clip.BlendTime = 0.30f;
        Clip.BlendOption = EAlphaBlendOption::CircularIn;
        Clip.TimeCurve = EPlumPoleTimeCurve::Linear;
        break;
    }
    return Clip;
}

float APlumPoleCharacter::ScorePoleCandidate_Implementation(float DirectionAlignment, float HorizontalDistance,
    float HeightDelta, float SearchRadius) const
{
    return HorizontalDistance;
}

EPlumPoleInputResponse APlumPoleCharacter::SelectPoleInputResponse_Implementation(EPlumPoleTraversalState State,
    bool bFlipRequested, bool bCanChainFromGround) const
{
    switch (State)
    {
    case EPlumPoleTraversalState::Grounded:
        return bFlipRequested ? EPlumPoleInputResponse::Ignore : EPlumPoleInputResponse::Enter;
    case EPlumPoleTraversalState::Perched:
        return bFlipRequested ? EPlumPoleInputResponse::PoleFlip : EPlumPoleInputResponse::Consume;
    case EPlumPoleTraversalState::Entering:
    case EPlumPoleTraversalState::Hopping:
        return bFlipRequested ? EPlumPoleInputResponse::BufferRequest : EPlumPoleInputResponse::Consume;
    case EPlumPoleTraversalState::Dismounting:
        return bFlipRequested ? EPlumPoleInputResponse::Ignore : EPlumPoleInputResponse::Consume;
    }
    return EPlumPoleInputResponse::Ignore;
}

void APlumPoleCharacter::BeginPlay()
{
    Super::BeginPlay();

    GetMesh()->AddTickPrerequisiteComponent(PlumPoleTraversal);
    PlumPoleTraversal->OnTimelineChanged.AddDynamic(this, &APlumPoleCharacter::OnTimelineChanged);
    PlumPoleTraversal->OnCameraAdditiveChanged.AddDynamic(this, &APlumPoleCharacter::OnCameraAdditiveChanged);

    ResolveActiveCameraComponents();

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (PC->IsLocalPlayerController())
        {
            RegisterInputDeviceDetector();
        }
    }

    if (InteractPromptComponent)
    {
        InteractPromptComponent->SetWidgetSpace(EWidgetSpace::Screen);
        InteractPromptComponent->SetDrawAtDesiredSize(true);
        InteractPromptComponent->SetDrawSize(FVector2D(100.0f, 100.0f));
        InteractPromptComponent->SetPivot(FVector2D(0.5f, 0.5f));
        if (APlayerController* PC = Cast<APlayerController>(GetController()))
        {
            if (ULocalPlayer* LP = PC->GetLocalPlayer())
            {
                InteractPromptComponent->SetOwnerPlayer(LP);
            }
        }
        UClass* WidgetCls = InteractPromptWidgetClass
            ? InteractPromptWidgetClass.Get()
            : UPlumPoleInteractPromptWidget::StaticClass();
        InteractPromptComponent->SetWidgetClass(WidgetCls);
        InteractPromptComponent->InitWidget();
        if (UPlumPoleInteractPromptWidget* Prompt = Cast<UPlumPoleInteractPromptWidget>(InteractPromptComponent->GetUserWidgetObject()))
        {
            Prompt->SetUsingGamepad(bUsingGamepad);
            Prompt->SetGamepadIconStyle(InteractPromptGamepadStyle);
            Prompt->SetInCombat(bInteractPromptInCombat);
            Prompt->SetPromptAlpha(1.0f);
            Prompt->ClearWidget();
        }
        CurrentPromptUIState = EPlumPoleInteractUIState::None;
        PromptAlpha = 0.0f;
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
    if (UPlumPoleInteractPromptWidget* Prompt = GetInteractPromptWidget())
    {
        Prompt->SetUsingGamepad(bUsingGamepad);
    }
}

UPlumPoleInteractPromptWidget* APlumPoleCharacter::GetInteractPromptWidget() const
{
    return InteractPromptComponent
        ? Cast<UPlumPoleInteractPromptWidget>(InteractPromptComponent->GetUserWidgetObject())
        : nullptr;
}

void APlumPoleCharacter::SetInteractPromptInCombat(bool bInCombat)
{
    bInteractPromptInCombat = bInCombat;
    if (UPlumPoleInteractPromptWidget* Prompt = GetInteractPromptWidget())
    {
        Prompt->SetInCombat(bInCombat);
    }
}

void APlumPoleCharacter::SetInteractPromptGamepadStyle(EPlumPoleGamepadIconStyle InStyle)
{
    InteractPromptGamepadStyle = InStyle;
    if (UPlumPoleInteractPromptWidget* Prompt = GetInteractPromptWidget())
    {
        Prompt->SetGamepadIconStyle(InStyle);
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

void APlumPoleCharacter::ReconcileCameraSpringArmOwner(USpringArmComponent* ActiveArm)
{
    if (AppliedCameraSpringArm.Get() == ActiveArm)
    {
        return;
    }

    if (USpringArmComponent* OldArm = AppliedCameraSpringArm.Get())
    {
        if (IsValid(OldArm))
        {
            OldArm->TargetArmLength -= AppliedAdditiveArmLength;
            OldArm->SocketOffset -= AppliedAdditiveSocketOffset;
        }
    }

    AppliedAdditiveArmLength = 0.0f;
    AppliedAdditiveSocketOffset = FVector::ZeroVector;
    AppliedCameraSpringArm = ActiveArm;

    if (bCameraBlending)
    {
        CameraBlendStartArm = 0.0f;
        CameraBlendStartSocket = FVector::ZeroVector;
        if (FMath::IsNearlyZero(CameraBlendTargetArm) && CameraBlendTargetSocket.IsNearlyZero())
        {
            bCameraBlending = false;
        }
    }
}

void APlumPoleCharacter::RestoreAppliedCameraAdditive()
{
    if (USpringArmComponent* OldArm = AppliedCameraSpringArm.Get())
    {
        if (IsValid(OldArm))
        {
            OldArm->TargetArmLength -= AppliedAdditiveArmLength;
            OldArm->SocketOffset -= AppliedAdditiveSocketOffset;
        }
    }
    AppliedAdditiveArmLength = 0.0f;
    AppliedAdditiveSocketOffset = FVector::ZeroVector;
    AppliedCameraSpringArm.Reset();
    bCameraBlending = false;
}

void APlumPoleCharacter::OnCameraAdditiveChanged(bool bEnablePoleCamera)
{
    if (!bUsePlumPoleCameraBlend)
    {
        return;
    }

    if (!IsValid(CachedCameraBoom) || !CachedCameraBoom->IsRegistered()
        || CachedCameraBoom->GetAttachParent() == nullptr || !CachedCameraBoom->IsActive())
    {
        ResolveActiveCameraComponents();
    }
    ReconcileCameraSpringArmOwner(CachedCameraBoom.Get());

    CameraBlendStartArm = AppliedAdditiveArmLength;
    CameraBlendTargetArm = bEnablePoleCamera ? PoleAdditiveArmLength : 0.0f;
    CameraBlendStartSocket = AppliedAdditiveSocketOffset;
    CameraBlendTargetSocket = bEnablePoleCamera ? PoleAdditiveSocketOffset : FVector::ZeroVector;
    CameraBlendElapsed = 0.0f;
    bCameraBlending = !(FMath::IsNearlyEqual(CameraBlendStartArm, CameraBlendTargetArm)
        && CameraBlendStartSocket.Equals(CameraBlendTargetSocket, KINDA_SMALL_NUMBER));
}

void APlumPoleCharacter::UpdatePlumPoleCamera(float DeltaSeconds)
{
    if (!bUsePlumPoleCameraBlend)
    {
        return;
    }

    const bool bNeedResolve = !IsValid(CachedCameraBoom) || !CachedCameraBoom->IsRegistered()
        || CachedCameraBoom->GetAttachParent() == nullptr || !CachedCameraBoom->IsActive();
    if (bNeedResolve)
    {
        ResolveActiveCameraComponents();
    }

    if (AppliedCameraSpringArm.Get() != CachedCameraBoom.Get())
    {
        const bool bDesiredPoleCamera = PlumPoleTraversal && PlumPoleTraversal->IsCameraAdditiveDesired();
        ReconcileCameraSpringArmOwner(CachedCameraBoom.Get());
        if (CachedCameraBoom && bDesiredPoleCamera)
        {
            CameraBlendStartArm = 0.0f;
            CameraBlendTargetArm = PoleAdditiveArmLength;
            CameraBlendStartSocket = FVector::ZeroVector;
            CameraBlendTargetSocket = PoleAdditiveSocketOffset;
            CameraBlendElapsed = 0.0f;
            bCameraBlending = true;
        }
    }

    if (!bCameraBlending || !CachedCameraBoom)
    {
        return;
    }

    // Exact finite-duration 0.5 s PBZ additive blend for StandingOnPoleDistance (+300 cm)
    // and StandingOnPoleSocketOffset (0, +100, 0) on top of the character's normal ground camera.
    const float Duration = FMath::Max(PoleCameraBlendDuration, KINDA_SMALL_NUMBER);
    CameraBlendElapsed = FMath::Min(CameraBlendElapsed + DeltaSeconds, Duration);
    const float RawAlpha = FMath::Clamp(CameraBlendElapsed / Duration, 0.0f, 1.0f);
    const float SmoothAlpha = FMath::SmoothStep(0.0f, 1.0f, RawAlpha);

    const float NextAdditiveArm = FMath::Lerp(CameraBlendStartArm, CameraBlendTargetArm, SmoothAlpha);
    const FVector NextAdditiveSocket = FMath::Lerp(CameraBlendStartSocket, CameraBlendTargetSocket, SmoothAlpha);

    AppliedCameraSpringArm = CachedCameraBoom;
    CachedCameraBoom->TargetArmLength += (NextAdditiveArm - AppliedAdditiveArmLength);
    CachedCameraBoom->SocketOffset += (NextAdditiveSocket - AppliedAdditiveSocketOffset);
    AppliedAdditiveArmLength = NextAdditiveArm;
    AppliedAdditiveSocketOffset = NextAdditiveSocket;

    if (CameraBlendElapsed >= Duration)
    {
        bCameraBlending = false;
    }
}

void APlumPoleCharacter::UpdateInteractPrompt(float DeltaSeconds)
{
    if (!InteractPromptComponent)
    {
        return;
    }

    const APlayerController* PC = Cast<APlayerController>(GetController());
    if (PC && !InteractPromptComponent->GetOwnerPlayer())
    {
        if (ULocalPlayer* LP = PC->GetLocalPlayer())
        {
            InteractPromptComponent->SetOwnerPlayer(LP);
        }
    }

    UPlumPoleInteractPromptWidget* Prompt = GetInteractPromptWidget();
    if (!Prompt)
    {
        InteractPromptComponent->InitWidget();
        Prompt = GetInteractPromptWidget();
        if (!Prompt)
        {
            return;
        }
        Prompt->SetUsingGamepad(bUsingGamepad);
        Prompt->SetGamepadIconStyle(InteractPromptGamepadStyle);
        Prompt->SetInCombat(bInteractPromptInCombat);
        Prompt->SetPromptAlpha(1.0f);
        Prompt->ClearWidget();
    }

    Prompt->SetUsingGamepad(bUsingGamepad);

    // Two-zone PBZ visibility while Grounded:
    //   1) Exact PBZ ground entry (<= 500 cm, 90 deg cone) -> InputAction
    //   2) Outer awareness zone (<= InteractPromptVirtualRadius) -> Virtual (hollow white ring)
    //   3) Otherwise or non-Grounded -> None (smooth fade out, then hide component)
    APlumPoleActor* SelectedPole = nullptr;
    EPlumPoleInteractUIState DesiredState = EPlumPoleInteractUIState::None;

    const bool bCanShowPrompt = bShowInteractPrompt
        && PlumPoleTraversal
        && PlumPoleTraversal->GetTraversalState() == EPlumPoleTraversalState::Grounded
        && !PlumPoleTraversal->IsPoleAbilityActive();

    if (bCanShowPrompt)
    {
        if (APlumPoleActor* EnterablePole = PlumPoleTraversal->FindEnterablePole())
        {
            SelectedPole = EnterablePole;
            DesiredState = EPlumPoleInteractUIState::InputAction;
        }
        else if (InteractPromptVirtualRadius > KINDA_SMALL_NUMBER)
        {
            FVector ViewForward = GetActorForwardVector();
            if (PC)
            {
                if (PC->PlayerCameraManager)
                {
                    ViewForward = PC->PlayerCameraManager->GetCameraRotation().Vector();
                }
                else
                {
                    ViewForward = PC->GetControlRotation().Vector();
                }
            }
            else if (CachedFollowCamera)
            {
                ViewForward = CachedFollowCamera->GetForwardVector();
            }

            if (APlumPoleActor* VirtualPole = PlumPoleTraversal->FindVirtualAwarenessPole(
                    InteractPromptVirtualRadius, InteractPromptVirtualViewAngle, ViewForward))
            {
                SelectedPole = VirtualPole;
                DesiredState = EPlumPoleInteractUIState::Virtual;
            }
        }
    }

    if (SelectedPole)
    {
        PromptTargetPole = SelectedPole;
        const FVector AnchorWorldLoc = SelectedPole->GetLandingLocation() + FVector::UpVector * PolePromptCrownOffsetZ;
        FVector DisplayWorldLoc = AnchorWorldLoc;
        bool bUseScreenEdge = false;

        if (bEnableOffscreenPromptArrow && PC)
        {
            int32 ViewW = 0;
            int32 ViewH = 0;
            PC->GetViewportSize(ViewW, ViewH);
            if (ViewW > 0 && ViewH > 0)
            {
                FVector CamLoc = FVector::ZeroVector;
                FRotator CamRot = FRotator::ZeroRotator;
                PC->GetPlayerViewPoint(CamLoc, CamRot);
                const FVector ToTarget = AnchorWorldLoc - CamLoc;
                const bool bInFront = FVector::DotProduct(CamRot.Vector(), ToTarget) > 0.0;

                FVector2D ScreenPos = FVector2D::ZeroVector;
                const bool bProjected = bInFront
                    && PC->ProjectWorldLocationToScreen(AnchorWorldLoc, ScreenPos, /*bPlayerViewportRelative=*/false);

                const bool bWasScreenState = (CurrentPromptUIState == EPlumPoleInteractUIState::VirtualScreen
                    || CurrentPromptUIState == EPlumPoleInteractUIState::InputActionScreen);
                const float DPIScale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), 0.5f);
                const float Margin = FMath::Max(InteractPromptScreenMargin, 55.0f) * DPIScale;
                const float Hysteresis = bWasScreenState ? (16.0f * DPIScale) : 0.0f;
                const float SafeMinX = FMath::Min(Margin + Hysteresis, static_cast<float>(ViewW) * 0.45f);
                const float SafeMaxX = static_cast<float>(ViewW) - SafeMinX;
                const float SafeMinY = FMath::Min(Margin + Hysteresis, static_cast<float>(ViewH) * 0.45f);
                const float SafeMaxY = static_cast<float>(ViewH) - SafeMinY;

                const bool bInsideSafeBox = bProjected
                    && ScreenPos.X >= SafeMinX && ScreenPos.X <= SafeMaxX
                    && ScreenPos.Y >= SafeMinY && ScreenPos.Y <= SafeMaxY;

                if (!bInsideSafeBox)
                {
                    const FVector2D ViewCenter(ViewW * 0.5, ViewH * 0.5);
                    FVector2D Dir2D = FVector2D::ZeroVector;
                    if (bProjected)
                    {
                        Dir2D = (ScreenPos - ViewCenter).GetSafeNormal();
                    }
                    else
                    {
                        const FRotationMatrix CamMat(CamRot);
                        const double RightDot = FVector::DotProduct(CamMat.GetUnitAxis(EAxis::Y), ToTarget);
                        const double UpDot = FVector::DotProduct(CamMat.GetUnitAxis(EAxis::Z), ToTarget);
                        Dir2D = FVector2D(RightDot, -UpDot).GetSafeNormal();
                    }

                    if (Dir2D.IsNearlyZero())
                    {
                        Dir2D = !LastValidScreenEdgeDir.IsNearlyZero()
                            ? LastValidScreenEdgeDir.GetSafeNormal()
                            : FVector2D(0.0, 1.0);
                    }
                    else
                    {
                        LastValidScreenEdgeDir = Dir2D;
                    }

                    const double ClampedMarginX = FMath::Min(static_cast<double>(Margin), ViewW * 0.45);
                    const double ClampedMarginY = FMath::Min(static_cast<double>(Margin), ViewH * 0.45);
                    const double HalfW = FMath::Max(ViewW * 0.5 - ClampedMarginX, 1.0);
                    const double HalfH = FMath::Max(ViewH * 0.5 - ClampedMarginY, 1.0);
                    const double ScaleX = FMath::Abs(Dir2D.X) > KINDA_SMALL_NUMBER ? HalfW / FMath::Abs(Dir2D.X) : BIG_NUMBER;
                    const double ScaleY = FMath::Abs(Dir2D.Y) > KINDA_SMALL_NUMBER ? HalfH / FMath::Abs(Dir2D.Y) : BIG_NUMBER;
                    const FVector2D ClampedScreen = ViewCenter + Dir2D * FMath::Min(ScaleX, ScaleY);

                    FVector RayOrigin = FVector::ZeroVector;
                    FVector RayDir = FVector::ForwardVector;
                    if (PC->DeprojectScreenPositionToWorld(
                            static_cast<float>(ClampedScreen.X),
                            static_cast<float>(ClampedScreen.Y),
                            RayOrigin,
                            RayDir))
                    {
                        DisplayWorldLoc = RayOrigin + RayDir * 200.0;
                        const float AngleDeg = FMath::RadiansToDegrees(
                            FMath::Atan2(static_cast<float>(Dir2D.Y), static_cast<float>(Dir2D.X)));
                        Prompt->SetScreenAngle(AngleDeg);
                        bUseScreenEdge = true;
                    }
                }
            }
        }

        if (bUseScreenEdge)
        {
            DesiredState = (DesiredState == EPlumPoleInteractUIState::InputAction)
                ? EPlumPoleInteractUIState::InputActionScreen
                : EPlumPoleInteractUIState::VirtualScreen;
        }

        LastPromptDisplayWorldLocation = DisplayWorldLoc;
        bLastPromptWasScreenEdge = bUseScreenEdge;
        InteractPromptComponent->SetWorldLocation(DisplayWorldLoc);
    }
    else if (PromptTargetPole.IsValid())
    {
        if (!bLastPromptWasScreenEdge)
        {
            LastPromptDisplayWorldLocation = PromptTargetPole->GetLandingLocation() + FVector::UpVector * PolePromptCrownOffsetZ;
        }
        InteractPromptComponent->SetWorldLocation(LastPromptDisplayWorldLocation);
    }

    if (DesiredState != EPlumPoleInteractUIState::None && !InteractPromptComponent->IsVisible())
    {
        InteractPromptComponent->SetVisibility(true);
    }

    CurrentPromptUIState = DesiredState;
    Prompt->TransitionToState(DesiredState);
    Prompt->AdvanceAnimation(DeltaSeconds);
    PromptAlpha = Prompt->GetVisibleAlpha();

    if (Prompt->IsVisuallyHidden())
    {
        if (InteractPromptComponent->IsVisible())
        {
            InteractPromptComponent->SetVisibility(false);
        }
        if (DesiredState == EPlumPoleInteractUIState::None)
        {
            PromptTargetPole.Reset();
            bLastPromptWasScreenEdge = false;
        }
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
    if (PlumPoleTraversal)
    {
        Frame.bPoleAbilityActive = PlumPoleTraversal->IsPoleAbilityActive();
        Frame.bUseSourceTimeClock = Frame.bPoleAbilityActive && PlumPoleTraversal->GetActiveTimeline() != EPlumPoleTimeline::None;
        Frame.SourceTimeSeconds = PlumPoleTraversal->GetActiveAnimTime();
        Frame.ActiveRate = PlumPoleTraversal->GetActiveRate();
        Frame.Progress = PlumPoleTraversal->GetTraversalProgress();
        Frame.Duration = PlumPoleTraversal->GetTraversalDuration();
    }
    return Frame;
}

void APlumPoleCharacter::OnTimelineChanged(
    EPlumPoleTimeline PreviousTimeline,
    EPlumPoleTimeline NewTimeline,
    float StartTimeSeconds,
    float PreviousAnimTimeSeconds,
    float OutgoingRate,
    bool bNaturalAutoBlendOut)
{
    ActivePoleAnimation = ChooseTimelinePlayback(NewTimeline, StartTimeSeconds);

    float BlendInTime = ActivePoleAnimation.BlendTime;
    EAlphaBlendOption BlendInOption = ActivePoleAnimation.BlendOption;
    float OutgoingBlendTime = BlendInTime;
    EAlphaBlendOption OutgoingBlendOption = BlendInOption;

    if (NewTimeline == EPlumPoleTimeline::None)
    {
        // Returning to ground pose: use the leaving timeline's exact PBZ BlendOut time & curve!
        GetPBZBlendOut(PreviousTimeline, OutgoingBlendTime, OutgoingBlendOption);
        BlendInTime = OutgoingBlendTime;
        BlendInOption = OutgoingBlendOption;
    }

    const bool bForceDropOutgoingRate = (NewTimeline == EPlumPoleTimeline::None && !bNaturalAutoBlendOut);
    const float SubFrameRemainingDelta = PlumPoleTraversal ? PlumPoleTraversal->GetPendingTransitionRemainingDelta() : -1.0f;

    if (UPlumPoleAnimInstance* Player = Cast<UPlumPoleAnimInstance>(GetMesh()->GetAnimInstance()))
    {
        Player->SetTimelineClip(
            ActivePoleAnimation.Sequence,
            ActivePoleAnimation.bLoop,
            StartTimeSeconds,
            BlendInTime,
            BlendInOption,
            OutgoingBlendTime,
            OutgoingBlendOption,
            PreviousAnimTimeSeconds,
            OutgoingRate,
            bForceDropOutgoingRate,
            ActivePoleAnimation.MirrorTable,
            SubFrameRemainingDelta);
    }
}

FPlumPolePlayback APlumPoleCharacter::SelectPoleAnimation_Implementation(
    EPlumPoleTraversalState State, EPlumPoleTraversalState PreviousState, bool bRightSupport, bool bPoleFlip,
    bool bShortExit, bool bChainedHop) const
{
    switch (State)
    {
    case EPlumPoleTraversalState::Entering:
        return ChooseTimelinePlayback(EPlumPoleTimeline::Dash, 0.0f);
    case EPlumPoleTraversalState::Perched:
        return ChooseTimelinePlayback(bRightSupport ? EPlumPoleTimeline::Idle_R : EPlumPoleTimeline::Idle_L, 0.0f);
    case EPlumPoleTraversalState::Hopping:
        if (bPoleFlip)
        {
            return ChooseTimelinePlayback(EPlumPoleTimeline::Dash, bChainedHop ? 0.20f : 0.0f);
        }
        return ChooseTimelinePlayback(
            bRightSupport ? EPlumPoleTimeline::Jump_R : EPlumPoleTimeline::Jump_L,
            bChainedHop ? 0.32f : 0.20f);
    case EPlumPoleTraversalState::Dismounting:
        return ChooseTimelinePlayback(
            bRightSupport ? EPlumPoleTimeline::JumpToPlatform_R : EPlumPoleTimeline::JumpToPlatform_L, 0.20f);
    case EPlumPoleTraversalState::Grounded:
    default:
        return ChooseTimelinePlayback(EPlumPoleTimeline::None, 0.0f);
    }
}
