#pragma once

#include <vector>
#include <cstdint>
#include <memory>

class MiniGoEngine {
public:
    enum Color : int8_t { EMPTY = 0, BLACK = 1, WHITE = 2 };
    
    struct Move {
        int x, y;
        bool pass;
        bool resign;
        
        static Move Play(int x, int y) { return {x, y, false, false}; }
        static Move Pass() { return {0, 0, true, false}; }
        static Move Resign() { return {0, 0, false, true}; }
        
        bool operator==(const Move& other) const {
            return x == other.x && y == other.y && pass == other.pass && resign == other.resign;
        }
    };

    MiniGoEngine(int size = 9);
    void reset(int size = -1);
    
    int getSize() const { return size; }
    Color getAt(int x, int y) const;
    bool makeMove(Move move, Color player);
    bool isFirstMove() const;
    
    // Non-blocking MCTS
    void startMCTS(Color aiColor);
    void runMCTSSteps(int count);
    Move finishMCTS();
    
    // Scoring
    float calculateScore(Color player) const; // Area scoring
    float calculateInfluence() const; // For debugging
    
    bool isGameOver() const { return consecutivePasses >= 2; }
    Color getWinner() const;

    // Rules
    bool isValidMove(Move move, Color player) const;

private:
    int size;
    std::vector<Color> board;
    std::vector<Color> lastBoard; // For Simple Ko
    int consecutivePasses = 0;

    struct Group {
        int stones[81];
        int stoneCount;
        int liberties;
    };

    Group getGroup(int x, int y, const Color* currentBoard) const;
    bool wouldBeSuicide(int x, int y, Color player, const Color* currentBoard) const;
    int captureStones(int x, int y, Color opponent, Color* targetBoard) const;
    bool isEye(int x, int y, Color color, const Color* currentBoard) const;
    float calculateInfluence(const Color* currentBoard) const;
    int countLiberties(int x, int y, const Color* currentBoard) const;
    bool shouldStopEarly() const;
    
    // Performance Buffers (Static to class to avoid allocation)
    // removed testBoard, stackBuf, visitedBuf

    // MCTS
    struct Node {
        Move move;
        int visits = 0;
        float wins = 0;
        std::vector<std::unique_ptr<Node>> children;
        Node* parent;
        Color playerToMove;
        
        Node(Move m, Node* p, Color nextPlayer) : move(m), parent(p), playerToMove(nextPlayer) {}
    };

    float simulate(Color* simBoard, Color toMove, Color aiColor);
    void backpropagate(Node* node, float result);
    Node* selectBestChild(Node* node, float exploration);

    std::unique_ptr<Node> mctsRoot;
    Color mctsAiColor;
};
