# Newbie-Renderer

## Prerequisites
- Visual Studio 2022
- Vulkan SDK 1.4.321.1
- CMake
- Vcpkg (set `VCPKG_ROOT` environment variable)
- Other dependencies will be automatically downloaded by Vcpkg and submodules.

## Build

### Method 1: Using CMake Presets (Recommended)

1. Clone the repository
    ```bash
    git clone https://github.com/C-none/Newbie-Renderer.git --recurse-submodules
    cd Newbie-Renderer
    ```

2. Configure using preset
    ```bash
    cmake --preset msvc-vcpkg
    ```

3. Build the project
    ```bash    
    # Release build
    cmake --build --preset release
    ```

### Method 2: Using Visual Studio 2022

1. Open Visual Studio 2022
2. Select "Open a local folder"
3. Choose the project root directory
4. Visual Studio will automatically detect the CMake presets
5. Select configuration from the dropdown: `msvc-vcpkg`
6. Select build preset: `release`
7. Build the solution (Ctrl+Shift+B)

## Packages

### Submodules
| Name      | Version  |
| --------- | -------- |
| Slang     | v2025.16 |
| NvAPI     | R580     |
| Aftermath | R580     |

tips: You may update submodules by git if u like. However, Aftermath has to be updated manually by downloading the latest version from NVIDIA Developer [website](https://developer.nvidia.com/nsight-aftermath).

### Vcpkg Packages
- glm