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

    std::vector<std::unique_ptr<NumericInput>> inputs;
    float startY = 30;
    inputs.push_back(std::make_unique<NumericInput>("Rects", numRects, sf::Vector2f(20, startY), font));
    inputs.push_back(std::make_unique<NumericInput>("Box Size", boxSize, sf::Vector2f(20, startY + 40), font));
    inputs.push_back(std::make_unique<NumericInput>("Min W", minW, sf::Vector2f(20, startY + 80), font));
    inputs.push_back(std::make_unique<NumericInput>("Max W", maxW, sf::Vector2f(20, startY + 120), font));
    inputs.push_back(std::make_unique<NumericInput>("Min H", minH, sf::Vector2f(20, startY + 160), font));
    inputs.push_back(std::make_unique<NumericInput>("Max H", maxH, sf::Vector2f(20, startY + 200), font));

    std::vector<std::unique_ptr<Button>> buttons;
    float btnY = 260;
    float btnH = 35;
    float btnW = 260;
    
    auto addBtn = [&](std::string label, std::function<void()> action) {
        auto btn = std::make_unique<Button>(label, sf::Vector2f(20, btnY), sf::Vector2f(btnW, btnH), font);
        btn->onClick = action;
        buttons.push_back(std::move(btn));
        btnY += btnH + 5;
    };

    addBtn("New Instance (Manual)", [&]() {
        activeGeomLS.reset(); activePermLS.reset();
        rects = InstanceGenerator::generate(numRects, minW, maxW, minH, maxH);
        auto badStart = std::make_unique<PackingSolution>(boxSize);
        GreedyAlgorithm<Rectangle, PackingSolution> badGreedy(rects, std::make_unique<BadStartingStrategy>(), std::move(badStart), placeOnePerBox);
        currentSol = badGreedy.solve();
    });

    addBtn("Random Instance (Max 40)", [&]() {
        activeGeomLS.reset(); activePermLS.reset();
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

    addBtn("Greedy (Area)", [&]() {
        activeGeomLS.reset(); activePermLS.reset();
        GreedyAlgorithm<Rectangle, PackingSolution> greedy(rects, std::make_unique<AreaStrategy>(), std::make_unique<PackingSolution>(boxSize), placePacking);
        currentSol = greedy.solve();
    });

    addBtn("Greedy (MaxSide)", [&]() {
        activeGeomLS.reset(); activePermLS.reset();
        GreedyAlgorithm<Rectangle, PackingSolution> greedy(rects, std::make_unique<MaxSideStrategy>(), std::make_unique<PackingSolution>(boxSize), placePacking);
        currentSol = greedy.solve();
    });

    addBtn("Greedy (Smallest)", [&]() {
        activeGeomLS.reset(); activePermLS.reset();
        GreedyAlgorithm<Rectangle, PackingSolution> greedy(rects, std::make_unique<SmallestFirstStrategy>(), std::make_unique<PackingSolution>(boxSize), placePacking);
        currentSol = greedy.solve();
    });

    addBtn("LS (Rand Swap)", [&]() {
        activeGeomLS.reset();
        std::vector<Rectangle> badPerm = rects;
        auto startSol = std::make_unique<PermutationSolution>(badPerm, boxSize);
        activePermLS = std::make_unique<LocalSearch<PermutationSolution>>(std::move(startSol), std::make_unique<RandomizedSwapNeighborhood>());
    });

    addBtn("LS (Syst Swap)", [&]() {
        activeGeomLS.reset();
        std::vector<Rectangle> badPerm = rects;
        auto startSol = std::make_unique<PermutationSolution>(badPerm, boxSize);
        activePermLS = std::make_unique<LocalSearch<PermutationSolution>>(std::move(startSol), std::make_unique<SystematicSwapNeighborhood>());
    });

    addBtn("LS (Rand Geometry)", [&]() {
        activePermLS.reset();
        auto emptySol = std::make_unique<PackingSolution>(boxSize);
        GreedyAlgorithm<Rectangle, PackingSolution> badGreedy(rects, std::make_unique<BadStartingStrategy>(), std::move(emptySol), placeOnePerBox);
        activeGeomLS = std::make_unique<LocalSearch<PackingSolution>>(badGreedy.solve(), std::make_unique<RandomizedGeometryNeighborhood>());
    });

    addBtn("LS (Syst Geometry)", [&]() {
        activePermLS.reset();
        auto emptySol = std::make_unique<PackingSolution>(boxSize);
        GreedyAlgorithm<Rectangle, PackingSolution> badGreedy(rects, std::make_unique<BadStartingStrategy>(), std::move(emptySol), placeOnePerBox);
        activeGeomLS = std::make_unique<LocalSearch<PackingSolution>>(badGreedy.solve(), std::make_unique<GeometryNeighborhood>());
    });

    addBtn("LS (Overlap Cooling)", [&]() {
        activePermLS.reset();
        auto emptySol = std::make_unique<PackingSolution>(boxSize);
        GreedyAlgorithm<Rectangle, PackingSolution> badGreedy(rects, std::make_unique<BadStartingStrategy>(), std::move(emptySol), placeOnePerBox);
        
        auto overlapNH = std::make_unique<OverlapNeighborhood>(1.0); // Start at 100%
        auto startSol = badGreedy.solve();
        startSol->setMaxOverlap(1.0); // Ensure initial solution knows about 100% overlap
        
        auto ls = std::make_unique<LocalSearch<PackingSolution>>(std::move(startSol), std::move(overlapNH));
        
        // Define cooling callback
        ls->setStepCallback([](LocalSearch<PackingSolution>& lsInst) {
            auto* nh = dynamic_cast<OverlapNeighborhood*>(&lsInst.getNeighborhood());
            if (nh) {
                double current = nh->getMaxOverlap();
                if (current > 0.0) {
                    double next = std::max(0.0, current - 0.005); 
                    nh->setMaxOverlap(next);
                    
                    // Sync the current solution as well so objective function uses new threshold
                    lsInst.getMutableSolution().setMaxOverlap(next);
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

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) window.close();
            for (auto& in : inputs) in->handleEvent(event, window);
            for (auto& btn : buttons) btn->handleEvent(event, window);
        }

        if (activeGeomLS) {
            bool foundImprovement = activeGeomLS->performStep();
            currentSol = std::make_shared<PackingSolution>(activeGeomLS->getCurrentSolution());
            
            if (!foundImprovement) {
                // Special handling for Overlap Cooling: only stop if threshold is 0
                auto* nh = dynamic_cast<OverlapNeighborhood*>(&activeGeomLS->getNeighborhood());
                if (nh && nh->getMaxOverlap() > 0.0) {
                    // Keep going, threshold will drop in next step callback
                } else {
                    activeGeomLS.reset();
                }
            }
        }
        if (activePermLS) {
            if (!activePermLS->performStep()) {
                auto& finalPerm = activePermLS->getCurrentSolution().permutation;
                auto finalSol = std::make_shared<PackingSolution>(boxSize);
                for(auto& r : finalPerm) finalSol->placeRectangle(r);
                currentSol = finalSol;
                activePermLS.reset();
            } else {
                auto& currentPerm = activePermLS->getCurrentSolution().permutation;
                auto tempSol = std::make_shared<PackingSolution>(boxSize);
                for(auto& r : currentPerm) tempSol->placeRectangle(r);
                currentSol = tempSol;
            }
        }

        window.clear(sf::Color::White);
        visualizer.draw(window, *currentSol);
        for (auto& in : inputs) in->draw(window);
        for (auto& btn : buttons) btn->draw(window);
        statsText.setString("Boxes used: " + std::to_string(currentSol->boxes.size()) + "\nObjective: " + std::to_string(currentSol->objectiveValue()));
        window.draw(statsText);
        window.display();
    }
    return 0;
}
