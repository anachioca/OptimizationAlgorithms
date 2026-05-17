#include "Framework.hpp"
#include "RectanglePacking.hpp"
#include "GreedyStrategies.hpp"
#include "InstanceGenerator.hpp"
#include "PermutationNeighborhood.hpp"
#include "GeometryNeighborhood.hpp"
#include "OverlapNeighborhood.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>
#include <fstream>
#include <cmath>
#include <map>

struct TestConfig {
    int numInstances;
    int numRects;
    int minW, maxW;
    int minH, maxH;
    int boxSize;
};

struct Stats {
    double totalBoxes = 0;
    double totalTime = 0;
    int count = 0;
};

void runExperiment(const std::vector<TestConfig>& configs, const std::string& logFile) {
    std::ofstream out(logFile);
    out << "numRects;boxSize;Algorithm;AvgBoxes;AvgTimeMS\n";
    std::cout << std::left << std::setw(10) << "Rects" << std::setw(10) << "L" << std::setw(28) << "Algo" << std::setw(12) << "AvgBoxes" << "AvgTime(ms)\n";
    std::cout << std::string(80, '-') << "\n";

    for (const auto& config : configs) {
        std::map<std::string, Stats> results;

        for (int i = 0; i < config.numInstances; ++i) {
            auto rects = InstanceGenerator::generate(config.numRects, config.minW, config.maxW, config.minH, config.maxH);

            auto runAlgo = [&](const std::string& name, auto func) {
                auto start = std::chrono::high_resolution_clock::now();
                auto sol_size = func();
                auto end = std::chrono::high_resolution_clock::now();
                results[name].totalBoxes += sol_size;
                results[name].totalTime += std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                results[name].count++;
            };

            // --- GREEDY ALGORITHMS ---
            runAlgo("Greedy-Area", [&]() {
                GreedyAlgorithm<Rectangle, PackingSolution> alg(rects, std::make_unique<AreaStrategy>(), std::make_unique<PackingSolution>(config.boxSize), placePacking);
                return alg.solve()->boxes.size();
            });

            runAlgo("Greedy-MaxSide", [&]() {
                GreedyAlgorithm<Rectangle, PackingSolution> alg(rects, std::make_unique<MaxSideStrategy>(), std::make_unique<PackingSolution>(config.boxSize), placePacking);
                return alg.solve()->boxes.size();
            });

            runAlgo("Greedy-SmallestFirst", [&]() {
                GreedyAlgorithm<Rectangle, PackingSolution> alg(rects, std::make_unique<SmallestFirstStrategy>(), std::make_unique<PackingSolution>(config.boxSize), placePacking);
                return alg.solve()->boxes.size();
            });

            // --- LOCAL SEARCH (PACKING) ---
            auto getStartSol = [&]() {
                GreedyAlgorithm<Rectangle, PackingSolution> badGreedy(rects, std::make_unique<BadStartingStrategy>(), std::make_unique<PackingSolution>(config.boxSize), placeOnePerBox);
                return badGreedy.solve();
            };

            runAlgo("LS-Random-Geometry", [&]() {
                auto start = getStartSol();
                int totalRects = 0;
                for (const auto& b : start->boxes) totalRects += b.rectangles.size();
                std::cout << " (Start: " << totalRects << " rects, " << start->boxes.size() << " boxes) " << std::flush;
                LocalSearch<PackingSolution> ls(std::move(start), std::make_unique<RandomizedGeometryNeighborhood>());
                return ls.solve()->boxes.size();
            });

            runAlgo("LS-Overlap-Random", [&]() {
                auto start = getStartSol();
                int totalRects = 0;
                for (const auto& b : start->boxes) totalRects += b.rectangles.size();
                std::cout << " (Start: " << totalRects << " rects, " << start->boxes.size() << " boxes) " << std::flush;
                LocalSearch<PackingSolution> ls(std::move(start), std::make_unique<OverlapNeighborhood>(0.05)); // 5% overlap allowed in neighborhood
                return ls.solve()->boxes.size();
            });

            // --- LOCAL SEARCH (PERMUTATION) ---
            if (config.numRects <= 100) {
                runAlgo("LS-Random-Swap", [&]() {
                    auto startPerm = std::make_unique<PermutationSolution>(rects, config.boxSize);
                    
                    PackingSolution startSol(config.boxSize);
                    for (const auto& r : startPerm->permutation) startSol.placeRectangle(r);
                    std::cout << " (Start: " << startPerm->permutation.size() << " rects, " << startSol.boxes.size() << " boxes) " << std::flush;

                    LocalSearch<PermutationSolution> ls(std::move(startPerm), std::make_unique<RandomizedSwapNeighborhood>());
                    auto finalPerm = ls.solve();

                    // Convert back to PackingSolution to get box count
                    PackingSolution finalSol(config.boxSize);
                    for (const auto& r : finalPerm->permutation) finalSol.placeRectangle(r);
                    return finalSol.boxes.size();
                });

            // --- SLOW SYSTEMATIC ALGORITHMS (Only for small N) ---
                runAlgo("LS-Systematic-Geometry", [&]() {
                    auto start = getStartSol();
                    int totalRects = 0;
                    for (const auto& b : start->boxes) totalRects += b.rectangles.size();
                    std::cout << " (Start: " << totalRects << " rects, " << start->boxes.size() << " boxes) " << std::flush;
                    LocalSearch<PackingSolution> ls(std::move(start), std::make_unique<GeometryNeighborhood>());
                    return ls.solve()->boxes.size();
                });

                if (config.numRects <= 50) { // Systematic swap is very slow, only run for very small instances
                    runAlgo("LS-Systematic-Swap", [&]() {
                        auto startPerm = std::make_unique<PermutationSolution>(rects, config.boxSize);
                        LocalSearch<PermutationSolution> ls(std::move(startPerm), std::make_unique<SystematicSwapNeighborhood>());
                        auto finalPerm = ls.solve();
                        PackingSolution finalSol(config.boxSize);
                        for (const auto& r : finalPerm->permutation) finalSol.placeRectangle(r);
                        return finalSol.boxes.size();
                    }); 
                }
            }
        }

        for (auto const& [name, s] : results) {
            double avgB = s.totalBoxes / s.count;
            double avgT = s.totalTime / s.count;
            std::cout << std::left << std::setw(10) << config.numRects << std::setw(10) << config.boxSize << std::setw(28) << name << std::setw(12) << std::fixed << std::setprecision(2) << avgB << avgT << "\n";
            out << config.numRects << ";" << config.boxSize << ";" << name << ";" << avgB << ";" << avgT << "\n";
        }
        std::cout << std::string(80, '-') << "\n";
    }
}

int main(int argc, char** argv) {
    std::vector<TestConfig> quickTest = {
        {2, 50, 1, 20, 1, 20, 50},
        {1, 100, 1, 30, 1, 30, 100}
    };

    std::vector<TestConfig> fullTest = {
        {5, 100, 1, 30, 1, 30, 100},
        {3, 500, 1, 50, 1, 50, 200},
        {1, 1000, 1, 100, 1, 100, 500}
    };

    if (argc > 1 && std::string(argv[1]) == "--full") {
        std::cout << "Running Full Test Environment...\n";
        runExperiment(fullTest, "full_test_log.csv");
    } else {
        std::cout << "Running Quick Test Environment...\n";
        runExperiment(quickTest, "quick_test_log.csv");
    }

    return 0;
}
