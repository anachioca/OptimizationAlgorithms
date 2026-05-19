#ifndef VISUALIZER_HPP
#define VISUALIZER_HPP

#include <SFML/Graphics.hpp>
#include "RectanglePacking.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <random>

class Visualizer {
private:
    float scale = 1.0f;
    float padding = 10.0f;
    sf::Font font;
    bool fontLoaded = false;

    sf::Color colorBox = sf::Color(200, 200, 200);      
    sf::Color colorRect = sf::Color(70, 130, 180);     
    sf::Color colorBackground = sf::Color::White;

public:
    float sidebarWidth = 250.0f;

    Visualizer() {
        if (font.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")) {
            fontLoaded = true;
        }
    }

    void draw(sf::RenderWindow& window, const PackingSolution& sol) {
        // Sidebar area background
        sf::RectangleShape sidebarBg({sidebarWidth, (float)window.getSize().y});
        sidebarBg.setFillColor(sf::Color(230, 230, 230));
        window.draw(sidebarBg);

        if (sol.boxes.empty()) return;

        int L = sol.L;
        float drawAreaWidth = (float)window.getSize().x - sidebarWidth;
        float drawAreaHeight = (float)window.getSize().y;
        
        int numBoxes = sol.boxes.size();
        int cols = std::ceil(std::sqrt(numBoxes));
        int rows = std::ceil((float)numBoxes / cols);

        float boxTotalSize = (float)L + padding;
        float neededWidth = cols * boxTotalSize;
        float neededHeight = rows * boxTotalSize;

        scale = std::min((drawAreaWidth - padding*2) / neededWidth, (drawAreaHeight - padding*2) / neededHeight);
        
        for (int i = 0; i < numBoxes; ++i) {
            int r = i / cols;
            int c = i % cols;

            float offsetX = sidebarWidth + padding + c * boxTotalSize * scale;
            float offsetY = padding + r * boxTotalSize * scale;

            sf::RectangleShape boxShape(sf::Vector2f(L * scale, L * scale));
            boxShape.setPosition(offsetX, offsetY);
            boxShape.setOutlineThickness(1.0f);
            boxShape.setOutlineColor(sf::Color::Black);
            boxShape.setFillColor(colorBox);
            window.draw(boxShape);

            for (const auto& rect : sol.boxes[i].rectangles) {
                sf::RectangleShape rectShape(sf::Vector2f(rect.getW() * scale, rect.getH() * scale));
                rectShape.setPosition(offsetX + rect.x * scale, offsetY + rect.y * scale);
                
                // Wong colorblind-safe palette (avoids red/green confusion)
                static const sf::Color palette[] = {
                    { 86, 180, 233},  // sky blue
                    {230, 159,   0},  // orange
                    {  0, 158, 115},  // bluish green
                    {240, 228,  66},  // yellow
                    {  0, 114, 178},  // blue
                    {213,  94,   0},  // vermillion
                    {204, 121, 167},  // reddish purple
                };
                rectShape.setFillColor(palette[rect.id % 7]);
                
                rectShape.setOutlineThickness(0.5f);
                rectShape.setOutlineColor(sf::Color::Black);
                window.draw(rectShape);
            }
        }
    }
};

#endif
