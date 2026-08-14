<div align="center">
  <img src="assets/icons/512x512/icon.png" alt="SharpMark Logo" width="128" height="128">
  <h1>SharpMark</h1>
  <p><b>Multi-threaded Image Sharpness Analyzer and Photo Culling Application</b></p>
</div>

## Overview

SharpMark is a desktop photo culling and image quality analysis tool built with C++20 and Qt6. It is designed to accelerate the initial review process for high-volume photographic shoots by automatically evaluating focus sharpness, identifying exposure defects, computing aesthetic quality scores, and clustering burst sequences.

The application integrates into standard photographic workflows through RAW decoding, real-time 3D LUT color grading, progressive image loading, two-way XMP metadata synchronization (compatible with Adobe Lightroom, Capture One, and Darktable), and direct handoff to external image editors.

## Key Features

### Modular Analysis Pipeline
* **Configurable Processing Stages:** Enable, disable, and reorder pipeline stages (Preprocessors, Processors, Postprocessors) from a dedicated sidebar before running a scan.
* **Laplacian Focus Analysis:** Calculates focal sharpness mathematically using Laplacian variance to detect out-of-focus and motion-blurred images based on a configurable threshold.
* **Exposure Verification:** Evaluates luminance distribution and flags severely underexposed or overexposed images via configurable pixel-clipping thresholds.
* **On-Device AI Aesthetic Scoring:** Runs local ONNX models (OpenAI CLIP ViT-B/32 and LAION Aesthetic Predictor v2) to calculate aesthetic scores on a 0 to 10 scale without requiring an internet connection. Includes configurable score color-coding.
* **Active Learning Adaptation:** Assigning star ratings trains an on-device linear regression model to calibrate subsequent aesthetic scores according to user preference.

### Burst Sequence Clustering
* **Perceptual Hash Grouping (dHash):** Fast preprocessor stage that detects and clusters rapid burst sequences based on Hamming distance thresholds.
* **Semantic Embedding Grouping (CLIP):** Optional postprocessor stage that clusters sequences based on 512-dimensional CLIP embedding cosine similarity.
* **Group Management:** Expand and collapse burst groups individually in both Grid and List views, or toggle grouping globally.
* **Best Shot Highlighting:** Automatically identifies and highlights the top-scoring image within each burst cluster.
* **Group-Preserving Sorting:** Sort images by Scan Order, Best First, or Worst First while maintaining cluster integrity.

### High-Performance Image Viewing
* **Full-Resolution Viewer:** Dedicated inspection window supporting smooth scroll-to-zoom, panning, and complete keyboard navigation.
* **Progressive RAW Loading:** Instantly displays embedded preview thumbnails while full-resolution RAW decoding proceeds asynchronously in the background.
* **Predictive Preloading:** Automatically preloads neighboring images in the viewer queue to eliminate navigation latency.
* **Live RGB Histogram:** Real-time channel distribution rendered directly in the viewer sidebar.
* **EXIF Metadata Inspection:** Displays camera model, lens, exposure time, aperture, ISO, focal length, and resolution.
* **3D LUT Color Grading:** Load and apply standard `.cube` 3D LUT color grading profiles in real time across the viewer and thumbnail previews.

### Culling and Workflow Integration
* **Star Ratings and Color Labels:** Assign 1–5 star ratings and color labels (Red, Yellow, Green, Blue, Purple) via single-key shortcuts.
* **Two-Way XMP Synchronization:** Uses bundled ExifTool to read existing metadata on import and write star ratings and color labels directly to image metadata or XMP sidecars. Changes are immediately recognized by Adobe Lightroom, Capture One, Darktable, and DigiKam.
* **Metadata Filtering:** Filter the grid view by minimum star rating (1+ through 5 stars) and active color label.
* **Multi-Selection:** Support for single selection, Shift-click range selection, and Ctrl-click multi-selection for batch operations.
* **External Editor Launch:** Open individual or batch-selected images directly in an external editor (such as Photoshop, Lightroom, or Affinity Photo) using a single shortcut.
* **Configurable Deletion:** Choose between moving files to the system Recycle Bin/Trash, moving to a dedicated `_Rejected` subfolder, or permanent deletion.
* **Disk Cache Management:** Persistent caching system with a built-in management interface to monitor and clear disk cache usage per directory.

## Keyboard Shortcuts

### Global and Grid Shortcuts

| Shortcut | Action |
|---|---|
| `1` – `5` | Set star rating on selected image(s) |
| `0` | Clear star rating on selected image(s) |
| `6` | Set Red color label on selected image(s) |
| `7` | Set Yellow color label on selected image(s) |
| `8` | Set Green color label on selected image(s) |
| `9` | Set Blue color label on selected image(s) |
| `p` | Set Purple color label on selected image(s) |
| `o` | Clear color label on selected image(s) |
| `e` | Open selected image(s) in configured external editor |
| `Delete` | Delete selected image(s) using configured deletion mode |
| `Escape` | Clear selection / Close dialogs |
| `Shift` + Click | Select range of images |
| `Ctrl` + Click | Toggle individual image selection |

### Image Viewer Shortcuts

| Shortcut | Action |
|---|---|
| `←` / `→` | Navigate to previous / next image |
| `1` – `5` | Assign star rating |
| `0` | Clear star rating |
| `6` – `9`, `p` | Assign color label (Red, Yellow, Green, Blue, Purple) |
| `o` | Clear color label |
| `e` | Open current image in external editor |
| `Delete` | Delete current image |
| `Escape` | Close viewer |

## Installation

Pre-compiled release packages are available on the [Releases](../../releases) page.

### Windows (Portable Package)
1. Download `SharpMark-...-Portable.zip` from the Releases page.
2. Extract the archive to any directory.
3. Run `SharpMark.exe`. The package is fully self-contained and includes all necessary runtime libraries, ONNX models, ExifTool, and default LUTs.

### Linux (Debian / Ubuntu Package)
1. Download the `.deb` package (e.g., `sharpmark-2.0.0-alpha.1-amd64.deb`) from the Releases page.
2. Install the package using `apt` to resolve all system dependencies:
   ```bash
   sudo apt install ./sharpmark-2.0.0-alpha.1-amd64.deb
   ```
3. Launch `SharpMark` from your application launcher or terminal.

### Running with Nix

For systems with [Nix](https://nixos.org/) and flakes enabled:

* Run directly from GitHub:
  ```bash
  nix run github:bluetuna42/SharpMark
  ```
* Run from a local clone:
  ```bash
  nix run .
  ```
* Enter a development environment with all required tools and dependencies:
  ```bash
  nix develop
  ```

## Building from Source

### Prerequisites
* C++20 compliant compiler (MSVC 2022, GCC 11+, or Clang 13+)
* CMake 3.16 or newer
* Ninja build system (recommended)
* Qt6 (`Core`, `Gui`, `Qml`, `Quick`)
* LibRaw development libraries

> **Note:** ONNX Runtime, ExifTool (Windows), and the vision model are downloaded and configured automatically by CMake during the build setup.

### Linux (Debian / Ubuntu)

1. Install build dependencies:
   ```bash
   sudo apt update
   sudo apt install build-essential cmake ninja-build libraw-dev qt6-base-dev qt6-declarative-dev
   ```

2. Clone repository and build:
   ```bash
   git clone https://github.com/bluetuna42/SharpMark.git
   cd SharpMark
   mkdir build && cd build
   cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
   ninja
   ```

3. Install to system:
   ```bash
   sudo ninja install
   ```

4. *(Optional)* Generate `.deb` package:
   ```bash
   cpack -G DEB
   ```

### Windows (MSVC + vcpkg)

1. Install prerequisites:
   * Visual Studio 2022 with the **Desktop development with C++** workload
   * CMake (3.16 or newer)
   * Ninja
   * [vcpkg](https://github.com/microsoft/vcpkg) (set the `VCPKG_ROOT` environment variable)
   * Qt6 (`MSVC 2022 64-bit` component)

2. Install LibRaw via vcpkg:
   ```cmd
   vcpkg install libraw:x64-windows
   ```

3. Clone the repository:
   ```cmd
   git clone https://github.com/bluetuna42/SharpMark.git
   cd SharpMark
   ```

4. Configure and compile:
   ```cmd
   cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2022_64 -B build
   cmake --build build --config Release
   ```

5. Assemble the standalone portable bundle:
   ```cmd
   cmake --install build
   ```
   The self-contained application will be created in the `SharpMark_Portable` directory. Run `SharpMark.exe` to start the program.

## Contributing

Bug reports, feature requests, and code contributions are welcome. Please open an issue or submit a pull request on the repository.

## License

This project is licensed under the [Apache License 2.0](LICENSE).
