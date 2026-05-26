#include "bmp.h"
#include <memory>
#include <string>
#include <future>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "../Lib/stb_image.h"
#include <libraw/libraw.h>

std::unique_ptr<ImageBuffer> ImageIO::readImage(const std::string& filename, int rawMode, bool wantRGB) {
    size_t extPos = filename.find_last_of('.');
    std::string ext = (extPos == std::string::npos) ? "" : filename.substr(extPos);

#ifdef _WIN32
    std::filesystem::path fs_path = std::filesystem::u8path(filename);
    std::wstring w_filename = fs_path.wstring();
#endif

    bool isRaw = (strcasecmp(ext.c_str(), ".CR2") == 0 || strcasecmp(ext.c_str(), ".NEF") == 0 ||
                  strcasecmp(ext.c_str(), ".ARW") == 0 || strcasecmp(ext.c_str(), ".DNG") == 0 ||
                  strcasecmp(ext.c_str(), ".RW2") == 0 || strcasecmp(ext.c_str(), ".RAF") == 0);

    if (isRaw) {
        libraw_data_t *lr = libraw_init(0);
        if (!lr) return nullptr;

        int openResult = LIBRAW_SUCCESS;
#ifdef _WIN32
        openResult = libraw_open_wfile(lr, w_filename.c_str());
#else
        openResult = libraw_open_file(lr, filename.c_str());
#endif

        if (openResult != LIBRAW_SUCCESS) {
            libraw_close(lr);
            return nullptr;
        }

        libraw_processed_image_t *img = nullptr;
    int err = 0;

    if (rawMode == 0) {
        if (libraw_unpack_thumb(lr) == LIBRAW_SUCCESS) {
            img = libraw_dcraw_make_mem_thumb(lr, &err);
    
            if (img && img->type == LIBRAW_IMAGE_JPEG) {
                libraw_dcraw_clear_mem(img);
                img = nullptr; 
            }
        }
    }

    if (!img) {
        lr->params.half_size = (rawMode == 2) ? 0 : 1; 
        
        libraw_unpack(lr);
        libraw_dcraw_process(lr);
        img = libraw_dcraw_make_mem_image(lr, &err);
    }
        
        if (!img) { 
            libraw_close(lr); 
            return nullptr; 
        }

        int channels = wantRGB ? 3 : 1;
        auto resultImg = std::make_unique<ImageBuffer>(img->width, img->height, channels);
        int total = img->width * img->height;

        auto process_chunk = [&](int start, int end) {
            for (int i = start; i < end; i++) {
                if (wantRGB) {
                    resultImg->data[i*3 + 0] = static_cast<float>(img->data[i*3 + 0]); // R
                    resultImg->data[i*3 + 1] = static_cast<float>(img->data[i*3 + 1]); // G
                    resultImg->data[i*3 + 2] = static_cast<float>(img->data[i*3 + 2]); // B
                } else {
                    resultImg->data[i] = 0.299f * img->data[i*3] + 
                                         0.587f * img->data[i*3+1] + 
                                         0.114f * img->data[i*3+2];
                }
            }
        };

        process_chunk(0, total);

        libraw_dcraw_clear_mem(img);
        libraw_close(lr);
        return resultImg;
    } else {
        int w, h, c;
        unsigned char *data = nullptr;

#ifdef _WIN32
        FILE* f = _wfopen(w_filename.c_str(), L"rb");
        if (f) {
            data = stbi_load_from_file(f, &w, &h, &c, 3);
            fclose(f);
        }
#else
        data = stbi_load(filename.c_str(), &w, &h, &c, 3);
#endif
        if (!data) return nullptr;

        int channels = wantRGB ? 3 : 1;
        auto resultImg = std::make_unique<ImageBuffer>(w, h, channels);
        int total = w * h;

        auto process_chunk = [&](int start, int end) {
            for (int i = start; i < end; i++) {
                if (wantRGB) {
                    resultImg->data[i*3 + 0] = static_cast<float>(data[i*3 + 0]);
                    resultImg->data[i*3 + 1] = static_cast<float>(data[i*3 + 1]);
                    resultImg->data[i*3 + 2] = static_cast<float>(data[i*3 + 2]);
                } else {
                    resultImg->data[i] = 0.299f * data[i*3] + 
                                         0.587f * data[i*3+1] + 
                                         0.114f * data[i*3+2];
                }
            }
        };

        int mid = total / 2;
        auto f1 = std::async(std::launch::async, process_chunk, 0, mid);
        process_chunk(mid, total);
        f1.wait();

        stbi_image_free(data);
        return resultImg;
    }
}

bool ImageIO::readOriginalSize(const std::string& filename, int& w, int& h) {
    size_t extPos = filename.find_last_of('.');
    std::string ext = (extPos == std::string::npos) ? "" : filename.substr(extPos);
    bool isRaw = (strcasecmp(ext.c_str(), ".CR2") == 0 || strcasecmp(ext.c_str(), ".NEF") == 0 ||
                  strcasecmp(ext.c_str(), ".ARW") == 0 || strcasecmp(ext.c_str(), ".DNG") == 0 ||
                  strcasecmp(ext.c_str(), ".RW2") == 0 || strcasecmp(ext.c_str(), ".RAF") == 0);

#ifdef _WIN32
    std::wstring w_filename = std::filesystem::u8path(filename).wstring();
#endif

    if (isRaw) {
        libraw_data_t *lr = libraw_init(0);
        if (!lr) return false;
        
        int openResult = LIBRAW_SUCCESS;
#ifdef _WIN32
        openResult = libraw_open_wfile(lr, w_filename.c_str());
#else
        openResult = libraw_open_file(lr, filename.c_str());
#endif
        if (openResult == LIBRAW_SUCCESS) {
            w = lr->sizes.width;
            h = lr->sizes.height;
            libraw_close(lr);
            return true;
        }
        libraw_close(lr);
    } else {
        int comp;
#ifdef _WIN32
        FILE* f = _wfopen(w_filename.c_str(), L"rb");
        if (f) {
            int res = stbi_info_from_file(f, &w, &h, &comp);
            fclose(f);
            return res != 0;
        }
#else
        return stbi_info(filename.c_str(), &w, &h, &comp) != 0;
#endif
    }
    return false;
}