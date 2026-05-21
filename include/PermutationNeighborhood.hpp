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

        // Picks two rectangles to swap, generates the neighbor, and returns it if better. Otherwise backtracks and tries another pair.
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
 * Randomized rule-based neighborhood (Tuned).
 *
 * Moves are 2-swap or 1-insert (chosen randomly per attempt). Move selection is
 * biased toward rectangles that landed in the sparsest box of the current
 * packing — the spec's explicit hint about "Rechtecke in relativ leeren Boxen
 * anderswo in der Permutation zu platzieren". Each attempt is therefore much
 * more likely to actually improve the score than a uniformly random swap, so
 * we can afford a much lower attempt budget and still beat the old version.
 */
class RandomizedSwapNeighborhood : public Neighborhood<PermutationSolution> {
private:
    std::mt19937 gen;

    // One pass of the packing: returns score, plus which box each rectangle id
    // landed in. Used to identify rectangles in the sparsest box.
    struct PackingInfo {
        double score;
        std::vector<int> boxOfRect;   // boxOfRect[id] = box index (-1 if unplaced)
        int sparsestBox = -1;
    };

    static PackingInfo analyze(const PermutationSolution& s) {
        PackingSolution packed(s.L);
        for (const auto& r : s.permutation) packed.placeRectangle(r);

        int maxId = 0;
        for (const auto& r : s.permutation) maxId = std::max(maxId, r.id);

        PackingInfo info;
        info.boxOfRect.assign(maxId + 1, -1);

        double density = 0;
        double minFill = 2.0;
        for (size_t b = 0; b < packed.boxes.size(); ++b) {
            double occupied = 0;
            for (const auto& r : packed.boxes[b].rectangles) {
                occupied += r.area();
                if (r.id >= 0 && r.id <= maxId) info.boxOfRect[r.id] = (int)b;
            }
            double fill = occupied / (double)(packed.L * packed.L);
            density += fill * fill;
            if (fill < minFill) { minFill = fill; info.sparsestBox = (int)b; }
        }

        info.score = static_cast<double>(packed.boxes.size())
                     - density * 0.1
                     + packed.unplacedRectangles.size() * 1000.0;
        return info;
    }

public:
    RandomizedSwapNeighborhood() : gen(std::random_device{}()) {}

    std::unique_ptr<PermutationSolution> findBetterNeighbor(const PermutationSolution& current) override {
        PackingInfo info = analyze(current);
        int n = static_cast<int>(current.permutation.size());
        if (n < 2) return nullptr;

        // Permutation positions of rectangles that landed in the sparsest box —
        // prime candidates to move elsewhere in the permutation.
        std::vector<int> sparsePositions;
        if (info.sparsestBox >= 0) {
            for (int p = 0; p < n; ++p) {
                int id = current.permutation[p].id;
                if (id >= 0 && id < (int)info.boxOfRect.size()
                        && info.boxOfRect[id] == info.sparsestBox) {
                    sparsePositions.push_back(p);
                }
            }
        }

        // Lower attempt cap than before: each attempt is more focused, so we
        // don't need 1000 random tries per step.
        int maxAttempts = std::min(150, 40 + n / 10);
        std::uniform_int_distribution<> pos(0, n - 1);
        std::uniform_real_distribution<> uniform01(0.0, 1.0);

        for (int k = 0; k < maxAttempts; ++k) {
            // Bias the source position: 70% chance to pick from the sparsest
            // box (when one exists), else uniform.
            int i;
            if (!sparsePositions.empty() && uniform01(gen) < 0.7) {
                i = sparsePositions[std::uniform_int_distribution<>(0, (int)sparsePositions.size() - 1)(gen)];
            } else {
                i = pos(gen);
            }
            int j = pos(gen);
            if (i == j) continue;

            auto tempPerm = current.permutation;

            // Half swap, half insert. Swap is local in permutation distance;
            // insert can pull a rectangle a long way without disturbing the rest.
            if (uniform01(gen) < 0.5) {
                std::swap(tempPerm[i], tempPerm[j]);
            } else {
                Rectangle r = tempPerm[i];
                tempPerm.erase(tempPerm.begin() + i);
                int insertPos = (j > i) ? j - 1 : j;
                tempPerm.insert(tempPerm.begin() + insertPos, r);
            }

            PermutationSolution neighbor(std::move(tempPerm), current.L);
            if (permutationScore(neighbor) < info.score) {
                return std::make_unique<PermutationSolution>(neighbor);
            }
        }
        return nullptr;
    }
};

#endif // PERMUTATION_NEIGHBORHOOD_HPP
