#!/bin/bash
set -e

echo "Starting build process..."
mkdir -p build && cd build

# Build the project
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja

echo "Building portable version..."
cd ..
PORTABLE_DIR="SharpMark_Portable"
mkdir -p "$PORTABLE_DIR"

# 1. Copy the executable
cp build/SharpMark.exe "$PORTABLE_DIR/"

# 2. Prepare directories for GTK3 resources
echo "Copying GTK3 resources..."
MSYS_UCRT_ROOT="/c/msys64/ucrt64"

mkdir -p "$PORTABLE_DIR/share/glib-2.0/schemas"
cp -r "$MSYS_UCRT_ROOT/share/glib-2.0/schemas/"* "$PORTABLE_DIR/share/glib-2.0/schemas/"

mkdir -p "$PORTABLE_DIR/share/icons"
cp -r "$MSYS_UCRT_ROOT/share/icons/Adwaita" "$PORTABLE_DIR/share/icons/"
cp -r "$MSYS_UCRT_ROOT/share/icons/hicolor" "$PORTABLE_DIR/share/icons/"

echo "Copying application icons..."
for size in 512x512 256x256 32x32 16x16; do
    APP_ICON_DIR="$PORTABLE_DIR/share/icons/hicolor/$size/apps"
    mkdir -p "$APP_ICON_DIR"
    if [ -f "assets/icons/$size/icon.png" ]; then
        cp "assets/icons/$size/icon.png" "$APP_ICON_DIR/"
        echo " - Copied $size icon"
    else
        echo " - Warning: assets/icons/$size/icon.png not found, skipping."
    fi
done

mkdir -p "$PORTABLE_DIR/lib/gdk-pixbuf-2.0/2.10.0/loaders"
cp "$MSYS_UCRT_ROOT/lib/gdk-pixbuf-2.0/2.10.0/loaders/"*.dll "$PORTABLE_DIR/lib/gdk-pixbuf-2.0/2.10.0/loaders/"

# 3. Copy DLL dependencies (for both the executable and plugins)
echo "Copying dependencies to lib directory..."

# Create the lib directory for DLLs
mkdir -p "$PORTABLE_DIR/lib"

# Create a temporary file with a list of everything that requires DLLs
TMP_LIST="dll_targets.txt"
echo "$PORTABLE_DIR/SharpMark.exe" > "$TMP_LIST"
find "$PORTABLE_DIR/lib/gdk-pixbuf-2.0/2.10.0/loaders" -name "*.dll" >> "$TMP_LIST"

# Gather all required DLLs and copy them directly into the lib folder
cat "$TMP_LIST" | xargs ldd | grep -i "ucrt64" | awk '{print $3}' | sort | uniq | xargs -I '{}' cp '{}' "$PORTABLE_DIR/lib/"
rm "$TMP_LIST"

# 4. Recreate loaders.cache with relative paths
echo "Generating local loaders.cache..."
cd "$PORTABLE_DIR"

# Temporarily copy the cache generator to the lib folder (so it sits next to its DLLs)
cp "$MSYS_UCRT_ROOT/bin/gdk-pixbuf-query-loaders.exe" ./lib/

# Generate cache. We prepend the lib folder to PATH so the generator can find glib/gdk DLLs
PATH="$PWD/lib:$PATH" ./lib/gdk-pixbuf-query-loaders.exe lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll > lib/gdk-pixbuf-2.0/2.10.0/loaders.cache

# Remove the generator, it is no longer needed
rm ./lib/gdk-pixbuf-query-loaders.exe

# Change absolute paths to relative ones in the generated cache (relative to the program root)
sed -i 's|lib/gdk-pixbuf|../lib/gdk-pixbuf|g' lib/gdk-pixbuf-2.0/2.10.0/loaders.cache

# Return to the project root directory
cd ..

# 5. Setting up native launcher with an icon
echo "Setting up native launcher with icon..."

# Hide the original executable inside the lib directory
mv "$PORTABLE_DIR/SharpMark.exe" "$PORTABLE_DIR/lib/SharpMark-core.exe"

# Define paths to existing resources
RC_PATH="assets/icons/app_icon.rc"
RES_COMPILED="launcher.res"

# Compile the existing resource file if it exists
if [ -f "$RC_PATH" ]; then
    echo " - Found existing resource file ($RC_PATH), compiling..."
    
    # We compile the .rc file from the directory it lives in so it can find the .ico easily
    # (assuming SharpMark.ico is right next to app_icon.rc as per the uploaded file)
    windres -i "$RC_PATH" -O coff -o "$RES_COMPILED"
    HAS_ICON=true
else
    echo " - Warning: $RC_PATH not found! Launcher will have a default system icon."
    HAS_ICON=false
fi

# Create the C launcher source code
cat << 'EOF' > launcher.c
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    // Launch the hidden core executable from the lib folder
    // The working directory remains the root of the portable folder
    if (!CreateProcessW(L"lib\\SharpMark-core.exe", GetCommandLineW(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        MessageBoxW(NULL, L"Failed to start lib\\SharpMark-core.exe", L"Launch Error", MB_ICONERROR);
        return 1;
    }
    
    // Close handles immediately to avoid resource leaks
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}
EOF

# Compile the launcher with or without the resource file
if [ "$HAS_ICON" = true ]; then
    echo " - Compiling launcher with embedded icon..."
    gcc launcher.c "$RES_COMPILED" -o "$PORTABLE_DIR/SharpMark.exe" -mwindows
    rm "$RES_COMPILED"
else
    echo " - Compiling launcher without icon..."
    gcc launcher.c -o "$PORTABLE_DIR/SharpMark.exe" -mwindows
fi

# Clean up launcher source
rm launcher.c

echo "Done! Portable app is ready in '$PORTABLE_DIR'"