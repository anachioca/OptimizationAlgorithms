#ifndef INSTANCE_GENERATOR_HPP
#define INSTANCE_GENERATOR_HPP

#include "RectanglePacking.hpp"
#include <random>

class InstanceGenerator {
public:
    static std::vector<Rectangle> generate(int numRects, int minW, int maxW, int minH, int maxH) {
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
