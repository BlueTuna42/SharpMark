<div align="center">
  <img src="assets/icons/512x512/icon.png" alt="SharpMark Logo" width="128" height="128">
  <h1>SharpMark</h1>
  <p><b>Fast, Multi-threaded Image Sharpness Analyzer and Culling Tool</b></p>
</div>

## About

**SharpMark** is a cross-platform graphical desktop application designed to automatically detect and filter out blurry and poorly-exposed images from large photographic batches.

Built with C++ and Qt6, the tool delivers a consistent native experience across Windows and Linux. It provides a clean, responsive interface while executing a configurable multi-stage analysis pipeline underneath. The pipeline evaluates focus sharpness via Laplacian variance, flags severely under- or overexposed shots, and optionally scores each image for aesthetic quality using on-device AI models. To integrate seamlessly into professional photography workflows, SharpMark handles RAW files natively via LibRaw, features a robust built-in image viewer for manual inspection, and can write evaluation results directly to XMP metadata using ExifTool.

## Features

* **Cross-Platform Compatibility:** Fully supported on Windows and Linux, providing a consistent native desktop experience across operating systems.
* **Integrated Image Viewer:** A dedicated full-resolution image viewer with smooth scroll-to-zoom, pan, keyboard navigation, and a collapsible metadata/histogram sidebar for manual inspection without relying on external software.
* **Native Graphical Interface:** A responsive Qt6 Quick-based GUI offering dynamic Mosaic Grid and Detailed List view modes, system-aware dark/light/system theme integration, and animated transitions.
* **High Performance:** Multi-threaded architecture designed to quickly parse and analyze large batches of high-resolution images.
* **Configurable Analysis Pipeline:** A toggleable sidebar lets you enable, disable, and drag-to-reorder the processing steps — Exposure Check, Laplacian Focus Check, and AI Aesthetic Scorer — before each scan.
* **Blur Detection:** Evaluates focal sharpness mathematically using Laplacian variance.
* **Exposure Check:** Automatically rejects severely underexposed or overexposed images based on a configurable pixel-clipping threshold, independent of the sharpness analysis.
* **AI Aesthetic Scoring:** Powered by on-device ONNX models (CLIP ViT + LAION Aesthetic Predictor), each image receives a floating-point aesthetic score displayed directly on its card. No internet connection is required at runtime.
* **Active Learning / Personal Model:** Assigning star ratings in the viewer trains a lightweight personal linear model that continuously adjusts future aesthetic scores to match your taste. Learned weights are saved locally and persist between sessions.
* **Burst Grouping:** Uses a fast perceptual dHash algorithm to automatically detect and group visually similar burst-sequence shots. Groups are collapsible in both Grid and List views, keeping the workspace uncluttered.
* **Advanced RAW Support:** Powered by LibRaw, featuring independently configurable RAW loading modes for viewing (Thumbnail, Half-size, Full-size) and analysis, so you can balance preview speed against computational accuracy.
* **3D LUT Preview:** Load any standard `.cube` LUT file to apply a real-time color grade in the image viewer. Switch between loaded presets from the toolbar — useful for evaluating images under a specific color treatment before exporting.
* **Live Histogram:** The viewer sidebar renders a real-time RGB histogram for the currently displayed image.
* **Star Ratings & XMP Metadata:** Assign 1–5 star ratings via on-screen clicks or keyboard shortcuts (`1`–`5`, `0` to clear). Ratings can be written directly to XMP metadata using the bundled ExifTool, making them immediately visible in Lightroom, Capture One, and darktable.
* **Result Sorting:** Sort the image list by Default Scan Order, Best First, or Worst First based on the computed aesthetic score.
* **Efficient File Management:** Includes bulk deletion capabilities to immediately move out-of-focus shots to the system trash.

---

## Viewer Keyboard Shortcuts

| Key | Action |
|---|---|
| `←` / `→` | Previous / Next image |
| `1` – `5` | Set star rating |
| `0` | Clear star rating |
| `Delete` | Move current image to Trash |
| `Escape` | Close viewer |

---

## Installation (Pre-compiled Releases)

Ready-to-use binaries are available on the [Releases](../../releases) page.

### Windows (Portable)
1. Navigate to the **Releases** tab and download `SharpMark-...-Portable.zip`.
2. Extract the archive to your preferred directory.
3. Execute `SharpMark.exe`. No installation is required.

### Linux (Debian / Ubuntu)
1. Download the `.deb` package (e.g., `sharpmark-2.0.0-alpha.1-amd64.deb`) from the **Releases** tab.
2. Open your terminal and install the package via `apt` to automatically resolve Qt6 dependencies:
   ```bash
   sudo apt install ./sharpmark-2.0.0-alpha.1-amd64.deb
   ```
3. Launch **SharpMark** from your desktop environment's application menu.

---

## Running with Nix

If you use [Nix](https://nixos.org/) with flakes enabled, you can run SharpMark instantly without manually installing dependencies.

To run the application directly from GitHub:
```bash
nix run github:bluetuna42/SharpMark
```

If you have cloned the repository locally, you can run:
```bash
nix run .
```

To enter a development shell with all necessary build dependencies and tools (like `gdb`, `cmake`, `pkg-config`):
```bash
nix develop
```

---

## Building from Source

To compile the application from source, follow the instructions below.

> **Note:** ONNX Runtime and ExifTool are downloaded automatically by CMake during the configure step. No manual setup is required for these dependencies.

### Dependencies
The following tools and libraries are required to build SharpMark:
* **C++20** compatible compiler (GCC, Clang, or MSVC)
* **CMake** (3.16 or newer)
* **Ninja** (Recommended build system)
* **Qt6** (`Core`, `Gui`, `Qml`, `Quick`)
* **LibRaw** (`libraw-dev`)

### Linux (Debian / Ubuntu)

1. Install the required toolchain and dependencies:
   ```bash
   sudo apt update
   sudo apt install build-essential cmake ninja-build libraw-dev qt6-base-dev qt6-declarative-dev
   ```
2. Clone the repository and configure the build environment:
   ```bash
   git clone https://github.com/bluetuna42/SharpMark.git
   cd SharpMark
   mkdir build && cd build

   cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
   ninja
   ```
3. Install the application to your system (requires root privileges):
   ```bash
   sudo ninja install
   ```
   *This will copy the executable, desktop entry, and icons to your system directories, allowing you to launch SharpMark from your application menu.*

4. **(Optional)** Generate a redistributable `.deb` package:
   ```bash
   cpack -G DEB
   ```

### Windows (MSVC + vcpkg)

For Windows environments, the project builds with the **MSVC** toolchain and uses **vcpkg** to manage native dependencies.

1. Install the prerequisites:
   - [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload
   - [CMake](https://cmake.org/download/) (3.16 or newer)
   - [Ninja](https://ninja-build.org/)
   - [vcpkg](https://github.com/microsoft/vcpkg) — follow its [quick-start guide](https://github.com/microsoft/vcpkg#quick-start-windows) and set the `VCPKG_ROOT` environment variable
   - [Qt6](https://www.qt.io/download) — install the `MSVC 2022 64-bit` component and set the `CMAKE_PREFIX_PATH` to your Qt installation (e.g. `C:\Qt\6.x.x\msvc2022_64`)

2. Install the native library dependency via vcpkg:
   ```cmd
   vcpkg install libraw:x64-windows
   ```

3. Clone the repository:
   ```bash
   git clone https://github.com/bluetuna42/SharpMark.git
   cd SharpMark
   ```

4. Configure and build:
   ```cmd
   cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2022_64 -B build
   cmake --build build --config Release
   ```

5. Run the CMake install step to assemble the portable package. CMake will automatically bundle Qt DLLs, all vcpkg dependencies, ONNX Runtime, AI models, and ExifTool into the output directory:
   ```cmd
   cmake --install build
   ```

6. The self-contained portable application will be available in the `SharpMark_Portable` directory. Run `SharpMark.exe` from there — no installation required.

---

## Contributing
Contributions, bug reports, and feature requests are welcome. Please check the [issues page](../../issues) for current tasks and submit a pull request for any proposed changes.

## License
This project is licensed under the [Apache License 2.0](LICENSE).
