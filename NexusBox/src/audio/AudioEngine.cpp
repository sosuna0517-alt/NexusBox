// AudioEngine.cpp
#include "AudioEngine.h"
#include <algorithm>
#include <cstring>

namespace nb
{

void AudioEngine::Initialize(uint32_t sampleRate, uint32_t channels)
{
    m_rate = sampleRate; m_channels = channels;
    UINT flags = 0;
#ifndef NDEBUG
    flags |= XAUDIO2_DEBUG_ENGINE;
#endif
    XAudio2Create(m_xaudio.ReleaseAndGetAddressOf(), flags);
    // 5.1/7.1 passthrough when the core supplies multichannel; default stereo.
    m_xaudio->CreateMasteringVoice(&m_master, m_channels, m_rate);

    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = (WORD)m_channels;
    wf.nSamplesPerSec = m_rate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = (WORD)(wf.nChannels * 2);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

    m_cb = std::make_unique<VoiceCallback>();
    m_cb->owner = this;

    XAUDIO2_VOICE_SENDS sends{};
    m_xaudio->CreateSourceVoice(&m_voice, &wf, XAUDIO2_VOICE_NOPITCH | XAUDIO2_VOICE_NOSRC,
        XAUDIO2_DEFAULT_FREQ_RATIO, m_cb.get(), &sends, nullptr);
    // XAUDIO2_VOICE_NOSRC: no resampling on the voice — cores are rate-matched
    // upstream in CoreEngine, keeping the path bit-exact and latency-free.

    m_voice->Start();
}

void AudioEngine::Configure(uint32_t rate, uint32_t channels)
{
    if (m_voice) m_voice->Stop();
    if (m_voice) m_voice->DestroyVoice();
    if (m_master) m_master->DestroyVoice();
    m_ring.Reset();
    Initialize(rate, channels);
}

size_t AudioEngine::WriteSamples(const int16_t* samples, size_t count)
{
    // Drop policy: prefer a clean underrun (brief silence) over latency buildup.
    return m_ring.Push(samples, count) ? count : 0;
}

void AudioEngine::Refill()
{
    if (m_suspended) return;
    // Submit a buffer sized to hit the target latency without underrunning.
    const size_t frames = m_ring.Available() / m_channels;
    if (!frames) return;
    const size_t toRead = std::min<size_t>(frames, kRingFrames);
    const size_t got = m_ring.Pop((int16_t*)m_refillBuf.data(), toRead * m_channels);
    if (!got) return;

    XAUDIO2_BUFFER buf{};
    buf.AudioBytes = (UINT32)(got * sizeof(int16_t));
    buf.pAudioData = m_refillBuf.data();
    buf.Flags = XAUDIO2_END_OF_STREAM;
    m_voice->SubmitSourceBuffer(&buf);
}

void AudioEngine::SuspendVoice() { m_suspended = true; if (m_voice) m_voice->Stop(); }
void AudioEngine::ResumeVoice()  { m_suspended = false; if (m_voice) m_voice->Start(); }

void AudioEngine::Shutdown()
{
    if (m_voice)  { m_voice->DestroyVoice();  m_voice = nullptr; }
    if (m_master) { m_master->DestroyVoice(); m_master = nullptr; }
    if (m_xaudio) m_xaudio.Reset();
}

} // namespace nb
