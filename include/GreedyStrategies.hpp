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

// Helper placement function for Greedy
inline void placePacking(PackingSolution& sol, Rectangle rect) {
    sol.placeRectangle(rect);
}

#endif // GREEDY_STRATEGIES_HPP