#include "preview_loader.h"
#include "../../img_tools/bmp.h"

#include <algorithm>
#include <cmath>
#include <libraw/libraw.h>
#include <strings.h>

static void free_pixbuf_pixels(guchar* pixels, gpointer data) {
    g_free(pixels);
}

static GdkPixbuf* scale_pixbuf_to_fit(GdkPixbuf* pixbuf, int maxWidth, int maxHeight) {
    if (!pixbuf || maxWidth <= 0 || maxHeight <= 0) {
        return pixbuf;
    }

    const int width = gdk_pixbuf_get_width(pixbuf);
    const int height = gdk_pixbuf_get_height(pixbuf);
    const double scale = std::min(static_cast<double>(maxWidth) / width,
                                  static_cast<double>(maxHeight) / height);
    if (scale >= 1.0) {
        return pixbuf;
    }

    const int scaledWidth = std::max(1, static_cast<int>(std::lround(width * scale)));
    const int scaledHeight = std::max(1, static_cast<int>(std::lround(height * scale)));
    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pixbuf, scaledWidth, scaledHeight, GDK_INTERP_BILINEAR);
    g_object_unref(pixbuf);
    return scaled;
}

static bool is_raw_file(const std::string& filename) {
    const size_t extPos = filename.find_last_of('.');
    const std::string ext = (extPos == std::string::npos) ? "" : filename.substr(extPos);

    return strcasecmp(ext.c_str(), ".CR2") == 0 || strcasecmp(ext.c_str(), ".NEF") == 0 ||
           strcasecmp(ext.c_str(), ".ARW") == 0 || strcasecmp(ext.c_str(), ".DNG") == 0 ||
           strcasecmp(ext.c_str(), ".RW2") == 0 || strcasecmp(ext.c_str(), ".RAF") == 0;
}

static GdkPixbuf* create_pixbuf_from_rgb_data(const libraw_processed_image_t& image, int maxWidth, int maxHeight) {
    if (image.width <= 0 || image.height <= 0 || image.colors < 3) {
        return nullptr;
    }

    const int channels = 3;
    const int rowstride = image.width * channels;
    guchar* pixels = static_cast<guchar*>(g_malloc(static_cast<gsize>(rowstride) * image.height));

    if (image.bits == 16) {
        const unsigned short* source = reinterpret_cast<const unsigned short*>(image.data);
        for (unsigned int y = 0; y < image.height; ++y) {
            for (unsigned int x = 0; x < image.width; ++x) {
                const unsigned int sourceIndex = (y * image.width + x) * image.colors;
                const unsigned int targetIndex = y * rowstride + x * channels;
                pixels[targetIndex] = static_cast<guchar>(source[sourceIndex] >> 8);
                pixels[targetIndex + 1] = static_cast<guchar>(source[sourceIndex + 1] >> 8);
                pixels[targetIndex + 2] = static_cast<guchar>(source[sourceIndex + 2] >> 8);
            }
        }
    } else {
        for (unsigned int y = 0; y < image.height; ++y) {
            for (unsigned int x = 0; x < image.width; ++x) {
                const unsigned int sourceIndex = (y * image.width + x) * image.colors;
                const unsigned int targetIndex = y * rowstride + x * channels;
                pixels[targetIndex] = image.data[sourceIndex];
                pixels[targetIndex + 1] = image.data[sourceIndex + 1];
                pixels[targetIndex + 2] = image.data[sourceIndex + 2];
            }
        }
    }

    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_data(pixels,
                                                 GDK_COLORSPACE_RGB,
                                                 FALSE,
                                                 8,
                                                 image.width,
                                                 image.height,
                                                 rowstride,
                                                 free_pixbuf_pixels,
                                                 NULL);

    return scale_pixbuf_to_fit(pixbuf, maxWidth, maxHeight);
}

static GdkPixbuf* load_raw_preview_pixbuf(const std::string& filename, int maxWidth, int maxHeight) {
    libraw_data_t *lr = libraw_init(0);
    if (!lr) {
        return nullptr;
    }

    lr->params.half_size = 1;
    lr->params.output_bps = 8;

    GdkPixbuf* pixbuf = nullptr;
    if (libraw_open_file(lr, filename.c_str()) == LIBRAW_SUCCESS &&
        libraw_unpack(lr) == LIBRAW_SUCCESS &&
        libraw_dcraw_process(lr) == LIBRAW_SUCCESS) {
        int err = 0;
        libraw_processed_image_t *image = libraw_dcraw_make_mem_image(lr, &err);
        if (image && image->type == LIBRAW_IMAGE_BITMAP) {
            pixbuf = create_pixbuf_from_rgb_data(*image, maxWidth, maxHeight);
        }
        if (image) {
            libraw_dcraw_clear_mem(image);
        }
    }

    libraw_close(lr);
    return pixbuf;
}

static GdkPixbuf* create_pixbuf_from_grayscale(const GrayscaleImage& image, int maxWidth, int maxHeight) {
    const int channels = 3;
    const int rowstride = image.width * channels;
    guchar* pixels = static_cast<guchar*>(g_malloc(static_cast<gsize>(rowstride) * image.height));

    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const int sourceIndex = y * image.width + x;
            const int targetIndex = y * rowstride + x * channels;
            float sourceValue = image.data[sourceIndex];
            if (!std::isfinite(sourceValue)) {
                sourceValue = 0.0f;
            }

            const auto value = static_cast<guchar>(std::clamp(static_cast<int>(std::lround(sourceValue)), 0, 255));
            pixels[targetIndex] = value;
            pixels[targetIndex + 1] = value;
            pixels[targetIndex + 2] = value;
        }
    }

    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_data(pixels,
                                                 GDK_COLORSPACE_RGB,
                                                 FALSE,
                                                 8,
                                                 image.width,
                                                 image.height,
                                                 rowstride,
                                                 free_pixbuf_pixels,
                                                 NULL);

    return scale_pixbuf_to_fit(pixbuf, maxWidth, maxHeight);
}

GdkPixbuf* load_preview_pixbuf(const std::string& filename, int maxWidth, int maxHeight) {
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(filename.c_str(), maxWidth, maxHeight, TRUE, &error);
    if (pixbuf) {
        if (error) {
            g_error_free(error);
        }
        return pixbuf;
    }

    if (error) {
        g_error_free(error);
    }

    if (is_raw_file(filename)) {
        pixbuf = load_raw_preview_pixbuf(filename, maxWidth, maxHeight);
        if (pixbuf) {
            return pixbuf;
        }
    }

    auto image = ImageIO::readImage(filename, true);
    if (!image) {
        return nullptr;
    }

    return create_pixbuf_from_grayscale(*image, maxWidth, maxHeight);
}

GdkPixbuf* add_status_border(GdkPixbuf* pixbuf, bool isBlurry) {
    if (!pixbuf) {
        return nullptr;
    }

    constexpr int borderWidth = 4;
    const int sourceWidth = gdk_pixbuf_get_width(pixbuf);
    const int sourceHeight = gdk_pixbuf_get_height(pixbuf);
    GdkPixbuf* framed = gdk_pixbuf_new(GDK_COLORSPACE_RGB,
                                       gdk_pixbuf_get_has_alpha(pixbuf),
                                       8,
                                       sourceWidth + borderWidth * 2,
                                       sourceHeight + borderWidth * 2);
    if (!framed) {
        return pixbuf;
    }

    const guint32 borderColor = isBlurry ? 0xd62728ff : 0x2e7d32ff;
    gdk_pixbuf_fill(framed, borderColor);
    gdk_pixbuf_copy_area(pixbuf, 0, 0, sourceWidth, sourceHeight, framed, borderWidth, borderWidth);
    g_object_unref(pixbuf);
    return framed;
}
