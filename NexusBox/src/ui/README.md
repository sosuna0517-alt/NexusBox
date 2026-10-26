# UI modules

- `Dashboard.h/.cpp` — 10-foot shell: boot splash, cover-flow grid library,
  game detail, settings, and the in-game Quick-Access Overlay (L3+R3). Gamepad
  navigation via ImGui's NavEnableGamepad; 28pt base font for couch legibility.
- `Scraper.h/.cpp` — gamelist.xml (EmulationStation format) and gamelist.json
  readers, extension->system/core mapping, async box-art/metadata scrape.
- `ImguiBackend.h` — interface the Renderer implements to host ImGui.

## Renderer additions required (interface contract)

```cpp
// In Renderer (implements nb::IImGuiHost):
void   InitImGuiBackend();              // ImGui_ImplWin32_Init + ImGui_ImplDX12_Init
void   RenderImGui(ImDrawData* data);   // between Blit and Present in the queue
void*  TextureId(const std::string& key);  // "media:<path>" -> SRV GPU handle
```

The overlay and the dashboard share one ImGui context; when a core runs, the
dashboard's Update is called with the overlay screen instead of the library, and
the draw data is composited over the blit result in the same frame — no second
swapchain, no extra present.
