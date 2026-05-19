#ifndef INSTANCE_GENERATOR_HPP
#define INSTANCE_GENERATOR_HPP

#include "RectanglePacking.hpp"
#include <random>

class InstanceGenerator {
public:
    static std::vector<Rectangle> generate(int numRects, int minW, int maxW, int minH, int maxH) {
        // Sanitise: uniform_int_distribution is UB (crash) when min > max
        numRects = std::max(1, numRects);
        minW = std::max(1, minW);  maxW = std::max(minW, maxW);
        minH = std::max(1, minH);  maxH = std::max(minH, maxH);

        std::vector<Rectangle> rects;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> disW(minW, maxW);
        std::uniform_int_distribution<> disH(minH, maxH);

        for (int i = 0; i < numRects; ++i) {
            rects.emplace_back(i, disW(gen), disH(gen));
        }
        return rects;
    }
};

#endif // INSTANCE_GENERATOR_HPP
