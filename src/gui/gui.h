#ifndef GUI_H
#define GUI_H

#include <string>

struct AppSettings {
    int themeMode = 0; // 0: System, 1: Light, 2: Dark
    bool writeExif = false;
    int rawAnalysisMode = 0; // 0: Thumb, 1: Half size, 2: Full size
    int rawViewMode = 0;     // 0: Thumb, 1: Half size, 2: Full size
    bool cacheLaplacian = false;
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
    void TogglePause();
    bool IsPaused() const;
    std::string GetCurrentDir() const;
    
    AppSettings GetSettings() const;
};

#endif