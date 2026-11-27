#pragma once

#include "CoreMinimal.h"

class ACharacter;
enum class EPlumPoleTimeline : uint8;

namespace PlumPoleAudio
{
    void Register(ACharacter* Character);
    void Unregister(ACharacter* Character);
    void Advance(ACharacter* Character, EPlumPoleTimeline Timeline, float From, float To, bool bInitialSkip = false);
    void BlendOut(ACharacter* Character, EPlumPoleTimeline Timeline, float From, float BlendDuration, float RemainingFrameTime);
    void PlayFootstep(ACharacter* Character, EPlumPoleTimeline Timeline, bool bLeftFoot, float AnimTime);
}
