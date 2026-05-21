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
                GreedyAlgorithm<Rectangle, PackingSolution> badGreedy(rects, std::make_unique<NoSort>(), std::make_unique<PackingSolution>(config.boxSize), placeOnePerBox);
                return badGreedy.solve();
            };

            runAlgo("LS-Random-Geometry", [&]() {
                auto start = getStartSol();
                int totalRects = 0;
                for (const auto& b : start->boxes) totalRects += b.rectangles.size();
                LocalSearch<PackingSolution> ls(std::move(start), std::make_unique<RandomizedGeometryNeighborhood>());
                return ls.solve()->boxes.size();
            });

            
            // Callback-based cooling. 
            // Each tolerance level gets one search step before the next tightening.
            runAlgo("LS-Overlap-Cooling", [&]() {
                auto currentSol = std::make_unique<PackingSolution>(config.boxSize);
                Box bigBox(config.boxSize);
                for (auto r : rects) { r.x = 0; r.y = 0; bigBox.rectangles.push_back(r); }
                currentSol->boxes.push_back(bigBox);

                // Seed empty boxes once up front
                for (int i = 0; i < 5; ++i) currentSol->boxes.push_back(Box(config.boxSize));

                int attempts = std::max(50, std::min(500, 30000 / config.numRects));

                LocalSearch<PackingSolution> ls(
                    std::move(currentSol),
                    std::make_unique<OverlapNeighborhood>(1.0, attempts));

                ls.setStepCallback([](LocalSearch<PackingSolution>& lsInst) {
                    auto* nh = dynamic_cast<OverlapNeighborhood*>(&lsInst.getNeighborhood());
                    if (nh && nh->getMaxOverlap() > 0.0) {
                        nh->setMaxOverlap(std::max(0.0, nh->getMaxOverlap() - 0.1));
                    }
                });

                // Keep stepping while either we found an improvement OR there's
                // still cooling left to do. Once tolerance hits 0 and no
                // improvement is found, exit.
                auto* nh = dynamic_cast<OverlapNeighborhood*>(&ls.getNeighborhood());
                while (ls.performStep() || (nh && nh->getMaxOverlap() > 0.0)) {}

                auto finalSol = std::make_unique<PackingSolution>(ls.getCurrentSolution());

                // guarantee overlap-free.
                std::vector<Rectangle> toRepack;
                for (auto& box : finalSol->boxes) {
                    if (box.getOverlapViolation(0.0) > 0.0) {
                        for (auto& r : box.rectangles) toRepack.push_back(r);
                        box.rectangles.clear();
                    }
                }
                finalSol->boxes.erase(
                    std::remove_if(finalSol->boxes.begin(), finalSol->boxes.end(),
                        [](const Box& b) { return b.rectangles.empty(); }),
                    finalSol->boxes.end());
                for (auto& r : toRepack) finalSol->placeRectangle(r);

                return finalSol->boxes.size();
            });

            // --- LOCAL SEARCH (PERMUTATION) ---
            // Decidedly bad start: SmallestFirst order. Deterministic and provably
            // worse than random shuffle in this problem.
            if (config.numRects <= 100) { // Permutation LS is very slow, only run for smaller instances
                runAlgo("LS-Random-Swap", [&]() {
                    std::vector<Rectangle> badPerm = rects;
                    SmallestFirstStrategy().sort(badPerm);
                    auto startPerm = std::make_unique<PermutationSolution>(std::move(badPerm), config.boxSize);

                    LocalSearch<PermutationSolution> ls(std::move(startPerm), std::make_unique<RandomizedSwapNeighborhood>());
                    auto finalPerm = ls.solve();

                    // Convert back to PackingSolution to get box count
                    PackingSolution finalSol(config.boxSize);
                    for (const auto& r : finalPerm->permutation) finalSol.placeRectangle(r);
                    return finalSol.boxes.size();
                });
            }

            // --- SLOW SYSTEMATIC ALGORITHMS (Only for small N) ---
            if (config.numRects <= 100) {
                runAlgo("LS-Systematic-Geometry", [&]() {
                    auto start = getStartSol();
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
        {3, 200, 1, 50, 1, 50, 200},
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
