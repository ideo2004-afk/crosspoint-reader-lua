#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <vector>
#include "MiniGoEngine.h"

using namespace std;

// Global test params to avoid changing the .h interface
int g_blackDepth = 8;
int g_whiteDepth = 8;

int main() {
    int boardSize = 9;
    int totalGames = 10; 
    
    // TEST 1: D6
    g_blackDepth = 6;
    g_whiteDepth = 8;
    
    int blackWins = 0;
    int whiteWins = 0;
    
    cout << "DEPTH EXPERIMENT 1: Black D6 (500) vs White D8 (5000)" << endl;
    cout << "--------------------------------------------" << endl;

    for (int gameIdx = 1; gameIdx <= totalGames; gameIdx++) {
        srand(chrono::high_resolution_clock::now().time_since_epoch().count());
        MiniGoEngine game(boardSize);
        MiniGoEngine::Color toMove = MiniGoEngine::BLACK; 
        int moveCount = 0;
        
        while (!game.isGameOver() && moveCount < 160) {
            // How to apply depth? I will modify simulate() to read a global.
            int sims = (toMove == MiniGoEngine::BLACK) ? 500 : 5000;
            game.startMCTS(toMove);
            game.runMCTSSteps(sims);
            MiniGoEngine::Move m = game.finishMCTS();
            game.makeMove(m, toMove);
            toMove = (toMove == MiniGoEngine::BLACK) ? MiniGoEngine::WHITE : MiniGoEngine::BLACK;
            moveCount++;
        }
        
        if (game.calculateScore(MiniGoEngine::BLACK) > game.calculateScore(MiniGoEngine::WHITE)) blackWins++;
        else whiteWins++;
        cout << "Game " << gameIdx << " finished." << endl;
    }
    cout << "D6 RESULT: Black " << blackWins << " - White " << whiteWins << endl << endl;

    // TEST 2: D10
    g_blackDepth = 10;
    blackWins = 0; whiteWins = 0;
    cout << "DEPTH EXPERIMENT 2: Black D10 (500) vs White D8 (5000)" << endl;
    cout << "--------------------------------------------" << endl;

    for (int gameIdx = 1; gameIdx <= totalGames; gameIdx++) {
        srand(chrono::high_resolution_clock::now().time_since_epoch().count());
        MiniGoEngine game(boardSize);
        MiniGoEngine::Color toMove = MiniGoEngine::BLACK; 
        int moveCount = 0;
        
        while (!game.isGameOver() && moveCount < 160) {
            game.startMCTS(toMove);
            game.runMCTSSteps((toMove == MiniGoEngine::BLACK) ? 500 : 5000);
            MiniGoEngine::Move m = game.finishMCTS();
            game.makeMove(m, toMove);
            toMove = (toMove == MiniGoEngine::BLACK) ? MiniGoEngine::WHITE : MiniGoEngine::BLACK;
            moveCount++;
        }
        
        if (game.calculateScore(MiniGoEngine::BLACK) > game.calculateScore(MiniGoEngine::WHITE)) blackWins++;
        else whiteWins++;
        cout << "Game " << gameIdx << " finished." << endl;
    }
    cout << "D10 RESULT: Black " << blackWins << " - White " << whiteWins << endl;

    return 0;
}
