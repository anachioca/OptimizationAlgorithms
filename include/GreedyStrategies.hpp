#ifndef GREEDY_STRATEGIES_HPP
#define GREEDY_STRATEGIES_HPP

#include "RectanglePacking.hpp"
#include <algorithm>

class AreaStrategy : public SelectionStrategy<Rectangle> {
public:
    void sort(std::vector<Rectangle>& elements) override {
        std::sort(elements.begin(), elements.end(), [](const Rectangle& a, const Rectangle& b) {
            return a.area() > b.area();
        });
    }
};

class MaxSideStrategy : public SelectionStrategy<Rectangle> {
public:
    void sort(std::vector<Rectangle>& elements) override {
        std::sort(elements.begin(), elements.end(), [](const Rectangle& a, const Rectangle& b) {
            return std::max(a.width, a.height) > std::max(b.width, b.height);
        });
    }
};

class SmallestFirstStrategy : public SelectionStrategy<Rectangle> {
public:
    void sort(std::vector<Rectangle>& elements) override {
        std::sort(elements.begin(), elements.end(), [](const Rectangle& a, const Rectangle& b) {
            return a.area() < b.area();
        });
    }
};

/**
 * Strategy to create a decidedly bad start solution.
 */
class NoSort : public SelectionStrategy<Rectangle> {
public:
    void sort(std::vector<Rectangle>& elements) override {
        // Just leave them in their original order
    }
};

// Helper placement function for Greedy
inline void placePacking(PackingSolution& sol, Rectangle rect) {
    sol.placeRectangle(rect);
}

// very bad placement: every rectangle gets its own box, in a random order
inline void placeOnePerBox(PackingSolution& sol, Rectangle rect) {
    Box newBox(sol.L);
    rect.x = 0;
    rect.y = 0;
    rect.rotated = false;
    newBox.addRectangle(rect);
    sol.boxes.push_back(newBox);
}

#endif // GREEDY_STRATEGIES_HPP