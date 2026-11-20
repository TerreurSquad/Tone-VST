#include "Lane.h"

#include <algorithm>

namespace xt
{
void Lane::trigger (const HitStart& h, int limit, double stealFade, bool cut) noexcept
{
    limit = std::clamp (limit, 1, kMaxVoices - 1);

    // displace the oldest playing voices until there is room
    if (cut)
        for (auto& v : voices) v.release (stealFade);

    for (int nonRel = cut ? 0 : nonReleasingCount(); nonRel >= limit; --nonRel)
    {
        HitVoice* oldest = nullptr;
        for (auto& v : voices)
            if (v.isActive() && ! v.isReleasing() && (oldest == nullptr || v.getStamp() < oldest->getStamp()))
                oldest = &v;
        if (oldest == nullptr) break;
        oldest->release (stealFade);
    }

    // free slot among the first limit+1 slots, otherwise kill the oldest fading voice
    const int slots = limit + 1;
    HitVoice* slot = nullptr;
    for (int i = 0; i < slots && slot == nullptr; ++i)
        if (! voices[i].isActive()) slot = &voices[i];
    if (slot == nullptr)
    {
        for (int i = 0; i < slots; ++i)
            if (slot == nullptr || voices[i].getStamp() < slot->getStamp()) slot = &voices[i];
        // Also clear voices beyond `slots` (left over after a lower limit was set)
    }
    for (int i = slots; i < kMaxVoices; ++i)
        voices[i].release (stealFade);

    slot->start (h, ++stampCounter);
}

int Lane::activeCount() const noexcept
{
    int n = 0;
    for (auto& v : voices) n += v.isActive() ? 1 : 0;
    return n;
}

int Lane::nonReleasingCount() const noexcept
{
    int n = 0;
    for (auto& v : voices) n += (v.isActive() && ! v.isReleasing()) ? 1 : 0;
    return n;
}

const HitVoice* Lane::newestVoice() const noexcept
{
    const HitVoice* best = nullptr;
    for (auto& v : voices)
        if (v.isActive() && ! v.isReleasing() && (best == nullptr || v.getStamp() > best->getStamp())) best = &v;
    return best;
}
} // namespace xt
