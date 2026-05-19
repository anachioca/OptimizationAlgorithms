#include "Framework.hpp"
#include "RectanglePacking.hpp"
#include "GreedyStrategies.hpp"
#include "InstanceGenerator.hpp"
#include "PermutationNeighborhood.hpp"
#include "GeometryNeighborhood.hpp"
#include "OverlapNeighborhood.hpp"
#include <iostream>
#include <chrono>
#include <ctime>
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
                struct timespec t0, t1;
                clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t0);
                auto sol_size = func();
                clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t1);
                double ms = (t1.tv_sec - t0.tv_sec) * 1000.0
                          + (t1.tv_nsec - t0.tv_nsec) / 1e6;
                results[name].totalBoxes += sol_size;
                results[name].totalTime  += ms;
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
                LocalSearch<PackingSolution> ls(std::move(start), std::make_unique<RandomizedGeometryNeighborhood>());
                return ls.solve()->boxes.size();
            });

            runAlgo("LS-Overlap-Cooling", [&]() {
                // Start with all rects in one box at 100% overlap, then cool down to 0%
                auto currentSol = std::make_unique<PackingSolution>(config.boxSize);
                Box bigBox(config.boxSize);
                for (auto r : rects) { r.x = 0; r.y = 0; bigBox.rectangles.push_back(r); }
                currentSol->boxes.push_back(bigBox);
                currentSol->maxOverlapAllowed = 1.0;

                int attempts      = std::max(50, std::min(500, 30000 / config.numRects));
                int maxStepsPhase = std::max(30, config.numRects / 7);

                double overlap = 1.0;
                while (overlap >= 0.0) {
                    for (int i = 0; i < 3; ++i) currentSol->boxes.push_back(Box(config.boxSize));
                    LocalSearch<PackingSolution> ls(
                        std::move(currentSol),
                        std::make_unique<OverlapNeighborhood>(overlap, attempts));
                    for (int step = 0; step < maxStepsPhase && ls.performStep(); ++step) {}
                    currentSol = std::make_unique<PackingSolution>(ls.getCurrentSolution());
                    if (overlap <= 0.0) break;
                    overlap -= 0.25;
                    if (overlap < 0.0) overlap = 0.0;
                    currentSol->maxOverlapAllowed = overlap;
                }

                // Guarantee overlap-free: extract all rects from violating boxes,
                // clear those boxes, then reinsert using placeRectangle which tries
                // all existing boxes before opening a new one.
                std::vector<Rectangle> toRepack;
                for (auto& box : currentSol->boxes) {
                    if (box.getOverlapViolation(0.0) > 0.0) {
                        for (auto& r : box.rectangles) toRepack.push_back(r);
                        box.rectangles.clear();
                    }
                }
                currentSol->boxes.erase(
                    std::remove_if(currentSol->boxes.begin(), currentSol->boxes.end(),
                        [](const Box& b) { return b.rectangles.empty(); }),
                    currentSol->boxes.end());
                for (auto& r : toRepack)
                    currentSol->placeRectangle(r);

                return currentSol->boxes.size();
            });

            // --- LOCAL SEARCH (PERMUTATION) ---
            if (config.numRects <= 100) {
                runAlgo("LS-Random-Swap", [&]() {
                    std::vector<Rectangle> badPerm = rects;
                    SmallestFirstStrategy().sort(badPerm); // decidedly bad: small rects first → more boxes
                    auto startPerm = std::make_unique<PermutationSolution>(std::move(badPerm), config.boxSize);
                    
                    PackingSolution startSol(config.boxSize);
                    for (const auto& r : startPerm->permutation) startSol.placeRectangle(r);
                    //std::cout << " (Start: " << startPerm->permutation.size() << " rects, " << startSol.boxes.size() << " boxes) " << std::flush;

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
                    // std::cout << " (Start: " << totalRects << " rects, " << start->boxes.size() << " boxes) " << std::flush;
                    LocalSearch<PackingSolution> ls(std::move(start), std::make_unique<GeometryNeighborhood>());
                    return ls.solve()->boxes.size();
                });

                if (config.numRects <= 50) { // Systematic swap is very slow, only run for very small instances
                    runAlgo("LS-Systematic-Swap", [&]() {
                        std::vector<Rectangle> badPerm = rects;
                        SmallestFirstStrategy().sort(badPerm);
                        auto startPerm = std::make_unique<PermutationSolution>(std::move(badPerm), config.boxSize);
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
