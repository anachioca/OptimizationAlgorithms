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

int main() {
    int numRects = 50, boxSize = 50, minW = 5, maxW = 20, minH = 5, maxH = 20;

    sf::RenderWindow window(sf::VideoMode(1200, 800), "Rectangle Packing GUI");
    window.setFramerateLimit(60); 

    sf::Font font;
    if (!font.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")) return -1;

    Visualizer visualizer;
    std::vector<Rectangle> rects = InstanceGenerator::generate(numRects, minW, maxW, minH, maxH);
    std::shared_ptr<PackingSolution> currentSol = std::make_shared<PackingSolution>(boxSize);

    // Active algorithm state
    std::unique_ptr<LocalSearch<PackingSolution>> activeGeomLS;
    std::unique_ptr<LocalSearch<PermutationSolution>> activePermLS;

    std::vector<std::unique_ptr<NumericInput>> inputs;
    inputs.push_back(std::make_unique<NumericInput>("Rectangles", numRects, sf::Vector2f(20, 50), font));
    inputs.push_back(std::make_unique<NumericInput>("Box Size", boxSize, sf::Vector2f(20, 100), font));
    inputs.push_back(std::make_unique<NumericInput>("Min Width", minW, sf::Vector2f(20, 150), font));
    inputs.push_back(std::make_unique<NumericInput>("Max Width", maxW, sf::Vector2f(20, 200), font));
    inputs.push_back(std::make_unique<NumericInput>("Min Height", minH, sf::Vector2f(20, 250), font));
    inputs.push_back(std::make_unique<NumericInput>("Max Height", maxH, sf::Vector2f(20, 300), font));

    std::vector<std::unique_ptr<Button>> buttons;
    
    auto genBtn = std::make_unique<Button>("New Instance", sf::Vector2f(20, 350), sf::Vector2f(210, 40), font);
    genBtn->onClick = [&]() {
        activeGeomLS.reset(); activePermLS.reset();
        rects = InstanceGenerator::generate(numRects, minW, maxW, minH, maxH);
        currentSol = std::make_shared<PackingSolution>(boxSize);
    };
    buttons.push_back(std::move(genBtn));

    auto greedyBtn = std::make_unique<Button>("Run Greedy (Area)", sf::Vector2f(20, 400), sf::Vector2f(210, 40), font);
    greedyBtn->onClick = [&]() {
        activeGeomLS.reset(); activePermLS.reset();
        GreedyAlgorithm<Rectangle, PackingSolution> greedy(rects, std::make_unique<AreaStrategy>(), std::make_unique<PackingSolution>(boxSize), placePacking);
        currentSol = greedy.solve();
    };
    buttons.push_back(std::move(greedyBtn));

    auto lsRuleBtn = std::make_unique<Button>("Run LS (Swap)", sf::Vector2f(20, 450), sf::Vector2f(210, 40), font);
    lsRuleBtn->onClick = [&]() {
        activeGeomLS.reset();
        std::vector<Rectangle> badPerm = rects;
        SmallestFirstStrategy().sort(badPerm);
        auto startSol = std::make_unique<PermutationSolution>(badPerm, boxSize);
        activePermLS = std::make_unique<LocalSearch<PermutationSolution>>(std::move(startSol), std::make_unique<RandomizedSwapNeighborhood>());
    };
    buttons.push_back(std::move(lsRuleBtn));

    auto lsGeomBtn = std::make_unique<Button>("Run LS (Geometry)", sf::Vector2f(20, 500), sf::Vector2f(210, 40), font);
    lsGeomBtn->onClick = [&]() {
        activePermLS.reset();
        auto emptySol = std::make_unique<PackingSolution>(boxSize);
        GreedyAlgorithm<Rectangle, PackingSolution> greedy(rects, std::make_unique<SmallestFirstStrategy>(), std::move(emptySol), placePacking);
        activeGeomLS = std::make_unique<LocalSearch<PackingSolution>>(greedy.solve(), std::make_unique<RandomizedGeometryNeighborhood>());
    };
    buttons.push_back(std::move(lsGeomBtn));

    sf::Text statsText;
    statsText.setFont(font);
    statsText.setCharacterSize(16);
    statsText.setFillColor(sf::Color::Blue);
    statsText.setPosition(20, 600);

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) window.close();
            for (auto& in : inputs) in->handleEvent(event, window);
            for (auto& btn : buttons) btn->handleEvent(event, window);
        }

        // Perform algorithm steps if active
        if (activeGeomLS) {
            if (!activeGeomLS->performStep()) {
                currentSol = std::make_shared<PackingSolution>(activeGeomLS->getCurrentSolution());
                activeGeomLS.reset();
            } else {
                currentSol = std::make_shared<PackingSolution>(activeGeomLS->getCurrentSolution());
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
        statsText.setString("Boxes used: " + std::to_string(currentSol->boxes.size()));
        window.draw(statsText);
        window.display();
    }
    return 0;
}
