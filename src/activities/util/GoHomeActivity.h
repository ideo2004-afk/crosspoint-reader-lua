#include "../Activity.h"
#include "GfxRenderer.h"

// Defined in main.cpp
extern void onGoHome();

// Proxy activity that facilitates a clean, ghost-free theme transition
class GoHomeActivity final : public Activity {
public:
  explicit GoHomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("GoHome", renderer, mappedInput) {}

  void onEnter() override {
    // Stage 3: Wipe the screen with a clean white flash
    renderer.clearScreen(Color::White);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }

  void loop() override {
    // Stage 4: Tell main.cpp to recreate the HomeActivity
    onGoHome();
  }
};
