#ifndef PERMUTATION_NEIGHBORHOOD_HPP
#define PERMUTATION_NEIGHBORHOOD_HPP

#include "RectanglePacking.hpp"
#include "GreedyStrategies.hpp"
#include <random>

class PermutationSolution : public Solution {
public:
    std::vector<Rectangle> permutation;
    int L;
    mutable double cachedObjective = -1;

    PermutationSolution(std::vector<Rectangle> p, int boxSize)
        : permutation(std::move(p)), L(boxSize) {}

    // Pure problem objective: build the implied packing and report its box count.
    double objectiveValue() const override {
        if (cachedObjective != -1) return cachedObjective;
        PackingSolution sol(L);
        for (const auto& r : permutation) sol.placeRectangle(r);
        cachedObjective = sol.objectiveValue();
        return cachedObjective;
    }

    std::unique_ptr<Solution> clone() const override {
        return std::make_unique<PermutationSolution>(*this);
    }
};

// Search-side scoring shared by the permutation neighborhoods: materialise the
// implied packing and add a density bonus so plateau-equivalent permutations
// can still be ordered.
inline double permutationScore(const PermutationSolution& s) {
    PackingSolution packed(s.L);
    for (const auto& r : s.permutation) packed.placeRectangle(r);
    double density = 0;
    for (const auto& box : packed.boxes) {
        double occupied = 0;
        for (const auto& r : box.rectangles) occupied += r.area();
        double fill = occupied / (double)(packed.L * packed.L);
        density += fill * fill;
    }
    return static_cast<double>(packed.boxes.size())
           - density * 0.1
           + packed.unplacedRectangles.size() * 1000.0;
}

/**
 * Original systematic swap neighborhood.
 * Slow for large N, but guaranteed to find a better neighbor if one exists in the swap space.
 */
class SystematicSwapNeighborhood : public Neighborhood<PermutationSolution> {
public:
    std::unique_ptr<PermutationSolution> findBetterNeighbor(const PermutationSolution& current) override {
        double currentScore = permutationScore(current);
        std::vector<Rectangle> tempPerm = current.permutation;
        int n = static_cast<int>(tempPerm.size());

        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                std::swap(tempPerm[i], tempPerm[j]);

                PermutationSolution neighbor(tempPerm, current.L);
                if (permutationScore(neighbor) < currentScore) {
                    return std::make_unique<PermutationSolution>(neighbor);
                }

                std::swap(tempPerm[i], tempPerm[j]); // backtrack
            }
        }
        return nullptr;
    }
};

/**
 * Randomized swap neighborhood (Tuned).
 * Much faster for large instances, as required by the project specs.
 */
class RandomizedSwapNeighborhood : public Neighborhood<PermutationSolution> {
private:
    std::mt19937 gen;
public:
    RandomizedSwapNeighborhood() : gen(std::random_device{}()) {}

    std::unique_ptr<PermutationSolution> findBetterNeighbor(const PermutationSolution& current) override {
        double currentScore = permutationScore(current);
        int n = static_cast<int>(current.permutation.size());

        // Try up to 2*N random swaps to find an improvement
        int maxAttempts = std::min(1000, n * 2);
        std::uniform_int_distribution<> dis(0, n - 1);

        for (int k = 0; k < maxAttempts; ++k) {
            int i = dis(gen);
            int j = dis(gen);
            if (i == j) continue;

            auto tempPerm = current.permutation;
            std::swap(tempPerm[i], tempPerm[j]);

            PermutationSolution neighbor(std::move(tempPerm), current.L);
            if (permutationScore(neighbor) < currentScore) {
                return std::make_unique<PermutationSolution>(neighbor);
            }
        }
        return nullptr;
    }
};

#endif // PERMUTATION_NEIGHBORHOOD_HPP
