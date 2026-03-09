#pragma once

#include "../Activity.h"
#include "MappedInputManager.h"
#include "MiniGoEngine.h"

class MiniGoActivity final : public Activity {
 public:
  MiniGoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> onGoBack);

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  std::function<void()> onGoBack;
  MiniGoEngine engine;
  
  int cursorX = 0;
  int cursorY = 0;
  int boardSize = 7;
  int selectedBoardSize = 7;
  
  bool isAiThinking = false;
  unsigned long aiThinkStartTime = 0;
  
  enum GameStatus { Playing, Won, Lost, Draw };
  GameStatus status = Playing;

  MiniGoEngine::Move lastMove = { -1, -1, false, false };
  int postGameMenuIndex = 0;

  bool inEscMenu = false;
  int escMenuIndex = 0; // 0: Resume, 1: size 5, 2: size 7, 3: size 9, 4: Pass, 5: Exit

  MiniGoEngine::Color playerColor = MiniGoEngine::WHITE;
  MiniGoEngine::Color aiColor = MiniGoEngine::BLACK;
  int aiSimulationsDone = 0;

  int aiMumbleIndex = -1;
  int playerMumbleIndex = -1;
  unsigned long lastMumbleChangeTime = 0;

  bool showSizeSelection = true;
  int sizeSelectionIndex = 0; // 0: 7x7, 1: 9x9
  bool showHandicapSelection = false;
  int handicapSelectionIndex = 0; // 0: None, 1: 2, 2: 3, 3: 4, 4: Chaos
  int handicapCount = 0;

  float blackScoreCache = 0;
  float whiteScoreCache = 0;
  bool resultsCached = false;

  void renderBoard(bool fullRefresh = false, HalDisplay::RefreshMode refreshMode = HalDisplay::FAST_REFRESH);
  void renderEscMenu();
  void renderSizeSelection();
  void renderHandicapSelection();
  bool handleInput();
  void resetGame(int size, bool skipSizeSelection);
  void makeAiMove();
};
