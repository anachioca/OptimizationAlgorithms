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
    std::mt19937 gen;

public:
    OverlapNeighborhood(double overlap) : maxOverlapPercent(overlap), gen(std::random_device{}()) {}

    std::unique_ptr<PackingSolution> findBetterNeighbor(const PackingSolution& current) override {
        double currentObj = current.objectiveValue();
        int numBoxes = static_cast<int>(current.boxes.size());
        if (numBoxes == 0) return nullptr;

        std::uniform_int_distribution<> boxDis(0, numBoxes - 1);
        
        // Try random moves with allowed overlap
        int attempts = 500;
        for (int k = 0; k < attempts; ++k) {
            int b1 = boxDis(gen);
            if (current.boxes[b1].rectangles.empty()) continue;

            std::uniform_int_distribution<> rectDis(0, current.boxes[b1].rectangles.size() - 1);
            int rIdx = rectDis(gen);

            int b2 = boxDis(gen);
            // Even if b1 == b2, we might want to move it within the box to reduce violation
            
            PackingSolution neighbor = current;
            neighbor.maxOverlapAllowed = maxOverlapPercent; // Sync threshold

            Rectangle rect = neighbor.boxes[b1].rectangles[rIdx];
            neighbor.boxes[b1].rectangles.erase(neighbor.boxes[b1].rectangles.begin() + rIdx);

            bool boxRemoved = false;
            if (neighbor.boxes[b1].rectangles.empty()) {
                neighbor.boxes.erase(neighbor.boxes.begin() + b1);
                boxRemoved = true;
                if (b2 >= (int)neighbor.boxes.size() && !neighbor.boxes.empty()) b2 = 0;
            }

            if (neighbor.boxes.empty()) { // Should not happen if we have rects
                 Box newBox(neighbor.L);
                 neighbor.boxes.push_back(newBox);
                 b2 = 0;
            }

            bool placed = false;
            rect.rotated = false;
            if (neighbor.boxes[b2].addRectangle(rect, maxOverlapPercent)) placed = true;
            else {
                rect.rotated = true;
                if (neighbor.boxes[b2].addRectangle(rect, maxOverlapPercent)) placed = true;
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
