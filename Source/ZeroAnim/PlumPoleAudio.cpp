#include "PlumPoleAudio.h"
#include "PlumPoleTraversalComponent.h"

#if WITH_POLE_WWISE
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PlumPoleActor.h"
#include "TimerManager.h"

THIRD_PARTY_INCLUDES_START
#include "Windows/AllowWindowsPlatformTypes.h"
#include <AK/AkWwiseSDKVersion.h>
#include <AK/SoundEngine/Common/AkMemoryMgrModule.h>
#include <AK/SoundEngine/Common/AkStreamMgrModule.h>
#include <AK/SoundEngine/Common/AkSoundEngine.h>
#include <AK/SoundEngine/Common/IAkPlugin.h>
#include <AK/Plugin/AkVorbisDecoderFactory.h>
#include <AK/Plugin/AkSilenceSourceFactory.h>
#include <AK/Plugin/AkParametricEQFXFactory.h>
#include <AK/Plugin/AkDelayFXFactory.h>
#include <AK/Plugin/AkCompressorFXFactory.h>
#include <AK/Plugin/AkExpanderFXFactory.h>
#include <AK/Plugin/AkPeakLimiterFXFactory.h>
#include <AK/Plugin/AkRoomVerbFXFactory.h>
#include <AK/Plugin/AkFlangerFXFactory.h>
#include <AK/Plugin/AkTimeStretchFXFactory.h>
#include <AK/Plugin/AkGainFXFactory.h>
#include <AK/Plugin/AkMeterFXFactory.h>
#include <AK/Plugin/MasteringSuiteFXFactory.h>
#include "Windows/HideWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_END

static_assert(AK_WWISESDK_VERSION_MAJOR == 2024 && AK_WWISESDK_VERSION_MINOR == 1 && AK_WWISESDK_VERSION_BUILD == 8920,
    "The PBZ banks use Wwise 2024.1.9.8920");
DEFINE_LOG_CATEGORY_STATIC(LogPlumPoleAudio, Log, All);

namespace
{
constexpr AkUniqueID CartWheel = 2844442802u, ClothDown = 3641247841u, CartWheelSingle = 292982209u, Footstep = 2760137366u;
constexpr AkSwitchGroupID SwitchGroupCartWheel = 2602340455u;
constexpr AkSwitchStateID SwitchStateCartWheelDefault = 717651098u;
constexpr AkSwitchGroupID SwitchGroupFootsteps = 2385628198u;
constexpr AkSwitchStateID SwitchStateWood = 2058049674u;

enum class EEmitter : uint8 { Body, Head, LeftFoot, RightFoot, Listener, Count };

struct FCue
{
    float Time;
    AkUniqueID Event;
    EEmitter Emitter = EEmitter::Body;
    // Takeoff whoosh/cloth cues whose authored pre-roll lies before ResolvedStart (e.g. chained hops/flips)
    // should still fire immediately on StartTimeline instead of being silently dropped.
    bool bAllowOnStartSkip = false;
};

// Frame-aligned cue tables derived from:
//   1) 60Hz root-stripped ball_l/ball_r contact frames in each active AS_Manny_* AnimSequence,
//   2) exact PBZ FRichCurve Rate integration + SkewWarp landing window, and
//   3) measured Wwise 2024.1.9 WEM transient attack times (Footstep_Wood peak ~20ms, Footstep_Dirt ~14ms,
//      CartWheel peak ~210-340ms, CartWheelSingle onset ~155ms / peak ~305ms, ClothDown peak ~318ms).
TConstArrayView<FCue> GetCues(EPlumPoleTimeline Timeline)
{
    // Dash (AS_Manny_Hun_LionHead_Dodge_F_R): takeoff 0.283s, apex 0.650s, LeftFoot plants 1.083-1.093s (WarpStage2End=1.093s, Combo=1.109s).
    static const FCue Dash[] = {
        {0.039364606f, CartWheel, EEmitter::Body, true},
        {0.26082963f,  ClothDown, EEmitter::Body, false},
        {1.072f,       Footstep,  EEmitter::LeftFoot, false},
    };
    // Jump_L (AS_Manny_PlumPole_HopFromLeft): takeoff 0.767s, apex 0.967s, RightFoot plants 1.217-1.233s (Combo=1.201s).
    static const FCue JumpL[] = {
        {0.13911754f, CartWheelSingle, EEmitter::Head, true},
        {1.198f,      Footstep,        EEmitter::RightFoot, false},
    };
    // Jump_R (AS_Manny_PlumPole_HopFromRight): mirror of Jump_L, lands on LeftFoot at 1.217-1.233s (Combo=1.200s).
    static const FCue JumpR[] = {
        {0.12415683f, CartWheelSingle, EEmitter::Head, true},
        {1.198f,      Footstep,        EEmitter::LeftFoot, false},
    };
    // JumpToPlatform_L (AS_Manny_Hun_LionHead_JumpOut_L_02): single-hop launch (0.767s), 1st landing RightFoot arrives at WarpStage2End (1.249s, CircularIn), 2nd LeftFoot (1.425s).
    static const FCue PlatformL[] = {
        {0.13911754f, CartWheelSingle, EEmitter::Head, true},
        {0.500f,      ClothDown,       EEmitter::Body, false},
        {1.232f,      Footstep,        EEmitter::RightFoot, false},
        {1.398f,      Footstep,        EEmitter::LeftFoot, false},
    };
    // JumpToPlatform_R (AS_Manny_Hun_LionHead_JumpOut_R_02): mirror of JumpToPlatform_L, 1st landing LeftFoot arrives at WarpStage2End (1.249s), 2nd RightFoot (1.425s).
    static const FCue PlatformR[] = {
        {0.12415683f, CartWheelSingle, EEmitter::Head, true},
        {0.500f,      ClothDown,       EEmitter::Body, false},
        {1.232f,      Footstep,        EEmitter::LeftFoot, false},
        {1.402f,      Footstep,        EEmitter::RightFoot, false},
    };
    // JumpOff (AS_Manny_Hun_LionHead_JumpOut_R): backflip takeoff (0.283s), apex (0.750s).
    // Two-foot ground touchdown is triggered dynamically by physics ground contact via PlumPoleAudio::PlayFootstep
    // so arbitrary pole heights stay frame-accurate; the recovery step plants RightFoot at 2.317s.
    static const FCue JumpOff[] = {
        {0.0878911f,  CartWheel, EEmitter::Body, true},
        {0.28805554f, ClothDown, EEmitter::Body, true},
        {2.295f,      Footstep,  EEmitter::RightFoot, false},
    };
    switch (Timeline)
    {
    case EPlumPoleTimeline::Dash: return Dash;
    case EPlumPoleTimeline::Jump_L: return JumpL;
    case EPlumPoleTimeline::Jump_R: return JumpR;
    case EPlumPoleTimeline::JumpToPlatform_L: return PlatformL;
    case EPlumPoleTimeline::JumpToPlatform_R: return PlatformR;
    case EPlumPoleTimeline::JumpOff: return JumpOff;
    default: return {};
    }
}

bool Succeeded(AKRESULT Result, const TCHAR* Operation)
{
    if (Result == AK_Success) return true;
    UE_LOG(LogPlumPoleAudio, Error, TEXT("%s failed (AKRESULT %d)"), Operation, static_cast<int>(Result));
    return false;
}

FVector GetEmitterLocation(const ACharacter& Character, EEmitter Slot)
{
    static const FName Names[] = {NAME_None, TEXT("Sound_Head"), TEXT("ball_l"), TEXT("ball_r")};
    if (Slot == EEmitter::Body || !Character.GetMesh()) return Character.GetActorLocation();
    FName Socket = Names[static_cast<uint8>(Slot)];
    if (Slot == EEmitter::Head && !Character.GetMesh()->DoesSocketExist(Socket)) Socket = TEXT("head");
    return Character.GetMesh()->GetSocketLocation(Socket);
}

void SetPosition(AkGameObjectID Id, const FVector& Position, const FQuat& Rotation)
{
    const FVector Front = Rotation.GetForwardVector();
    const FVector Up = Rotation.GetUpVector();
    AkSoundPosition Transform;
    Transform.Set(Position.Y, Position.Z, Position.X, Front.Y, Front.Z, Front.X, Up.Y, Up.Z, Up.X);
    AK::SoundEngine::SetPosition(Id, Transform);
}

bool ShouldTraceFootstepSurface(EPlumPoleTimeline Timeline)
{
    return Timeline == EPlumPoleTimeline::JumpOff
        || Timeline == EPlumPoleTimeline::JumpToPlatform_L
        || Timeline == EPlumPoleTimeline::JumpToPlatform_R;
}

AkSwitchStateID ResolveFootstepSurface(const ACharacter& Character, EPlumPoleTimeline Timeline, const FVector& FootLocation, FString* OutSurfaceName = nullptr)
{
    if (!ShouldTraceFootstepSurface(Timeline))
    {
        if (OutSurfaceName) *OutSurfaceName = TEXT("Wood");
        return SwitchStateWood;
    }

    const UWorld* World = Character.GetWorld();
    if (!World)
    {
        if (OutSurfaceName) *OutSurfaceName = TEXT("Dirt");
        return AK::SoundEngine::GetIDFromString("Dirt");
    }

    TArray<FHitResult> Hits;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(PlumPoleFootstep), false, &Character);
    Params.bReturnPhysicalMaterial = true;
    World->LineTraceMultiByChannel(Hits, FootLocation + FVector(0.0, 0.0, 45.0), FootLocation - FVector(0.0, 0.0, 240.0), ECC_Visibility, Params);

    for (const FHitResult& Hit : Hits)
    {
        const APlumPoleActor* HitPole = Cast<APlumPoleActor>(Hit.GetActor());
        if (HitPole)
        {
            // Exit pole markers sit on top of ground/platform geometry; ignore them so we trace the real surface beneath.
            if (HitPole->IsExitPole())
            {
                continue;
            }
            if (OutSurfaceName) *OutSurfaceName = TEXT("Wood");
            return SwitchStateWood;
        }

        FString Name = Hit.PhysMaterial.IsValid() ? Hit.PhysMaterial->GetName() : FString();
        Name.RemoveFromStart(TEXT("PM_"));
        if (Name.IsEmpty() || Name == TEXT("DefaultPhysicalMaterial")) Name = TEXT("Dirt");
        if (OutSurfaceName) *OutSurfaceName = Name;
        return AK::SoundEngine::GetIDFromString(*Name);
    }

    if (OutSurfaceName) *OutSurfaceName = TEXT("Dirt");
    return AK::SoundEngine::GetIDFromString("Dirt");
}

class FPoleSoundEngine
{
public:
    bool Init()
    {
        AkMemSettings Memory;
        AK::MemoryMgr::GetDefaultSettings(Memory);
        if (!Succeeded(AK::MemoryMgr::Init(&Memory), TEXT("MemoryMgr::Init"))) return false;
        AkStreamMgrSettings Streams;
        AK::StreamMgr::GetDefaultSettings(Streams);
        if (!AK::StreamMgr::Create(Streams)) return false;

        AkInitSettings Settings;
        AkPlatformInitSettings Platform;
        AK::SoundEngine::GetDefaultInitSettings(Settings);
        AK::SoundEngine::GetDefaultPlatformInitSettings(Platform);
        // Reduce WASAPI output latency from Wwise default 1024x4 (~85ms) to 256x4 (~21ms @ 48kHz)
        // so acoustic transients align tightly with visual frames while keeping a 4-refill safety margin.
        Settings.uNumSamplesPerFrame = 256;
        Platform.uNumRefillsInVoice = 4;
        if (!Succeeded(AK::SoundEngine::Init(&Settings, &Platform), TEXT("SoundEngine::Init"))) return false;

        const FString Directory = FPaths::ProjectContentDir() / TEXT("PlumPole/Wwise/Windows");
        const AkBankID Banks[] = {1355168291u, CartWheel, CartWheelSingle, ClothDown, Footstep};
        for (AkBankID Expected : Banks)
        {
            const FString Name = Expected == Banks[0] ? TEXT("Init.bnk") : FString::Printf(TEXT("%u.bnk"), Expected);
            TArray<uint8> Bytes;
            AkBankID Loaded;
            AkBankType Type;
            if (!FFileHelper::LoadFileToArray(Bytes, *(Directory / Name)) ||
                !Succeeded(AK::SoundEngine::LoadBankMemoryCopy(Bytes.GetData(), Bytes.Num(), Loaded, Type), *Name) ||
                Loaded != Expected) return false;
        }
        TArray<FString> Files;
        IFileManager::Get().FindFiles(Files, *(Directory / TEXT("*.wem")), true, false);
        if (Files.Num() != 158)
        {
            UE_LOG(LogPlumPoleAudio, Error, TEXT("Expected 158 PBZ media files in %s, found %d"), *Directory, Files.Num());
            return false;
        }
        TArray<AkSourceSettings> Sources;
        for (const FString& Name : Files)
        {
            TArray<uint8>& Media = MediaBuffers.AddDefaulted_GetRef();
            if (!FFileHelper::LoadFileToArray(Media, *(Directory / Name))) return false;
            Sources.Add({static_cast<AkUniqueID>(FCString::Strtoui64(*Name, nullptr, 10)), Media.GetData(), static_cast<AkUInt32>(Media.Num())});
        }
        if (!Succeeded(AK::SoundEngine::SetMedia(Sources.GetData(), Sources.Num()), TEXT("SetMedia"))) return false;
        TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FPoleSoundEngine::Tick));
        UE_LOG(LogPlumPoleAudio, Log, TEXT("Wwise 2024.1.9 loaded PBZ Init, four event banks and %d media (frame=%u refills=%u)"),
            Sources.Num(), Settings.uNumSamplesPerFrame, Platform.uNumRefillsInVoice);
        return true;
    }

    ~FPoleSoundEngine()
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
        if (AK::SoundEngine::IsInitialized()) AK::SoundEngine::Term();
        if (AK::IAkStreamMgr::Get()) AK::IAkStreamMgr::Get()->Destroy();
        if (AK::MemoryMgr::IsInitialized()) AK::MemoryMgr::Term();
    }

    bool Add(ACharacter* Character)
    {
        if (Emitters.Contains(Character)) return true;
        const AkGameObjectID First = NextId;
        NextId += static_cast<uint8>(EEmitter::Count);
        for (AkGameObjectID Id = First; Id < NextId; ++Id)
        {
            if (!Succeeded(AK::SoundEngine::RegisterGameObj(Id), TEXT("RegisterGameObj")))
            {
                for (AkGameObjectID Registered = First; Registered < Id; ++Registered)
                    AK::SoundEngine::UnregisterGameObj(Registered);
                return false;
            }
        }
        const AkGameObjectID Listener = First + static_cast<uint8>(EEmitter::Listener);
        for (AkGameObjectID Id = First; Id < Listener; ++Id) AK::SoundEngine::SetListeners(Id, &Listener, 1);
        Emitters.Add(Character, First);
        UpdateListenerPosition(*Character, First);
        return true;
    }

    void Remove(ACharacter* Character)
    {
        AkGameObjectID First;
        if (!Emitters.RemoveAndCopyValue(Character, First)) return;
        ReleaseGameObjects(First);
        AK::SoundEngine::RenderAudio();
    }

    bool Play(ACharacter* Character, EPlumPoleTimeline Timeline, const FCue& Cue, float LiveAnimTime = -1.0f)
    {
        if (!Character) return false;
        const AkGameObjectID* First = Emitters.Find(Character);
        if (!First) return false;

        // Sync listener on the exact play frame so spatialization never lags camera motion by 1 frame.
        UpdateListenerPosition(*Character, *First);

        const AkGameObjectID Id = *First + static_cast<uint8>(Cue.Emitter);
        const FVector Location = GetEmitterLocation(*Character, Cue.Emitter);
        SetPosition(Id, Location, Character->GetActorQuat());

        if (Cue.Event == CartWheel)
        {
            AK::SoundEngine::SetSwitch(SwitchGroupCartWheel, SwitchStateCartWheelDefault, Id);
        }

        FString SurfaceName;
        if (Cue.Event == Footstep)
        {
            const AkSwitchStateID Surface = ResolveFootstepSurface(*Character, Timeline, Location, &SurfaceName);
            AK::SoundEngine::SetSwitch(SwitchGroupFootsteps, Surface, Id);
        }

        const AkPlayingID Playing = AK::SoundEngine::PostEvent(Cue.Event, Id);
        const float ReportedAnimTime = LiveAnimTime >= 0.0f ? LiveAnimTime : Cue.Time;
        const double WorldTime = Character->GetWorld() ? Character->GetWorld()->GetTimeSeconds() : 0.0;
        UE_LOG(LogPlumPoleAudio, Verbose,
            TEXT("%s world=%.4f timeline=%d cueTime=%.4f animTime=%.4f event=%u emitter=%d loc=(%.1f,%.1f,%.1f) surface=%s playing=%u"),
            *Character->GetName(), WorldTime, int(Timeline), Cue.Time, ReportedAnimTime, Cue.Event, int(Cue.Emitter),
            Location.X, Location.Y, Location.Z, SurfaceName.IsEmpty() ? TEXT("-") : *SurfaceName, Playing);
        if (Playing == AK_INVALID_PLAYING_ID)
        {
            UE_LOG(LogPlumPoleAudio, Error, TEXT("Wwise rejected event %u"), Cue.Event);
            return false;
        }
        return true;
    }

    void FlushAudio()
    {
        AK::SoundEngine::RenderAudio();
    }

    bool IsEmpty() const { return Emitters.IsEmpty(); }

private:
    static void ReleaseGameObjects(AkGameObjectID First)
    {
        for (uint8 Slot = 0; Slot < static_cast<uint8>(EEmitter::Count); ++Slot)
        {
            AK::SoundEngine::StopAll(First + Slot);
            AK::SoundEngine::UnregisterGameObj(First + Slot);
        }
    }

    static void UpdateListenerPosition(const ACharacter& Character, AkGameObjectID First)
    {
        const UWorld* World = Character.GetWorld();
        const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
        if (Controller)
        {
            FVector Location;
            FRotator Rotation;
            Controller->GetPlayerViewPoint(Location, Rotation);
            SetPosition(First + static_cast<uint8>(EEmitter::Listener), Location, Rotation.Quaternion());
        }
    }

    bool Tick(float)
    {
        TArray<TWeakObjectPtr<ACharacter>> StaleKeys;
        for (const auto& Pair : Emitters)
        {
            const ACharacter* Character = Pair.Key.Get();
            if (!Character || !Character->GetWorld())
            {
                StaleKeys.Add(Pair.Key);
                continue;
            }
            UpdateListenerPosition(*Character, Pair.Value);
            for (uint8 Slot = 0; Slot < static_cast<uint8>(EEmitter::Listener); ++Slot)
                SetPosition(Pair.Value + Slot, GetEmitterLocation(*Character, static_cast<EEmitter>(Slot)), Character->GetActorQuat());
        }
        for (const TWeakObjectPtr<ACharacter>& Stale : StaleKeys)
        {
            AkGameObjectID First;
            if (Emitters.RemoveAndCopyValue(Stale, First))
            {
                ReleaseGameObjects(First);
            }
        }
        AK::SoundEngine::RenderAudio();
        return true;
    }

    FTSTicker::FDelegateHandle TickHandle;
    TMap<TWeakObjectPtr<ACharacter>, AkGameObjectID> Emitters;
    AkGameObjectID NextId = 1;
    // SetMedia borrows these buffers for the lifetime of the sound engine.
    TArray<TArray<uint8>> MediaBuffers;
};
TUniquePtr<FPoleSoundEngine> Engine;
}
#endif

void PlumPoleAudio::Register(ACharacter* Character)
{
#if WITH_POLE_WWISE
    UWorld* World = Character ? Character->GetWorld() : nullptr;
    if (!World || !World->AllowAudioPlayback() || !GEngine || !GEngine->UseSound()) return;
    if (!Engine)
    {
        auto NewEngine = MakeUnique<FPoleSoundEngine>();
        if (!NewEngine->Init()) return;
        Engine = MoveTemp(NewEngine);
    }
    Engine->Add(Character);
#endif
}

void PlumPoleAudio::Unregister(ACharacter* Character)
{
#if WITH_POLE_WWISE
    if (!Engine) return;
    Engine->Remove(Character);
    if (Engine->IsEmpty()) Engine.Reset();
#endif
}

void PlumPoleAudio::Advance(ACharacter* Character, EPlumPoleTimeline Timeline, float From, float To, bool bInitialSkip)
{
#if WITH_POLE_WWISE
    if (!Engine || !Character) return;
    bool bPlayedAny = false;
    for (const FCue& Cue : GetCues(Timeline))
    {
        if (Cue.Time > From && Cue.Time <= To && (!bInitialSkip || Cue.bAllowOnStartSkip))
        {
            bPlayedAny |= Engine->Play(Character, Timeline, Cue, To);
        }
    }
    // Flush immediately on cue frames so Wwise starts synthesizing on this frame rather than next tick's FTSTicker.
    if (bPlayedAny)
    {
        Engine->FlushAudio();
    }
#endif
}

void PlumPoleAudio::BlendOut(ACharacter* Character, EPlumPoleTimeline Timeline, float From, float BlendDuration, float RemainingFrameTime)
{
#if WITH_POLE_WWISE
    if (!Engine || !Character || !Character->GetWorld() || BlendDuration <= 0.0f) return;
    bool bPlayedAny = false;
    // Outgoing sequences continue at 1x. Their foot notifies can fall just after a combo handoff.
    for (const FCue& Cue : GetCues(Timeline))
    {
        const float Delay = Cue.Time - From;
        if (Cue.Event != Footstep || Delay <= 0.0f || Delay >= BlendDuration) continue;
        const float Remaining = Delay - FMath::Max(0.0f, RemainingFrameTime);
        if (Remaining <= 0.0f)
        {
            bPlayedAny |= Engine->Play(Character, Timeline, Cue, From + Delay);
        }
        else
        {
            FTimerHandle Handle;
            Character->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Character, [Character, Timeline, Cue]
            {
                if (Engine && Engine->Play(Character, Timeline, Cue, Cue.Time))
                {
                    Engine->FlushAudio();
                }
            }), Remaining, false);
        }
    }
    if (bPlayedAny)
    {
        Engine->FlushAudio();
    }
#endif
}

void PlumPoleAudio::PlayFootstep(ACharacter* Character, EPlumPoleTimeline Timeline, bool bLeftFoot, float AnimTime)
{
#if WITH_POLE_WWISE
    if (!Engine || !Character) return;
    const FCue Cue{AnimTime, Footstep, bLeftFoot ? EEmitter::LeftFoot : EEmitter::RightFoot, false};
    if (Engine->Play(Character, Timeline, Cue, AnimTime))
    {
        Engine->FlushAudio();
    }
#endif
}
