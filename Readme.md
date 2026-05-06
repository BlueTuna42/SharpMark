<div align="center">
  <img src="assets/icons/512x512/icon.png" alt="SharpMark Logo" width="128" height="128">
  <h1>SharpMark</h1>
  <p><b>Fast, Multi-threaded Image Sharpness Analyzer and Culling Tool</b></p>
</div>

## About

**SharpMark** is a cross-platform graphical desktop application designed to automatically detect and filter out blurry images from large photographic batches. 

Built with C++ and GTK3, the tool delivers a consistent native experience across Windows and Linux. It provides a clean, responsive interface while executing high-performance image evaluation underneath, calculating sharpness via Laplacian variance. To integrate seamlessly into professional photography workflows, SharpMark handles RAW files natively via LibRaw, features a robust built-in image viewer for manual inspection, and can optionally write evaluation results directly to EXIF metadata using libexif.

## Features

* **Cross-Platform Compatibility:** Fully supported on Windows and Linux, providing a consistent and native desktop experience across different operating systems.
* **Integrated Image Viewer:** Features a dedicated, built-in full-resolution image viewer, allowing users to manually inspect photos and verify focus accuracy without relying on external software.
* **Native Graphical Interface:** A responsive GTK3-based GUI offering dynamic List and Grid view modes, `Ctrl + Scroll` thumbnail scaling, and system-aware dark/light theme integration.
* **High Performance:** Multi-threaded architecture designed to quickly parse and analyze large batches of high-resolution images.
* **Accurate Blur Detection:** Evaluates focal sharpness mathematically using Laplacian variance.
* **Advanced RAW Support:** Powered by LibRaw, featuring configurable RAW loading modes (Full-size, Half-size, or Preview) to balance computational accuracy and processing speed.
* **Optional Metadata Tagging:** Powered by libexif, the application can be configured to automatically write standard star ratings to image metadata (1-star for blurry, 5-star for sharp) for immediate integration with Lightroom, Capture One, and darktable.
* **Efficient File Management:** Includes bulk deletion capabilities to immediately move out-of-focus shots to the system trash.

---

## Installation (Pre-compiled Releases)

Ready-to-use binaries are available on the [Releases](../../releases) page.

### Windows (Portable)
1. Navigate to the **Releases** tab and download `SharpMark_Portable.zip`.
2. Extract the archive to your preferred directory.
3. Execute `SharpMark.exe`. No installation is required.

### Linux (Debian / Ubuntu)
1. Download the `.deb` package (e.g., `sharpmark_1.0.0_amd64.deb`) from the **Releases** tab.
2. Open your terminal and install the package via `apt` to automatically resolve GTK3 dependencies:
   ```bash
   sudo apt install ./sharpmark_1.0.0_amd64.deb
   ```
3. Launch **SharpMark** from your desktop environment's application menu.

---

## Building from Source

To compile the application from source, follow the instructions below.

### Dependencies
The following tools and libraries are required to build SharpMark:
* **C++17** compatible compiler (GCC, Clang, or MSVC)
* **CMake** (3.15 or newer)
* **Ninja** (Recommended build system)
* **GTK3** (`libgtk-3-dev`)
* **LibRaw** (`libraw-dev`)
* **libexif** (`libexif-dev`)

### Linux (Debian / Ubuntu)

1. Install the required toolchain and dependencies:
   ```bash
   sudo apt update
   sudo apt install build-essential cmake ninja-build libgtk-3-dev libraw-dev libexif-dev
   ```
2. Clone the repository and configure the build environment:
   ```bash
   git clone https://github.com/yourusername/SharpMark.git
   cd SharpMark
   mkdir build && cd build
   
   cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
   ninja
   ```
3. **(Optional)** Generate a redistributable `.deb` package:
   ```bash
   cpack -G DEB
   ```

### Windows (MSYS2 / MinGW)

For Windows environments, the project is configured to build using the **MSYS2 UCRT64** toolchain.

1. Download and install [MSYS2](https://www.msys2.org/).
2. Launch the **MSYS2 UCRT64** terminal and install the necessary packages:
   ```bash
   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-gtk3 mingw-w64-ucrt-x86_64-libraw mingw-w64-ucrt-x86_64-libexif
   ```
3. Clone the repository:
   ```bash
   git clone https://github.com/yourusername/SharpMark.git
   cd SharpMark
   ```
4. Execute the automated portable build script. This will compile the application and bundle it with all required GTK DLLs, image parsing libraries, themes, and icons into a standalone directory:
   ```bash
   ./build_portable.sh
   ```
5. The compiled application will be available in the `SharpMark_Portable` directory.

---

## Contributing
Contributions, bug reports, and feature requests are welcome. Please check the [issues page](../../issues) for current tasks and submit a pull request for any proposed changes.

## License
This project is licensed under the [Apache License 2.0](LICENSE).