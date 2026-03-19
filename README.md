# Newbie-Renderer

## Introduction

Newbie-Renderer is a Vulkan-based renderer with C++23 support.

## Prerequisites
- Visual Studio 2026
- Vulkan SDK 1.4.321.1
- CMake
- Vcpkg (set `VCPKG_ROOT` environment variable)
- Other dependencies will be automatically downloaded by Vcpkg and submodules.

## Build

### Command Line (CMake Presets)

1. Clone the repository
    ```bash
    git clone https://github.com/C-none/Newbie-Renderer.git --recurse-submodules
    cd Newbie-Renderer
    ```

2. Configure
    ```bash
    cmake --preset msvc
    ```

3. Build
    ```bash
    # Release
    cmake --build --preset release --target main

    # Debug
    cmake --build --preset debug --target main
    ```

4. Run
    ```bash
    # Release
    cmake --build --preset run-release

    # Debug
    cmake --build --preset run-debug
    ```


## Packages

### Submodules
| Name      | Version  |
| --------- | -------- |
| Slang     | v2026.4.1 |
| Aftermath | R590     |

tips: You may update submodules by git if u like. However, Aftermath has to be updated manually by downloading the latest version from NVIDIA Developer [website](https://developer.nvidia.com/nsight-aftermath).

### Vcpkg Packages
- glm
- imgui
- glfw3
- vulkan-memory-allocator
- assimp