# Photoshoot Image Processing GUI

## A cross-platform C++ application using GLFW, OpenGL, and ImGui to:

Load JPG images via file dialog

Display the image with pan & zoom controls

View real-time color or grayscale histograms

### Apply a variety of filters and transforms:

Intensity adjustments: clamp, normalize, brightness, contrast, histogram stretch

Thresholding: manual, Otsu, auto-minima, double, hysteresis, Niblack, Sauvola, Wolf-Jolion

Binary morphology: erode, dilate, open, close

Spatial filters: box, Gaussian blur, Laplacian, sharpen

Edge detection: Sobel, Prewitt, directional Laplace, contour compare

Order-statistics: min, max, median

Color reduction: quantize, posterize, k-means clustering

Undo/redo support with Ctrl+Z

Save modified images as JPG


## Requirements:

C++17 compiler (gcc, clang, MSVC)

GLFW 3.x

OpenGL 3.0+ context

ImGui

stb_image and stb_image_write

tinyfiledialogs


## License: MIT

