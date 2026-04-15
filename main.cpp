#include <SFML/Graphics.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <cmath>

#include "struct.h"
#include "FFT.h"
#include "bmp.h"
#include "scan.h"
#include "XMP_tools.h"
#include "tinyfiledialogs.h"

namespace fs = std::filesystem;

// --- Цветовая палитра ---
namespace Theme {
    const sf::Color Bg = sf::Color(30, 30, 30);
    const sf::Color Sidebar = sf::Color(45, 45, 45);
    const sf::Color Text = sf::Color(220, 220, 220);
    const sf::Color Accent = sf::Color(0, 122, 204);
    const sf::Color AccentHover = sf::Color(30, 140, 220);
    const sf::Color Sharp = sf::Color(50, 200, 50);
    const sf::Color Blurry = sf::Color(220, 50, 50);
}

// --- Стилизованная кнопка ---
struct StyledButton {
    sf::RectangleShape rect;
    sf::Text text;
    bool isHovered = false;

    StyledButton(const std::string& label, sf::Font& font, sf::Vector2f pos) {
        text.setFont(font);
        text.setString(label);
        text.setCharacterSize(18);
        text.setFillColor(sf::Color::White);
        
        sf::FloatRect tr = text.getLocalBounds();
        rect.setSize({200, 45});
        rect.setFillColor(Theme::Accent);
        rect.setPosition(pos);
        
        // Центрируем текст
        text.setPosition(pos.x + (200 - tr.width) / 2.0f, pos.y + (45 - tr.height) / 2.0f - 5);
    }

    void update(const sf::Vector2i& mousePos) {
        isHovered = rect.getGlobalBounds().contains(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y));
        rect.setFillColor(isHovered ? Theme::AccentHover : Theme::Accent);
    }

    bool isClicked(sf::Event& event) {
        return isHovered && event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left;
    }

    void draw(sf::RenderWindow& window) {
        window.draw(rect);
        window.draw(text);
    }
};

class FocusCheckerApp {
private:
    const double focusConst = 0.12;
    FFTProcessor fftProcessor;
public:
    bool checkFocus(const std::string& f) {
        auto img = ImageIO::readImage(f);
        if (!img) return false;
        auto fft = fftProcessor.forwardFFT(*img);
        fftProcessor.shift(*fft);
        double ER = fftProcessor.energyRatio(*fft);
        
        // Запись рейтинга в XMP
        XMPTools::writeXmpRating(f, (ER >= focusConst) ? 5 : 1);
        return ER >= focusConst;
    }
};

void drawAnalysisChart(sf::RenderWindow& window, sf::Vector2f pos, int sharp, int blurry, sf::Font& font) {
    float total = sharp + blurry;
    float percentage = (total > 0) ? ((float)sharp / total) : 0.0f;
    
    float radius = 60.f;
    float thickness = 12.f;

    // 1. Фон кольца — Красный (Blurry)
    sf::CircleShape background(radius);
    background.setOrigin(radius, radius);
    background.setPosition(pos);
    background.setFillColor(sf::Color::Transparent);
    background.setOutlineThickness(-thickness);
    background.setOutlineColor(Theme::Blurry);
    window.draw(background);

    // 2. Сектор прогресса — Зеленый (Sharp)
    if (total > 0 && sharp > 0) {
        sf::VertexArray sector(sf::TriangleFan, 40);
        sector[0].position = pos;
        sector[0].color = Theme::Sharp;

        float angleLimit = percentage * 360.f;
        for (int i = 1; i < 40; ++i) {
            float angle = (i - 1) * angleLimit / 38.f - 90.f;
            float rad = angle * 3.14159f / 180.f;
            sector[i].position = pos + sf::Vector2f(std::cos(rad) * radius, std::sin(rad) * radius);
            sector[i].color = Theme::Sharp;
        }
        window.draw(sector);

        // Маска центра (дырка бублика)
        sf::CircleShape mask(radius - thickness);
        mask.setOrigin(radius - thickness, radius - thickness);
        mask.setPosition(pos);
        mask.setFillColor(Theme::Bg); // ИСПРАВЛЕНО: было Background, стало Bg
        window.draw(mask);
    }

    // 3. Текст с % в центре
    sf::Text pctText(std::to_string((int)(percentage * 100)) + "%", font, 20);
    pctText.setFillColor(sf::Color::White);
    sf::FloatRect b = pctText.getLocalBounds();
    pctText.setOrigin(b.left + b.width/2.0f, b.top + b.height/2.0f);
    pctText.setPosition(pos);
    window.draw(pctText);
}

int main() {
    sf::RenderWindow window(sf::VideoMode(900, 600), "Focus Analyzer GUI", sf::Style::Titlebar | sf::Style::Close);
    window.setFramerateLimit(60);

    sf::Font font;
    if (!font.loadFromFile("arial.ttf")) {
        std::cerr << "arial.ttf not found!" << std::endl;
        return 1;
    }

    sf::RectangleShape sidebar(sf::Vector2f(250, 600));
    sidebar.setFillColor(Theme::Sidebar);

    StyledButton analyzeButton("Analyze Folder", font, {25, 30});
    
    sf::Text statsTitle("Statistics", font, 22);
    statsTitle.setPosition(35, 110);
    statsTitle.setFillColor(Theme::Text);

    sf::Text statsContent("No data", font, 16);
    statsContent.setPosition(35, 150);
    statsContent.setFillColor(sf::Color(180, 180, 180));

    FocusCheckerApp app;
    struct ResultEntry { std::string name; bool isSharp; };
    std::vector<ResultEntry> results;

    bool isAnalyzing = false;
    float progress = 0.0f;
    size_t totalFiles = 0;
    size_t processedFiles = 0;

    while (window.isOpen()) {
        sf::Vector2i mousePos = sf::Mouse::getPosition(window);
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) window.close();
            
            if (analyzeButton.isClicked(event)) {
                const char* path = tinyfd_selectFolderDialog("Select Folder", "");

                if (path) {
                    results.clear();
                    auto files = Scanner::scanBmpFiles(path);
                    totalFiles = files.size();

                    if (totalFiles == 0) {
                        results.clear();
                        isAnalyzing = false;
                        continue; // ← выходим, ничего не анализируем
                    }

                    processedFiles = 0;
                    progress = 0.f;
                    isAnalyzing = true;

                    for (const auto& f : files) {
                        sf::Event e;
                        while (window.pollEvent(e)) {
                            if (e.type == sf::Event::Closed) {
                                window.close();
                                return 0;
                            }
                        }
                        // Обновляем прогресс
                        processedFiles++;
                        progress = (totalFiles > 0) 
                            ? static_cast<float>(processedFiles) / totalFiles 
                            : 0.f;
                        // Анализ
                        bool res = app.checkFocus(f);
                        results.push_back({ fs::path(f).filename().string(), res });

                        // --- ХИТРОСТЬ ДЛЯ ОБНОВЛЕНИЯ GUI ---
                        // Отрисовываем полоску прямо во время цикла, чтобы она двигалась
                        window.clear(Theme::Bg);
                        window.draw(sidebar);
                        analyzeButton.draw(window);
                        
                        // Рисуем временный индикатор загрузки
                        sf::RectangleShape progressBar(sf::Vector2f(200.f * progress, 10.f));
                        progressBar.setPosition(25.f, 85.f);
                        progressBar.setFillColor(Theme::Accent);
                        window.draw(progressBar);

                        sf::Text loadingText("Processing: " + std::to_string(processedFiles) + "/" + std::to_string(totalFiles), font, 14);
                        loadingText.setPosition(25.f, 100.f);
                        loadingText.setFillColor(Theme::Text);
                        window.draw(loadingText);

                        window.display(); 
                        // ----------------------------------
                    }
                    isAnalyzing = false;
                }
            }
        }

        analyzeButton.update(mousePos);

        window.clear(Theme::Bg);
        window.draw(sidebar);
        analyzeButton.draw(window);
        //window.draw(statsTitle);
        //window.draw(statsContent);

    // --- СЕКЦИЯ ЛЕВОЙ ПАНЕЛИ (СТАТИСТИКА ИЛИ ПРОГРЕСС) ---
    if (isAnalyzing) {
        // 1. Рисуем подложку прогресс-бара
        sf::RectangleShape barBg({200.f, 20.f});
        barBg.setPosition(25.f, 150.f);
        barBg.setFillColor(sf::Color(50, 50, 50));
        window.draw(barBg);

        // 2. Рисуем заполнение прогресс-бара
        sf::RectangleShape barFill({200.f * progress, 20.f});
        barFill.setPosition(25.f, 150.f);
        barFill.setFillColor(Theme::Accent);
        window.draw(barFill);

        // 3. Текст "Processing..."
        sf::Text loadingText("Processing: " + std::to_string(processedFiles) + "/" + std::to_string(totalFiles), font, 16);
        loadingText.setPosition(25.f, 180.f);
        loadingText.setFillColor(Theme::Text);
        window.draw(loadingText);

    } else if (!results.empty()) {
        // Если анализ завершен — рисуем твою кольцевую диаграмму
        int sharpCount = 0;
        for (const auto& res : results) if (res.isSharp) sharpCount++;
        int blurryCount = static_cast<int>(results.size()) - sharpCount;

        drawAnalysisChart(window, sf::Vector2f(130.f, 150.f), sharpCount, blurryCount, font);

        sf::Text statsText("Sharp: " + std::to_string(sharpCount) + 
                        "\nBlurry: " + std::to_string(blurryCount), font, 16);
        statsText.setPosition(80.f, 230.f);
        statsText.setFillColor(Theme::Text);
        window.draw(statsText);
    }

    // --- СЕКЦИЯ СПИСКА ФАЙЛОВ (СПРАВА) ---
    const float LIST_START_X = 280.f;
    const float LIST_START_Y = 40.f;
    const float ROW_HEIGHT   = 25.f;

    for (size_t i = 0; i < results.size() && i < 20; ++i) {
        float currentY = LIST_START_Y + i * ROW_HEIGHT;

        // Индикатор (цветная точка)
        sf::CircleShape indicator(6.f);
        indicator.setPosition(LIST_START_X, currentY + 5.f);
        indicator.setFillColor(results[i].isSharp ? Theme::Sharp : Theme::Blurry);
        
        // Имя файла
        sf::Text fileName(results[i].name, font, 16);
        fileName.setPosition(LIST_START_X + 20.f, currentY);
        fileName.setFillColor(Theme::Text);

        window.draw(indicator);
        window.draw(fileName);
    }

    window.display();
    }
    return 0;
}