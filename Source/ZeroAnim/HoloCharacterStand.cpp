#include "HoloCharacterStand.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "HoloSwitchPromptWidget.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AHoloCharacterStand::AHoloCharacterStand()
{
    PrimaryActorTick.bCanEverTick = true;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    PreviewPivot = CreateDefaultSubobject<USceneComponent>(TEXT("PreviewPivot"));
    PreviewPivot->SetupAttachment(SceneRoot);
    PreviewPivot->SetRelativeLocation(FVector(0.0f, 0.0f, HologramHoverHeight));

    GlowPadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GlowPadMesh"));
    GlowPadMesh->SetupAttachment(SceneRoot);
    GlowPadMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 0.4f));
    GlowPadMesh->SetRelativeScale3D(FVector(1.15f, 1.15f, 1.0f));
    GlowPadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GlowPadMesh->SetCastShadow(false);

    // Inverted translucent volumetric projection light cone (LightCutter): tip at floor emitter orb, expanding up to hovering feet
    LightConeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LightConeMesh"));
    LightConeMesh->SetupAttachment(SceneRoot);
    LightConeMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 30.0f));
    LightConeMesh->SetRelativeRotation(FRotator(180.0f, 0.0f, 0.0f));
    LightConeMesh->SetRelativeScale3D(FVector(1.55f, 1.55f, 0.50f));
    LightConeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    LightConeMesh->SetCastShadow(false);

    // Dual-shell glowing cyan emitter energy orb (InsideSphere + OuterSphere)
    EmitterInnerSphere = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EmitterInnerSphere"));
    EmitterInnerSphere->SetupAttachment(SceneRoot);
    EmitterInnerSphere->SetRelativeLocation(FVector(0.0f, 0.0f, 10.0f));
    EmitterInnerSphere->SetRelativeScale3D(FVector(0.16f, 0.16f, 0.16f));
    EmitterInnerSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    EmitterInnerSphere->SetCastShadow(false);

    EmitterOuterSphere = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EmitterOuterSphere"));
    EmitterOuterSphere->SetupAttachment(SceneRoot);
    EmitterOuterSphere->SetRelativeLocation(FVector(0.0f, 0.0f, 10.0f));
    EmitterOuterSphere->SetRelativeScale3D(FVector(0.24f, 0.24f, 0.24f));
    EmitterOuterSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    EmitterOuterSphere->SetCastShadow(false);

    HoloPointLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("HoloPointLight"));
    HoloPointLight->SetupAttachment(SceneRoot);
    HoloPointLight->SetRelativeLocation(FVector(0.0f, 0.0f, 65.0f));
    HoloPointLight->SetIntensity(2200.0f);
    HoloPointLight->SetAttenuationRadius(300.0f);
    HoloPointLight->SetLightColor(FLinearColor(0.02f, 0.58f, 1.0f));
    HoloPointLight->SetCastShadows(false);

    PromptWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("PromptWidgetComponent"));
    PromptWidgetComponent->SetupAttachment(SceneRoot);
    PromptWidgetComponent->SetRelativeLocation(FVector(0.0f, 0.0f, PromptHeightZ));
    PromptWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
    PromptWidgetComponent->SetDrawAtDesiredSize(true);
    PromptWidgetComponent->SetPivot(FVector2D(0.5f, 0.5f));
    PromptWidgetComponent->SetWidgetClass(UHoloSwitchPromptWidget::StaticClass());
    PromptWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PromptWidgetComponent->SetVisibility(false);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
    if (PlaneFinder.Succeeded())
    {
        GlowPadMesh->SetStaticMesh(PlaneFinder.Object);
    }

    static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
    if (ConeFinder.Succeeded())
    {
        LightConeMesh->SetStaticMesh(ConeFinder.Object);
    }

    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (SphereFinder.Succeeded())
    {
        EmitterInnerSphere->SetStaticMesh(SphereFinder.Object);
        EmitterOuterSphere->SetStaticMesh(SphereFinder.Object);
    }

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> HoloMatFinder(
        TEXT("/Game/CharacterShowcase/Materials/M_HoloCharacter.M_HoloCharacter"));
    if (HoloMatFinder.Succeeded())
    {
        HologramMaterial = HoloMatFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> PadMatFinder(
        TEXT("/Game/CharacterShowcase/Materials/M_HoloPad.M_HoloPad"));
    if (PadMatFinder.Succeeded())
    {
        PadMaterial = PadMatFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> ConeMatFinder(
        TEXT("/Game/CharacterShowcase/Materials/M_HoloLightCone.M_HoloLightCone"));
    if (ConeMatFinder.Succeeded())
    {
        LightConeMaterial = ConeMatFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> OrbMatFinder(
        TEXT("/Game/CharacterShowcase/Materials/M_HoloEmitterOrb.M_HoloEmitterOrb"));
    if (OrbMatFinder.Succeeded())
    {
        EmitterOrbMaterial = OrbMatFinder.Object;
    }

    CharacterDisplayName = FText::FromString(TEXT("角色"));
}

void AHoloCharacterStand::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if (PreviewPivot)
    {
        PreviewPivot->SetRelativeLocation(FVector(0.0f, 0.0f, HologramHoverHeight));
    }
    if (PromptWidgetComponent)
    {
        PromptWidgetComponent->SetRelativeLocation(FVector(0.0f, 0.0f, PromptHeightZ));
    }
    RebuildPreviewMeshes();
    ApplyHologramMaterials();
}

void AHoloCharacterStand::BeginPlay()
{
    Super::BeginPlay();
    if (SpawnedPreviewMeshes.IsEmpty() && !PreviewParts.IsEmpty())
    {
        RebuildPreviewMeshes();
    }
    ApplyHologramMaterials();
    EnsurePromptWidget();
}

void AHoloCharacterStand::RebuildPreviewMeshes()
{
    for (USkeletalMeshComponent* Comp : SpawnedPreviewMeshes)
    {
        if (IsValid(Comp))
        {
            Comp->DestroyComponent();
        }
    }
    SpawnedPreviewMeshes.Reset();

    USkeletalMeshComponent* PoseDriverComp = nullptr;
    USkeletalMeshComponent* LeaderComp = nullptr;
    for (int32 i = 0; i < PreviewParts.Num(); ++i)
    {
        const FHoloMeshPart& Part = PreviewParts[i];
        if (!Part.Mesh)
        {
            continue;
        }

        const FName CompName(*FString::Printf(TEXT("HoloPreviewMesh_%d"), i));
        USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(this, CompName);
        if (!SkelComp)
        {
            continue;
        }

        if (Part.bAttachToPoseDriver && PoseDriverComp)
        {
            SkelComp->SetupAttachment(PoseDriverComp);
        }
        else if (Part.bCopyPoseFromFirstPart && LeaderComp)
        {
            SkelComp->SetupAttachment(LeaderComp);
        }
        else
        {
            SkelComp->SetupAttachment(PreviewPivot);
        }

        SkelComp->SetSkeletalMesh(Part.Mesh);
        SkelComp->SetRelativeLocation(Part.RelativeOffset);
        SkelComp->SetRelativeRotation(Part.RelativeRotation);
        SkelComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        SkelComp->SetCastShadow(false);
        SkelComp->bDisableClothSimulation = true;
        SkelComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

        if (Part.bHiddenPoseDriver)
        {
            SkelComp->SetVisibility(false, false);
            SkelComp->SetHiddenInGame(true, false);
        }

        SkelComp->RegisterComponent();

        if (Part.bCopyPoseFromFirstPart && LeaderComp)
        {
            SkelComp->SetLeaderPoseComponent(LeaderComp);
        }
        else if (Part.AnimClass)
        {
            SkelComp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
            SkelComp->SetAnimInstanceClass(Part.AnimClass);
        }
        else if (Part.IdleAnim)
        {
            SkelComp->SetAnimationMode(EAnimationMode::AnimationSingleNode);
            SkelComp->PlayAnimation(Part.IdleAnim, true);
        }

        if (Part.bHiddenPoseDriver)
        {
            PoseDriverComp = SkelComp;
        }
        else if (!LeaderComp)
        {
            LeaderComp = SkelComp;
        }
        SpawnedPreviewMeshes.Add(SkelComp);
    }
}

void AHoloCharacterStand::ApplyHologramMaterials()
{
    if (HologramMaterial)
    {
        if (!HoloBodyMID || HoloBodyMID->Parent != HologramMaterial)
        {
            HoloBodyMID = UMaterialInstanceDynamic::Create(HologramMaterial, this);
        }
        if (HoloBodyMID)
        {
            HoloBodyMID->SetVectorParameterValue(TEXT("HoloColor"), HologramColor);
            HoloBodyMID->SetScalarParameterValue(TEXT("HoloOpacity"), 0.92f);
            HoloBodyMID->SetScalarParameterValue(TEXT("DissolveAmount"), 0.0f);
            HoloBodyMID->SetScalarParameterValue(TEXT("WorldBaseZ"), static_cast<float>(GetActorLocation().Z + HologramHoverHeight));
            HoloBodyMID->SetScalarParameterValue(TEXT("EmissiveBoost"), 3.6f);
            HoloBodyMID->SetScalarParameterValue(TEXT("GlitchStrength"), GlitchStrength);
        }
    }

    if (HoloBodyMID)
    {
        for (USkeletalMeshComponent* Comp : SpawnedPreviewMeshes)
        {
            if (!IsValid(Comp) || Comp->bHiddenInGame)
            {
                continue;
            }
            const int32 NumMats = Comp->GetNumMaterials();
            for (int32 Slot = 0; Slot < NumMats; ++Slot)
            {
                Comp->SetMaterial(Slot, HoloBodyMID);
            }
        }
    }

    if (PadMaterial && GlowPadMesh)
    {
        if (!HoloPadMID || HoloPadMID->Parent != PadMaterial)
        {
            HoloPadMID = UMaterialInstanceDynamic::Create(PadMaterial, this);
        }
        if (HoloPadMID)
        {
            HoloPadMID->SetVectorParameterValue(TEXT("HoloColor"), HologramColor);
            HoloPadMID->SetScalarParameterValue(TEXT("HoloOpacity"), 0.72f);
            HoloPadMID->SetScalarParameterValue(TEXT("PulseBoost"), 1.0f);
            GlowPadMesh->SetMaterial(0, HoloPadMID);
        }
    }

    if (LightConeMaterial && LightConeMesh)
    {
        if (!LightConeMID || LightConeMID->Parent != LightConeMaterial)
        {
            LightConeMID = UMaterialInstanceDynamic::Create(LightConeMaterial, this);
        }
        if (LightConeMID)
        {
            LightConeMID->SetVectorParameterValue(TEXT("HoloColor"), HologramColor);
            LightConeMID->SetScalarParameterValue(TEXT("ConeBaseZ"), static_cast<float>(GetActorLocation().Z + 5.0));
            LightConeMID->SetScalarParameterValue(TEXT("ConeHeight"), 50.0f);
            LightConeMID->SetScalarParameterValue(TEXT("ConeOpacity"), 0.68f);
            LightConeMesh->SetMaterial(0, LightConeMID);
        }
    }

    if (EmitterOrbMaterial && EmitterInnerSphere && EmitterOuterSphere)
    {
        if (!EmitterInnerMID || EmitterInnerMID->Parent != EmitterOrbMaterial)
        {
            EmitterInnerMID = UMaterialInstanceDynamic::Create(EmitterOrbMaterial, this);
        }
        if (!EmitterOuterMID || EmitterOuterMID->Parent != EmitterOrbMaterial)
        {
            EmitterOuterMID = UMaterialInstanceDynamic::Create(EmitterOrbMaterial, this);
        }
        if (EmitterInnerMID)
        {
            EmitterInnerMID->SetVectorParameterValue(TEXT("HoloColor"), HologramColor);
            EmitterInnerMID->SetScalarParameterValue(TEXT("CoreFill"), 0.85f);
            EmitterInnerMID->SetScalarParameterValue(TEXT("EmissiveBoost"), 5.5f);
            EmitterInnerMID->SetScalarParameterValue(TEXT("OrbOpacity"), 0.92f);
            EmitterInnerSphere->SetMaterial(0, EmitterInnerMID);
        }
        if (EmitterOuterMID)
        {
            EmitterOuterMID->SetVectorParameterValue(TEXT("HoloColor"), HologramColor);
            EmitterOuterMID->SetScalarParameterValue(TEXT("CoreFill"), 0.10f);
            EmitterOuterMID->SetScalarParameterValue(TEXT("EmissiveBoost"), 4.2f);
            EmitterOuterMID->SetScalarParameterValue(TEXT("OrbOpacity"), 0.75f);
            EmitterOuterSphere->SetMaterial(0, EmitterOuterMID);
        }
    }

    if (HoloPointLight)
    {
        HoloPointLight->SetLightColor(HologramColor);
    }
}

UHoloSwitchPromptWidget* AHoloCharacterStand::EnsurePromptWidget()
{
    if (!PromptWidgetComponent)
    {
        return nullptr;
    }
    if (!PromptWidgetComponent->GetUserWidgetObject())
    {
        PromptWidgetComponent->InitWidget();
    }
    if (UHoloSwitchPromptWidget* Widget = Cast<UHoloSwitchPromptWidget>(PromptWidgetComponent->GetUserWidgetObject()))
    {
        const FString LabelStr = FString::Printf(TEXT("切换 · %s"), *CharacterDisplayName.ToString());
        Widget->SetPromptLabel(FText::FromString(LabelStr));
        return Widget;
    }
    return nullptr;
}

void AHoloCharacterStand::SetIsActiveCharacterStand(bool bInActive)
{
    bIsActiveCharacter = bInActive;
}

void AHoloCharacterStand::TriggerSwitchPulse()
{
    SwitchPulseAlpha = 1.0f;
}

FVector AHoloCharacterStand::GetPromptAnchorWorldLocation() const
{
    return GetActorLocation() + FVector(0.0f, 0.0f, PromptHeightZ);
}

void AHoloCharacterStand::SetPromptState(EPlumPoleInteractUIState NewState, float DeltaSeconds, bool bUsingGamepad)
{
    if (!PromptWidgetComponent)
    {
        return;
    }

    UHoloSwitchPromptWidget* Widget = EnsurePromptWidget();
    if (!Widget)
    {
        return;
    }

    Widget->SetUsingGamepad(bUsingGamepad);

    if (NewState != EPlumPoleInteractUIState::None && !PromptWidgetComponent->IsVisible())
    {
        PromptWidgetComponent->SetVisibility(true);
    }

    CurrentPromptState = NewState;
    Widget->TransitionToState(NewState);
    Widget->AdvanceAnimation(DeltaSeconds);
    Widget->RefreshLabelVisibility();

    if (Widget->IsVisuallyHidden() && PromptWidgetComponent->IsVisible())
    {
        PromptWidgetComponent->SetVisibility(false);
    }
}

void AHoloCharacterStand::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    ElapsedTime += DeltaSeconds;
    SwitchPulseAlpha = FMath::Max(0.0f, SwitchPulseAlpha - DeltaSeconds * 1.8f);

    // Subtle floating holographic hover + continuous turntable yaw rotation (matching reference RotateAroundYAxis)
    const float HoverOffset = HologramHoverHeight + FMath::Sin(ElapsedTime * 2.1f) * 2.2f;
    if (PreviewPivot)
    {
        PreviewPivot->SetRelativeLocation(FVector(0.0f, 0.0f, HoverOffset));
        if (!FMath::IsNearlyZero(HologramRotationSpeed))
        {
            PreviewPivot->AddLocalRotation(FRotator(0.0f, HologramRotationSpeed * DeltaSeconds, 0.0f));
        }
    }

    const FLinearColor TargetColor = bIsActiveCharacter ? ActiveStandbyColor : HologramColor;
    const float BaseOpacity = bIsActiveCharacter ? 0.72f : 0.94f;
    const float Shimmer = 0.94f + 0.06f * FMath::Sin(ElapsedTime * 4.7f);
    const float PulseBoost = 1.0f + SwitchPulseAlpha * 2.4f;

    if (HoloBodyMID)
    {
        HoloBodyMID->SetVectorParameterValue(TEXT("HoloColor"), TargetColor);
        HoloBodyMID->SetScalarParameterValue(TEXT("HoloOpacity"), BaseOpacity * Shimmer);
        HoloBodyMID->SetScalarParameterValue(TEXT("EmissiveBoost"), (bIsActiveCharacter ? 3.0f : 3.8f) * PulseBoost);
        HoloBodyMID->SetScalarParameterValue(TEXT("WorldBaseZ"), static_cast<float>(GetActorLocation().Z + HoverOffset));
        HoloBodyMID->SetScalarParameterValue(TEXT("GlitchStrength"), GlitchStrength * (1.0f + SwitchPulseAlpha * 1.5f));
    }

    if (HoloPadMID)
    {
        HoloPadMID->SetVectorParameterValue(TEXT("HoloColor"), TargetColor);
        HoloPadMID->SetScalarParameterValue(TEXT("HoloOpacity"), bIsActiveCharacter ? 0.55f : 0.72f);
        HoloPadMID->SetScalarParameterValue(TEXT("PulseBoost"), PulseBoost);
    }

    if (LightConeMID)
    {
        LightConeMID->SetVectorParameterValue(TEXT("HoloColor"), TargetColor);
        LightConeMID->SetScalarParameterValue(TEXT("ConeBaseZ"), static_cast<float>(GetActorLocation().Z + 5.0));
        LightConeMID->SetScalarParameterValue(TEXT("ConeHeight"), 50.0f);
        LightConeMID->SetScalarParameterValue(TEXT("ConeOpacity"), (bIsActiveCharacter ? 0.52f : 0.72f) * Shimmer);
    }

    if (EmitterInnerMID)
    {
        EmitterInnerMID->SetVectorParameterValue(TEXT("HoloColor"), TargetColor);
        EmitterInnerMID->SetScalarParameterValue(TEXT("EmissiveBoost"), 5.5f * Shimmer * PulseBoost);
    }

    if (EmitterOuterMID)
    {
        EmitterOuterMID->SetVectorParameterValue(TEXT("HoloColor"), TargetColor);
        EmitterOuterMID->SetScalarParameterValue(TEXT("EmissiveBoost"), 4.2f * Shimmer * PulseBoost);
    }

    if (HoloPointLight)
    {
        HoloPointLight->SetLightColor(TargetColor);
        HoloPointLight->SetIntensity((bIsActiveCharacter ? 1500.0f : 2300.0f) * Shimmer * PulseBoost);
    }
}
