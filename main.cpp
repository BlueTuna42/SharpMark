#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <memory>

#include "struct.h"
#include "FFT.h"
#include "bmp.h"
#include "scan.h"
#include "XMP_tools.h"

class FocusCheckerApp {
private:
    const double focusConst = 0.12;
    FFTProcessor fftProcessor;

public:
    bool checkFocus(const std::string& inputFilename) {
        // Smart pointer, manages image memory
        auto image = ImageIO::readImage(inputFilename);
        if (!image) {
            std::cerr << "Error reading image: " << inputFilename << std::endl;
            return false;
        }

        auto fft = fftProcessor.forwardFFT(*image);
        fftProcessor.shift(*fft);

        double ER = fftProcessor.energyRatio(*fft);
        
        // ImageIO::writeJpg(*fft, "fft.jpg", 50);
        // std::cout << "ER: " << ER << std::endl;

        if (ER < focusConst) {
            XMPTools::writeXmpRating(inputFilename, 1);
            return false; // Blurry
        } else {
            XMPTools::writeXmpRating(inputFilename, 5);
            return true;  // Sharp
        }
    }
};

int main(int argc, char* argv[]) {
    std::string dirpath;
    
    if (argc > 1) {
        dirpath = argv[1];
    } else {
        std::cout << "Enter directory path: ";
        if (!std::getline(std::cin, dirpath)) {
            std::cerr << "Error reading input." << std::endl;
            return 1;
        }
    }

    std::vector<std::string> image_files = Scanner::scanBmpFiles(dirpath);
    std::cout << "Found " << image_files.size() << " image file(s):" << std::endl;

    std::ofstream outfile("BlurryList.txt");
    if (!outfile.is_open()) {
        std::cerr << "Failed to open BlurryList.txt for writing." << std::endl;
        return 1;
    }

    FocusCheckerApp app;

    for (const auto& file : image_files) {
        if (app.checkFocus(file)) {
            std::cout << file << " is sharp" << std::endl;
        } else {
            std::cout << file << " is blurry" << std::endl;
            outfile << file << std::endl;
        }
    }

    outfile.close();
    // Plan and image memory is now cleaned up automatically in destructors
    return 0;
}