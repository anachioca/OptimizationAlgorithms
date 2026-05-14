#include "Framework.hpp"
#include "RectanglePacking.hpp"
#include "GreedyStrategies.hpp"
#include "InstanceGenerator.hpp"
#include "PermutationNeighborhood.hpp"
#include "GeometryNeighborhood.hpp"
#include "OverlapNeighborhood.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>

void runBenchmark(int numRects, int boxSize) {
    std::cout << "Benchmarking with " << numRects << " rectangles, Box Size: " << boxSize << "\n";
    std::cout << std::string(80, '-') << "\n";
    std::cout << std::left << std::setw(30) << "Algorithm" 
              << std::setw(15) << "Boxes" 
              << std::setw(15) << "Time (ms)" << "\n";
    std::cout << std::string(80, '-') << "\n";

    auto rects = InstanceGenerator::generate(numRects, 1, boxSize/2, 1, boxSize/2);

    // 1. Greedy - Area Strategy
    {
        auto start = std::chrono::high_resolution_clock::now();
        GreedyAlgorithm<Rectangle, PackingSolution> greedy(
            rects, std::make_unique<AreaStrategy>(), std::make_unique<PackingSolution>(boxSize), placePacking
        );
        auto sol = greedy.solve();
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        std::cout << std::left << std::setw(30) << "Greedy (Area)" 
                  << std::setw(15) << sol->boxes.size() 
                  << std::setw(15) << duration << "\n";
    }

    // 2. Rule-based Local Search
    {
        std::vector<Rectangle> badPerm = rects;
        SmallestFirstStrategy().sort(badPerm);
        auto startSol = std::make_unique<PermutationSolution>(badPerm, boxSize);
        auto start = std::chrono::high_resolution_clock::now();
        LocalSearch<PermutationSolution> ls(
            std::move(startSol), std::make_unique<RandomizedSwapNeighborhood>()
        );
        auto sol = ls.solve();
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        PackingSolution finalSol(boxSize);
        for(auto& r : sol->permutation) finalSol.placeRectangle(r);
        std::cout << std::left << std::setw(30) << "LS (Rule-based)" 
                  << std::setw(15) << finalSol.boxes.size() 
                  << std::setw(15) << duration << "\n";
    }

    // 3. Geometry-based Local Search
    {
        auto emptySol = std::make_unique<PackingSolution>(boxSize);
        GreedyAlgorithm<Rectangle, PackingSolution> greedy(
            rects, std::make_unique<SmallestFirstStrategy>(), std::move(emptySol), placePacking
        );
        auto startSol = greedy.solve();
        auto start = std::chrono::high_resolution_clock::now();
        LocalSearch<PackingSolution> ls(
            std::move(startSol), std::make_unique<RandomizedGeometryNeighborhood>()
        );
        auto sol = ls.solve();
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        std::cout << std::left << std::setw(30) << "LS (Geometry-based)" 
                  << std::setw(15) << sol->boxes.size() 
                  << std::setw(15) << duration << "\n";
    }

    // 4. Overlap-based Local Search
    {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Start with everything in 1 box
        auto currentSol = std::make_unique<PackingSolution>(boxSize);
        Box bigBox(boxSize);
        for(auto r : rects) {
            r.x = 0; r.y = 0; 
            bigBox.rectangles.push_back(r);
        }
        currentSol->boxes.push_back(bigBox);
        currentSol->maxOverlapAllowed = 1.0;

        double overlap = 1.0;
        while (overlap >= 0.0) {
            // To reduce overlap, we might need more boxes. 
            // We add a few empty boxes to give the neighborhood room to move things.
            // (LocalSearch will eventually remove them if they stay empty).
            for(int i=0; i < 5; ++i) currentSol->boxes.push_back(Box(boxSize));

            auto nh = std::make_unique<OverlapNeighborhood>(overlap);
            LocalSearch<PackingSolution> ls(std::move(currentSol), std::move(nh));
            currentSol = ls.solve();
            
            if (overlap <= 0.0) break;
            overlap -= 0.2;
            if (overlap < 0.0) overlap = 0.0;
            currentSol->maxOverlapAllowed = overlap;
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        std::cout << std::left << std::setw(30) << "LS (Overlap-based)" 
                  << std::setw(15) << currentSol->boxes.size() 
                  << std::setw(15) << duration << "\n";
    }
    std::cout << std::string(80, '-') << "\n\n";
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--full") {
        runBenchmark(100, 100);
        runBenchmark(200, 100);
    } else {
        std::cout << "Running Quick Test.\n";
        runBenchmark(50, 50);
    }
    return 0;
}
