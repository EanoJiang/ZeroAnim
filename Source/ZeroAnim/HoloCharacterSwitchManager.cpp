#include "HoloCharacterSwitchManager.h"

#include "Components/MeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HoloCharacterStand.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PlumPoleCharacter.h"
#include "UObject/ConstructorHelpers.h"

AHoloCharacterSwitchManager::AHoloCharacterSwitchManager()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;

    TransitionFlashLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("TransitionFlashLight"));
    SetRootComponent(TransitionFlashLight);
    TransitionFlashLight->SetIntensity(0.0f);
    TransitionFlashLight->SetAttenuationRadius(320.0f);
    TransitionFlashLight->SetCastShadows(false);
    TransitionFlashLight->SetVisibility(false);

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> HoloMatFinder(
        TEXT("/Game/CharacterShowcase/Materials/M_HoloCharacter.M_HoloCharacter"));
    if (HoloMatFinder.Succeeded())
    {
        HologramMaterial = HoloMatFinder.Object;
    }
}

void AHoloCharacterSwitchManager::BeginPlay()
{
    Super::BeginPlay();
    RefreshStandsList();
}

void AHoloCharacterSwitchManager::RefreshStandsList()
{
    Stands.Reset();
    for (TActorIterator<AHoloCharacterStand> It(GetWorld()); It; ++It)
    {
        if (IsValid(*It))
        {
            Stands.Add(*It);
        }
    }
}

void AHoloCharacterSwitchManager::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (Stands.IsEmpty())
    {
        RefreshStandsList();
    }

    if (SwitchPhase != EHoloSwitchPhase::Idle)
    {
        TickSwitchTransition(DeltaSeconds);
    }

    UpdateStandPromptsAndInput(DeltaSeconds);
}

void AHoloCharacterSwitchManager::UpdateStandPromptsAndInput(float DeltaSeconds)
{
    UWorld* World = GetWorld();
    APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
    APawn* Pawn = PC ? PC->GetPawn() : nullptr;

    const APlumPoleCharacter* PoleChar = Cast<APlumPoleCharacter>(Pawn);
    const bool bUsingGamepad = PoleChar ? PoleChar->IsUsingGamepad() : false;
    const bool bCanInteract = (SwitchPhase == EHoloSwitchPhase::Idle)
        && IsValid(Pawn)
        && (!PoleChar || !PoleChar->IsPoleAbilityActive());

    AHoloCharacterStand* ClosestInteractStand = nullptr;
    float BestInteractDistSq = MAX_flt;
    const FVector PawnLoc = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;

    // First pass: mark active character stand and find the single closest interactable stand
    for (const TWeakObjectPtr<AHoloCharacterStand>& WeakStand : Stands)
    {
        AHoloCharacterStand* Stand = WeakStand.Get();
        if (!IsValid(Stand))
        {
            continue;
        }

        const bool bIsSameClass = Pawn && Stand->TargetCharacterClass
            && Pawn->GetClass() == Stand->TargetCharacterClass;
        Stand->SetIsActiveCharacterStand(bIsSameClass);

        if (!bCanInteract || bIsSameClass || !Stand->TargetCharacterClass)
        {
            continue;
        }

        const float DistSq = FVector::DistSquared2D(PawnLoc, Stand->GetActorLocation());
        if (DistSq <= FMath::Square(Stand->InteractRadius) && DistSq < BestInteractDistSq)
        {
            BestInteractDistSq = DistSq;
            ClosestInteractStand = Stand;
        }
    }

    HighlightedStand = ClosestInteractStand;

    // Second pass: drive PBZ prompt states on every stand cleanly
    for (const TWeakObjectPtr<AHoloCharacterStand>& WeakStand : Stands)
    {
        AHoloCharacterStand* Stand = WeakStand.Get();
        if (!IsValid(Stand))
        {
            continue;
        }

        EPlumPoleInteractUIState DesiredState = EPlumPoleInteractUIState::None;
        if (bCanInteract && !Stand->IsActiveCharacterStand() && Stand->TargetCharacterClass)
        {
            if (Stand == ClosestInteractStand)
            {
                DesiredState = EPlumPoleInteractUIState::InputAction;
            }
            else
            {
                const float DistSq = FVector::DistSquared2D(PawnLoc, Stand->GetActorLocation());
                if (DistSq <= FMath::Square(Stand->VirtualAwarenessRadius))
                {
                    DesiredState = EPlumPoleInteractUIState::Virtual;
                }
            }
        }

        Stand->SetPromptState(DesiredState, DeltaSeconds, bUsingGamepad);
    }

    // Listen for F key (or Gamepad Face-Left X/Square) to trigger holographic character switch
    if (bCanInteract && ClosestInteractStand && PC)
    {
        const bool bPressedSwitch = PC->WasInputKeyJustPressed(EKeys::F)
            || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Left);
        if (bPressedSwitch)
        {
            TriggerSwitchToStand(ClosestInteractStand);
        }
    }
}

bool AHoloCharacterSwitchManager::TriggerSwitchToStand(AHoloCharacterStand* TargetStand)
{
    if (SwitchPhase != EHoloSwitchPhase::Idle || !IsValid(TargetStand) || !TargetStand->TargetCharacterClass)
    {
        return false;
    }

    UWorld* World = GetWorld();
    APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
    APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (!PC || !IsValid(Pawn))
    {
        return false;
    }
    if (Pawn->GetClass() == TargetStand->TargetCharacterClass)
    {
        return false;
    }

    if (!HologramMaterial && TargetStand->HologramMaterial)
    {
        HologramMaterial = TargetStand->HologramMaterial;
    }
    if (!HologramMaterial)
    {
        return false;
    }

    if (!BodyHoloMID || BodyHoloMID->Parent != HologramMaterial)
    {
        BodyHoloMID = UMaterialInstanceDynamic::Create(HologramMaterial, this);
    }
    if (!OverlayHoloMID || OverlayHoloMID->Parent != HologramMaterial)
    {
        OverlayHoloMID = UMaterialInstanceDynamic::Create(HologramMaterial, this);
    }

    ActiveSwitchStand = TargetStand;
    TransitionPawn = Pawn;
    TransitionColor = TargetStand->HologramColor;
    TargetStand->TriggerSwitchPulse();

    // Freeze horizontal locomotion gently while keeping free mouse camera orbit responsive
    if (ACharacter* Char = Cast<ACharacter>(Pawn))
    {
        if (UCharacterMovementComponent* Move = Char->GetCharacterMovement())
        {
            Move->StopMovementImmediately();
        }
    }
    PC->SetIgnoreMoveInput(true);
    bMoveInputLocked = true;

    CachedMeshes.Reset();
    CachedGrooms.Reset();
    CollectVisibleMeshesAndGrooms(Pawn, CachedMeshes, CachedGrooms);

    const float BaseZ = static_cast<float>(Pawn->GetActorLocation().Z - 90.0);
    if (OverlayHoloMID)
    {
        OverlayHoloMID->SetVectorParameterValue(TEXT("HoloColor"), TransitionColor);
        OverlayHoloMID->SetScalarParameterValue(TEXT("HoloOpacity"), 0.0f);
        OverlayHoloMID->SetScalarParameterValue(TEXT("DissolveAmount"), 0.0f);
        OverlayHoloMID->SetScalarParameterValue(TEXT("WorldBaseZ"), BaseZ);
        OverlayHoloMID->SetScalarParameterValue(TEXT("EmissiveBoost"), 4.2f);
        OverlayHoloMID->SetScalarParameterValue(TEXT("GlitchStrength"), 0.35f);
    }
    ApplyOverlayMaterial(CachedMeshes, OverlayHoloMID);

    if (TransitionFlashLight)
    {
        TransitionFlashLight->SetWorldLocation(Pawn->GetActorLocation() + FVector(0.0f, 0.0f, 30.0f));
        TransitionFlashLight->SetLightColor(TransitionColor);
        TransitionFlashLight->SetIntensity(0.0f);
        TransitionFlashLight->SetVisibility(true);
    }

    bGroomsHiddenDuringDissolve = false;
    SwitchPhase = EHoloSwitchPhase::OverlayEngulf;
    PhaseElapsed = 0.0f;
    return true;
}

void AHoloCharacterSwitchManager::TickSwitchTransition(float DeltaSeconds)
{
    PhaseElapsed += DeltaSeconds;
    APawn* Pawn = TransitionPawn.Get();

    if (TransitionFlashLight && IsValid(Pawn))
    {
        TransitionFlashLight->SetWorldLocation(Pawn->GetActorLocation() + FVector(0.0f, 0.0f, 30.0f));
    }

    if (SwitchPhase == EHoloSwitchPhase::OverlayEngulf)
    {
        if (!IsValid(Pawn))
        {
            FinishSwitchTransition();
            return;
        }
        const float T = FMath::Clamp(PhaseElapsed / FMath::Max(OverlayEngulfDuration, 0.01f), 0.0f, 1.0f);
        const float SmoothT = FMath::SmoothStep(0.0f, 1.0f, T);
        if (OverlayHoloMID)
        {
            OverlayHoloMID->SetScalarParameterValue(TEXT("HoloOpacity"), SmoothT * 0.95f);
            OverlayHoloMID->SetScalarParameterValue(TEXT("WorldBaseZ"), static_cast<float>(Pawn->GetActorLocation().Z - 90.0));
        }
        if (TransitionFlashLight)
        {
            TransitionFlashLight->SetIntensity(SmoothT * 3200.0f);
        }

        if (T >= 1.0f)
        {
            // Transition into pure translucent hologram dissolve-out!
            ApplyOverlayMaterial(CachedMeshes, nullptr);
            if (BodyHoloMID)
            {
                BodyHoloMID->SetVectorParameterValue(TEXT("HoloColor"), TransitionColor);
                BodyHoloMID->SetScalarParameterValue(TEXT("HoloOpacity"), 1.0f);
                BodyHoloMID->SetScalarParameterValue(TEXT("DissolveAmount"), 0.0f);
                BodyHoloMID->SetScalarParameterValue(TEXT("WorldBaseZ"), static_cast<float>(Pawn->GetActorLocation().Z - 90.0));
                BodyHoloMID->SetScalarParameterValue(TEXT("EmissiveBoost"), 4.6f);
                BodyHoloMID->SetScalarParameterValue(TEXT("GlitchStrength"), 1.5f);
            }
            ApplyReplacementMaterial(CachedMeshes, BodyHoloMID);
            SwitchPhase = EHoloSwitchPhase::DissolveOut;
            PhaseElapsed = 0.0f;
        }
        return;
    }

    if (SwitchPhase == EHoloSwitchPhase::DissolveOut)
    {
        if (!IsValid(Pawn))
        {
            FinishSwitchTransition();
            return;
        }
        const float T = FMath::Clamp(PhaseElapsed / FMath::Max(DissolveOutDuration, 0.01f), 0.0f, 1.0f);
        const float SmoothT = FMath::SmoothStep(0.0f, 1.0f, T);
        const float Dissolve = SmoothT * 1.15f;
        const float Opacity = 1.0f - FMath::Pow(SmoothT, 1.6f);

        if (BodyHoloMID)
        {
            BodyHoloMID->SetScalarParameterValue(TEXT("DissolveAmount"), Dissolve);
            BodyHoloMID->SetScalarParameterValue(TEXT("HoloOpacity"), FMath::Max(0.0f, Opacity));
            BodyHoloMID->SetScalarParameterValue(TEXT("WorldBaseZ"), static_cast<float>(Pawn->GetActorLocation().Z - 90.0));
        }
        if (!bGroomsHiddenDuringDissolve && T >= 0.45f)
        {
            SetGroomsVisible(CachedGrooms, false);
            bGroomsHiddenDuringDissolve = true;
        }
        if (TransitionFlashLight)
        {
            TransitionFlashLight->SetIntensity(3200.0f + SmoothT * 2600.0f);
        }

        if (T >= 1.0f)
        {
            ExecuteAtomicPawnSwap();
        }
        return;
    }

    if (SwitchPhase == EHoloSwitchPhase::MaterializeIn)
    {
        if (!IsValid(Pawn))
        {
            FinishSwitchTransition();
            return;
        }
        const float T = FMath::Clamp(PhaseElapsed / FMath::Max(MaterializeInDuration, 0.01f), 0.0f, 1.0f);
        const float SmoothT = FMath::SmoothStep(0.0f, 1.0f, T);
        const float Dissolve = (1.0f - SmoothT) * 1.15f;
        const float Opacity = FMath::Pow(SmoothT, 0.7f);

        if (BodyHoloMID)
        {
            BodyHoloMID->SetScalarParameterValue(TEXT("DissolveAmount"), Dissolve);
            BodyHoloMID->SetScalarParameterValue(TEXT("HoloOpacity"), Opacity);
            BodyHoloMID->SetScalarParameterValue(TEXT("WorldBaseZ"), static_cast<float>(Pawn->GetActorLocation().Z - 90.0));
        }
        if (TransitionFlashLight)
        {
            TransitionFlashLight->SetIntensity(5800.0f - SmoothT * 2600.0f);
        }

        if (T >= 1.0f)
        {
            // Restore real PBR / MetaHuman materials + Groom hair while keeping glowing hologram overlay fading out!
            RestoreOriginalMaterials(CachedMeshes);
            SetGroomsVisible(CachedGrooms, true);
            ResetClothOnPawn(Pawn);

            if (OverlayHoloMID)
            {
                OverlayHoloMID->SetVectorParameterValue(TEXT("HoloColor"), TransitionColor);
                OverlayHoloMID->SetScalarParameterValue(TEXT("HoloOpacity"), 0.92f);
                OverlayHoloMID->SetScalarParameterValue(TEXT("DissolveAmount"), 0.0f);
                OverlayHoloMID->SetScalarParameterValue(TEXT("WorldBaseZ"), static_cast<float>(Pawn->GetActorLocation().Z - 90.0));
                OverlayHoloMID->SetScalarParameterValue(TEXT("EmissiveBoost"), 4.2f);
                OverlayHoloMID->SetScalarParameterValue(TEXT("GlitchStrength"), 0.25f);
            }
            ApplyOverlayMaterial(CachedMeshes, OverlayHoloMID);

            SwitchPhase = EHoloSwitchPhase::OverlayFadeOut;
            PhaseElapsed = 0.0f;
        }
        return;
    }

    if (SwitchPhase == EHoloSwitchPhase::OverlayFadeOut)
    {
        const float T = FMath::Clamp(PhaseElapsed / FMath::Max(OverlayFadeOutDuration, 0.01f), 0.0f, 1.0f);
        const float SmoothT = FMath::SmoothStep(0.0f, 1.0f, T);
        if (OverlayHoloMID)
        {
            OverlayHoloMID->SetScalarParameterValue(TEXT("HoloOpacity"), (1.0f - SmoothT) * 0.92f);
        }
        if (TransitionFlashLight)
        {
            TransitionFlashLight->SetIntensity((1.0f - SmoothT) * 3200.0f);
        }

        if (T >= 1.0f)
        {
            FinishSwitchTransition();
        }
    }
}

void AHoloCharacterSwitchManager::ExecuteAtomicPawnSwap()
{
    UWorld* World = GetWorld();
    APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
    APawn* OldPawn = TransitionPawn.Get();
    AHoloCharacterStand* Stand = ActiveSwitchStand.Get();

    if (!World || !PC || !IsValid(OldPawn) || !IsValid(Stand) || !Stand->TargetCharacterClass)
    {
        FinishSwitchTransition();
        return;
    }

    const FVector SavedLoc = OldPawn->GetActorLocation();
    const FRotator SavedRot = OldPawn->GetActorRotation();
    const FRotator SavedControlRot = PC->GetControlRotation();

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ACharacter* NewPawn = World->SpawnActor<ACharacter>(Stand->TargetCharacterClass, SavedLoc, SavedRot, SpawnParams);
    if (!IsValid(NewPawn))
    {
        FinishSwitchTransition();
        return;
    }

    // Immediately prepare NewPawn in fully-dissolved hologram state BEFORE its first rendered frame!
    CachedMeshes.Reset();
    CachedGrooms.Reset();
    CollectVisibleMeshesAndGrooms(NewPawn, CachedMeshes, CachedGrooms);

    if (BodyHoloMID)
    {
        BodyHoloMID->SetVectorParameterValue(TEXT("HoloColor"), TransitionColor);
        BodyHoloMID->SetScalarParameterValue(TEXT("DissolveAmount"), 1.15f);
        BodyHoloMID->SetScalarParameterValue(TEXT("HoloOpacity"), 0.0f);
        BodyHoloMID->SetScalarParameterValue(TEXT("WorldBaseZ"), static_cast<float>(SavedLoc.Z - 90.0));
        BodyHoloMID->SetScalarParameterValue(TEXT("EmissiveBoost"), 4.6f);
        BodyHoloMID->SetScalarParameterValue(TEXT("GlitchStrength"), 1.5f);
    }
    ApplyReplacementMaterial(CachedMeshes, BodyHoloMID);
    SetGroomsVisible(CachedGrooms, false);
    ResetClothOnPawn(NewPawn);

    // Seamless possession preserving exact camera angle
    PC->Possess(NewPawn);
    PC->SetControlRotation(SavedControlRot);
    if (UCharacterMovementComponent* Move = NewPawn->GetCharacterMovement())
    {
        Move->StopMovementImmediately();
    }

    OldPawn->Destroy();
    TransitionPawn = NewPawn;
    SwitchPhase = EHoloSwitchPhase::MaterializeIn;
    PhaseElapsed = 0.0f;
}

void AHoloCharacterSwitchManager::FinishSwitchTransition()
{
    ApplyOverlayMaterial(CachedMeshes, nullptr);
    RestoreOriginalMaterials(CachedMeshes);
    SetGroomsVisible(CachedGrooms, true);

    if (TransitionFlashLight)
    {
        TransitionFlashLight->SetIntensity(0.0f);
        TransitionFlashLight->SetVisibility(false);
    }

    if (bMoveInputLocked)
    {
        if (UWorld* World = GetWorld())
        {
            if (APlayerController* PC = World->GetFirstPlayerController())
            {
                PC->SetIgnoreMoveInput(false);
            }
        }
        bMoveInputLocked = false;
    }

    CachedMeshes.Reset();
    CachedGrooms.Reset();
    TransitionPawn.Reset();
    ActiveSwitchStand.Reset();
    SwitchPhase = EHoloSwitchPhase::Idle;
    PhaseElapsed = 0.0f;
}

void AHoloCharacterSwitchManager::CollectVisibleMeshesAndGrooms(
    APawn* Pawn,
    TArray<FHoloSavedMeshMaterials>& OutMeshes,
    TArray<FHoloSavedGroomVisibility>& OutGrooms) const
{
    if (!IsValid(Pawn))
    {
        return;
    }

    TInlineComponentArray<UPrimitiveComponent*> PrimComps;
    Pawn->GetComponents(PrimComps);
    for (UPrimitiveComponent* Prim : PrimComps)
    {
        if (!IsValid(Prim) || !Prim->IsRegistered())
        {
            continue;
        }

        const FString ClassName = Prim->GetClass()->GetName();
        if (ClassName.Contains(TEXT("Groom")))
        {
            FHoloSavedGroomVisibility Entry;
            Entry.Comp = Prim;
            Entry.bWasVisible = Prim->IsVisible();
            Entry.bWasHiddenInGame = Prim->bHiddenInGame;
            OutGrooms.Add(Entry);
            continue;
        }

        if (UMeshComponent* MeshComp = Cast<UMeshComponent>(Prim))
        {
            // Skip hidden parent mannequin / hidden legacy hair cards so we never reveal them!
            if (!MeshComp->IsVisible() || MeshComp->bHiddenInGame)
            {
                continue;
            }
            const int32 NumMats = MeshComp->GetNumMaterials();
            if (NumMats <= 0)
            {
                continue;
            }
            FHoloSavedMeshMaterials Entry;
            Entry.MeshComp = MeshComp;
            Entry.CompName = MeshComp->GetFName();
            Entry.NumMaterialSlots = NumMats;
            Entry.OriginalOverrideMaterials = MeshComp->OverrideMaterials;
            OutMeshes.Add(MoveTemp(Entry));
        }
    }
}

void AHoloCharacterSwitchManager::ApplyOverlayMaterial(const TArray<FHoloSavedMeshMaterials>& Meshes, UMaterialInterface* Mat) const
{
    APawn* Pawn = TransitionPawn.Get();
    for (const FHoloSavedMeshMaterials& Entry : Meshes)
    {
        UMeshComponent* Comp = Entry.MeshComp.Get();
        if (!Comp && IsValid(Pawn) && !Entry.CompName.IsNone())
        {
            TInlineComponentArray<UMeshComponent*> LiveComps;
            Pawn->GetComponents(LiveComps);
            for (UMeshComponent* Live : LiveComps)
            {
                if (IsValid(Live) && Live->GetFName() == Entry.CompName)
                {
                    Comp = Live;
                    break;
                }
            }
        }
        if (Comp)
        {
            Comp->SetOverlayMaterial(Mat);
        }
    }
}

void AHoloCharacterSwitchManager::ApplyReplacementMaterial(const TArray<FHoloSavedMeshMaterials>& Meshes, UMaterialInterface* Mat) const
{
    for (const FHoloSavedMeshMaterials& Entry : Meshes)
    {
        if (UMeshComponent* Comp = Entry.MeshComp.Get())
        {
            for (int32 Slot = 0; Slot < Entry.NumMaterialSlots; ++Slot)
            {
                Comp->SetMaterial(Slot, Mat);
            }
        }
    }
}

void AHoloCharacterSwitchManager::RestoreOriginalMaterials(const TArray<FHoloSavedMeshMaterials>& Meshes) const
{
    APawn* Pawn = TransitionPawn.Get();
    for (const FHoloSavedMeshMaterials& Entry : Meshes)
    {
        UMeshComponent* Comp = Entry.MeshComp.Get();
        if (!Comp && IsValid(Pawn) && !Entry.CompName.IsNone())
        {
            TInlineComponentArray<UMeshComponent*> LiveComps;
            Pawn->GetComponents(LiveComps);
            for (UMeshComponent* Live : LiveComps)
            {
                if (IsValid(Live) && Live->GetFName() == Entry.CompName)
                {
                    Comp = Live;
                    break;
                }
            }
        }
        if (Comp)
        {
            Comp->SetOverlayMaterial(nullptr);
            Comp->EmptyOverrideMaterials();
            for (int32 Slot = 0; Slot < Entry.OriginalOverrideMaterials.Num(); ++Slot)
            {
                if (Entry.OriginalOverrideMaterials[Slot])
                {
                    Comp->SetMaterial(Slot, Entry.OriginalOverrideMaterials[Slot]);
                }
            }
            Comp->MarkRenderStateDirty();
        }
    }
}

void AHoloCharacterSwitchManager::SetGroomsVisible(const TArray<FHoloSavedGroomVisibility>& Grooms, bool bShow) const
{
    for (const FHoloSavedGroomVisibility& Entry : Grooms)
    {
        if (UPrimitiveComponent* Comp = Entry.Comp.Get())
        {
            if (bShow)
            {
                Comp->SetVisibility(Entry.bWasVisible, false);
                Comp->SetHiddenInGame(Entry.bWasHiddenInGame, false);
            }
            else
            {
                Comp->SetVisibility(false, false);
                Comp->SetHiddenInGame(true, false);
            }
        }
    }
}

void AHoloCharacterSwitchManager::ResetClothOnPawn(APawn* Pawn) const
{
    if (!IsValid(Pawn))
    {
        return;
    }
    TInlineComponentArray<USkeletalMeshComponent*> SkelComps;
    Pawn->GetComponents(SkelComps);
    for (USkeletalMeshComponent* Skel : SkelComps)
    {
        if (IsValid(Skel))
        {
            Skel->ForceClothNextUpdateTeleportAndReset();
        }
    }
}
