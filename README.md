# Newbie-Renderer

## Prerequisites
- Visual Studio 2022
- Vulkan SDK
- CMake
- Ninja
- Xmake
- Other dependencies will be automatically downloaded by xmake

## Build
1. Clone the repository
    ```bash
    git clone https://github.com/C-none/Newbie-Renderer.git
    cd Newbie-Renderer
    ```
2. Create .SLN file using Xmake
    ```bash
    xmake project -k vsxmake
    ```
    It will automatically download and build dependencies.

3. Open the generated `vsxmake2022/Newbie-Renderer.sln` file in Visual Studio 2022 and build the solution.

4. Set the startup project to whatever you want to run e.g., `main2`, etc.(Right-click the project -> Set as Startup Project)


Tips: It supposed to support other toolchains like `gcc`, `clang`, etc. However, you may need to manually fix some issues while installing required dependencies.(Add some flags maybe)

