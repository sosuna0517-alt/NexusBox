// AudioEngine.cpp
#include "AudioEngine.h"
#include <cstring>

namespace nb
{

bool AudioEngine::Initialize()
{
    if (m_xaudio) return true;
    if (FAILED(XAudio2Create(m_xaudio.ReleaseAndGetAddressOf(), 0)))
        return false;
    if (FAILED(m_xaudio->CreateMasteringVoice(&m_master)))   // device default rate/channels
    {
        m_xaudio.Reset();
        return false;
    }
    m_cb.owner = this;
    return true;
}

void AudioEngine::Configure(uint32_t sampleRate)
{
    if (!m_xaudio && !Initialize()) return;
    if (m_voice) { m_voice->Stop(); m_voice->FlushSourceBuffers(); m_voice->DestroyVoice(); m_voice = nullptr; }
    m_ring.Reset();
    m_rate = sampleRate ? sampleRate : 48000;

    WAVEFORMATEX wf{};
    wf.wFormatTag      = WAVE_FORMAT_PCM;
    wf.nChannels       = 2;
    wf.nSamplesPerSec  = m_rate;
    wf.wBitsPerSample  = 16;
    wf.nBlockAlign     = 4;
    wf.nAvgBytesPerSec = m_rate * 4;

    // XAudio2 performs sample-rate conversion to the mastering voice's rate.
    if (FAILED(m_xaudio->CreateSourceVoice(&m_voice, &wf, 0, 2.0f, &m_cb)))
    {
        m_voice = nullptr;
        return;
    }
    m_voice->Start();
}

void AudioEngine::WriteFrames(const int16_t* samples, size_t frames)
{
    if (!m_voice || m_suspended) return;
    m_ring.Push(samples, frames * 2);
}

void AudioEngine::Pump()
{
    if (!m_voice || m_suspended) return;
    XAUDIO2_VOICE_STATE st{};
    m_voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    while (st.BuffersQueued < kBuffers)
    {
        auto& chunk = m_chunks[m_nextChunk];
        size_t got = m_ring.Pop(chunk.data(), chunk.size());
        if (got < 2)
        {
            // Underrun: queue a short silent chunk so the voice keeps calling us.
            if (st.BuffersQueued > 0) break;
            std::memset(chunk.data(), 0, 256 * 4);
            got = 256 * 2;
        }
        got &= ~size_t(1);   // whole stereo frames only
        XAUDIO2_BUFFER buf{};
        buf.AudioBytes = (UINT32)(got * sizeof(int16_t));
        buf.pAudioData = (const BYTE*)chunk.data();
        if (FAILED(m_voice->SubmitSourceBuffer(&buf))) break;
        m_nextChunk = (m_nextChunk + 1) % kBuffers;
        ++st.BuffersQueued;
    }
}

void AudioEngine::SuspendVoice() { m_suspended = true;  if (m_voice) m_voice->Stop(); }
void AudioEngine::ResumeVoice()  { m_suspended = false; if (m_voice) m_voice->Start(); }

void AudioEngine::Shutdown()
{
    if (m_voice)  { m_voice->DestroyVoice();  m_voice = nullptr; }
    if (m_master) { m_master->DestroyVoice(); m_master = nullptr; }
    m_xaudio.Reset();
}

} // namespace nb
