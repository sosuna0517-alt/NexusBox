# NexusBox 10-foot UI design notes

## Principles
- **Couch-first**: 28pt base text, 420x590 box-art tiles, 36px gaps. Everything
  reachable with D-pad + A/B, no pointer required.
- **One queue**: dashboard, overlay, and game blit share a single D3D12 command
  queue and swapchain. Screen transitions are instant.
- **Never block the emulation thread**: the UI runs on the CoreWindow thread;
  cross-thread reads use seqlocks (input) and the profiler's ring buffer.

## Screens
| Screen | Entry | Exit |
|---|---|---|
| Boot | App start | A |
| Library | default | A on tile -> GameDetail/Play |
| Game Detail | A on tile | B -> Library |
| Settings | Menu button | B -> Library |
| Overlay | L3+R3 (or B in game) | L3+R3 / Resume |

## Overlay features
Save/load state slots, rewind toggle, fast-forward slider (1x-8x), live
frametime/FPS/memory/latency readout, shader picker (wired to
`Renderer::SetPostChain`), core options (plumbed via retro_environment).

## Data model
Library entries are filled by `StorageManager::ScanLibrary` (filesystem walk),
enriched by `Scraper` (gamelist.xml/json + box art cache). The dashboard owns
the list; persistence of playtime/stats is queued to the save worker.
