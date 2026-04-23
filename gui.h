#ifndef GUI_H
#define GUI_H

#include <string>

class VisualGUI {
public:
    VisualGUI();
    ~VisualGUI();

    std::string SelectDirectory();
    void AddResult(const std::string& filename, bool isBlurry);
    void ShowFinished();
};

#endif