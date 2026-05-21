#ifndef OVERLAP_NEIGHBORHOOD_HPP
#define OVERLAP_NEIGHBORHOOD_HPP

#include "RectanglePacking.hpp"
#include <random>

/**
 * Neighborhood that allows partial overlaps.
 */
class OverlapNeighborhood : public Neighborhood<PackingSolution> {
private:
    double maxOverlapPercent;
    int maxAttempts;
    std::mt19937 gen;

public:
    OverlapNeighborhood(double overlap, int attempts = 500)
        : maxOverlapPercent(overlap), maxAttempts(attempts), gen(std::random_device{}()) {}

    void setMaxOverlap(double overlap) {
        maxOverlapPercent = overlap;
    }

    double getMaxOverlap() const {
        return maxOverlapPercent;
    }

    // Search-side scoring: counts boxes, hard-penalises overlap above the current
    // tolerance, and rewards dense boxes so plateau moves remain visible.
    double score(const PackingSolution& sol) const {
        double penalty = 0;
        double density = 0;
        for (const auto& box : sol.boxes) {
            penalty += box.getOverlapViolation(maxOverlapPercent);
            double occupied = 0;
            for (const auto& r : box.rectangles) occupied += r.area();
            double fill = occupied / (double)(sol.L * sol.L);
            density += fill * fill;
        }
        return static_cast<double>(sol.boxes.size())
               - density * 0.1
               + sol.unplacedRectangles.size() * 1000.0
               + penalty * 5000.0;
    }

    std::unique_ptr<PackingSolution> findBetterNeighbor(const PackingSolution& current) override {
        double currentScore = score(current);
        int numBoxes = static_cast<int>(current.boxes.size());
        if (numBoxes == 0) return nullptr;

        std::uniform_int_distribution<> boxDis(0, numBoxes - 1);  // For selecting random boxes

        // Similar to RandomizedGeometryNeighborhood but with relaxed placement allowing overlaps up to maxOverlapPercent.
        for (int k = 0; k < maxAttempts; ++k) {
            int b1 = boxDis(gen);
            if (current.boxes[b1].rectangles.empty()) continue;

            std::uniform_int_distribution<> rectDis(0, current.boxes[b1].rectangles.size() - 1);
            int rIdx = rectDis(gen);

            PackingSolution neighbor = current;

            Rectangle rect = neighbor.boxes[b1].rectangles[rIdx];
            neighbor.boxes[b1].rectangles.erase(neighbor.boxes[b1].rectangles.begin() + rIdx);

            if (neighbor.boxes[b1].rectangles.empty()) {
                neighbor.boxes.erase(neighbor.boxes.begin() + b1);
            }

            // Ensure there's at least one box to try placing into
            if (neighbor.boxes.empty()) {
                 Box newBox(neighbor.L);
                 neighbor.boxes.push_back(newBox);
            }

            // Try to place in a random box b2
            int b2 = std::uniform_int_distribution<>(0, neighbor.boxes.size() - 1)(gen);
            bool placed = false;

            rect.rotated = false;
            if (neighbor.boxes[b2].addRectangle(rect, maxOverlapPercent)) placed = true;
            else {
                rect.rotated = true;
                if (neighbor.boxes[b2].addRectangle(rect, maxOverlapPercent)) placed = true;
            }

            // If it couldn't be placed in a random box, try a new box
            // resolves overlaps as the threshold cools.
            if (!placed) {
                Box newBox(neighbor.L);
                rect.rotated = false;
                if (newBox.addRectangle(rect, maxOverlapPercent)) {
                    neighbor.boxes.push_back(newBox);
                    placed = true;
                }
            }

            if (placed && score(neighbor) < currentScore) {
                return std::make_unique<PackingSolution>(std::move(neighbor));
            }
        }
        return nullptr;
    }
};

#endif // OVERLAP_NEIGHBORHOOD_HPP
