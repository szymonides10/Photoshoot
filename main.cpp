#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <numeric>
#include <cmath>
#include <algorithm>
#include <random>
#include "tinyfiledialogs.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <cstdint>
#include <functional>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "GLFW/stb_image.h"
#include "GLFW/stb_image_write.h"

// -------------------------------------------------------------------------------------------+
// Program używa gotowej biblioteki stb_image, algorytmy działają na poszczególnych pikselach |
// -------------------------------------------------------------------------------------------+

// --- Global state for pan & zoom -------------------------------------------
static float g_zoomFactor = 1.0f;
static float g_panX = 0.0f, g_panY = 0.0f;
static bool  g_dragging = false;
static double g_dragStartX = 0.0, g_dragStartY = 0.0;
static float g_panStartX = 0.0f, g_panStartY = 0.0f;
static bool g_isBinary = false;

bool showTAutoMin = false, showTDouble = false, showTHyst = false;
bool showTNiblack = false, showTSauvola = false, showTWolf = false;
int  tAutoMin = 0;                  // będzie obliczane automatycznie
int  t1 = 0, t2 = 255;              // dla dwuprogowego
int  tLow = 50, tHigh = 150;        // dla histerezy
int  winSize = 15;                  // dla lokalnych
float kParam = 0.2f;                // dla Niblack/Sauvola/Wolf
float Rparam = 128.0f;              // dodatkowy parametr Sauvola

// --- UI layout -------------------------------------------------------------
const int TOP_BAR_HEIGHT = 50;
const int RIGHT_BAR_WIDTH = 524;

struct ImageData {
    GLuint                       textureID = 0;
    int                          width = 0, height = 0, channels = 0;
    std::vector<unsigned char>   pixels;
    std::vector<float>           histGray, histR, histG, histB;
};

struct Snapshot {
    std::vector<unsigned char> pixels;
    int                        channels;
};

bool  initGLFW();
GLFWwindow* createWindow(int w, int h, const char* t);
void  setupGLFWCallbacks(GLFWwindow* win);
void  initImGui(GLFWwindow* win);
void  cleanupImGui();

bool  loadImageFromFile(ImageData& img);
void  cleanupImage(ImageData& img);
void  computeHistograms(ImageData& img);
void  uploadTexture(ImageData& img);

void  setupProjection(int w, int h);
void  resetViewForImage(const ImageData& img, int winW, int winH);
void  renderImage(const ImageData& img, int winW, int winH);
void  renderHistogram(const ImageData& img);

void  clampImage(ImageData& img, int lo, int hi);
void  normalizeImagePerChannel(ImageData& img, int newLo, int newHi);
void  brightnessImage(ImageData& img, int delta);
void  stretchHistogram(ImageData& img, float pLow, float pHigh);
void  contrastImage(ImageData& img, float factor);
void  thresholdManual(ImageData& img, int T);
void  thresholdOtsu(ImageData& img);

void mainLoop(GLFWwindow* window, ImageData& img);

// =============================== ENTRY =====================================
int main() {
    if (!initGLFW()) return -1;
    GLFWwindow* win = createWindow(1280, 720, "Photoshoot");
    if (!win) return -1;
    setupGLFWCallbacks(win);
    initImGui(win);

    ImageData img;
    if (!loadImageFromFile(img)) {
        cleanupImGui(); glfwTerminate(); return -1;
    }

    int w, h; glfwGetFramebufferSize(win, &w, &h);
    setupProjection(w, h);
    resetViewForImage(img, w, h);

    mainLoop(win, img);

    cleanupImage(img);
    cleanupImGui();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}



// ===================== GLFW + ImGui BOILERPLATE ============================
bool initGLFW() { if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return false; } return true; }
GLFWwindow* createWindow(int w, int h, const char* t) {
    GLFWwindow* win = glfwCreateWindow(w, h, t, nullptr, nullptr);
    if (!win) { std::cerr << "Window create failed\n"; return nullptr; }
    glfwMakeContextCurrent(win); return win;
}

void scroll_cb(GLFWwindow*, double, double y) { g_zoomFactor *= (y > 0 ? 1.1f : 1 / 1.1f); }
void mouse_btn_cb(GLFWwindow* win, int btn, int act, int) { if (btn == GLFW_MOUSE_BUTTON_LEFT) { g_dragging = (act == GLFW_PRESS); if (g_dragging) { glfwGetCursorPos(win, &g_dragStartX, &g_dragStartY); g_panStartX = g_panX; g_panStartY = g_panY; } } }
void cursor_cb(GLFWwindow*, double x, double y) { if (g_dragging) { g_panX = g_panStartX + float(x - g_dragStartX); g_panY = g_panStartY - float(y - g_dragStartY); } }
void setupGLFWCallbacks(GLFWwindow* w) { glfwSetScrollCallback(w, scroll_cb); glfwSetMouseButtonCallback(w, mouse_btn_cb); glfwSetCursorPosCallback(w, cursor_cb); }

void initImGui(GLFWwindow* w) { IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGui::StyleColorsDark(); ImGui_ImplGlfw_InitForOpenGL(w, true); ImGui_ImplOpenGL3_Init("#version 130"); }
void cleanupImGui() { ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext(); }

// ==================== image load =================================
bool loadImageFromFile(ImageData& img) {
    const char* filters[] = { "*.jpg" };
    const char* fn = tinyfd_openFileDialog("Select image", "", 1, filters, "Images", 0);
    if (!fn) { std::cerr << "No file selected\n"; return false; }
    int w, h, ch;
    
    unsigned char* data = stbi_load(fn, &w, &h, &ch, 0);
    if (!data) { std::cerr << "Load failed\n"; return false; }
    img.width = w; img.height = h; img.channels = ch;
    img.pixels.assign(data, data + w * h * ch);
    stbi_image_free(data);
    uploadTexture(img);
    computeHistograms(img);
    return img.textureID != 0;
}

void cleanupImage(ImageData& img) { if (img.textureID) { glDeleteTextures(1, &img.textureID); img.textureID = 0; } img.pixels.clear(); }

void uploadTexture(ImageData& img) {
    if (!img.textureID) glGenTextures(1, &img.textureID);
    glBindTexture(GL_TEXTURE_2D, img.textureID);

    // filtering & alignment
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // ——— swizzle gray → RGB
    if (img.channels == 1) {
#ifndef GL_TEXTURE_SWIZZLE_R
        // define these once at the top of your file if missing:
#define GL_TEXTURE_SWIZZLE_R 0x8E42
#define GL_TEXTURE_SWIZZLE_G 0x8E43
#define GL_TEXTURE_SWIZZLE_B 0x8E44
#define GL_TEXTURE_SWIZZLE_A 0x8E45
#endif

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
    }

    // choose the right format
    GLenum fmt = (img.channels == 1 ? GL_RED
        : img.channels == 3 ? GL_RGB
        : GL_RGBA);

    // upload the pixels
    glTexImage2D(GL_TEXTURE_2D,
        0,      // mipmap level
        fmt,    // internal format
        img.width, img.height,
        0,      // border
        fmt,    // source pixel format
        GL_UNSIGNED_BYTE,
        img.pixels.data());
}

void computeHistograms(ImageData& img) {
    const int bins = 256;
    size_t nPixels = img.width * img.height;
    const unsigned char* p = img.pixels.data();

    img.histGray.assign(bins, 0.0f);
    img.histR   .assign(bins, 0.0f);
    img.histG   .assign(bins, 0.0f);
    img.histB   .assign(bins, 0.0f);

    size_t idx = 0;
    if (img.channels == 1) {
        // grayscale
        for (size_t i = 0; i < nPixels; ++i) {
            unsigned char v = p[idx++];
            img.histGray[v] += 1.0f;
        }
    }
    else {
        // RGB / RGBA
        for (size_t i = 0; i < nPixels; ++i) {
            unsigned char r = p[idx++];
            unsigned char g = p[idx++];
            unsigned char b = p[idx++];
            if (img.channels == 4) {
                //skip alpha
                idx++;
            }
            img.histR[r] += 1.0f;
            img.histG[g] += 1.0f;
            img.histB[b] += 1.0f;
        }
    }

    float maxGray = 0.0f, maxR = 0.0f, maxG = 0.0f, maxB = 0.0f;
    if (img.channels == 1) {
        for (int i = 0; i < bins; ++i) {
            if (img.histGray[i] > maxGray)
                maxGray = img.histGray[i];
        }
    }
    else {
        for (int i = 0; i < bins; ++i) {
            if (img.histR[i] > maxR) maxR = img.histR[i];
            if (img.histG[i] > maxG) maxG = img.histG[i];
            if (img.histB[i] > maxB) maxB = img.histB[i];
        }
    }
}

// =================== VIEW SETUP / RENDER ===================================
void setupProjection(int w, int h) { glViewport(0, 0, w, h); glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, w, 0, h, -1, 1); glMatrixMode(GL_MODELVIEW); glLoadIdentity(); }
void resetViewForImage(const ImageData& img, int winW, int winH) { g_zoomFactor = 1.0f; int cw = winW - RIGHT_BAR_WIDTH, ch = winH - TOP_BAR_HEIGHT; g_panX = (cw - img.width) * 0.5f; g_panY = (ch - img.height) * 0.5f; }

void renderImage(const ImageData& img, int winW, int winH) {
    if (!img.textureID) return; int regionW = winW - RIGHT_BAR_WIDTH; int regionH = winH - TOP_BAR_HEIGHT;
    float dispW = img.width * g_zoomFactor, dispH = img.height * g_zoomFactor;

    if (dispW < regionW)
        g_panX = (regionW - dispW) / 2.0f;
    else
        g_panX = std::max(regionW - dispW, std::min(g_panX, 0.0f));
    if (dispH < regionH)
        g_panY = (regionH - dispH) / 2.0f;
    else
        g_panY = std::max(regionH - dispH, std::min(g_panY, 0.0f));

    glPushMatrix(); glTranslatef(g_panX, TOP_BAR_HEIGHT + g_panY, 0); glScalef(g_zoomFactor, g_zoomFactor, 1);
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, img.textureID);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex2i(0, 0); glTexCoord2f(1, 1); glVertex2i(img.width, 0);
    glTexCoord2f(1, 0); glVertex2i(img.width, img.height); glTexCoord2f(0, 0); glVertex2i(0, img.height);
    glEnd(); glDisable(GL_TEXTURE_2D); glPopMatrix();
}

void renderHistogram(const ImageData& img) {
    if (!img.textureID) return;

    // detect gray vs color
    bool isGray = (img.channels == 1);
    if (!isGray && img.channels >= 3) {
        isGray = true;
        for (int i = 0; i < 256; ++i)
            if (img.histR[i] != img.histG[i] || img.histR[i] != img.histB[i]) {
                isGray = false;
                break;
            }
    }

    if (isGray) {
        // find the max bar
        float maxCount = 0.0f;
        const auto& H = (img.channels == 1 ? img.histGray : img.histR);
        for (float v : H) if (v > maxCount) maxCount = v;

        ImGui::Text("Grayscale Histogram");
        ImGui::PlotHistogram(
            "##Gray", H.data(), 256, 0,
            nullptr,
            0.0f, maxCount,
            ImVec2(512, 150)
        );
    }
    else {
        // RGB
        float maxR = 0, maxG = 0, maxB = 0;
        for (int i = 0; i < 256; ++i) {
            maxR = std::max(maxR, img.histR[i]);
            maxG = std::max(maxG, img.histG[i]);
            maxB = std::max(maxB, img.histB[i]);
        }

        ImGui::Text("Red Channel");
        ImGui::PlotHistogram("##R", img.histR.data(), 256, 0, nullptr, 0.0f, maxR, ImVec2(512, 150));

        ImGui::Text("Green Channel");
        ImGui::PlotHistogram("##G", img.histG.data(), 256, 0, nullptr, 0.0f, maxG, ImVec2(512, 150));

        ImGui::Text("Blue Channel");
        ImGui::PlotHistogram("##B", img.histB.data(), 256, 0, nullptr, 0.0f, maxB, ImVec2(512, 150));
    }
}

bool isBinaryImage(const ImageData& img)
{
    for (unsigned char v : img.pixels)
        if (v != 0 && v != 255) return false;
    return true;
}

// ==================== algorithms ===========================

// obcina wartości wszystkich kanałów do przedziału [lo..hi]
void clampImage(ImageData& img, int lo, int hi) {
    size_t nPixels = img.width * img.height;
    int C = img.channels;
    for (size_t i = 0; i < nPixels; ++i) {
        size_t idx = i * C;
        int v = img.pixels[idx];
        if (v < lo) v = lo;
        else if (v > hi) v = hi;
        img.pixels[idx] = static_cast<unsigned char>(v);

        if (C >= 3) {
            int g = img.pixels[idx + 1];
            if (g < lo) g = lo;
            else if (g > hi) g = hi;
            img.pixels[idx + 1] = static_cast<unsigned char>(g);

            int b = img.pixels[idx + 2];
            if (b < lo) b = lo;
            else if (b > hi) b = hi;
            img.pixels[idx + 2] = static_cast<unsigned char>(b);
        }
    }
}

// skaluje każdy kanał niezależnie tak, aby min = newLo, max = newHi
void normalizeImagePerChannel(ImageData& img, int newLo, int newHi) {
    int C = img.channels;
    int nPixels = img.width * img.height;

    for (int ch = 0; ch < C; ++ch) {
        // minmax dla kanału
        int minV = 255, maxV = 0;
        for (int i = 0; i < nPixels; ++i) {
            int idx = i * C + ch;
            unsigned char v = img.pixels[idx];
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
        }
        if (maxV == minV) {
            continue;
        }

        // skaluj każdy piksel w tym kanale do [newLo..newHi]
        float scale = float(newHi - newLo) / float(maxV - minV);
        for (int i = 0; i < nPixels; ++i) {
            int idx = i * C + ch;
            int v = img.pixels[idx];
            int mapped = int((v - minV) * scale + newLo + 0.5f);
            if (mapped < 0)      mapped = 0;
            else if (mapped > 255) mapped = 255;
            img.pixels[idx] = static_cast<unsigned char>(mapped);
        }
    }
}

// zmienia jasność każdego piksela o delta, z obcięciem do [0..255]
void brightnessImage(ImageData& img, int delta) {
    for (size_t i = 0; i < img.pixels.size(); ++i) {
        int v = int(img.pixels[i]) + delta;
        if (v < 0) v = 0;
        else if (v > 255) v = 255;
        img.pixels[i] = static_cast<unsigned char>(v);
    }
}

// mnoży odchylenie od 128 przez factor, z obcięciem do [0..255]
void contrastImage(ImageData& img, float factor) {
    for (size_t i = 0; i < img.pixels.size(); ++i) {
        float diff = float(img.pixels[i]) - 128.0f;
        int v = int(diff * factor + 128.0f + 0.5f);
        if (v < 0) v = 0;
        else if (v > 255) v = 255;
        img.pixels[i] = static_cast<unsigned char>(v);
    }
}

// rozciąga histogram liniowo pomiędzy percentylami pLow i pHigh
void stretchHistogram(ImageData& img, float pLow = 0.01f, float pHigh = 0.99f) {
    int W = img.width, H = img.height, C = img.channels;
    int N = W * H;

    // tablica histogramów dla każdego kanału
    std::vector<std::vector<int>> hist(C, std::vector<int>(256, 0));
    for (int i = 0, idx = 0; i < N; ++i) {
        for (int ch = 0; ch < C; ++ch, ++idx) {
            ++hist[ch][img.pixels[idx]];
        }
    }

    // tworzenie dystrybuanty
    std::vector<int> lo(C), hi(C);
    for (int ch = 0; ch < C; ++ch) {
        std::vector<float> cdf(256);
        float cum = 0.0f;
        for (int v = 0; v < 256; ++v) {
            cum += hist[ch][v];
            cdf[v] = cum;
        }
        float total = cum;
        for (int v = 0; v < 256; ++v) {
            cdf[v] /= total;
        }

        // wyznaczanie progów percentylowych
        lo[ch] = 0;
        while (lo[ch] < 255 && cdf[lo[ch]] < pLow) ++lo[ch];
        hi[ch] = 255;
        while (hi[ch] > 0 && cdf[hi[ch]] > pHigh) --hi[ch];
        if (hi[ch] <= lo[ch]) {
            // przywrócenie pełnego zakresu, gdy progi się pokrywają
            lo[ch] = 0;
            hi[ch] = 255;
        }
    }

    // przeskalowanie wartości pikseli: obcięcie do [lo..hi], a następnie rozciągnięcie do [0..255]
    for (int i = 0, idx = 0; i < N; ++i) {
        for (int ch = 0; ch < C; ++ch, ++idx) {
            int v = img.pixels[idx];
            if (v <= lo[ch])      v = lo[ch];
            else if (v >= hi[ch]) v = hi[ch];
            float t = float(v - lo[ch]) / float(hi[ch] - lo[ch]);
            int v2 = int(t * 255.0f + 0.5f);
            img.pixels[idx] = static_cast<unsigned char>(v2);
        }
    }
}

// binaryzuje obraz progiem T
void thresholdManual(ImageData& img, int T) {
    int C = img.channels;
    size_t nPixels = img.width * img.height;
    for (size_t i = 0; i < nPixels; ++i) {
        size_t idx = i * C;
        unsigned char gray;
        if (C == 1) {
            gray = img.pixels[idx];
        }
        else {
            gray = static_cast<unsigned char>(
                0.299f * img.pixels[idx] +
                0.587f * img.pixels[idx + 1] +
                0.114f * img.pixels[idx + 2] + 0.5f
                );
        }
        unsigned char out = (gray >= T ? 255 : 0);
        img.pixels[idx] = out;
        if (C >= 3) {
            img.pixels[idx + 1] = out;
            img.pixels[idx + 2] = out;
        }
    }
}

// znajduje dwa największe szczyty w histogramie i próg w najniższym punkcie pomiędzy nimi
int computeAutoMinThreshold(const ImageData& img) {
    // budowanie histogramu
    std::vector<float> hist(256, 0.0f);
    size_t n = img.width * img.height;
    size_t idx = 0;

    for (size_t i = 0; i < n; ++i) {
        unsigned char g;
        if (img.channels == 1) {
            g = img.pixels[idx++];
        }
        else {
            g = static_cast<unsigned char>(
                0.299f * img.pixels[idx++] +
                0.587f * img.pixels[idx++] +
                0.114f * img.pixels[idx++] + 0.5f
                );
            if (img.channels == 4) {
                ++idx;
            }
        }
        hist[g] += 1.0f;
    }

    // odnajdywanie szczytów histogramu
    int p1 = 0, p2 = 1;
    for (int i = 1; i < 256; ++i) {
        if (hist[i] > hist[p1]) {
            p1 = i;
        }
    }
    for (int i = 1; i < 256; ++i) {
        if (i != p1 && hist[i] > hist[p2]) {
            p2 = i;
        }
    }

    int lo = std::min(p1, p2);
    int hi = std::max(p1, p2);
    int tAutoMin = lo;
    for (int i = lo + 1; i < hi; ++i) {
        if (hist[i] < hist[tAutoMin]) {
            tAutoMin = i;
        }
    }
    //std::cout << "Lo: " << lo << std::endl << "Hi: " << hi << std::endl;
    return tAutoMin;
}

// oblicza optymalny próg metodą Otsu na podstawie histogramu
int otsuThreshold(const std::vector<float>& hist, size_t total) {
    // suma wszystkich poziomów pomnożonych przez ich liczbę
    float sumAll = 0.0f;
    for (int t = 0; t < 256; ++t)
        sumAll += t * hist[t];

    float sumB = 0.0f;      // skumulowana suma poziomów tła
    size_t wB = 0;          // liczba pikseli w tle
    float maxVar = 0.0f;    // najlepsza dotychczasowa wariancja międzyklasowa
    int bestT = 0;          // najlepszy próg

    // dla każdego możliwego poziomu t obliczana jest wariancja międzyklasowa
    for (int t = 0; t < 256; ++t) {
        wB += size_t(hist[t]);  // waga tła
        if (wB == 0) continue;  // pomiń, jeśli brak pikseli w tle
        size_t wF = total - wB; // waga pierwszego planu
        if (wF == 0) break;     // zakończ, gdy brak pikseli na pierwszym planie

        sumB += t * hist[t];                    // aktualizuj sumę dla tła
        float mB = sumB / float(wB);            // średnia tła
        float mF = (sumAll - sumB) / float(wF); // średnia pierwszego planu

        // oblicz wariancję międzyklasową dla bieżącego progu
        float varBetween = float(wB) * float(wF) * (mB - mF) * (mB - mF);
        // zapamiętanie progu, który maksymalizuje tę wariancję
        if (varBetween > maxVar) {
            maxVar = varBetween;
            bestT = t;
        }
    }
    return bestT;
}

// binaryzuje obraz przy użyciu progu wyznaczonego metodą Otsu
void thresholdOtsu(ImageData& img) {
    size_t nPixels = img.width * img.height;

    // histogram poziomów szarości
    std::vector<float> hist(256, 0.0f);

    // konwersja na jeden kanal szarosci
    int C = img.channels;
    for (size_t i = 0; i < nPixels; ++i) {
        size_t idx = i * C;
        unsigned char gray;
        if (C == 1) {
            gray = img.pixels[idx];
        }
        else {
            gray = static_cast<unsigned char>(
                0.299f * img.pixels[idx] +
                0.587f * img.pixels[idx + 1] +
                0.114f * img.pixels[idx + 2] + 0.5f
                );
        }
        hist[gray] += 1.0f;
    }
    // wyznaczenie optymalnego progu metodą Otsu i progowanie manualne z otrzymanym parametrem
    int T = otsuThreshold(hist, nPixels);
    thresholdManual(img, T);
}

// dopuszcza piksele w przedziale [T1..T2), resztę ustawia na zero
void thresholdDouble(ImageData& img, int T1, int T2) {
    int C = img.channels, n = img.width * img.height;
    for (int i = 0, idx = 0; i < n; ++i, idx += C) {
        unsigned char gray = (C == 1 ? img.pixels[idx]
            : static_cast<unsigned char>(0.299f * img.pixels[idx]
                + 0.587f * img.pixels[idx + 1]
                    + 0.114f * img.pixels[idx + 2] + 0.5f));

        unsigned char out = (gray >= T1 && gray < T2) ? 255 : 0;
        for (int c = 0; c < C; ++c)
            img.pixels[idx + c] = out;
    }
}

// progowanie z histerezą: silne piksele - większe/równe niż T_high, słabe - mniejsze/równe niż T_low, słabe piksele stają się silne gdy mają chociaż jednego silnego sąsiada
void thresholdHysteresis(ImageData& img, int T_low, int T_high) {
    int w = img.width;
    int h = img.height;
    int C = img.channels;
    int N = w * h;

    std::vector<unsigned char> gray(N);
    for (int i = 0, idx = 0; i < N; ++i, idx += C) {
        if (C == 1) {
            gray[i] = img.pixels[idx];
        }
        else {
            gray[i] = static_cast<unsigned char>(0.299f * img.pixels[idx] + 0.587f * img.pixels[idx + 1] + 0.114f * img.pixels[idx + 2] + 0.5f);
        }
    }

    // wstępne oznaczenie: 0 - wyłączony, 1 - słaby, 2 - silny
    std::vector<unsigned char> mark(N, 0);
    for (int i = 0; i < N; ++i) {
        if (gray[i] >= T_high)      mark[i] = 2;
        else if (gray[i] >= T_low)  mark[i] = 1;
    }

    // dopóki jakieś słabe piksele mają sąsiada silnego, zamieniamy je na silne
    bool changed = true;
    while (changed) {
        changed = false;
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                int i = y * w + x;
                if (mark[i] == 1) {
                    // jeśli jest słaby, sprawdź jego 8 sąsiadów
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0) continue;
                            int ni = (y + dy) * w + (x + dx);
                            if (mark[ni] == 2) {    // jeśli sąsiad jest silny, zamień i zaznacz zmianę
                                mark[i] = 2;
                                changed = true;
                                dx = dy = 2;
                            }
                        }
                    }
                }
            }
        }
    }

    // zapis do obrazu
    for (int i = 0, idx = 0; i < N; ++i, idx += C) {
        unsigned char v = (mark[i] == 2 ? 255 : 0);
        for (int c = 0; c < C; ++c)
            img.pixels[idx + c] = v;
    }
}

// lokalne progowanie Niblacka: T = μ + k·σ w oknie rozmiaru windowSize
void thresholdNiblack(ImageData& img, int windowSize, float k) {
    int w = img.width, h = img.height, C = img.channels;
    int r = windowSize / 2;

    std::vector<unsigned char> orig = img.pixels;
    std::vector<float> gray(w * h);
    for (int i = 0, idx = 0; i < w * h; ++i, idx += C)
        gray[i] = (C == 1 ? orig[idx] :
            0.299f * orig[idx] + 0.587f * orig[idx + 1] + 0.114f * orig[idx + 2]);

    // sum[i]  : suma wartości jasności w oknie od (0,0) do (x,y)
    // sum2[i] : suma kwadratów wartości jasności w tym samym zakresie
    std::vector<double> sum(w * h), sum2(w * h);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        int i = y * w + x;
        double v = gray[i], vsq = v * v;

        // S(x, y) = I(x, y) + S(x - 1, y) + S(x, y - 1) - S(x - 1, y - 1)
        double left = (x > 0) ? sum[i - 1] : 0;
        double top = (y > 0) ? sum[i - w] : 0;
        double diag = (x > 0 && y > 0) ? sum[i - w - 1] : 0;
        sum[i] = v + left + top - diag;

        // S^2(x, y) = ...
        double left2 = (x > 0) ? sum2[i - 1] : 0;
        double top2 = (y > 0) ? sum2[i - w] : 0;
        double diag2 = (x > 0 && y > 0) ? sum2[i - w - 1] : 0;
        sum2[i] = vsq + left2 + top2 - diag2;
    }

    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        // wyznaczenie granic prostokątnego okna wokół piksela (x,y)
        int x0 = std::max(0, x - r), y0 = std::max(0, y - r);
        int x1 = std::min(w - 1, x + r), y1 = std::min(h - 1, y + r);

        // granice okna
        int A = (y0 - 1) * w + (x0 - 1);
        int B = (y0 - 1) * w + x1;
        int Cc = y1 * w + (x0 - 1);
        int D = y1 * w + x1;

        // obliczenie sumy wartości jasności S i sumy kwadratów S^2 w oknie
        double S = sum[D];
        double S2 = sum2[D];
        if (x0 > 0) { S -= sum[Cc]; S2 -= sum2[Cc]; }
        if (y0 > 0) { S -= sum[B];  S2 -= sum2[B]; }
        if (x0 > 0 && y0 > 0) { S += sum[A]; S2 += sum2[A]; }

        int area = (x1 - x0 + 1) * (y1 - y0 + 1);   // pole
        double mean = S / area;                     // średnia jasności
        double var = S2 / area - mean * mean;       // wariancja - sigma^2
        double stddev = sqrt(std::max(0.0, var));   // odchylenie standardowe - sigma

        // T(x, y) = μ(x, y) + k * σ(x, y)
        float T = mean + k * stddev;
        unsigned char out = (gray[y * w + x] >= T ? 255 : 0);
        int base = (y * w + x) * C;
        for (int c = 0; c < C; ++c)
            img.pixels[base + c] = out;
    }
}

// lokalne progowanie Sauvoli: T = μ·(1 + k·(σ/R − 1)) w oknie rozmiaru windowSize
void thresholdSauvola(ImageData& img, int windowSize, float k, float R) {
    int w = img.width, h = img.height, C = img.channels;
    int r = windowSize / 2;

    std::vector<float> gray(w * h);
    for (int i = 0, idx = 0; i < w * h; ++i, idx += C) {
        gray[i] = (C == 1 ? img.pixels[idx] :
            0.299f * img.pixels[idx] + 0.587f * img.pixels[idx + 1] + 0.114f * img.pixels[idx + 2]);
    }

    // sum[i]  : suma wartości jasności w oknie od (0,0) do (x,y)
    // sum2[i] : suma kwadratów wartości jasności w tym samym zakresie
    std::vector<double> sum(w * h), sum2(w * h);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        int i = y * w + x;
        double v = gray[i], vsq = v * v;

        // S(x, y) = I(x, y) + S(x - 1, y) + S(x, y - 1) - S(x - 1, y - 1)
        double left = (x > 0 ? sum[i - 1] : 0);
        double top = (y > 0 ? sum[i - w] : 0);
        double diag = (x > 0 && y > 0 ? sum[i - w - 1] : 0);
        sum[i] = v + left + top - diag;

        // S^2(x, y) = ...
        double left2 = (x > 0 ? sum2[i - 1] : 0);
        double top2 = (y > 0 ? sum2[i - w] : 0);
        double diag2 = (x > 0 && y > 0 ? sum2[i - w - 1] : 0);
        sum2[i] = vsq + left2 + top2 - diag2;
    }

    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        // wyznaczenie granic prostokątnego okna wokół piksela (x,y)
        int x0 = std::max(0, x - r), y0 = std::max(0, y - r);
        int x1 = std::min(w - 1, x + r), y1 = std::min(h - 1, y + r);

        // granice okna
        int A = (y0 - 1) * w + (x0 - 1);
        int B = (y0 - 1) * w + x1;
        int Cc = y1 * w + (x0 - 1);
        int D = y1 * w + x1;

        // obliczenie sumy wartości jasności S i sumy kwadratów S^2 w oknie
        double S = sum[D];
        double S2 = sum2[D];
        if (x0 > 0) { S -= sum[Cc]; S2 -= sum2[Cc]; }
        if (y0 > 0) { S -= sum[B];  S2 -= sum2[B]; }
        if (x0 > 0 && y0 > 0) { S += sum[A]; S2 += sum2[A]; }

        int area = (x1 - x0 + 1) * (y1 - y0 + 1);       // pole
        double m = S / area;                            // średnia jasności
        double var = S2 / area - m * m;                 // wariancja - sigma^2
        double stddev = std::sqrt(std::max(0.0, var));  // odchylenie standardowe - sigma

        // T(x, y) = μ(x, y) * (1 + k * ((σ(x,y)/R) - 1))
        double T = m * (1 + k * ((stddev / R) - 1));
        int idx = y * w + x;
        unsigned char out = (gray[idx] >= T) ? 255 : 0;
        int base = idx * C;
        for (int c = 0; c < C; ++c)
            img.pixels[base + c] = out;
    }
}

// lokalne progowanie Wolf‑Jolion: T = μ + k·(σ−σ_min)·((μ−Imin)/(Imax−Imin))
void thresholdWolfJolion(ImageData& img, int windowSize, float k) {
    int w = img.width, h = img.height, C = img.channels;
    int r = windowSize / 2;

    // konwersja na jeden kanal szarosci
    std::vector<float> gray(w * h);
    for (int i = 0, idx = 0; i < w * h; ++i, idx += C) {
        gray[i] = (C == 1 ? img.pixels[idx] :
            0.299f * img.pixels[idx] + 0.587f * img.pixels[idx + 1] + 0.114f * img.pixels[idx + 2]);
    }

    // szukanie najmniejszej i największej wartości w każdym pikselu
    float Imin = gray[0], Imax = gray[0];
    for (float v : gray) {
        Imin = std::min(Imin, v);
        Imax = std::max(Imax, v);
    }

    // sum[i]  : suma wartości jasności w oknie od (0,0) do (x,y)
    // sum2[i] : suma kwadratów wartości jasności w tym samym zakresie
    std::vector<double> sum(w * h), sum2(w * h);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        int i = y * w + x;
        double v = gray[i], vsq = v * v;

        // S(x, y) = I(x, y) + S(x - 1, y) + S(x, y - 1) - S(x - 1, y - 1)
        double left = (x > 0 ? sum[i - 1] : 0);
        double top = (y > 0 ? sum[i - w] : 0);
        double diag = (x > 0 && y > 0 ? sum[i - w - 1] : 0);
        sum[i] = v + left + top - diag;

        // S^2(x, y) = ...
        double left2 = (x > 0 ? sum2[i - 1] : 0);
        double top2 = (y > 0 ? sum2[i - w] : 0);
        double diag2 = (x > 0 && y > 0 ? sum2[i - w - 1] : 0);
        sum2[i] = vsq + left2 + top2 - diag2;
    }

    // mu[i] : średnia lokalna
    // sigma[i] : odchylenie standardowe
    std::vector<double> mu(w * h), sigma(w * h);

    // minimalne odchylenie w całym obrazaie
    double sigma_min = std::numeric_limits<double>::infinity();
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {

        // wyznaczenie granic prostokątnego okna wokół piksela (x,y)
        int x0 = std::max(0, x - r), y0 = std::max(0, y - r);
        int x1 = std::min(w - 1, x + r), y1 = std::min(h - 1, y + r);

        // granice okna
        int A = (y0 - 1) * w + (x0 - 1);
        int B = (y0 - 1) * w + x1;
        int Cc = y1 * w + (x0 - 1);
        int D = y1 * w + x1;

        // obliczenie sumy wartości jasności S i sumy kwadratów S^2 w oknie
        double S = sum[D];
        double S2 = sum2[D];
        if (x0 > 0) { S -= sum[Cc]; S2 -= sum2[Cc]; }
        if (y0 > 0) { S -= sum[B];  S2 -= sum2[B]; }
        if (x0 > 0 && y0 > 0) { S += sum[A]; S2 += sum2[A]; }

        int area = (x1 - x0 + 1) * (y1 - y0 + 1);   // pole
        double m = S / area;                        // średnia jasności
        double var = S2 / area - m * m;             // wariancja - sigma^2
        double sd = std::sqrt(std::max(0.0, var));  // odchylenie standardowe - sigma

        // średnia i odchylenie w pikselu (x, y)
        int idx = y * w + x;
        mu[idx] = m;
        sigma[idx] = sd;
        sigma_min = std::min(sigma_min, sd);
    }

    for (int i = 0; i < w * h; ++i) {
        // T(x, y) = μ + k * (σ - σ_min) * ( (μ - Imin) / (Imax - Imin) )
        double T = mu[i] + k * (sigma[i] - sigma_min) * ((mu[i] - Imin) / (Imax - Imin));
        unsigned char out = (gray[i] >= T) ? 255 : 0;
        int base = i * C;
        for (int c = 0; c < C; ++c)
            img.pixels[base + c] = out;
    }
}

// usuwa białe plamy mniejsze niż okno (erozja)
void erodeBinary(ImageData& img, int windowSize) {
    int W = img.width;
    int H = img.height;
    int C = img.channels;
    int radius = windowSize / 2;

    std::vector<unsigned char> original = img.pixels;

    for (int row = 0; row < H; ++row) {
        for (int col = 0; col < W; ++col) {
            bool keepWhite = true;

            // sprawdzanie sąsiadujących pikseli
            for (int dy = -radius; dy <= radius && keepWhite; ++dy) {
                for (int dx = -radius; dx <= radius && keepWhite; ++dx) {
                    int ny = row + dy;
                    int nx = col + dx;
                    // poprawka na granice zdjęcia
                    if (nx >= 0 && nx < W && ny >= 0 && ny < H) {
                        int idx = (ny * W + nx) * C; 
                        if (original[idx] == 0) {
                            keepWhite = false;
                        }
                    }
                }
            }

            // ustalenie wartości piksela
            unsigned char outVal = keepWhite ? 255 : 0;
            int base = (row * W + col) * C;
            for (int ch = 0; ch < C; ++ch) {
                img.pixels[base + ch] = outVal;
            }
        }
    }
}

// łączy białe obszary przez rozszerzenie (dylatacja)
void dilateBinary(ImageData& img, int windowSize) {
    int W = img.width;
    int H = img.height;
    int C = img.channels;
    int radius = windowSize / 2;

    std::vector<unsigned char> original = img.pixels;

    for (int row = 0; row < H; ++row) {
        for (int col = 0; col < W; ++col) {
            bool turnWhite = false;

            // sprawdzanie sąsiadujących pikseli
            for (int dy = -radius; dy <= radius && !turnWhite; ++dy) {
                for (int dx = -radius; dx <= radius && !turnWhite; ++dx) {
                    int ny = row + dy;
                    int nx = col + dx;
                    // poprawka na granice zdjęcia
                    if (nx >= 0 && nx < W && ny >= 0 && ny < H) {
                        int idx = (ny * W + nx) * C;
                        if (original[idx] == 255) {
                            turnWhite = true;
                        }
                    }
                }
            }

            // ustalenie wartości piksela
            unsigned char outVal = turnWhite ? 255 : 0;
            int base = (row * W + col) * C;
            for (int ch = 0; ch < C; ++ch) {
                img.pixels[base + ch] = outVal;
            }
        }
    }
}

// otwarcie morfologiczne = erozja, a potem dylatacja
inline void openBinary(ImageData& img, int win) { erodeBinary(img, win);  dilateBinary(img, win); }

// zamknięcie morfologiczne = dylatacja, a potem erozja
inline void closeBinary(ImageData& img, int win) { dilateBinary(img, win); erodeBinary(img, win); }

// zastępuje każdy piksel minimalną wartością w otoczeniu o boku długości windowSize
void minFilter(ImageData& img, int windowSize) {
    int W = img.width;
    int H = img.height;
    int C = img.channels;
    int radius = windowSize / 2;

    std::vector<unsigned char> original = img.pixels;

    for (int row = 0; row < H; ++row) {
        for (int col = 0; col < W; ++col) {
            int baseIndex = (row * W + col) * C;

            for (int ch = 0; ch < C; ++ch) {
                unsigned char minValue = 255;

                for (int dy = -radius; dy <= radius; ++dy) {
                    int ny = row + dy;
                    if (ny < 0 || ny >= H) continue;

                    for (int dx = -radius; dx <= radius; ++dx) {
                        int nx = col + dx;
                        if (nx < 0 || nx >= W) continue;

                        unsigned char v = original[(ny * W + nx) * C + ch];
                        if (v < minValue) {
                            minValue = v;
                        }
                    }
                }

                img.pixels[baseIndex + ch] = minValue;
            }
        }
    }
}

// zastępuje każdy piksel maksymalną wartością w otoczeniu o boku długości windowSize
void maxFilter(ImageData& img, int windowSize) {
    int W = img.width;
    int H = img.height;
    int C = img.channels;
    int radius = windowSize / 2;

    std::vector<unsigned char> original = img.pixels;

    for (int row = 0; row < H; ++row) {
        for (int col = 0; col < W; ++col) {
            int baseIndex = (row * W + col) * C;

            for (int ch = 0; ch < C; ++ch) {
                unsigned char maxValue = 0;
                for (int dy = -radius; dy <= radius; ++dy) {
                    int ny = row + dy;
                    if (ny < 0 || ny >= H) continue;

                    for (int dx = -radius; dx <= radius; ++dx) {
                        int nx = col + dx;
                        if (nx < 0 || nx >= W) continue;

                        unsigned char v = original[(ny * W + nx) * C + ch];
                        if (v > maxValue) {
                            maxValue = v;
                        }
                    }
                }

                img.pixels[baseIndex + ch] = maxValue;
            }
        }
    }
}

// zastępuje każdy piksel medianą wartości w otoczeniu o boku długości windowSize
void medianFilter(ImageData& img, int windowSize) {
    int W = img.width;
    int H = img.height;
    int C = img.channels;
    int radius = windowSize / 2;
    int windowArea = windowSize * windowSize;

    std::vector<unsigned char> neighborhood;
    neighborhood.reserve(windowArea);

    std::vector<unsigned char> original = img.pixels;

    for (int row = 0; row < H; ++row) {
        for (int col = 0; col < W; ++col) {
            int baseIndex = (row * W + col) * C;

            for (int ch = 0; ch < C; ++ch) {
                // lista sąsiedztw
                neighborhood.clear();
                for (int dy = -radius; dy <= radius; ++dy) {
                    int ny = row + dy;
                    if (ny < 0 || ny >= H) continue;

                    for (int dx = -radius; dx <= radius; ++dx) {
                        int nx = col + dx;
                        if (nx < 0 || nx >= W) continue;
                        neighborhood.push_back(original[(ny * W + nx) * C + ch]);
                    }
                }

                // znajdowanie mediany
                int mid = (int)neighborhood.size() / 2;
                std::nth_element(
                    neighborhood.begin(),
                    neighborhood.begin() + mid,
                    neighborhood.end()
                );
                unsigned char medianValue = neighborhood[mid];

                img.pixels[baseIndex + ch] = medianValue;
            }
        }
    }
}

// nakłada dowolne jądro splotowe kernel o rozmiarze kSize×kSize
void convolveFilter(ImageData& img, const std::vector<float>& kernel, int kSize)
{
    int width = img.width;
    int height = img.height;
    int channels = img.channels;
    int radius = kSize / 2;     // promień okna

    std::vector<unsigned char> original = img.pixels;

    // dla każdego piksela (y, x) wykonujemy operację splotu
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int baseIndex = (y * width + x) * channels;  // początkowy indeks piksela

            // filtrujemy każdy kanał
            for (int c = 0; c < channels; ++c) {
                float sum = 0.0f;  // wynik splotu

                // iterujemy po oknie
                for (int dy = -radius; dy <= radius; ++dy) {
                    int yy = y + dy;
                    // jeśli wychodzimy poza górną/dolną krawędź, zostajemy na brzegu
                    if (yy < 0)        yy = 0;
                    else if (yy >= height) yy = height - 1;

                    for (int dx = -radius; dx <= radius; ++dx) {
                        int xx = x + dx;
                        // jeśli wychodzimy poza lewą/prawą krawędź, zostajemy na brzegu
                        if (xx < 0)        xx = 0;
                        else if (xx >= width) xx = width - 1;

                        int idxOriginal = (yy * width + xx) * channels + c;
                        float kval = kernel[(dy + radius) * kSize + (dx + radius)];
                        sum += original[idxOriginal] * kval;
                    }
                }

                // zaokrąglanie wyniku
                int value = int(sum + 0.5f);
                if (value < 0)   value = 0;
                if (value > 255) value = 255;
                img.pixels[baseIndex + c] = (unsigned char)value;
            }
        }
    }
}

// filtry dolnoprzepustowe ====================

// 3×3 filtr uśredniający
void boxFilter3x3(ImageData& img) {
    std::vector<float> k(9, 1.0f / 9.0f);
    convolveFilter(img, k, 3);
}

// 5×5 filtr uśredniający
void boxFilter5x5(ImageData& img) {
    std::vector<float> k(25, 1.0f / 25.0f);
    convolveFilter(img, k, 5);
}

// 5×5 filtr Gaussowski
void gaussFilter5x5(ImageData& img) {
    std::vector<float> k = {
        1,  4,  6,  4, 1,
        4, 16, 24, 16, 4,
        6, 24, 36, 24, 6,
        4, 16, 24, 16, 4,
        1,  4,  6,  4, 1
    };
    float sum = 0;
    for (auto v : k) sum += v;
    for (auto& v : k) v /= sum;
    convolveFilter(img, k, 5);
}

// filtry górnoprzepustowe ====================

// Laplacjan 3×3 (4-sąsiedztwo)
void laplacian3x3(ImageData& img) {
    std::vector<float> k = {
         0, -1,  0,
        -1,  4, -1,
         0, -1,  0
    };
    convolveFilter(img, k, 3);
}

// Laplacjan 3×3 (8-sąsiedztwo)
void laplacian8x8(ImageData& img) {
    std::vector<float> k = {
        -1, -1, -1,
        -1,  8, -1,
        -1, -1, -1
    };
    convolveFilter(img, k, 3);
}

// filtr wyostrzający 3×3
void sharpen3x3(ImageData& img) {
    std::vector<float> k = {
         0, -1,  0,
        -1,  5, -1,
         0, -1,  0
    };
    convolveFilter(img, k, 3);
}

// ==================== filtry gradientowo-kierunkowe ====================

// Sobel X (krawędzie pionowe)
void sobelX(ImageData& img) {
    std::vector<float> k = {
        -1,  0,  1,
        -2,  0,  2,
        -1,  0,  1
    };
    convolveFilter(img, k, 3);
}

// Sobel Y (krawędzie poziome)
void sobelY(ImageData& img) {
    std::vector<float> k = {
        -1, -2, -1,
         0,  0,  0,
         1,  2,  1
    };
    convolveFilter(img, k, 3);
}

// Prewitt X (krawędzie pionowe)
void prewittX(ImageData& img) {
    std::vector<float> k = {
        -1, 0, 1,
        -1, 0, 1,
        -1, 0, 1
    };
    convolveFilter(img, k, 3);
}

// Prewitt Y (krawędzie poziome)
void prewittY(ImageData& img) {
    std::vector<float> k = {
        -1, -1, -1,
         0,  0,  0,
         1,  1,  1
    };
    convolveFilter(img, k, 3);
}

// Sobel 45
void sobel45(ImageData& img) {
    std::vector<float> k = {
         0,  1,  2,
        -1,  0,  1,
        -2, -1,  0
    };
    convolveFilter(img, k, 3);
}

// Sobel 135
void sobel135(ImageData& img) {
    std::vector<float> k = {
        -2, -1,  0,
        -1,  0,  1,
         0,  1,  2
    };
    convolveFilter(img, k, 3);
}

// Laplace poziomy (poziome krawędzie)
void laplaceHorizontal(ImageData& img) {
    std::vector<float> k = {
         0,  0,  0,
        -1,  2, -1,
         0,  0,  0
    };
    convolveFilter(img, k, 3);
}

// Laplace pionowy (pionowe krawędzie)
void laplaceVertical(ImageData& img) {
    std::vector<float> k = {
         0, -1,  0,
         0,  2,  0,
         0, -1,  0
    };
    convolveFilter(img, k, 3);
}

// porównanie Sobel X z Laplace 3x3 4n. w kierunku poziomym
void compareContourX(ImageData& img) {
    std::vector<unsigned char> backup = img.pixels;

    // Sobel X
    sobelX(img);
    std::vector<unsigned char> afterSobel = img.pixels;

    img.pixels = backup;

    // Laplace 3×3
    laplacian3x3(img);
    std::vector<unsigned char> afterLap = img.pixels;

    // różnica z przesunięciem o 128
    for (size_t i = 0; i < img.pixels.size(); ++i) {
        int d = int(afterSobel[i]) - int(afterLap[i]);
        d += 128;
        if (d < 0)   d = 0;
        if (d > 255) d = 255;
        img.pixels[i] = (unsigned char)d;
    }
}

// porównanie Sobel Y z Laplace 3×3 4n. w kierunku pionowym
void compareContourY(ImageData& img) {
    std::vector<unsigned char> backup = img.pixels;

    // Sobel Y
    sobelY(img);
    std::vector<unsigned char> afterSobel = img.pixels;

    // oryginał
    img.pixels = backup;

    // Laplace 3×3
    laplacian3x3(img);
    std::vector<unsigned char> afterLap = img.pixels;

    // różnica z przesunięciem o 128
    for (size_t i = 0; i < img.pixels.size(); ++i) {
        int d = int(afterSobel[i]) - int(afterLap[i]);
        d += 128;
        if (d < 0)   d = 0;
        if (d > 255) d = 255;
        img.pixels[i] = (unsigned char)d;
    }
}

// redukuje liczbę poziomów tonalnych do L (kwantyzacja)
void quantizeImage(ImageData& img, int L) {
    if (L < 2) return;                // co najmniej 2 poziomy
    if (L > 256) L = 256;             // maksymalnie 256 dla 8‐bit

    int W = img.width;
    int H = img.height;
    int C = img.channels;
    int nPixels = W * H;

    // Przeliczniki:
    //   faktor_q   = L / 256.0f        (float), żeby dostać q = floor(v * L/256)
    //   faktor_r   = 256.0f / L        (float), żeby wrócić do [0..255]
    float faktor_q = float(L) / 256.0f;
    float faktor_r = 256.0f / float(L);

    // Przetwarzamy każdy bajt pikseli:
    for (int i = 0; i < nPixels; ++i) {
        int base = i * C;
        for (int ch = 0; ch < C; ++ch) {
            unsigned char v = img.pixels[base + ch];
            // oblicz q = floor(v * L / 256)
            int q = int(v * faktor_q);
            // zabezpiecz q ∈ [0..L-1]
            if (q < 0) q = 0;
            if (q >= L) q = L - 1;
            // oblicz wartość po rekonstrukcji
            unsigned char v_new = (unsigned char)(std::round(q * faktor_r + 0.5f));

            img.pixels[base + ch] = v_new;
        }
    }
}

// zmniejsza liczbę poziomów kolorów do określonej liczby (posteryzacja)
void posterizeImage(ImageData& img, int levels) {
    // minimum 2 poziomy, maksimum 256
    if (levels < 2) levels = 2;
    if (levels > 256) levels = 256;

    int W = img.width;
    int H = img.height;
    int C = img.channels;
    int nPixels = W * H;

    // krok kwantyzacji (w przybliżeniu)
    int binSize = 256 / levels;
    if (binSize < 1) binSize = 1;

    // off-set, aby poziom trafił na „środek” swojego binu: binSize/2
    int halfBin = binSize / 2;

    for (int i = 0; i < nPixels; ++i) {
        int base = i * C;
        for (int ch = 0; ch < C; ++ch) {
            // oryginalna wartość 0..255
            int v = img.pixels[base + ch];
            // które to bin (0..levels-1)
            int q = v / binSize;
            if (q >= levels) q = levels - 1;  // zabezpieczenie na wypadek, gdyby v=255 i binSize*levels < 256

            // nowa wartość to środek binu:
            int v_new = q * binSize + halfBin;
            if (v_new > 255) v_new = 255;
            else if (v_new < 0) v_new = 0;

            img.pixels[base + ch] = static_cast<unsigned char>(v_new);
        }
    }
}

static double euclideanDistance(const std::vector<double>& a, const std::vector<double>& b) {
    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        double d = a[i] - b[i];
        sum += d * d;
    }
    return std::sqrt(sum);
}

// redukcja kolorów metodą k‑means na k centroidów
void kMeansColorQuantization(ImageData& img, int k, int maxIters = 10) {
    int W = img.width;
    int H = img.height;
    int C = img.channels;
    int N = W * H;
    if (N == 0 || k < 1) return;

    int dim = (C >= 3 ? 3 : 1);

    // wektor punktów w przestrzeni dim-wymiarowej
    std::vector<std::vector<double>> data(N, std::vector<double>(dim));
    for (int i = 0; i < N; ++i) {
        int base = i * C;
        if (dim == 1) {
            data[i][0] = static_cast<double>(img.pixels[base]);
        }
        else {
            data[i][0] = static_cast<double>(img.pixels[base]);       // R
            data[i][1] = static_cast<double>(img.pixels[base + 1]);   // G
            data[i][2] = static_cast<double>(img.pixels[base + 2]);   // B
        }
    }

    // wybierz losowo k odrębnych punktów z data
    std::vector<std::vector<double>> centroids(k, std::vector<double>(dim));
    {
        std::vector<int> idx(N);
        for (int i = 0; i < N; ++i) idx[i] = i;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::shuffle(idx.begin(), idx.end(), gen);
        for (int c = 0; c < k; ++c) {
            centroids[c] = data[idx[c]];
        }
    }

    // dla każdego piksela zapisz, do którego klastra należy
    std::vector<int> labels(N, 0);

    // pomocnicze tablice do wyliczania nowych centroidów:
    std::vector<std::vector<double>> sum(k, std::vector<double>(dim));
    std::vector<int> count(k);

    for (int iter = 0; iter < maxIters; ++iter) {
        bool anyChange = false;

        // zainicjalizuj sumy i liczniki na 0
        for (int c = 0; c < k; ++c) {
            std::fill(sum[c].begin(), sum[c].end(), 0.0);
            count[c] = 0;
        }

        // przypisz każdy punkt do najbliższego centroidu
        for (int i = 0; i < N; ++i) {
            // znajdź najbliższy centroid
            int bestCluster = 0;
            double bestDist = euclideanDistance(data[i], centroids[0]);
            for (int c = 1; c < k; ++c) {
                double d = euclideanDistance(data[i], centroids[c]);
                if (d < bestDist) {
                    bestDist = d;
                    bestCluster = c;
                }
            }
            if (labels[i] != bestCluster) {
                anyChange = true;
                labels[i] = bestCluster;
            }
            // dodaj punkt do sumy klastra
            for (int d = 0; d < dim; ++d) {
                sum[bestCluster][d] += data[i][d];
            }
            count[bestCluster] += 1;
        }

        // jeśli nic się nie zmieniło, zakończ wczesną konwergencją
        if (!anyChange && iter > 0) {
            break;
        }

        // przelicz centroidy jako średnią punktów w każdym klastrze
        for (int c = 0; c < k; ++c) {
            if (count[c] > 0) {
                for (int d = 0; d < dim; ++d) {
                    centroids[c][d] = sum[c][d] / static_cast<double>(count[c]);
                }
            }
        }
    }

    // zastąp każdy piksel kolorem centroidu jego klastra
    for (int i = 0; i < N; ++i) {
        int base = i * C;
        int c = labels[i];
        if (dim == 1) {
            unsigned char v = static_cast<unsigned char>(std::round(centroids[c][0]));
            img.pixels[base] = v;
            if (C >= 3) {
                // w przypadku RGB/RGBA, gdy obraz jest w skali szarości,
                // ustawiamy R=G=B=v
                img.pixels[base + 1] = v;
                img.pixels[base + 2] = v;
            }
        }
        else {
            // RGB / RGBA
            img.pixels[base] = static_cast<unsigned char>(std::round(centroids[c][0])); // R
            img.pixels[base + 1] = static_cast<unsigned char>(std::round(centroids[c][1])); // G
            img.pixels[base + 2] = static_cast<unsigned char>(std::round(centroids[c][2])); // B
        }
    }
}

// ========================== MAIN LOOP =====================================
void mainLoop(GLFWwindow* win, ImageData& img) {
    static std::vector<Snapshot> undoStack;
    static bool                  undoInit = false;
    static bool                  undoPressedLast = false;

    if (!undoInit) {
        undoStack.push_back({ img.pixels, img.channels });
        undoInit = true;
    }

    // ─── popup flags & params ─────────────────────────────
    bool showClamp = false,
        showNorm = false,
        showBright = false,
        showScale = false,
        showContrast = false,
        showStretch = false,
        showTManual = false,
        showTAutoMin = false,
        showTOtsu = false,
        showTDouble = false,
        showTHyst = false,
        showTNiblack = false,
        showTSauvola = false,
        showTWolf = false;
    // binary‐morphology popups
    bool showErode = false,
        showDilate = false,
        showOpen = false,
        showClose = false;
    // new filters popups (added)
    bool showBox3 = false,
        showBox5 = false,
        showGauss5 = false,
        showLap3 = false,
        showLap8 = false,
        showSharpen = false,
        showSobelX = false,
        showSobelY = false,
        showPrewittX = false,
        showPrewittY = false,
        showSobel45 = false,
        showSobel135 = false,
        showLapHor = false,
        showLapVer = false,
        showCompareX = false,
        showCompareY = false,
        showMinFilter = false,
        showMaxFilter = false,
        showMedianFilter = false,
        showQuantize = false,
        showPosterize = false,
        showKMeans = false;

    int  clampLo = 0, clampHi = 255,
        normLo = 0, normHi = 255,
        brightDelta = 0,
        scaleB = 0,
        stretchLo = 0, stretchHi = 255,
        tManual = 128,
        tOtsu = 0,
        t1 = 50, t2 = 200,
        tLow = 50, tHigh = 150,
        winSize = 15,
        binWin = 3;
    static int quantizeLevels = 4;  
    static int posterizeLevels = 4;
    static int kMeansClusters = 4;
    static int prevKMeansClusters = -1;

    float contrastFactor = 1.0f,
        kParam = 0.2f,
        Rparam = 128.0f;

    // *** parameters for the new filters ***
    int minWinSize = 3;      // window size for minFilter
    int maxWinSize = 3;      // window size for maxFilter
    int medianWinSize = 3;   // window size for medianFilter

    // ─── backup buffers & init flags for preview ──────────
    static std::vector<unsigned char>
        bpClamp, bpNorm, bpBright, bpScale, bpContrast, bpStretch,
        bpTAutoMin, bpTOtsu, bpTDouble, bpTHyst, bpTNiblack, bpTSauvola, bpTWolf,
        bpErode, bpDilate, bpOpen, bpClose,
        bpBox3, bpBox5, bpGauss5,
        bpLap3, bpLap8, bpSharpen,
        bpSobelX, bpSobelY, bpPrewittX, bpPrewittY,
        bpSobel45, bpSobel135, bpLapHor, bpLapVer,
        bpCmpX, bpCmpY, bpMinFilter, bpMaxFilter, bpMedianFilter,
        bpQuantize, bpPosterize, bpKMeansOriginal, bpKMeansPreview;

    static bool
        initClamp = false,
        initNorm = false,
        initBright = false,
        initScale = false,
        initContrast = false,
        initStretch = false,
        initTAutoMin = false,
        initTOtsu = false,
        initTDouble = false,
        initTHyst = false,
        initTNiblack = false,
        initTSauvola = false,
        initTWolf = false,

        initErode = false,
        initDilate = false,
        initOpen = false,
        initClose = false,

        initBox3 = false,
        initBox5 = false,
        initGauss5 = false,
        initLap3 = false,
        initLap8 = false,
        initSharpen = false,
        initSobelX = false,
        initSobelY = false,
        initPrewittX = false,
        initPrewittY = false,
        initSobel45 = false,
        initSobel135 = false,
        initLapHor = false,
        initLapVer = false,
        initCmpX = false,
        initCmpY = false,

        initMinFilter = false,
        initMaxFilter = false,
        initMedianFilter = false,
        initQuantize = false,
        initPosterize = false,
        initKMeans = false;

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        bool ctrl = (glfwGetKey(win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
            glfwGetKey(win, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
        bool z = (glfwGetKey(win, GLFW_KEY_Z) == GLFW_PRESS);

        if (ctrl && z && !undoPressedLast && undoStack.size() > 1) {
            undoStack.pop_back();
            auto& snap = undoStack.back();
            img.pixels = snap.pixels;
            img.channels = snap.channels;
            uploadTexture(img);
            computeHistograms(img);
            undoPressedLast = true;
            g_isBinary = isBinaryImage(img);
        }
        if (!(ctrl && z)) {
            undoPressedLast = false;
        }
        g_isBinary = isBinaryImage(img);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ─── MENU BAR ─────────────────────────────────────────
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open")) {
                    cleanupImage(img);
                    if (loadImageFromFile(img)) {
                        undoStack.clear();
                        undoStack.push_back({ img.pixels, img.channels });
                        undoInit = true;
                        undoPressedLast = false;

                        int ww, hh; glfwGetFramebufferSize(win, &ww, &hh);
                        setupProjection(ww, hh);
                        resetViewForImage(img, ww, hh);
                        g_isBinary = isBinaryImage(img);
                    }
                }
                if (ImGui::MenuItem("Save")) {
                    const char* filters[] = { "*.jpg" };
                    const char* fn = tinyfd_saveFileDialog(
                        "Save Image", "untitled.jpg", 1, filters, "JPEG files (*.jpg>"
                    );
                    if (fn) {
                        stbi_write_jpg(fn, img.width, img.height, img.channels,
                            img.pixels.data(), /*quality=*/100);
                    }
                }
                if (ImGui::MenuItem("Exit"))
                    glfwSetWindowShouldClose(win, true);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Histogram")) {
                ImGui::MenuItem("Clamp", nullptr, &showClamp);
                ImGui::MenuItem("Normalize", nullptr, &showNorm);
                ImGui::MenuItem("Brightness", nullptr, &showBright);
                ImGui::MenuItem("Contrast", nullptr, &showContrast);
                ImGui::MenuItem("Stretch", nullptr, &showStretch);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Thresholding")) {
                ImGui::MenuItem("Manual", nullptr, &showTManual);
                if (ImGui::MenuItem("Automatic (minima)")) showTAutoMin = true;
                if (ImGui::MenuItem("Otsu")) {
                    showTOtsu = true;
                    /*undoStack.push_back({ img.pixels, img.channels });
                    thresholdOtsu(img);
                    uploadTexture(img); computeHistograms(img);*/
                }
                if (ImGui::MenuItem("Double threshold")) showTDouble = true;
                if (ImGui::MenuItem("Hysteresis"))       showTHyst = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Niblack"))    showTNiblack = true;
                if (ImGui::MenuItem("Sauvola"))    showTSauvola = true;
                if (ImGui::MenuItem("Wolf-Jolion"))showTWolf = true;
                ImGui::EndMenu();
            }

            if (g_isBinary && ImGui::BeginMenu("Binary")) {
                ImGui::MenuItem("Erode", nullptr, &showErode);
                ImGui::MenuItem("Dilate", nullptr, &showDilate);
                ImGui::MenuItem("Open", nullptr, &showOpen);
                ImGui::MenuItem("Close", nullptr, &showClose);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Filters")) {
                // low‐pass filters
                if (ImGui::BeginMenu("Low-bands")) {
                    ImGui::MenuItem("Box 3x3", nullptr, &showBox3);
                    ImGui::MenuItem("Box 5x5", nullptr, &showBox5);
                    ImGui::MenuItem("Gauss 5x5", nullptr, &showGauss5);
                    ImGui::EndMenu();
                }
                // high‐pass filters
                if (ImGui::BeginMenu("High-bands")) {
                    ImGui::MenuItem("Laplacian 3x3 (4‐neighbours)", nullptr, &showLap3);
                    ImGui::MenuItem("Laplacian 3x3 (8‐neighbours)", nullptr, &showLap8);
                    ImGui::MenuItem("Sharpen 3x3", nullptr, &showSharpen);
                    ImGui::EndMenu();
                }
                // directional gradients
                if (ImGui::BeginMenu("Directional gradient")) {
                    ImGui::MenuItem("Sobel X", nullptr, &showSobelX);
                    ImGui::MenuItem("Sobel Y", nullptr, &showSobelY);
                    ImGui::MenuItem("Prewitt X", nullptr, &showPrewittX);
                    ImGui::MenuItem("Prewitt Y", nullptr, &showPrewittY);
                    ImGui::MenuItem("Sobel 45 degrees", nullptr, &showSobel45);
                    ImGui::MenuItem("Sobel 135 degrees", nullptr, &showSobel135);
                    ImGui::EndMenu();
                }
                // Laplace directional filters
                if (ImGui::BeginMenu("Laplace")) {
                    ImGui::MenuItem("Horizontal", nullptr, &showLapHor);
                    ImGui::MenuItem("Vertical", nullptr, &showLapVer);
                    ImGui::EndMenu();
                }
                // contour comparison
                if (ImGui::BeginMenu("Contour")) {
                    ImGui::MenuItem("Compare X", nullptr, &showCompareX);
                    ImGui::MenuItem("Compare Y", nullptr, &showCompareY);
                    ImGui::EndMenu();
                }
                // static
                if (ImGui::BeginMenu("Static")) {
                    ImGui::MenuItem("Min Filter", nullptr, &showMinFilter);
                    ImGui::MenuItem("Max Filter", nullptr, &showMaxFilter);
                    ImGui::MenuItem("Median Filter", nullptr, &showMedianFilter);
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Color Reduction")) {
                ImGui::MenuItem("Quantize", nullptr, &showQuantize);
                ImGui::MenuItem("Posterize", nullptr, &showPosterize);
                ImGui::MenuItem("K-means", nullptr, &showKMeans);
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        // ─── CLAMP POPUP ───────────────────────────────────────
        if (showClamp) {
            if (!initClamp) { bpClamp = img.pixels; initClamp = true; }
            img.pixels = bpClamp;
            clampImage(img, clampLo, clampHi);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Clamp", &showClamp, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderInt("Low", &clampLo, 0, 255); ImGui::SameLine();
            ImGui::InputInt("Low##i", &clampLo, 1);
            ImGui::SliderInt("High", &clampHi, 0, 255); ImGui::SameLine();
            ImGui::InputInt("High##i", &clampHi, 1);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpClamp = img.pixels;
                showClamp = initClamp = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpClamp;
                uploadTexture(img); computeHistograms(img);
                showClamp = initClamp = false;
            }
            ImGui::End();
        }

        // ─── NORMALIZE POPUP ───────────────────────────────────
        if (showNorm) {
            if (!initNorm) { bpNorm = img.pixels; initNorm = true; }
            img.pixels = bpNorm;
            normalizeImagePerChannel(img, normLo, normHi);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Normalize", &showNorm, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderInt("New Low", &normLo, 0, 255); ImGui::SameLine();
            ImGui::InputInt("New Low##i", &normLo, 1);
            ImGui::SliderInt("New High", &normHi, 0, 255); ImGui::SameLine();
            ImGui::InputInt("New High##i", &normHi, 1);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpNorm = img.pixels;
                showNorm = initNorm = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpNorm;
                uploadTexture(img); computeHistograms(img);
                showNorm = initNorm = false;
            }
            ImGui::End();
        }

        // ─── BRIGHTNESS POPUP ─────────────────────────────────
        if (showBright) {
            if (!initBright) { bpBright = img.pixels; initBright = true; }
            img.pixels = bpBright;
            brightnessImage(img, brightDelta);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Brightness", &showBright, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderInt("Delta", &brightDelta, -255, 255); ImGui::SameLine();
            ImGui::InputInt("Delta##i", &brightDelta, 1);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpBright = img.pixels;
                showBright = initBright = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpBright;
                uploadTexture(img); computeHistograms(img);
                showBright = initBright = false;
            }
            ImGui::End();
        }

        // ─── CONTRAST POPUP ───────────────────────────────────
        if (showContrast) {
            if (!initContrast) { bpContrast = img.pixels; initContrast = true; }
            img.pixels = bpContrast;
            contrastImage(img, contrastFactor);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Contrast", &showContrast, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderFloat("Factor", &contrastFactor, 0.1f, 3.0f); ImGui::SameLine();
            ImGui::InputFloat("Factor##i", &contrastFactor, 0.01f, 0.1f, "%.2f");
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpContrast = img.pixels;
                showContrast = initContrast = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpContrast;
                uploadTexture(img); computeHistograms(img);
                showContrast = initContrast = false;
            }
            ImGui::End();
        }

        // ─── HISTOGRAM STRETCH POPUP ─────────────────────────
        if (showStretch) {
            if (!initStretch) {
                bpStretch = img.pixels;
                initStretch = true;
            }
            img.pixels = bpStretch;

            // perform the true histogram stretch using percentiles
            float pLow = stretchLo * 0.01f;   // e.g. 1 → 0.01
            float pHigh = stretchHi * 0.01f;   // e.g. 99 → 0.99
            stretchHistogram(img, pLow, pHigh);

            uploadTexture(img);
            computeHistograms(img);

            ImGui::Begin("Contrast Stretch", &showStretch, ImGuiWindowFlags_AlwaysAutoResize);

            // Slider for lower percentile (0–100%)
            ImGui::SliderInt("Low %", &stretchLo, 0, 100);
            ImGui::SameLine();
            ImGui::InputInt("Low##i", &stretchLo, 1);
            // Slider for upper percentile (0–100%)
            ImGui::SliderInt("High %", &stretchHi, 0, 100);
            ImGui::SameLine();
            ImGui::InputInt("High##i", &stretchHi, 1);

            // ensure valid ordering
            if (stretchLo < 0)   stretchLo = 0;
            if (stretchLo > 100) stretchLo = 100;
            if (stretchHi < 0)   stretchHi = 0;
            if (stretchHi > 100) stretchHi = 100;
            if (stretchHi <= stretchLo) {
                // keep at least 1% gap
                stretchHi = std::min(stretchLo + 1, 100);
            }

            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpStretch = img.pixels;
                showStretch = false;
                initStretch = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpStretch;
                uploadTexture(img);
                computeHistograms(img);
                showStretch = false;
                initStretch = false;
            }
            ImGui::End();
        }

        // ─── MANUAL THRESHOLD POPUP ──────────────────────────
        static std::vector<unsigned char> bpTManual;
        static bool initTManual = false;
        if (showTManual) {
            if (!initTManual) { bpTManual = img.pixels; initTManual = true; }
            img.pixels = bpTManual;
            thresholdManual(img, tManual);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Threshold Manual", &showTManual, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderInt("T", &tManual, 0, 255); ImGui::SameLine();
            ImGui::InputInt("T##i", &tManual, 1);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpTManual = img.pixels;
                showTManual = initTManual = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpTManual;
                uploadTexture(img); g_isBinary = isBinaryImage(img);
                computeHistograms(img);
                showTManual = initTManual = false;
            }
            ImGui::End();
        }

        // ─── AUTO-MINIMA THRESHOLD POPUP ────────────────────
        if (showTAutoMin) {
            if (!initTAutoMin) {
                bpTAutoMin = img.pixels;
                initTAutoMin = true;
            }
            // Restore original pixels before computing each preview:
            img.pixels = bpTAutoMin;

            // Compute threshold via standalone function:
            int tAutoMin = computeAutoMinThreshold(img);

            // Apply threshold and update UI:
            thresholdManual(img, tAutoMin);
            uploadTexture(img);
            computeHistograms(img);
            g_isBinary = isBinaryImage(img);

            ImGui::Begin("Auto‐Minima Threshold", &showTAutoMin, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("T = %d", tAutoMin);
            if (ImGui::Button("Apply")) {
                // Push current state onto undo stack:
                undoStack.push_back({ img.pixels, img.channels });
                bpTAutoMin = img.pixels;
                showTAutoMin = initTAutoMin = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                // Revert to backup and recompute histograms:
                img.pixels = bpTAutoMin;
                uploadTexture(img);
                computeHistograms(img);
                showTAutoMin = initTAutoMin = false;
            }
            ImGui::End();
        }

        // ─── OTSU THRESHOLD POPUP ────────────────────
        if (showTOtsu) {
            if (!initTOtsu) {
                bpTOtsu = img.pixels;   // zachowujemy oryginał
                initTOtsu = true;
            }
            img.pixels = bpTOtsu;
            uploadTexture(img);
            computeHistograms(img);

            // histogram szarości
            std::vector<float> hist(256);
            if (img.channels == 1) {
                // jeśli to obraz w skali szarości, użyj bezpośrednio histGray
                hist = img.histGray;
            }
            else {
                // w przeciwnym razie uśredniamy kanały
                for (int i = 0; i < 256; ++i) {
                    hist[i] = (img.histR[i] + img.histG[i] + img.histB[i]) * (1.0f / 3.0f);
                }
            }

            // policz próg i zastosuj binaryzację
            size_t total = size_t(img.width) * img.height;
            tOtsu = otsuThreshold(hist, total);
            thresholdManual(img, tOtsu);

            uploadTexture(img);
            computeHistograms(img);
            g_isBinary = isBinaryImage(img);

            ImGui::Begin("Otsu Threshold", &showTOtsu, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("T = %d", tOtsu);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpTOtsu = img.pixels;
                initTOtsu = showTOtsu = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpTOtsu;
                uploadTexture(img);
                computeHistograms(img);
                initTOtsu = showTOtsu = false;
            }
            ImGui::End();
        }

        // ─── DOUBLE THRESHOLD POPUP ─────────────────────────
        if (showTDouble) {
            if (!initTDouble) { bpTDouble = img.pixels; initTDouble = true; }
            img.pixels = bpTDouble;
            thresholdDouble(img, t1, t2);
            uploadTexture(img); computeHistograms(img); g_isBinary = isBinaryImage(img);

            ImGui::Begin("Double Threshold", &showTDouble, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderInt("T1", &t1, 0, 255); ImGui::SameLine();
            ImGui::InputInt("T1##i", &t1, 1);
            ImGui::SliderInt("T2", &t2, 0, 255); ImGui::SameLine();
            ImGui::InputInt("T2##i", &t2, 1);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels,img.channels });
                bpTDouble = img.pixels;
                showTDouble = initTDouble = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpTDouble;
                uploadTexture(img); computeHistograms(img);
                showTDouble = initTDouble = false;
            }
            ImGui::End();
        }

        // ─── HYSTERESIS POPUP ───────────────────────────────
        if (showTHyst) {
            if (!initTHyst) { bpTHyst = img.pixels; initTHyst = true; }
            img.pixels = bpTHyst;
            thresholdHysteresis(img, tLow, tHigh);
            uploadTexture(img); computeHistograms(img); g_isBinary = isBinaryImage(img);

            ImGui::Begin("Hysteresis Threshold", &showTHyst, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderInt("Low", &tLow, 0, 255); ImGui::SameLine();
            ImGui::InputInt("Low##i", &tLow, 1);
            ImGui::SliderInt("High", &tHigh, 0, 255); ImGui::SameLine();
            ImGui::InputInt("High##i", &tHigh, 1);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels,img.channels });
                bpTHyst = img.pixels;
                showTHyst = initTHyst = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpTHyst;
                uploadTexture(img); computeHistograms(img);
                showTHyst = initTHyst = false;
            }
            ImGui::End();
        }

        // ─── NIBLACK POPUP ─────────────────────────────────
        if (showTNiblack) {
            ImGui::Begin("Niblack Threshold", &showTNiblack, ImGuiWindowFlags_AlwaysAutoResize);
            if (!initTNiblack) { bpTNiblack = img.pixels; initTNiblack = true; }

            ImGui::SliderInt("Window", &winSize, 3, 101); ImGui::SameLine();
            ImGui::InputInt("Win##i", &winSize, 1);
            ImGui::SliderFloat("k", &kParam, -1.0f, 1.0f); ImGui::SameLine();
            ImGui::InputFloat("k##i", &kParam, 0.01f, 0.1f, "%.3f");

            img.pixels = bpTNiblack;
            thresholdNiblack(img, winSize, kParam);
            uploadTexture(img); g_isBinary = isBinaryImage(img);
            computeHistograms(img);

            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels,img.channels });
                initTNiblack = showTNiblack = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpTNiblack;
                uploadTexture(img); computeHistograms(img);
                initTNiblack = showTNiblack = false;
            }
            ImGui::End();
        }

        // ─── SAUVOLA POPUP ──────────────────────────────────
        if (showTSauvola) {
            ImGui::Begin("Sauvola Threshold", &showTSauvola, ImGuiWindowFlags_AlwaysAutoResize);
            if (!initTSauvola) { bpTSauvola = img.pixels; initTSauvola = true; }

            ImGui::SliderInt("Window", &winSize, 3, 101); ImGui::SameLine();
            ImGui::InputInt("Win##i", &winSize, 1);
            ImGui::SliderFloat("k", &kParam, -1.0f, 1.0f); ImGui::SameLine();
            ImGui::InputFloat("k##i", &kParam, 0.01f, 0.1f, "%.3f");
            ImGui::SliderFloat("R", &Rparam, 1.0f, 255.0f); ImGui::SameLine();
            ImGui::InputFloat("R##i", &Rparam, 1.0f, 10.0f, "%.1f");

            img.pixels = bpTSauvola;
            thresholdSauvola(img, winSize, kParam, Rparam);
            uploadTexture(img); g_isBinary = isBinaryImage(img);
            computeHistograms(img);

            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels,img.channels });
                initTSauvola = showTSauvola = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpTSauvola;
                uploadTexture(img); computeHistograms(img);
                initTSauvola = showTSauvola = false;
            }
            ImGui::End();
        }

        // ─── WOLF-JOLION POPUP ──────────────────────────────
        if (showTWolf) {
            ImGui::Begin("Wolf-Jolion Threshold", &showTWolf, ImGuiWindowFlags_AlwaysAutoResize);
            if (!initTWolf) { bpTWolf = img.pixels; initTWolf = true; }

            ImGui::SliderInt("Window", &winSize, 3, 401); ImGui::SameLine();
            ImGui::InputInt("Win##i", &winSize, 1);
            ImGui::SliderFloat("k", &kParam, -1.0f, 1.0f); ImGui::SameLine();
            ImGui::InputFloat("k##i", &kParam, 0.01f, 0.1f, "%.3f");

            img.pixels = bpTWolf;
            thresholdWolfJolion(img, winSize, kParam);
            uploadTexture(img); g_isBinary = isBinaryImage(img);
            computeHistograms(img);

            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels,img.channels });
                initTWolf = showTWolf = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpTWolf;
                uploadTexture(img); computeHistograms(img);
                initTWolf = showTWolf = false;
            }
            ImGui::End();
        }

        // ─── ERODE POPUP ──────────────────────────────────────
        if (showErode) {
            if (!initErode) { bpErode = img.pixels; initErode = true; }
            img.pixels = bpErode;
            erodeBinary(img, binWin);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Erode", &showErode, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderInt("Window", &binWin, 1, 101); ImGui::SameLine();
            ImGui::InputInt("Win##i", &binWin, 1);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels,img.channels });
                bpErode = img.pixels;
                initErode = showErode = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpErode;
                uploadTexture(img); computeHistograms(img);
                initErode = showErode = false;
            }
            ImGui::End();
        }

        // ─── DILATE POPUP ─────────────────────────────────────
        if (showDilate) {
            if (!initDilate) { bpDilate = img.pixels; initDilate = true; }
            img.pixels = bpDilate;
            dilateBinary(img, binWin);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Dilate", &showDilate, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderInt("Window", &binWin, 1, 101); ImGui::SameLine();
            ImGui::InputInt("Win##i", &binWin, 1);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels,img.channels });
                bpDilate = img.pixels;
                initDilate = showDilate = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpDilate;
                uploadTexture(img); computeHistograms(img);
                initDilate = showDilate = false;
            }
            ImGui::End();
        }

        // ─── OPEN (ERODE→DILATE) POPUP ─────────────────────────
        if (showOpen) {
            if (!initOpen) { bpOpen = img.pixels; initOpen = true; }
            img.pixels = bpOpen;
            openBinary(img, binWin);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Open (Erode→Dilate)", &showOpen, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderInt("Window", &binWin, 1, 101); ImGui::SameLine();
            ImGui::InputInt("Win##i", &binWin, 1);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels,img.channels });
                bpOpen = img.pixels;
                initOpen = showOpen = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpOpen;
                uploadTexture(img); computeHistograms(img);
                initOpen = showOpen = false;
            }
            ImGui::End();
        }

        // ─── CLOSE (DILATE→ERODE) POPUP ───────────────────────
        if (showClose) {
            if (!initClose) { bpClose = img.pixels; initClose = true; }
            img.pixels = bpClose;
            closeBinary(img, binWin);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Close (Dilate→Erode)", &showClose, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderInt("Window", &binWin, 1, 101); ImGui::SameLine();
            ImGui::InputInt("Win##i", &binWin, 1);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels,img.channels });
                bpClose = img.pixels;
                initClose = showClose = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpClose;
                uploadTexture(img); computeHistograms(img);
                initClose = showClose = false;
            }
            ImGui::End();
        }

        // ─── BOX 3×3 POPUP ─────────────────
        if (showBox3) {
            if (!initBox3) { bpBox3 = img.pixels; initBox3 = true; }
            img.pixels = bpBox3;
            boxFilter3x3(img);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Box Filter 3×3", &showBox3, ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpBox3 = img.pixels;
                initBox3 = showBox3 = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpBox3;
                uploadTexture(img); computeHistograms(img);
                initBox3 = showBox3 = false;
            }
            ImGui::End();
        }

        // ─── BOX 5×5 POPUP ─────────────────
        if (showBox5) {
            if (!initBox5) { bpBox5 = img.pixels; initBox5 = true; }
            img.pixels = bpBox5;
            boxFilter5x5(img);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Box Filter 5×5", &showBox5, ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpBox5 = img.pixels;
                initBox5 = showBox5 = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpBox5;
                uploadTexture(img); computeHistograms(img);
                initBox5 = showBox5 = false;
            }
            ImGui::End();
        }

        // ─── GAUSS 5×5 POPUP ──────────────
        if (showGauss5) {
            if (!initGauss5) { bpGauss5 = img.pixels; initGauss5 = true; }
            img.pixels = bpGauss5;
            gaussFilter5x5(img);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Gauss Filter 5×5", &showGauss5, ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpGauss5 = img.pixels;
                initGauss5 = showGauss5 = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpGauss5;
                uploadTexture(img); computeHistograms(img);
                initGauss5 = showGauss5 = false;
            }
            ImGui::End();
        }

        // ─── LAPLACIAN 3×3 (4-sąs.) POPUP ──────────────
        if (showLap3) {
            if (!initLap3) { bpLap3 = img.pixels; initLap3 = true; }
            img.pixels = bpLap3;
            laplacian3x3(img);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Laplacian 3×3 (4-sąs.)", &showLap3, ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpLap3 = img.pixels;
                initLap3 = showLap3 = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpLap3;
                uploadTexture(img); computeHistograms(img);
                initLap3 = showLap3 = false;
            }
            ImGui::End();
        }

        // ─── LAPLACIAN 3×3 (8-sąs.) POPUP ──────────────
        if (showLap8) {
            if (!initLap8) { bpLap8 = img.pixels; initLap8 = true; }
            img.pixels = bpLap8;
            laplacian8x8(img);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Laplacian 3×3 (8-sąs.)", &showLap8, ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpLap8 = img.pixels;
                initLap8 = showLap8 = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpLap8;
                uploadTexture(img); computeHistograms(img);
                initLap8 = showLap8 = false;
            }
            ImGui::End();
        }

        // ─── SHARPEN 3×3 POPUP ─────────────
        if (showSharpen) {
            if (!initSharpen) { bpSharpen = img.pixels; initSharpen = true; }
            img.pixels = bpSharpen;
            sharpen3x3(img);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Sharpen 3×3", &showSharpen, ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpSharpen = img.pixels;
                initSharpen = showSharpen = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpSharpen;
                uploadTexture(img); computeHistograms(img);
                initSharpen = showSharpen = false;
            }
            ImGui::End();
        }

        // ─── SOBEL X POPUP ──────────
        if (showSobelX) {
            if (!initSobelX) { bpSobelX = img.pixels; initSobelX = true; }
            img.pixels = bpSobelX;
            sobelX(img);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Sobel X", &showSobelX, ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpSobelX = img.pixels;
                initSobelX = showSobelX = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpSobelX;
                uploadTexture(img); computeHistograms(img);
                initSobelX = showSobelX = false;
            }
            ImGui::End();
        }

        // ─── SOBEL Y POPUP ──────────
        if (showSobelY) {
            if (!initSobelY) { bpSobelY = img.pixels; initSobelY = true; }
            img.pixels = bpSobelY;
            sobelY(img);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Sobel Y", &showSobelY, ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpSobelY = img.pixels;
                initSobelY = showSobelY = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpSobelY;
                uploadTexture(img); computeHistograms(img);
                initSobelY = showSobelY = false;
            }
            ImGui::End();
        }

        // ─── PREWITT X POPUP ─────────
        if (showPrewittX) {
            if (!initPrewittX) { bpPrewittX = img.pixels; initPrewittX = true; }
            img.pixels = bpPrewittX;
            prewittX(img);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Prewitt X", &showPrewittX, ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpPrewittX = img.pixels;
                initPrewittX = showPrewittX = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpPrewittX;
                uploadTexture(img); computeHistograms(img);
                initPrewittX = showPrewittX = false;
            }
            ImGui::End();
        }

        // ─── PREWITT Y POPUP ─────────
        if (showPrewittY) {
            if (!initPrewittY) { bpPrewittY = img.pixels; initPrewittY = true; }
            img.pixels = bpPrewittY;
            prewittY(img);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Prewitt Y", &showPrewittY, ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpPrewittY = img.pixels;
                initPrewittY = showPrewittY = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpPrewittY;
                uploadTexture(img); computeHistograms(img);
                initPrewittY = showPrewittY = false;
            }
            ImGui::End();
        }

        // ─── SOBEL 45° POPUP ────────
        if (showSobel45) {
            if (!initSobel45) { bpSobel45 = img.pixels; initSobel45 = true; }
            img.pixels = bpSobel45;
            sobel45(img);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Sobel 45°", &showSobel45, ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpSobel45 = img.pixels;
                initSobel45 = showSobel45 = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpSobel45;
                uploadTexture(img); computeHistograms(img);
                initSobel45 = showSobel45 = false;
            }
            ImGui::End();
        }

        // ─── SOBEL 135° POPUP ───────
        if (showSobel135) {
            if (!initSobel135) { bpSobel135 = img.pixels; initSobel135 = true; }
            img.pixels = bpSobel135;
            sobel135(img);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Sobel 135°", &showSobel135, ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpSobel135 = img.pixels;
                initSobel135 = showSobel135 = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpSobel135;
                uploadTexture(img); computeHistograms(img);
                initSobel135 = showSobel135 = false;
            }
            ImGui::End();
        }

        // ─── LAPLACE HORIZONTAL POPUP ────────────
        if (showLapHor) {
            if (!initLapHor) { bpLapHor = img.pixels; initLapHor = true; }
            img.pixels = bpLapHor;
            laplaceHorizontal(img);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Laplace Horizontal", &showLapHor, ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpLapHor = img.pixels;
                initLapHor = showLapHor = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpLapHor;
                uploadTexture(img); computeHistograms(img);
                initLapHor = showLapHor = false;
            }
            ImGui::End();
        }

        // ─── LAPLACE VERTICAL POPUP ──────────────
        if (showLapVer) {
            if (!initLapVer) { bpLapVer = img.pixels; initLapVer = true; }
            img.pixels = bpLapVer;
            laplaceVertical(img);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Laplace Vertical", &showLapVer, ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpLapVer = img.pixels;
                initLapVer = showLapVer = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpLapVer;
                uploadTexture(img); computeHistograms(img);
                initLapVer = showLapVer = false;
            }
            ImGui::End();
        }

        // ─── PORÓWNANIE KONTRU X POPUP ─
        if (showCompareX) {
            if (!initCmpX) { bpCmpX = img.pixels; initCmpX = true; }
            img.pixels = bpCmpX;
            compareContourX(img);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Compare Contour X", &showCompareX, ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpCmpX = img.pixels;
                initCmpX = showCompareX = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpCmpX;
                uploadTexture(img); computeHistograms(img);
                initCmpX = showCompareX = false;
            }
            ImGui::End();
        }

        // ─── PORÓWNANIE KONTRU Y POPUP ─
        if (showCompareY) {
            if (!initCmpY) { bpCmpY = img.pixels; initCmpY = true; }
            img.pixels = bpCmpY;
            compareContourY(img);
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Compare Contour Y", &showCompareY, ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpCmpY = img.pixels;
                initCmpY = showCompareY = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpCmpY;
                uploadTexture(img); computeHistograms(img);
                initCmpY = showCompareY = false;
            }
            ImGui::End();
        }

        // ─── MIN FILTER ───────────────────────────
        if (showMinFilter) {
            if (!initMinFilter) {
                bpMinFilter = img.pixels;
                initMinFilter = true;
            }
            img.pixels = bpMinFilter;
            uploadTexture(img); computeHistograms(img);
            // (binary status does not apply to grayscale filters)

            ImGui::Begin("Min Filter", &showMinFilter, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderInt("Window Size", &minWinSize, 1, 15);
            ImGui::SameLine();
            ImGui::InputInt("Win##i", &minWinSize, 1);
            if (minWinSize % 2 == 0) minWinSize += 1;    // ensure an odd window
            if (minWinSize < 1) minWinSize = 1;

            if (ImGui::Button("Apply")) {
                minFilter(img, minWinSize);
                uploadTexture(img); computeHistograms(img);
                undoStack.push_back({ img.pixels, img.channels });
                bpMinFilter = img.pixels;
                initMinFilter = showMinFilter = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpMinFilter;
                uploadTexture(img); computeHistograms(img);
                initMinFilter = showMinFilter = false;
            }
            ImGui::End();
        }

        // ─── MAX FILTER ───────────────────────────
        if (showMaxFilter) {
            if (!initMaxFilter) {
                bpMaxFilter = img.pixels;
                initMaxFilter = true;
            }
            img.pixels = bpMaxFilter;
            // preview with the current window size
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Max Filter", &showMaxFilter, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderInt("Window Size", &maxWinSize, 1, 15);
            ImGui::SameLine();
            ImGui::InputInt("Win##i", &maxWinSize, 1);
            if (maxWinSize % 2 == 0) maxWinSize += 1;    // ensure an odd window
            if (maxWinSize < 1) maxWinSize = 1;

            if (ImGui::Button("Apply")) {
                maxFilter(img, maxWinSize);
                uploadTexture(img); computeHistograms(img);
                undoStack.push_back({ img.pixels, img.channels });
                bpMaxFilter = img.pixels;
                initMaxFilter = showMaxFilter = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpMaxFilter;
                uploadTexture(img); computeHistograms(img);
                initMaxFilter = showMaxFilter = false;
            }
            ImGui::End();
        }

        // ─── MEDIAN FILTER ────────────────────────
        if (showMedianFilter) {
            if (!initMedianFilter) {
                bpMedianFilter = img.pixels;
                initMedianFilter = true;
            }
            img.pixels = bpMedianFilter;
            // preview with the current window size
            uploadTexture(img); computeHistograms(img);

            ImGui::Begin("Median Filter", &showMedianFilter, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderInt("Window Size", &medianWinSize, 1, 15);
            ImGui::SameLine();
            ImGui::InputInt("Win##i", &medianWinSize, 1);
            if (medianWinSize % 2 == 0) medianWinSize += 1;    // ensure an odd window
            if (medianWinSize < 1) medianWinSize = 1;

            if (ImGui::Button("Apply")) {
                medianFilter(img, medianWinSize);
                uploadTexture(img);
                computeHistograms(img);
                undoStack.push_back({ img.pixels, img.channels });
                bpMedianFilter = img.pixels;
                initMedianFilter = showMedianFilter = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpMedianFilter;
                initMedianFilter = showMedianFilter = false;
            }
            ImGui::End();
        }

        if (showQuantize) {
            if (!initQuantize) {
                // zachowaj oryginalny stan obrazka
                bpQuantize = img.pixels;
                initQuantize = true;
            }
            // przywróć oryginał przed każdą aktualizacją podglądu
            img.pixels = bpQuantize;

            // podgląd kwantyzacji na img
            quantizeImage(img, quantizeLevels);
            uploadTexture(img);
            computeHistograms(img);

            ImGui::Begin("Quantize Image", &showQuantize, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderInt("Levels (L)", &quantizeLevels, 2, 10);
            ImGui::SameLine();
            ImGui::InputInt("##L", &quantizeLevels);
            if (quantizeLevels < 2)  quantizeLevels = 2;
            if (quantizeLevels > 10) quantizeLevels = 10;

            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpQuantize = img.pixels;
                initQuantize = false;
                showQuantize = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpQuantize;
                uploadTexture(img);
                computeHistograms(img);
                initQuantize = false;
                showQuantize = false;
            }
            ImGui::End();
        }

        if (showPosterize) {
            if (!initPosterize) {
                bpPosterize = img.pixels;
                initPosterize = true;
            }
            img.pixels = bpPosterize;

            posterizeImage(img, posterizeLevels);
            uploadTexture(img);
            computeHistograms(img);

            ImGui::Begin("Posterize Image", &showPosterize, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderInt("Levels", &posterizeLevels, 2, 10);
            ImGui::SameLine();
            ImGui::InputInt("##Levels", &posterizeLevels);
            if (posterizeLevels < 2)  posterizeLevels = 2;
            if (posterizeLevels > 10) posterizeLevels = 10;

            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpPosterize = img.pixels;
                initPosterize = false;
                showPosterize = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpPosterize;
                uploadTexture(img);
                computeHistograms(img);
                initPosterize = false;
                showPosterize = false;
            }
            ImGui::End();
        }

        if (showKMeans) {
            if (!initKMeans) {
                bpKMeansOriginal = img.pixels;
                initKMeans = true;
                prevKMeansClusters = -1;
            }

            // sprawdź czy zmienił się kMeansClusters od ostatniego razu
            if (kMeansClusters != prevKMeansClusters) {
                // przywróć oryginał na wejście
                img.pixels = bpKMeansOriginal;
                // uruchom k-means tylko raz
                kMeansColorQuantization(img, kMeansClusters, /* maxIters= */ 10);

                // zapisz efekt do bufora podglądu
                bpKMeansPreview = img.pixels;
                prevKMeansClusters = kMeansClusters;
            }
            else {
                // nie zmieniało się k
                img.pixels = bpKMeansPreview;
            }

            // wyślij do tekstury i histogram:
            uploadTexture(img);
            computeHistograms(img);

            // rysuj okno ImGui:
            ImGui::Begin("K-means Quantization", &showKMeans, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SliderInt("Clusters (k)", &kMeansClusters, 1, 16);
            ImGui::SameLine();
            ImGui::InputInt("##k", &kMeansClusters);
            if (kMeansClusters < 1)  kMeansClusters = 1;
            if (kMeansClusters > 256) kMeansClusters = 256;

            if (ImGui::Button("Apply")) {
                undoStack.push_back({ img.pixels, img.channels });
                bpKMeansOriginal = img.pixels;
                initKMeans = false;
                showKMeans = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                img.pixels = bpKMeansOriginal;
                uploadTexture(img);
                computeHistograms(img);
                initKMeans = false;
                showKMeans = false;
            }
            ImGui::End();
        }

        // ─── RENDER IMAGE & HISTOGRAM ───────────────────────
        int w, h; glfwGetFramebufferSize(win, &w, &h);
        setupProjection(w, h);
        glClear(GL_COLOR_BUFFER_BIT);
        renderImage(img, w, h);

        ImGui::SetNextWindowPos(ImVec2(w - RIGHT_BAR_WIDTH, TOP_BAR_HEIGHT));
        ImGui::SetNextWindowSize(ImVec2(RIGHT_BAR_WIDTH, h - TOP_BAR_HEIGHT));
        ImGui::Begin("Histogram", nullptr,
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse);
        renderHistogram(img);
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }
}

//int main() {
//    // 1) Load the source image (color.jpg)
//    int w, h, ch;
//    unsigned char* data = stbi_load("gray.jpg", &w, &h, &ch, 0);
//    if (!data) {
//        std::cerr << "Failed to load color.jpg\n";
//        return -1;
//    }
//    ImageData img;
//    img.width = w;
//    img.height = h;
//    img.channels = ch;
//    img.pixels.assign(data, data + (w * h * ch));
//    stbi_image_free(data);
//
//    // Helper to save a copy under a given suffix:
//    auto saveEffect = [&](const std::string& suffix) {
//        std::string fn = "gray_" + suffix + ".jpg";
//        stbi_write_jpg(fn.c_str(), img.width, img.height, img.channels, img.pixels.data(), 100);
//        std::cout << "Saved: " << fn << "\n";
//        };
//
//    // 2) For each effect: restore original, apply, save
//    std::vector<unsigned char> backup = img.pixels;
//
//    // clampImage
//    img.pixels = backup;
//    clampImage(img, 50, 200);
//    saveEffect("clamp");
//
//    // normalizeImagePerChannel
//    img.pixels = backup;
//    normalizeImagePerChannel(img, 0, 255);
//    saveEffect("normalize");
//
//    // brightnessImage
//    img.pixels = backup;
//    brightnessImage(img, 30);
//    saveEffect("bright");
//
//    // contrastImage
//    img.pixels = backup;
//    contrastImage(img, 1.5f);
//    saveEffect("contrast");
//
//    // stretchHistogram
//    img.pixels = backup;
//    stretchHistogram(img, 0, 255);
//    saveEffect("stretch");
//
//    // thresholdManual
//    img.pixels = backup;
//    thresholdManual(img, 128);
//    saveEffect("threshold_manual");
//
//    // thresholdOtsu
//    img.pixels = backup;
//    thresholdOtsu(img);
//    saveEffect("threshold_otsu");
//
//    // thresholdDouble
//    img.pixels = backup;
//    thresholdDouble(img, 50, 200);
//    saveEffect("threshold_double");
//
//    // thresholdHysteresis
//    img.pixels = backup;
//    thresholdHysteresis(img, 50, 150);
//    saveEffect("threshold_hysteresis");
//
//    // Niblack, Sauvola, Wolf-Jolion
//    img.pixels = backup;
//    thresholdNiblack(img, 15, 0.2f);
//    saveEffect("niblack");
//    img.pixels = backup;
//    thresholdSauvola(img, 15, 0.2f, 128.0f);
//    saveEffect("sauvola");
//    img.pixels = backup;
//    thresholdWolfJolion(img, 15, 0.2f);
//    saveEffect("wolfjolion");
//
//    // Binary morphology (only meaningful after a threshold, so we'll use the Otsu result)
//    img.pixels = backup;
//    thresholdDouble(img, 50, 200);
//    g_isBinary = isBinaryImage(img);
//    erodeBinary(img, 5); saveEffect("erode");
//    img.pixels = backup; thresholdOtsu(img);
//    dilateBinary(img, 5); saveEffect("dilate");
//    img.pixels = backup; thresholdOtsu(img);
//    openBinary(img, 9); saveEffect("open");
//    img.pixels = backup; thresholdOtsu(img);
//    closeBinary(img, 9); saveEffect("close");
//
//    // Low‐pass filters
//    img.pixels = backup;
//    boxFilter3x3(img);   saveEffect("box3x3");
//    img.pixels = backup;
//    boxFilter5x5(img);   saveEffect("box5x5");
//    img.pixels = backup;
//    gaussFilter5x5(img); saveEffect("gauss5x5");
//
//    // High‐pass filters
//    img.pixels = backup;
//    laplacian3x3(img);   saveEffect("lap3_4n");
//    img.pixels = backup;
//    laplacian8x8(img);   saveEffect("lap3_8n");
//    img.pixels = backup;
//    sharpen3x3(img);     saveEffect("sharpen");
//
//    // Directional gradients
//    img.pixels = backup;
//    sobelX(img);         saveEffect("sobel_x");
//    img.pixels = backup;
//    sobelY(img);         saveEffect("sobel_y");
//    img.pixels = backup;
//    prewittX(img);       saveEffect("prewitt_x");
//    img.pixels = backup;
//    prewittY(img);       saveEffect("prewitt_y");
//    img.pixels = backup;
//    sobel45(img);        saveEffect("sobel_45");
//    img.pixels = backup;
//    sobel135(img);       saveEffect("sobel_135");
//
//    // Laplace directional
//    img.pixels = backup;
//    laplaceHorizontal(img); saveEffect("lap_horizontal");
//    img.pixels = backup;
//    laplaceVertical(img);   saveEffect("lap_vertical");
//
//    // Contour comparisons
//    img.pixels = backup;
//    compareContourX(img);  saveEffect("compare_x");
//    img.pixels = backup;
//    compareContourY(img);  saveEffect("compare_y");
//
//    // Static rank filters
//    img.pixels = backup;
//    minFilter(img, 5);  saveEffect("min3");
//    img.pixels = backup;
//    maxFilter(img, 5);  saveEffect("max3");
//    img.pixels = backup;
//    medianFilter(img, 9);  saveEffect("median3");
//
//    // Color reduction
//    img.pixels = backup;
//    quantizeImage(img, 4);  saveEffect("quantize4");
//    img.pixels = backup;
//    posterizeImage(img, 6);  saveEffect("posterize4");
//    img.pixels = backup;
//    kMeansColorQuantization(img, 8);
//    saveEffect("kmeans4");
//
//    return 0;
//}

