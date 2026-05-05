#ifndef GUI_H
#define GUI_H

#include <string>

struct AppSettings {
    int themeMode = 0; // 0: System, 1: Light, 2: Dark
    bool writeExif = false;
    int rawMode = 0;   // 0: Half size, 1: Full size, 2: Preview
};

class VisualGUI {
public:
    VisualGUI();
    ~VisualGUI();

    std::string SelectDirectory();
    void SetCurrentDirectory(const std::string& dirpath);
    void AddResult(const std::string& filename, bool isBlurry);
    void ResetProgress(int totalFiles);
    void UpdateProgress(int processedFiles, int totalFiles);
    void ShowFinished(int sharpFiles, int blurryFiles);
    bool IsClosed() const;
    
    AppSettings GetSettings() const;
};

#endif