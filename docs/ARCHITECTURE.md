# NexusBox Architecture

## Threads
| Thread | Priority | Role |
|---|---|---|
| UI (CoreApplication) | Normal | Window events, dashboard/overlay, `CoreEngine::Tick` |
| Emulation | TIME_CRITICAL, core 1 | `retro_run`, input snapshot, rewind capture, blit queue |
| Input poller | HIGHEST | 1kHz `Gamepad::GetCurrentReading` -> seqlock state |
| XAudio2 | (system) | Drains the SPSC ring via voice callbacks |
| Save worker | Normal | LZ77 compress (pure C++), disk IO, auto-save |

No locks on the hot path: emulation <-> UI uses seqlocks and fences only.

## Frame flow (per tick)
1. Input snapshot (seqlock read) -> `retro_input_state_t` callbacks.
2. Optional rewind capture (memcpy into staging; compress on save worker).
3. `retro_run()` — the core may push audio samples (ring) and video (buffer).
4. Netplay `OnFrame` (rollback resim if a remote input arrived late).
5. `Renderer::Blit` — upload, scale pass, post chain, fence signal.
6. Drift-free pacing to target FPS (no burst catch-up).

## Memory budget
Xbox Dev Mode grants a large commit but terminates on overrun. `MemoryBudget`
watches `AppMemoryReport`; above 85% the rewind ring and shader PSO cache shed
resources before the sandbox does it for us.
