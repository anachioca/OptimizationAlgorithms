
#ifndef GEOMETRY_NEIGHBORHOOD_HPP
#define GEOMETRY_NEIGHBORHOOD_HPP

#include "RectanglePacking.hpp"
#include <random>

// Search-side scoring shared by the geometry neighborhoods: pure box count plus a
// fill-density bonus so moves that don't immediately drop a box still register
// as progress.
inline double geometryScore(const PackingSolution& sol) {
    double density = 0;
    for (const auto& box : sol.boxes) {
        double occupied = 0;
        for (const auto& r : box.rectangles) occupied += r.area();
        double fill = occupied / (double)(sol.L * sol.L);
        density += fill * fill;
    }
    return static_cast<double>(sol.boxes.size())
           - density * 0.1
           + sol.unplacedRectangles.size() * 1000.0;
}

/**
 * Geometry-based neighborhood.
 * Moves rectangles between boxes to find a better packing.
 * Returns the first better neighbor found (first improvement).
 * Worst case: O(numBoxes^2 * avgRectsPerBox).
 */
class GeometryNeighborhood : public Neighborhood<PackingSolution> {
public:
    std::unique_ptr<PackingSolution> findBetterNeighbor(const PackingSolution& current) override {
        double currentScore = geometryScore(current);
        int numBoxes = static_cast<int>(current.boxes.size());

        // For each box and each rectangle in that box, try moving it to another box
        for (int b1 = 0; b1 < numBoxes; ++b1) {
            for (size_t rIdx = 0; rIdx < current.boxes[b1].rectangles.size(); ++rIdx) {
                // Try moving rectangle current.boxes[b1].rectangles[rIdx] to another box
                for (int b2 = 0; b2 < numBoxes; ++b2) {
                    if (b1 == b2) continue;

                    PackingSolution neighbor = current;

                    // 1. Pick the rectangle to move
                    Rectangle rect = neighbor.boxes[b1].rectangles[rIdx];
                    
                    // 2. Remove from b1
                    neighbor.boxes[b1].rectangles.erase(neighbor.boxes[b1].rectangles.begin() + rIdx);
                    
                    // 3. If b1 is now empty, remove it and adjust b2
                    int adjustedB2 = b2;
                    if (neighbor.boxes[b1].rectangles.empty()) {
                        neighbor.boxes.erase(neighbor.boxes.begin() + b1);
                        if (b2 > b1) adjustedB2--;
                    }

                    // 4. Try placing in b2 (normal and rotated)
                    bool placed = false;
                    if (adjustedB2 >= 0 && adjustedB2 < (int)neighbor.boxes.size()) {
                        rect.rotated = false;
                        if (neighbor.boxes[adjustedB2].addRectangle(rect)) {
                            placed = true;
                        } else {
                            rect.rotated = true;
                            if (neighbor.boxes[adjustedB2].addRectangle(rect)) {
                                placed = true;
                            }
                        }
                    }

                    // 5. Only return if the rectangle actually exists in the new solution
                    if (placed && geometryScore(neighbor) < currentScore) {
                        return std::make_unique<PackingSolution>(std::move(neighbor));
                    }
                }
            }
        }
        return nullptr;
    }
};

/**
 * Randomized version of GeometryNeighborhood for faster execution on large instances.
 * Worst case: O(attempts * numBoxes * avgRectsPerBox)
 */
class RandomizedGeometryNeighborhood : public Neighborhood<PackingSolution> {
private:
    std::mt19937 gen;
public:
    RandomizedGeometryNeighborhood() : gen(std::random_device{}()) {}

    std::unique_ptr<PackingSolution> findBetterNeighbor(const PackingSolution& current) override {
        double currentScore = geometryScore(current);
        int numBoxes = static_cast<int>(current.boxes.size());
        if (numBoxes == 0) return nullptr;

        std::uniform_int_distribution<> boxDis(0, numBoxes - 1);
        
        int attempts = 500; 

        // Select random rectangles and try to move them to random boxes
        for (int k = 0; k < attempts; ++k) {
            int b1 = boxDis(gen);
            if (b1 >= (int)current.boxes.size() || current.boxes[b1].rectangles.empty()) continue;

            std::uniform_int_distribution<> rectDis(0, current.boxes[b1].rectangles.size() - 1);
            int rIdx = rectDis(gen);

            int b2 = boxDis(gen);
            if (b1 == b2) continue;

            PackingSolution neighbor = current;
            Rectangle rect = neighbor.boxes[b1].rectangles[rIdx];
            
            // 1. Remove from b1
            neighbor.boxes[b1].rectangles.erase(neighbor.boxes[b1].rectangles.begin() + rIdx);
            
            // 2. If b1 is now empty, remove it and adjust b2
            int adjustedB2 = b2;
            if (neighbor.boxes[b1].rectangles.empty()) {
                neighbor.boxes.erase(neighbor.boxes.begin() + b1);
                if (b2 > b1) adjustedB2--;
            }

            // 3. Try placing in adjustedB2 (normal and rotated)
            bool placed = false;
            if (adjustedB2 >= 0 && adjustedB2 < (int)neighbor.boxes.size()) {
                rect.rotated = false;
                if (neighbor.boxes[adjustedB2].addRectangle(rect)) {
                    placed = true;
                } else {
                    rect.rotated = true;
                    if (neighbor.boxes[adjustedB2].addRectangle(rect)) {
                        placed = true;
                    }
                }
            }

            // 4. Only return if placed and better
            if (placed && geometryScore(neighbor) < currentScore) {
                return std::make_unique<PackingSolution>(std::move(neighbor));
            }
        }
        return nullptr;
    }
};

#endif // GEOMETRY_NEIGHBORHOOD_HPP
