#include <SFML/Graphics.hpp>
#include "Visualizer.hpp"
#include "GUI.hpp"
#include "InstanceGenerator.hpp"
#include "GreedyStrategies.hpp"
#include "PermutationNeighborhood.hpp"
#include "GeometryNeighborhood.hpp"
#include "OverlapNeighborhood.hpp"
#include <iostream>
#include <memory>
#include <vector>
#include <random>
#include <chrono>
#include <optional>

int main() {
    int numRects = 40, boxSize = 50, minW = 5, maxW = 20, minH = 5, maxH = 20;

    sf::RenderWindow window(sf::VideoMode(1300, 900), "Rectangle Packing GUI");
    window.setFramerateLimit(60); 

    sf::Font font;
    if (!font.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")) return -1;

    Visualizer visualizer;
    visualizer.sidebarWidth = 300.0f;
    std::vector<Rectangle> rects = InstanceGenerator::generate(numRects, minW, maxW, minH, maxH);
    std::shared_ptr<PackingSolution> currentSol = std::make_shared<PackingSolution>(boxSize);

    // Active algorithm state
    std::unique_ptr<LocalSearch<PackingSolution>> activeGeomLS;
    std::unique_ptr<LocalSearch<PermutationSolution>> activePermLS;

    struct GreedyAnimState { std::vector<Rectangle> sortedRects; size_t idx = 0; };
    std::optional<GreedyAnimState> activeGreedy;

    // Elapsed time tracking
    auto algoStartTime = std::chrono::steady_clock::now();
    double elapsedMs   = 0.0;

    // Define buttons and inputs
    std::vector<std::unique_ptr<NumericInput>> inputs;
    float startY = 30;
    inputs.push_back(std::make_unique<NumericInput>("Rects", numRects, sf::Vector2f(20, startY), font));
    inputs.push_back(std::make_unique<NumericInput>("Box Size", boxSize, sf::Vector2f(20, startY + 40), font));
    inputs.push_back(std::make_unique<NumericInput>("Min W", minW, sf::Vector2f(20, startY + 80), font));
    inputs.push_back(std::make_unique<NumericInput>("Max W", maxW, sf::Vector2f(20, startY + 120), font));
    inputs.push_back(std::make_unique<NumericInput>("Min H", minH, sf::Vector2f(20, startY + 160), font));
    inputs.push_back(std::make_unique<NumericInput>("Max H", maxH, sf::Vector2f(20, startY + 200), font));

    std::vector<std::unique_ptr<Button>> buttons;
    float btnY = 295;
    float btnH = 35;
    float btnW = 260;
    
    // Helper to add buttons with less boilerplate
    auto addBtn = [&](std::string label, std::function<void()> action) {
        auto btn = std::make_unique<Button>(label, sf::Vector2f(20, btnY), sf::Vector2f(btnW, btnH), font);
        btn->onClick = action;
        buttons.push_back(std::move(btn));
        btnY += btnH + 8;
    };

    addBtn("New Instance (Manual)", [&]() {
        activeGeomLS.reset(); activePermLS.reset(); activeGreedy.reset();
        rects = InstanceGenerator::generate(numRects, minW, maxW, minH, maxH);
        auto badStart = std::make_unique<PackingSolution>(boxSize);
        GreedyAlgorithm<Rectangle, PackingSolution> badGreedy(rects, std::make_unique<BadStartingStrategy>(), std::move(badStart), placeOnePerBox);
        currentSol = badGreedy.solve();
    });

    addBtn("Random Instance (Max 40)", [&]() {
        activeGeomLS.reset(); activePermLS.reset(); activeGreedy.reset();
        std::random_device rd;
        std::mt19937 gen(rd());
        numRects = std::uniform_int_distribution<>(10, 40)(gen);
        boxSize = std::uniform_int_distribution<>(40, 80)(gen);
        minW = std::uniform_int_distribution<>(5, 15)(gen);
        maxW = std::uniform_int_distribution<>(minW + 5, minW + 20)(gen);
        minH = std::uniform_int_distribution<>(5, 15)(gen);
        maxH = std::uniform_int_distribution<>(minH + 5, minH + 20)(gen);

        rects = InstanceGenerator::generate(numRects, minW, maxW, minH, maxH);
        auto badStart = std::make_unique<PackingSolution>(boxSize);
        GreedyAlgorithm<Rectangle, PackingSolution> badGreedy(rects, std::make_unique<BadStartingStrategy>(), std::move(badStart), placeOnePerBox);
        currentSol = badGreedy.solve();
    });

    auto startGreedy = [&](auto strategy) {
        activeGeomLS.reset(); activePermLS.reset(); activeGreedy.reset();
        std::vector<Rectangle> sorted = rects;
        algoStartTime = std::chrono::steady_clock::now();
        strategy.sort(sorted);
        { PackingSolution tmp(boxSize); for (auto& r : sorted) placePacking(tmp, r); }
        elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - algoStartTime).count();
        currentSol = std::make_shared<PackingSolution>(boxSize);
        activeGreedy = GreedyAnimState{std::move(sorted), 0};
    };

    addBtn("Greedy (Area)",     [&]() { startGreedy(AreaStrategy()); });
    addBtn("Greedy (MaxSide)",  [&]() { startGreedy(MaxSideStrategy()); });
    addBtn("Greedy (Smallest)", [&]() { startGreedy(SmallestFirstStrategy()); });

    auto startPermLS = [&](auto neighborhood) {
        activeGeomLS.reset(); activeGreedy.reset();
        algoStartTime = std::chrono::steady_clock::now(); elapsedMs = 0.0;
        // Decidedly bad start: smallest rectangles first means big ones can't
        // fit later → many extra boxes. Deterministic, much worse than random.
        std::vector<Rectangle> badPerm = rects;
        SmallestFirstStrategy().sort(badPerm);
        currentSol = std::make_shared<PackingSolution>(boxSize);
        for (auto& r : badPerm) currentSol->placeRectangle(r);
        activePermLS = std::make_unique<LocalSearch<PermutationSolution>>(
            std::make_unique<PermutationSolution>(std::move(badPerm), boxSize),
            std::move(neighborhood));
    };

    addBtn("LS (Rand Swap)", [&]() { startPermLS(std::make_unique<RandomizedSwapNeighborhood>()); });
    addBtn("LS (Syst Swap)", [&]() { startPermLS(std::make_unique<SystematicSwapNeighborhood>()); });

    addBtn("LS (Rand Geometry)", [&]() {
        activePermLS.reset(); activeGreedy.reset();
        algoStartTime = std::chrono::steady_clock::now(); elapsedMs = 0.0;
        auto emptySol = std::make_unique<PackingSolution>(boxSize);
        GreedyAlgorithm<Rectangle, PackingSolution> badGreedy(rects, std::make_unique<BadStartingStrategy>(), std::move(emptySol), placeOnePerBox);
        activeGeomLS = std::make_unique<LocalSearch<PackingSolution>>(badGreedy.solve(), std::make_unique<RandomizedGeometryNeighborhood>());
    });

    addBtn("LS (Syst Geometry)", [&]() {
        activePermLS.reset(); activeGreedy.reset();
        algoStartTime = std::chrono::steady_clock::now(); elapsedMs = 0.0;
        auto emptySol = std::make_unique<PackingSolution>(boxSize);
        GreedyAlgorithm<Rectangle, PackingSolution> badGreedy(rects, std::make_unique<BadStartingStrategy>(), std::move(emptySol), placeOnePerBox);
        activeGeomLS = std::make_unique<LocalSearch<PackingSolution>>(badGreedy.solve(), std::make_unique<GeometryNeighborhood>());
    });

    addBtn("LS (Overlap Cooling)", [&]() {
        activePermLS.reset(); activeGreedy.reset();
        algoStartTime = std::chrono::steady_clock::now(); elapsedMs = 0.0;

        // All rects stacked in one box at 100% overlap — trivially 1 box, easy starting point
        auto startSol = std::make_unique<PackingSolution>(boxSize);
        Box bigBox(boxSize);
        for (auto r : rects) { r.x = 0; r.y = 0; bigBox.rectangles.push_back(r); }
        startSol->boxes.push_back(bigBox);
        // Empty boxes give the neighborhood room to spread rects into
        // Create 5 empty boxes to start with so the first few steps can already try placing into new boxes as the overlap threshold cools.
        for (int i = 0; i < 5; ++i) startSol->boxes.push_back(Box(boxSize));

        auto overlapNH = std::make_unique<OverlapNeighborhood>(1.0);
        auto ls = std::make_unique<LocalSearch<PackingSolution>>(std::move(startSol), std::move(overlapNH));

        // Cooling callback: after each step, lower the neighborhood's overlap tolerance.
        // The tolerance lives inside the neighborhood now, so no solution-side sync is needed.
        ls->setStepCallback([](LocalSearch<PackingSolution>& lsInst) {
            auto* nh = dynamic_cast<OverlapNeighborhood*>(&lsInst.getNeighborhood());
            if (nh) {
                double current = nh->getMaxOverlap();
                if (current > 0.0) {
                    double next = std::max(0.0, current - 0.1);
                    nh->setMaxOverlap(next);
                }
            }
        });
        
        activeGeomLS = std::move(ls);
    });

    sf::Text statsText;
    statsText.setFont(font);
    statsText.setCharacterSize(16);
    statsText.setFillColor(sf::Color::Blue);
    statsText.setPosition(20, btnY + 10);

    // Step-skipping state: avoid flooding the viewer with micro-changes every frame
    size_t geomLastBoxCount    = 0;
    int    geomFramesSinceUpdate = 0;

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) window.close();
            for (auto& in : inputs) in->handleEvent(event, window);
            for (auto& btn : buttons) btn->handleEvent(event, window);
        }

        if (activeGeomLS) {
            // Run a batch of steps per frame; only redraw when box count changes
            // (meaningful visual event) or every DISPLAY_INTERVAL frames as a heartbeat.
            const int STEPS_PER_FRAME  = 15;
            const int DISPLAY_INTERVAL = 20;
            auto* overlapNH = dynamic_cast<OverlapNeighborhood*>(&activeGeomLS->getNeighborhood());

            bool foundImprovement = false;
            // breaks only if no improvement is found after a full batch, allowing the cooling to continue even through plateaus
            for (int s = 0; s < STEPS_PER_FRAME; ++s) {
                bool improved = activeGeomLS->performStep();
                if (improved) foundImprovement = true;
                if (!improved && !overlapNH) break;
            }

            size_t newBoxCount = activeGeomLS->getCurrentSolution().boxes.size();
            geomFramesSinceUpdate++;
            if (newBoxCount != geomLastBoxCount
                    || geomFramesSinceUpdate >= DISPLAY_INTERVAL
                    || !foundImprovement) {
                currentSol = std::make_shared<PackingSolution>(activeGeomLS->getCurrentSolution());
                geomLastBoxCount     = newBoxCount;
                geomFramesSinceUpdate = 0;
            }

            if (!foundImprovement) {
                if (overlapNH && overlapNH->getMaxOverlap() > 0.0) {
                    // Keep going; overlap threshold will drop via step callback
                } else {
                    // Post-process: extract rects from violating boxes and repack strictly
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
                    activeGeomLS.reset();
                }
            }
        }
        if (activePermLS) {
            // 1 step per frame; always redraw so every improvement is visible
            bool improved = activePermLS->performStep();
            auto& perm = activePermLS->getCurrentSolution().permutation;
            auto sol = std::make_shared<PackingSolution>(boxSize);
            for (auto& r : perm) sol->placeRectangle(r);
            currentSol = sol;
            if (!improved) activePermLS.reset();
        }
        if (activeGreedy) {
            if (activeGreedy->idx < activeGreedy->sortedRects.size()) {
                placePacking(*currentSol, activeGreedy->sortedRects[activeGreedy->idx]);
                ++activeGreedy->idx;
            } else {
                activeGreedy.reset();
            }
        }

        window.clear(sf::Color::White);
        visualizer.draw(window, *currentSol);
        for (auto& in : inputs) in->draw(window);

        // Separator between parameter inputs and algorithm buttons
        sf::RectangleShape sep(sf::Vector2f(260, 1));
        sep.setPosition(20, 278);
        sep.setFillColor(sf::Color(180, 180, 180));
        window.draw(sep);

        for (auto& btn : buttons) btn->draw(window);
        // Keep elapsed time ticking while an algorithm is running
        if (activeGeomLS || activePermLS)
            elapsedMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - algoStartTime).count();

        // Format elapsed time
        std::string timeStr;
        if (elapsedMs < 1000.0) {
            timeStr = std::to_string(static_cast<int>(elapsedMs)) + " ms";
        } else {
            int s    = static_cast<int>(elapsedMs / 1000.0);
            int frac = static_cast<int>(elapsedMs / 100.0) % 10;
            timeStr  = std::to_string(s) + "." + std::to_string(frac) + " s";
        }

        statsText.setString(
            "Boxes used: "  + std::to_string(currentSol->boxes.size()) +
            "\nObjective: " + std::to_string(currentSol->objectiveValue()) +
            "\nTime: "      + timeStr);
        window.draw(statsText);
        window.display();
    }
    return 0;
}
