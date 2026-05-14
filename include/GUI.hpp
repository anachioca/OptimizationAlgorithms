#ifndef GUI_HPP
#define GUI_HPP

#include <SFML/Graphics.hpp>
#include <string>
#include <functional>

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
public:
    int& value;
    sf::Text labelText;
    sf::Text valueText;
    Button plusBtn, minusBtn;

    NumericInput(std::string label, int& val, sf::Vector2f pos, sf::Font& font) 
        : value(val), 
          plusBtn("+", {pos.x + 140, pos.y}, {30, 30}, font),
          minusBtn("-", {pos.x + 100, pos.y}, {30, 30}, font) 
    {
        labelText.setFont(font);
        labelText.setString(label);
        labelText.setCharacterSize(14);
        labelText.setPosition(pos.x, pos.y + 5);
        labelText.setFillColor(sf::Color::Black);

        valueText.setFont(font);
        valueText.setCharacterSize(14);
        valueText.setPosition(pos.x + 175, pos.y + 5);
        valueText.setFillColor(sf::Color::Black);

        plusBtn.onClick = [&]() { value += 5; };
        minusBtn.onClick = [&]() { if (value > 5) value -= 5; };
    }

    void handleEvent(sf::Event& event, sf::RenderWindow& window) {
        plusBtn.handleEvent(event, window);
        minusBtn.handleEvent(event, window);
    }

    void draw(sf::RenderWindow& window) {
        valueText.setString(std::to_string(value));
        window.draw(labelText);
        window.draw(valueText);
        plusBtn.draw(window);
        minusBtn.draw(window);
    }
};

#endif
