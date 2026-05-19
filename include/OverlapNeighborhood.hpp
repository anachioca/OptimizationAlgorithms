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

    std::unique_ptr<PackingSolution> findBetterNeighbor(const PackingSolution& current) override {
        double currentObj = current.objectiveValue();
        int numBoxes = static_cast<int>(current.boxes.size());
        if (numBoxes == 0) return nullptr;

        std::uniform_int_distribution<> boxDis(0, numBoxes - 1);

        for (int k = 0; k < maxAttempts; ++k) {
            int b1 = boxDis(gen);
            if (current.boxes[b1].rectangles.empty()) continue;

            std::uniform_int_distribution<> rectDis(0, current.boxes[b1].rectangles.size() - 1);
            int rIdx = rectDis(gen);

            PackingSolution neighbor = current;
            neighbor.maxOverlapAllowed = maxOverlapPercent; 

            Rectangle rect = neighbor.boxes[b1].rectangles[rIdx];
            neighbor.boxes[b1].rectangles.erase(neighbor.boxes[b1].rectangles.begin() + rIdx);

            if (neighbor.boxes[b1].rectangles.empty()) {
                neighbor.boxes.erase(neighbor.boxes.begin() + b1);
            }

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

            // If it couldn't be placed in a random box, try a NEW box
            // This is crucial for resolving overlaps as the threshold cools.
            if (!placed) {
                Box newBox(neighbor.L);
                rect.rotated = false;
                if (newBox.addRectangle(rect, maxOverlapPercent)) {
                    neighbor.boxes.push_back(newBox);
                    placed = true;
                }
            }

            if (placed) {
                if (neighbor.objectiveValue() < currentObj) {
                    return std::make_unique<PackingSolution>(std::move(neighbor));
                }
            }
        }
        return nullptr;
    }
};

#endif // OVERLAP_NEIGHBORHOOD_HPP
