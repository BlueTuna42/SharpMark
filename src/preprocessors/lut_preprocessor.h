#pragma once
#include "../pipeline/interfaces.h"
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <QDebug>
#include <QImage>

// Trilinear 3D LUT application.
// lut layout:  flat [C=3][B][G][R]  all indices in [0, lut_dim-1]
// tensor layout: CHW interleaved, values in [0, 1]
// targetSize: width == height of the square tensor (targetSize * targetSize pixels)

inline void applyLutToChwTensor(std::vector<float>& chw, const std::vector<float>& lut, int targetSize, int lut_dim) {
    const int stride  = targetSize * targetSize;
    const int dim2    = lut_dim * lut_dim;
    const int dim3    = lut_dim * lut_dim * lut_dim;

    float* r_plane = chw.data();
    float* g_plane = chw.data() + stride;
    float* b_plane = chw.data() + stride * 2;

    auto get_val = [&](int c, int b_idx, int g_idx, int r_idx) -> float {
        return lut[c * dim3 + b_idx * dim2 + g_idx * lut_dim + r_idx];
    };

    for (int i = 0; i < stride; ++i) {
        float r = r_plane[i] * (lut_dim - 1);
        float g = g_plane[i] * (lut_dim - 1);
        float b = b_plane[i] * (lut_dim - 1);

        int r0 = std::clamp(static_cast<int>(r), 0, lut_dim - 2);
        int g0 = std::clamp(static_cast<int>(g), 0, lut_dim - 2);
        int b0 = std::clamp(static_cast<int>(b), 0, lut_dim - 2);
        int r1 = r0 + 1, g1 = g0 + 1, b1 = b0 + 1;

        float rd = r - r0, gd = g - g0, bd = b - b0;

        for (int c = 0; c < 3; ++c) {
            float c000 = get_val(c, b0, g0, r0);
            float c100 = get_val(c, b0, g0, r1);
            float c010 = get_val(c, b0, g1, r0);
            float c110 = get_val(c, b0, g1, r1);
            float c001 = get_val(c, b1, g0, r0);
            float c101 = get_val(c, b1, g0, r1);
            float c011 = get_val(c, b1, g1, r0);
            float c111 = get_val(c, b1, g1, r1);

            float c00 = c000 * (1 - rd) + c100 * rd;
            float c01 = c001 * (1 - rd) + c101 * rd;
            float c10 = c010 * (1 - rd) + c110 * rd;
            float c11 = c011 * (1 - rd) + c111 * rd;
            float c0  = c00  * (1 - gd) + c10  * gd;
            float c1  = c01  * (1 - gd) + c11  * gd;
            float val = c0   * (1 - bd) + c1   * bd;

            val = std::clamp(val, 0.0f, 1.0f);
            if (c == 0) r_plane[i] = val;
            else if (c == 1) g_plane[i] = val;
            else b_plane[i] = val;
        }
    }
}

// ---------------------------------------------------------------------------
// Apply a 3D LUT in-place to a QImage (Format_RGB888 or Format_RGB32/ARGB32).
// Returns the corrected QImage (same dimensions, Format_RGB888).
// lut layout: flat [C=3][B][G][R], values in [0,1].
// ---------------------------------------------------------------------------

inline QImage applyLutToQImage(const QImage& src, const std::vector<float>& lut, int lut_dim) {
    if (lut.empty() || lut_dim < 2) return src;

    QImage img = src.convertToFormat(QImage::Format_RGB888);
    const int w = img.width();
    const int h = img.height();
    const int n = w * h;

    // Build CHW tensor [0,1]
    std::vector<float> chw(3 * n);
    float* rp = chw.data();
    float* gp = chw.data() + n;
    float* bp = chw.data() + n * 2;

    for (int y = 0; y < h; ++y) {
        const uchar* row = img.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            int i = y * w + x;
            rp[i] = row[x * 3 + 0] / 255.0f;
            gp[i] = row[x * 3 + 1] / 255.0f;
            bp[i] = row[x * 3 + 2] / 255.0f;
        }
    }

    // Apply LUT (reuse the flat square logic — use w*h pixels, treat as 1D)
    const int dim2 = lut_dim * lut_dim;
    const int dim3 = lut_dim * lut_dim * lut_dim;
    auto get_val = [&](int c, int b_idx, int g_idx, int r_idx) -> float {
        return lut[c * dim3 + b_idx * dim2 + g_idx * lut_dim + r_idx];
    };

    for (int i = 0; i < n; ++i) {
        float r = rp[i] * (lut_dim - 1);
        float g = gp[i] * (lut_dim - 1);
        float b = bp[i] * (lut_dim - 1);

        int r0 = std::clamp(static_cast<int>(r), 0, lut_dim - 2);
        int g0 = std::clamp(static_cast<int>(g), 0, lut_dim - 2);
        int b0 = std::clamp(static_cast<int>(b), 0, lut_dim - 2);
        int r1 = r0 + 1, g1 = g0 + 1, b1 = b0 + 1;

        float rd = r - r0, gd = g - g0, bd = b - b0;

        for (int c = 0; c < 3; ++c) {
            float c000 = get_val(c, b0, g0, r0);
            float c100 = get_val(c, b0, g0, r1);
            float c010 = get_val(c, b0, g1, r0);
            float c110 = get_val(c, b0, g1, r1);
            float c001 = get_val(c, b1, g0, r0);
            float c101 = get_val(c, b1, g0, r1);
            float c011 = get_val(c, b1, g1, r0);
            float c111 = get_val(c, b1, g1, r1);

            float c00 = c000 * (1 - rd) + c100 * rd;
            float c01 = c001 * (1 - rd) + c101 * rd;
            float c10 = c010 * (1 - rd) + c110 * rd;
            float c11 = c011 * (1 - rd) + c111 * rd;
            float c0  = c00  * (1 - gd) + c10  * gd;
            float c1  = c01  * (1 - gd) + c11  * gd;
            float val = c0   * (1 - bd) + c1   * bd;

            val = std::clamp(val, 0.0f, 1.0f);
            if (c == 0) rp[i] = val;
            else if (c == 1) gp[i] = val;
            else bp[i] = val;
        }
    }

    // Write back
    for (int y = 0; y < h; ++y) {
        uchar* row = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            int i = y * w + x;
            row[x * 3 + 0] = static_cast<uchar>(std::clamp(rp[i] * 255.0f, 0.0f, 255.0f));
            row[x * 3 + 1] = static_cast<uchar>(std::clamp(gp[i] * 255.0f, 0.0f, 255.0f));
            row[x * 3 + 2] = static_cast<uchar>(std::clamp(bp[i] * 255.0f, 0.0f, 255.0f));
        }
    }

    return img;
}

class LutPreprocessor : public IImagePreprocessor {
public:
    static constexpr int DEFAULT_DIM = 33;

    LutPreprocessor() {
        setIdentity(DEFAULT_DIM);
    }

    std::string name() const override { return "lut_3d"; }

    void setIdentity(int dim = DEFAULT_DIM) {
        m_dim = dim;
        const int total = 3 * dim * dim * dim;
        m_lut.resize(total);
        // Identity: output[c][b][g][r] = input channel value normalised to [0,1]
        // Channel order in lut: [C][B][G][R] where C=0→R, C=1→G, C=2→B
        const int dim2 = dim * dim;
        const int dim3 = dim * dim * dim;
        for (int b = 0; b < dim; ++b)
        for (int g = 0; g < dim; ++g)
        for (int r = 0; r < dim; ++r) {
            int idx = b * dim2 + g * dim + r;
            m_lut[0 * dim3 + idx] = static_cast<float>(r) / (dim - 1); // R channel
            m_lut[1 * dim3 + idx] = static_cast<float>(g) / (dim - 1); // G channel
            m_lut[2 * dim3 + idx] = static_cast<float>(b) / (dim - 1); // B channel
        }
        m_isIdentity = true;
    }

    // Set a custom LUT. data must be in standard .cube order:
    // R-major (R changes fastest), values are [R_out, G_out, B_out] triplets.
    // We repack into our [C][B][G][R] layout.
    void setLut(const std::vector<float>& cubeData, int dim) {
        m_dim = dim;
        const int dim3 = dim * dim * dim;
        m_lut.resize(3 * dim3);
        // cubeData layout: flat list of dim^3 triplets, R-major (r fastest, b slowest)
        // index in cubeData: (b * dim*dim + g * dim + r) * 3 + channel
        for (int b = 0; b < dim; ++b)
        for (int g = 0; g < dim; ++g)
        for (int r = 0; r < dim; ++r) {
            int cubeIdx = (b * dim * dim + g * dim + r) * 3;
            int lutIdx  =  b * dim * dim + g * dim + r;   // position in [B][G][R] space
            m_lut[0 * dim3 + lutIdx] = cubeData[cubeIdx + 0]; // R out
            m_lut[1 * dim3 + lutIdx] = cubeData[cubeIdx + 1]; // G out
            m_lut[2 * dim3 + lutIdx] = cubeData[cubeIdx + 2]; // B out
        }
        m_isIdentity = false;
    }

    // Returns true when no transform will be applied (identity or disabled).
    bool isIdentity() const { return m_isIdentity; }
    int  dim()        const { return m_dim; }
    const std::vector<float>& lutData() const { return m_lut; }

    static std::vector<float> parseCubeFile(const std::filesystem::path& path, int& outDim) {
        std::ifstream f(path);
        if (!f.is_open())
            throw std::runtime_error("Cannot open .cube file: " + path.string());

        outDim = 0;
        std::vector<float> data;
        std::string line;

        while (std::getline(f, line)) {
            // Trim leading whitespace
            auto start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            line = line.substr(start);

            if (line.empty() || line[0] == '#') continue;

            if (line.rfind("LUT_3D_SIZE", 0) == 0) {
                std::istringstream ss(line);
                std::string key; ss >> key >> outDim;
                continue;
            }
            if (line.rfind("DOMAIN_", 0) == 0) continue;
            if (line.rfind("TITLE",   0) == 0) continue;
            if (line.rfind("LUT_1D",  0) == 0) continue;

            // Data line: three floats
            std::istringstream ss(line);
            float rv, gv, bv;
            if (ss >> rv >> gv >> bv) {
                data.push_back(rv);
                data.push_back(gv);
                data.push_back(bv);
            }
        }

        if (outDim <= 0)
            throw std::runtime_error(".cube file missing LUT_3D_SIZE");

        const int expected = outDim * outDim * outDim * 3;
        if (static_cast<int>(data.size()) < expected) {
            qWarning() << "[LUT] .cube file has" << data.size()
                       << "values, expected" << expected
                       << ". Padding with identity.";
            data.resize(expected, 0.0f);
        }

        return data;
    }

    void preprocess(std::unique_ptr<GrayscaleImage>& image,
                    const ProcessingContext& /*ctx*/,
                    ProcessingResult& /*result*/) override
    {
        if (!image || m_isIdentity) return;
        if (image->channels != 3) return; // grayscale — nothing meaningful to do

        const int w = image->width;
        const int h = image->height;
        const int n = w * h;
        const int dim3 = m_dim * m_dim * m_dim;
        const int dim2 = m_dim * m_dim;

        // Build a temporary CHW tensor in [0,1] from the HWC [0,255] buffer
        std::vector<float> chw(3 * n);
        float* rp = chw.data();
        float* gp = chw.data() + n;
        float* bp = chw.data() + n * 2;

        const float* src = image->data;
        for (int i = 0; i < n; ++i) {
            rp[i] = src[i * 3 + 0] / 255.0f;
            gp[i] = src[i * 3 + 1] / 255.0f;
            bp[i] = src[i * 3 + 2] / 255.0f;
        }

        // Trilinear LUT application
        auto get_val = [&](int c, int b_idx, int g_idx, int r_idx) -> float {
            return m_lut[c * dim3 + b_idx * dim2 + g_idx * m_dim + r_idx];
        };

        for (int i = 0; i < n; ++i) {
            float r = rp[i] * (m_dim - 1);
            float g = gp[i] * (m_dim - 1);
            float b = bp[i] * (m_dim - 1);

            int r0 = std::clamp(static_cast<int>(r), 0, m_dim - 2);
            int g0 = std::clamp(static_cast<int>(g), 0, m_dim - 2);
            int b0 = std::clamp(static_cast<int>(b), 0, m_dim - 2);
            int r1 = r0 + 1, g1 = g0 + 1, b1 = b0 + 1;

            float rd = r - r0, gd = g - g0, bd = b - b0;

            for (int c = 0; c < 3; ++c) {
                float c000 = get_val(c, b0, g0, r0);
                float c100 = get_val(c, b0, g0, r1);
                float c010 = get_val(c, b0, g1, r0);
                float c110 = get_val(c, b0, g1, r1);
                float c001 = get_val(c, b1, g0, r0);
                float c101 = get_val(c, b1, g0, r1);
                float c011 = get_val(c, b1, g1, r0);
                float c111 = get_val(c, b1, g1, r1);

                float c00 = c000*(1-rd) + c100*rd;
                float c01 = c001*(1-rd) + c101*rd;
                float c10 = c010*(1-rd) + c110*rd;
                float c11 = c011*(1-rd) + c111*rd;
                float c0  = c00 *(1-gd) + c10 *gd;
                float c1  = c01 *(1-gd) + c11 *gd;
                float val = c0  *(1-bd) + c1  *bd;

                val = std::clamp(val, 0.0f, 1.0f);
                if (c == 0) rp[i] = val;
                else if (c == 1) gp[i] = val;
                else bp[i] = val;
            }
        }

        // Write back to HWC [0,255]
        float* dst = image->data;
        for (int i = 0; i < n; ++i) {
            dst[i * 3 + 0] = rp[i] * 255.0f;
            dst[i * 3 + 1] = gp[i] * 255.0f;
            dst[i * 3 + 2] = bp[i] * 255.0f;
        }
    }

private:
    std::vector<float> m_lut;
    int  m_dim       = DEFAULT_DIM;
    bool m_isIdentity = true;
};
