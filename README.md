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

### Method 2: Using Visual Studio 2026

1. Open Visual Studio 2026
2. Select "Open a local folder"
3. Choose the project root directory
4. Visual Studio will automatically detect the CMake presets
5. Select configuration from the dropdown: `msvc-vcpkg`
6. Select build preset: `release`
7. Build the solution (Ctrl+Shift+B)

## Test Framework

The project keeps a built-in minimal test framework module for C++ modules workflow:

- Framework module: `test/framework/nr_test.ixx` (`import nr.test;`)
- Test runner object source: `test/framework/nr_test_main.cpp`
- Core API (no macros):
    - `nr::test::CaseRegistrar{name, [] { ... }}`
    - `nr::test::require(condition, message)`
    - `nr::test::requireEqual(lhs, rhs, message)`

## Shader Compile Profiling

Use `nr_slang_profile` to run and trace `nrrhi`/`nrslang` compilation on `shader/test/main/main.slang`:

```bash
cmake --build --preset debug --target nr_slang_profile
./build/src/Debug/nr_slang_profile.exe
```

## Packages

### Submodules
| Name      | Version  |
| --------- | -------- |
| Slang     | v2026.3.1 |
| NvAPI     | R590     |
| Aftermath | R590     |

tips: You may update submodules by git if u like. However, Aftermath has to be updated manually by downloading the latest version from NVIDIA Developer [website](https://developer.nvidia.com/nsight-aftermath).

### Vcpkg Packages
- glm
- imgui
- glfw3
- vulkan-memory-allocator