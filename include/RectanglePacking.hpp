#ifndef RECTANGLE_PACKING_HPP
#define RECTANGLE_PACKING_HPP

#include "Framework.hpp"
#include <vector>
#include <algorithm>
#include <memory>

struct Rectangle {
    int id;
    int width;
    int height;
    bool rotated = false;
    int x = 0;
    int y = 0;
    int boxIndex = -1;

    Rectangle(int id, int w, int h) : id(id), width(w), height(h) {}

    int getW() const { return rotated ? height : width; }
    int getH() const { return rotated ? width : height; }
    int area() const { return width * height; }
};

class Box {
public:
    int L;
    std::vector<Rectangle> rectangles;

    Box(int L) : L(L) {}

    double getOverlapViolation(double maxOverlapPercent) const {
        double totalViolation = 0;
        for (size_t i = 0; i < rectangles.size(); ++i) {
            for (size_t j = i + 1; j < rectangles.size(); ++j) {
                const auto& r1 = rectangles[i];
                const auto& r2 = rectangles[j];
                
                int overlapW = std::max(0, std::min(r1.x + r1.getW(), r2.x + r2.getW()) - std::max(r1.x, r2.x));
                int overlapH = std::max(0, std::min(r1.y + r1.getH(), r2.y + r2.getH()) - std::max(r1.y, r2.y));
                
                if (overlapW > 0 && overlapH > 0) {
                    double overlapArea = (double)overlapW * (double)overlapH;
                    double maxArea = (double)std::max(r1.area(), r2.area());
                    double overlapRatio = overlapArea / maxArea;
                    if (overlapRatio > maxOverlapPercent) {
                        totalViolation += (overlapRatio - maxOverlapPercent);
                    }
                }
            }
        }
        return totalViolation;
    }

    bool canPlace(const Rectangle& rect, int x, int y, double maxOverlapPercent, int* blockingIdx = nullptr) const { 
        int rw = rect.getW();
        int rh = rect.getH();

        if (x < 0 || y < 0 || x + rw > L || y + rh > L) return false;

        for (size_t i = 0; i < rectangles.size(); ++i) {
            const auto& r = rectangles[i];
            int overlapW = std::max(0, std::min(x + rw, r.x + r.getW()) - std::max(x, r.x));
            int overlapH = std::max(0, std::min(y + rh, r.y + r.getH()) - std::max(y, r.y));
            
            if (overlapW > 0 && overlapH > 0) {
                if (maxOverlapPercent <= 0.0) {
                    if (blockingIdx) *blockingIdx = static_cast<int>(i);
                    return false;
                }

                double overlapArea = (double)overlapW * (double)overlapH;
                double maxArea = (double)std::max(rect.area(), r.area());
                if (overlapArea / maxArea > maxOverlapPercent) {
                    if (blockingIdx) *blockingIdx = static_cast<int>(i);
                    return false;
                }
            }
        }
        return true;
    }

    bool addRectangle(Rectangle rect, double maxOverlapPercent = 0.0) {
        std::vector<int> xCoords = {0};
        std::vector<int> yCoords = {0};

        for (const auto& r : rectangles) {
            int right = r.x + r.getW();
            int top = r.y + r.getH();
            if (right <= L - rect.getW()) xCoords.push_back(right);
            if (top <= L - rect.getH()) yCoords.push_back(top);
        }

        std::sort(xCoords.begin(), xCoords.end());
        xCoords.erase(std::unique(xCoords.begin(), xCoords.end()), xCoords.end());
        std::sort(yCoords.begin(), yCoords.end());
        yCoords.erase(std::unique(yCoords.begin(), yCoords.end()), yCoords.end());

        int bestX = -1;
        int bestY = -1;

        // Optimized Best-Fit (Bottom-Left) with Coordinate Jumping
        for (int y : yCoords) {
            for (int ix = 0; ix < (int)xCoords.size(); ++ix) {
                int x = xCoords[ix];
                int blockingIdx = -1;
                
                if (canPlace(rect, x, y, maxOverlapPercent, &blockingIdx)) {
                    bestX = x;
                    bestY = y;
                    break;
                } else if (blockingIdx != -1) {
                    // PRUNING: If blocked by a rectangle, jump to its right edge.
                    // All intermediate xCoords will also be blocked by this same rectangle.
                    int jumpTo = rectangles[blockingIdx].x + rectangles[blockingIdx].getW();
                    while (ix + 1 < (int)xCoords.size() && xCoords[ix + 1] < jumpTo) {
                        ix++;
                    }
                }
            }
            if (bestX != -1) break; 
        }

        if (bestX != -1) {
            rect.x = bestX;
            rect.y = bestY;
            rectangles.push_back(rect);
            return true;
        }
        return false;
    }
};

class PackingSolution : public Solution {
public:
    int L;
    std::vector<Box> boxes;
    std::vector<Rectangle> unplacedRectangles;
    double maxOverlapAllowed = 0.0;

    PackingSolution(int L) : L(L) {}

    double objectiveValue() const override {
        double violationPenalty = 0;
        double heuristic = 0;
        
        for (const auto& box : boxes) {
            violationPenalty += box.getOverlapViolation(maxOverlapAllowed);
            
            double occupiedArea = 0;
            for (const auto& r : box.rectangles) occupiedArea += r.area();
            double fillRatio = occupiedArea / (double)(L * L);
            heuristic += fillRatio * fillRatio;
        }

        return static_cast<double>(boxes.size()) 
               - (heuristic * 0.1) 
               + (unplacedRectangles.size() * 1000.0)
               + (violationPenalty * 5000.0); 
    }

    std::unique_ptr<Solution> clone() const override {
        return std::make_unique<PackingSolution>(*this);
    }

    void setMaxOverlap(double val) {
        maxOverlapAllowed = val;
    }

    void placeRectangle(Rectangle rect) {
        rect.rotated = false;
        if (tryPlace(rect)) return;

        rect.rotated = true;
        if (tryPlace(rect)) return;

        Box newBox(L);
        rect.rotated = false;
        if (newBox.addRectangle(rect)) {
            boxes.push_back(newBox);
        } else {
            unplacedRectangles.push_back(rect);
        }
    }

private:
    bool tryPlace(Rectangle& rect) {
        for (size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].addRectangle(rect)) {
                rect.boxIndex = static_cast<int>(i);
                return true;
            }
        }
        return false;
    }
};

#endif
