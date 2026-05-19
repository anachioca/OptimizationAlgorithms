#ifndef GUI_HPP
#define GUI_HPP

#include <SFML/Graphics.hpp>
#include <string>
#include <functional>
#include <algorithm>

class Button {
public:
    sf::RectangleShape shape;
    sf::Text text;
    std::function<void()> onClick;

    Button(std::string label, sf::Vector2f pos, sf::Vector2f size, sf::Font& font) {
        shape.setPosition(pos);
        shape.setSize(size);
        shape.setFillColor(sf::Color(100, 100, 100));
        shape.setOutlineThickness(2);
        shape.setOutlineColor(sf::Color::Black);

        text.setFont(font);
        text.setString(label);
        text.setCharacterSize(16);
        text.setFillColor(sf::Color::White);
        
        sf::FloatRect textRect = text.getLocalBounds();
        text.setOrigin(textRect.left + textRect.width/2.0f, textRect.top  + textRect.height/2.0f);
        text.setPosition(pos.x + size.x/2.0f, pos.y + size.y/2.0f);
    }

    bool handleEvent(sf::Event& event, sf::RenderWindow& window) {
        if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
            sf::Vector2f mousePos(static_cast<float>(event.mouseButton.x), static_cast<float>(event.mouseButton.y));
            if (shape.getGlobalBounds().contains(mousePos)) {
                if (onClick) onClick();
                return true;
            }
        }
        return false;
    }

    void draw(sf::RenderWindow& window) {
        sf::Vector2i mPos = sf::Mouse::getPosition(window);
        if (shape.getGlobalBounds().contains(static_cast<float>(mPos.x), static_cast<float>(mPos.y))) {
            shape.setFillColor(sf::Color(130, 130, 130));
        } else {
            shape.setFillColor(sf::Color(100, 100, 100));
        }
        window.draw(shape);
        window.draw(text);
    }
};

class NumericInput {
    int minValue;
    bool focused = false;
    std::string inputBuffer;
    sf::RectangleShape valueBox;

    void commit() {
        if (!inputBuffer.empty()) {
            try { value = std::max(minValue, std::stoi(inputBuffer)); }
            catch (...) {}
            inputBuffer = "";
        }
        focused = false;
    }

public:
    int& value;
    sf::Text labelText;
    sf::Text valueText;
    Button plusBtn, minusBtn;

    NumericInput(std::string label, int& val, sf::Vector2f pos, sf::Font& font, int minVal = 1)
        : value(val), minValue(minVal),
          plusBtn("+", {pos.x + 160, pos.y}, {28, 28}, font),
          minusBtn("-", {pos.x + 128, pos.y}, {28, 28}, font)
    {
        labelText.setFont(font);
        labelText.setString(label);
        labelText.setCharacterSize(14);
        labelText.setPosition(pos.x, pos.y + 7);
        labelText.setFillColor(sf::Color::Black);

        valueBox.setPosition({pos.x + 68, pos.y});
        valueBox.setSize({56, 28});
        valueBox.setFillColor(sf::Color::White);
        valueBox.setOutlineThickness(1);
        valueBox.setOutlineColor(sf::Color(150, 150, 150));

        valueText.setFont(font);
        valueText.setCharacterSize(14);
        valueText.setPosition(pos.x + 72, pos.y + 7);
        valueText.setFillColor(sf::Color::Black);

        plusBtn.onClick  = [&]() { commit(); value += 5; };
        minusBtn.onClick = [&]() { commit(); value = std::max(minValue, value - 5); };
    }

    void handleEvent(sf::Event& event, sf::RenderWindow& window) {
        plusBtn.handleEvent(event, window);
        minusBtn.handleEvent(event, window);

        if (event.type == sf::Event::MouseButtonPressed) {
            sf::Vector2f mp(event.mouseButton.x, event.mouseButton.y);
            if (valueBox.getGlobalBounds().contains(mp)) {
                focused = true;
                inputBuffer = std::to_string(value);
            } else {
                commit();
            }
        }

        if (focused && event.type == sf::Event::TextEntered) {
            sf::Uint32 c = event.text.unicode;
            if (c >= '0' && c <= '9' && inputBuffer.size() < 5) {
                inputBuffer += static_cast<char>(c);
                try { value = std::max(minValue, std::stoi(inputBuffer)); }
                catch (...) {}
            } else if (c == 8 && !inputBuffer.empty()) { // backspace
                inputBuffer.pop_back();
                if (!inputBuffer.empty()) {
                    try { value = std::max(minValue, std::stoi(inputBuffer)); }
                    catch (...) {}
                }
            } else if (c == 13) { // enter
                commit();
            }
        }

        if (focused && event.type == sf::Event::KeyPressed &&
                event.key.code == sf::Keyboard::Escape) {
            focused = false;
            inputBuffer = "";
        }
    }

    void draw(sf::RenderWindow& window) {
        valueBox.setOutlineColor(focused ? sf::Color(0, 120, 215) : sf::Color(150, 150, 150));
        window.draw(labelText);
        window.draw(valueBox);
        std::string display = focused ? inputBuffer + "|" : std::to_string(value);
        valueText.setString(display);
        window.draw(valueText);
        plusBtn.draw(window);
        minusBtn.draw(window);
    }
};

#endif
