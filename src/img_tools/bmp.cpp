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
#include "../tools/raw_mutex.h"

std::unique_ptr<ImageBuffer> ImageIO::readImage(const QString& filename, int rawMode, bool wantRGB) {
#ifdef _WIN32
    std::filesystem::path fs_path(filename.toStdWString());
#else
    std::filesystem::path fs_path(filename.toUtf8().constData());
#endif
    std::string ext = fs_path.extension().string();

#ifdef _WIN32
    std::wstring w_filename = fs_path.wstring();
#endif

    bool isRaw = (strcasecmp(ext.c_str(), ".CR2") == 0 || strcasecmp(ext.c_str(), ".NEF") == 0 ||
                  strcasecmp(ext.c_str(), ".ARW") == 0 || strcasecmp(ext.c_str(), ".DNG") == 0 ||
                  strcasecmp(ext.c_str(), ".RW2") == 0 || strcasecmp(ext.c_str(), ".RAF") == 0);

    if (isRaw) {
        LibRaw lr;
        lr.imgdata.params.use_camera_wb = 1;

        int openResult = LIBRAW_SUCCESS;
#ifdef _WIN32
        openResult = lr.open_file(w_filename.c_str());
#else
        openResult = lr.open_file(fs_path.c_str());
#endif

        if (openResult != LIBRAW_SUCCESS) {
            return nullptr;
        }

        libraw_processed_image_t *img = nullptr;
        int err = 0;

        if (rawMode == 0) {
            if (lr.unpack_thumb() == LIBRAW_SUCCESS) {
                img = lr.dcraw_make_mem_thumb(&err);
                if (img && img->type == LIBRAW_IMAGE_JPEG) {
                    LibRaw::dcraw_clear_mem(img);
                    img = nullptr; 
                }
            }
        }

        if (!img) {
            lr.imgdata.params.half_size = (rawMode == 2) ? 0 : 1; 
            if (lr.unpack() != LIBRAW_SUCCESS || lr.dcraw_process() != LIBRAW_SUCCESS) {
                lr.recycle();
                return nullptr;
            }
            img = lr.dcraw_make_mem_image(&err);
        }
        
        if (!img) { 
            lr.recycle(); 
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

        LibRaw::dcraw_clear_mem(img);
        lr.recycle();
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
        data = stbi_load(fs_path.c_str(), &w, &h, &c, 3);
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

bool ImageIO::readOriginalSize(const QString& filename, int& w, int& h) {
#ifdef _WIN32
    std::filesystem::path fs_path(filename.toStdWString());
    std::wstring w_filename = fs_path.wstring();
#else
    std::filesystem::path fs_path(filename.toUtf8().constData());
#endif
    std::string ext = fs_path.extension().string();
    bool isRaw = (strcasecmp(ext.c_str(), ".CR2") == 0 || strcasecmp(ext.c_str(), ".NEF") == 0 ||
                  strcasecmp(ext.c_str(), ".ARW") == 0 || strcasecmp(ext.c_str(), ".DNG") == 0 ||
                  strcasecmp(ext.c_str(), ".RW2") == 0 || strcasecmp(ext.c_str(), ".RAF") == 0);

    if (isRaw) {
        LibRaw lr;
        int openResult = LIBRAW_SUCCESS;
#ifdef _WIN32
        openResult = lr.open_file(w_filename.c_str());
#else
        openResult = lr.open_file(fs_path.c_str());
#endif
        if (openResult == LIBRAW_SUCCESS) {
            w = lr.imgdata.sizes.width;
            h = lr.imgdata.sizes.height;
            lr.recycle();
            return true;
        }
        lr.recycle();
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
        return stbi_info(fs_path.c_str(), &w, &h, &comp) != 0;
#endif
    }
    return false;
}