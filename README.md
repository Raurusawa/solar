# Solar System Simulator

基于 OpenGL 3.3 的实时 3D 太阳系模拟器。包含完整的延迟渲染管线、HDR 自动曝光、Bloom、SSAO、大气散射、开普勒轨道力学等高级特性。

---

## 功能特性

- **太阳系全体天体**：太阳 + 8 大行星 + 14 颗主要卫星（含冥王星级别精度）
- **真实轨道力学**：J2000.0 历元开普勒方程（牛顿迭代），完整轨道根数（半长轴、离心率、倾角、升交点经度、近日点角距）
- **双精度渲染**：CPU 端 `double` 轨道计算，消除行星抖动；GPU 端 `float` 保持性能
- **延迟渲染管线**：G-Buffer (MRT) → SSAO → Composite + 日冕 → 大气散射 → Luminance → Bloom → Final
- **HDR 自动曝光**：对数亮度直方图，0.17 key value，支持手动 EV 补偿 (±2 EV)
- **大气散射**：Rayleigh + Mie 散射（地球专属）
- **角半径软阴影**：Space Engine 方案，平滑行星明暗交界线
- **动态近裁剪面**：自动适应相机与行星距离，防止 Z-fighting
- **自由相机**：四元数旋转，无万向锁；WASD + 鼠标 + 滚轮调速
- **菜单系统**：标题 / 暂停 / 设置页面，内嵌位图字体，无外部 UI 依赖
- **配置文件**：`config.ini` 控制全部参数，无需重新编译

---

## 截图

> *(可在此处插入截图)*

---

## 依赖项

| 库 | 用途 | 版本要求 |
|---|---|---|
| OpenGL | 渲染 API | 3.3+ |
| [GLFW](https://www.glfw.org/) | 窗口 & 输入 | 3.x |
| [GLEW](https://glew.sourceforge.net/) | OpenGL 扩展加载 | 2.x |
| [GLM](https://github.com/g-truc/glm) | 数学库 | 0.9.9+ |
| [FreeType](https://freetype.org/) | 字体渲染 | 2.x |
| [stb_image](https://github.com/nothings/stb) | 纹理加载 | 已内嵌 `src/stb_image.h` |

---

## 编译

### Windows（MSYS2 MinGW，推荐）

1. 安装 [MSYS2](https://www.msys2.org/)，然后在 MSYS2 MinGW64 终端中安装依赖：
   ```bash
   pacman -S mingw-w64-x86_64-gcc \
             mingw-w64-x86_64-glfw \
             mingw-w64-x86_64-glew \
             mingw-w64-x86_64-glm \
             mingw-w64-x86_64-freetype
   ```

2. 安装 `mingw32-make`（通过 WinGet 或直接使用 MSYS2 中的 `make`）：
   ```powershell
   winget install GnuWin32.Make
   ```
   或在 MSYS2 MinGW64 终端中直接使用 `make`。

3. 克隆仓库并编译：
   ```powershell
   git clone <仓库地址>
   cd solar
   $env:PATH = "C:\msys64\mingw64\bin;" + $env:PATH
   mingw32-make
   ```

4. 运行：
   ```powershell
   .\solar_system.exe
   ```
   > 运行时需要 `src/shaders/`、`textures/`、`config.ini` 与可执行文件在同一工作目录，或使用 `Release/` 目录（已打包齐全）。

---

### Linux（GCC + 包管理器）

1. 安装依赖（以 Ubuntu/Debian 为例）：
   ```bash
   sudo apt update
   sudo apt install build-essential libglfw3-dev libglew-dev libglm-dev libfreetype-dev
   ```
   Arch Linux：
   ```bash
   sudo pacman -S gcc glfw glew glm freetype2
   ```

2. 修改 `Makefile`，将 Windows 特定字段替换：
   ```makefile
   CXX     = g++
   CXXFLAGS = -std=c++11 -Wall $(shell pkg-config --cflags freetype2)
   LDFLAGS  = -lglfw -lGLEW -lGL $(shell pkg-config --libs freetype2)
   ```
   同时删除 `MINGW` 变量及 `-mwindows` 链接选项。

3. 编译并运行：
   ```bash
   make
   ./solar_system
   ```

---

### macOS（Homebrew）

1. 安装依赖：
   ```bash
   brew install glfw glew glm freetype
   ```

2. 修改 `Makefile`：
   ```makefile
   CXX      = g++
   CXXFLAGS = -std=c++11 -Wall $(shell pkg-config --cflags freetype2)
   LDFLAGS  = -lglfw -lGLEW -framework OpenGL $(shell pkg-config --libs freetype2)
   ```
   > macOS 上 OpenGL 使用 `-framework OpenGL` 而非 `-lGL`。macOS 12+ 已废弃 OpenGL，建议在系统偏好中忽略警告或迁移至 Metal。

3. 编译并运行：
   ```bash
   make
   ./solar_system
   ```

---

## 目录结构

```
solar/
├── src/
│   ├── main.cpp            # 主循环 & 渲染管线
│   ├── planet.h/cpp        # 行星 & 卫星，开普勒轨道力学
│   ├── camera.h/cpp        # 四元数自由相机
│   ├── menu.h/cpp          # 菜单系统（标题 / 暂停 / 设置）
│   ├── config.h/cpp        # config.ini 解析
│   ├── shader.h/cpp        # GLSL 着色器加载
│   ├── texture.h/cpp       # 纹理加载（stb_image）
│   ├── sphere.h/cpp        # 球体网格生成
│   ├── text_renderer.h/cpp # FreeType 文字渲染
│   ├── stb_image.h         # 第三方头文件（内嵌）
│   └── shaders/
│       ├── planet.vert/frag      # 行星（PBR + 软阴影）
│       ├── sun.frag              # 太阳（emissive + limb darkening）
│       ├── atmosphere.vert/frag  # 大气散射（Rayleigh + Mie）
│       ├── composite.frag        # SSAO 合成 + 日冕 ray-march
│       ├── ssao.frag             # 屏幕空间环境光遮蔽
│       ├── luminance.frag        # 亮度提取（自动曝光）
│       ├── final.frag            # Tone mapping + Bloom 合成
│       ├── lensflare.vert/frag   # 炫光
│       └── planet_glow.vert/frag # 行星辉光
├── textures/               # 行星 & 卫星纹理（JPG equirectangular）
├── config.ini              # 全局配置（窗口、相机、模拟参数、行星数据）
├── Makefile
└── Release/                # 预编译可执行文件 + 运行时所需文件
```

---

## 操作说明

### 相机控制

| 操作 | 功能 |
|---|---|
| `W` / `S` | 前进 / 后退 |
| `A` / `D` | 左平移 / 右平移 |
| `R` / `F` | 上升 / 下降 |
| `Q` / `E` | 逆时针 / 顺时针滚转 |
| 鼠标移动 | 旋转视角 |
| 滚轮 | 调整移动速度（0.5 ~ 200） |

### 功能键

| 按键 | 功能 |
|---|---|
| `ESC` | 打开 / 关闭菜单 |
| `B` | 切换 Bloom |
| `L` | 切换炫光（Lens Flare） |
| `N` | 切换直接输出模式 |
| `KP +` / `KP -` | 手动曝光补偿 ±0.5 EV |

### 菜单

- **标题页**：开始游戏 / 设置 / 退出
- **暂停页**（游戏中按 `ESC`）：继续 / 设置 / 返回主菜单
- **设置页**：Bloom / 炫光 / 自动曝光 / 线框模式 开关

---

## 配置说明

编辑根目录下的 `config.ini` 可自定义所有参数，无需重新编译。

> **⚠️ 注意：纹理路径是相对于工作目录（working directory）的，不是相对于 `config.ini` 文件位置的。**
> 启动程序时，请确保工作目录为 `solar/` 根目录，或者将 `config.ini` 中的纹理路径改为绝对路径。
> 例如从 `textures/sun.jpg` 改为 `C:/Users/yourname/Desktop/solar/textures/sun.jpg`。

```ini
[window]
window.width       = 1920
window.height      = 1080
window.fullscreen  = 0

[simulation]
simulation.timeScale = 1.0   ; 1.0 = 真实速度

[settings]
bloom        = 1
autoExposure = 1
```

行星轨道参数使用 J2000.0 均值轨道根数（来源：VSOP87 / DE405），修改后下次启动立即生效。

---

## 渲染管线概览

```
G-Buffer (MRT)
  color RGBA16F + normal RGBA16F + depth
        │
        ▼
      SSAO
        │
        ▼
   Composite
  (SSAO × scene + corona ray-march)
        │
        ▼
  Atmosphere (additive, Earth only)
        │
        ▼
  Luminance (cascaded downsample, auto-exposure)
        │
        ▼
 Bright Extract → Gaussian Blur → Bloom
        │
        ▼
    Final Pass
  (Tone mapping + Bloom composite)
        │
        ▼
  Lens Flare (optional, additive)
```

---

## 打包发布

`Release/` 目录已包含运行所需的所有非系统 DLL（共 17 个）。分发时只需将以下文件放在一起：

```
Release/
├── solar_system.exe
├── config.ini
├── src/shaders/      ← 整个文件夹
├── textures/         ← 整个文件夹
└── *.dll             ← 17 个 MinGW 运行时 DLL
```

**依赖 DLL 清单**（均已包含在 `Release/` 中）：

| DLL | 用途 |
|---|---|
| `glew32.dll` | OpenGL 扩展加载 |
| `glfw3.dll` | 窗口 & 输入 |
| `libfreetype-6.dll` | 字体渲染 |
| `libharfbuzz-0.dll` | 文字塑形 |
| `libglib-2.0-0.dll` | GLib 工具库 |
| `libgraphite2.dll` | 文字渲染引擎 |
| `libpng16-16.dll` | PNG 纹理加载 |
| `zlib1.dll` | 压缩（libpng 依赖） |
| `libbz2-1.dll` | BZIP2 压缩 |
| `libbrotlidec.dll` / `libbrotlienc.dll` / `libbrotlicommon.dll` | Brotli 压缩（FreeType 依赖） |
| `libiconv-2.dll` | 字符编码转换 |
| `libintl-8.dll` | 国际化支持 |
| `libpcre2-8-0.dll` | 正则表达式（GLib 依赖） |
| `libgcc_s_seh-1.dll` / `libstdc++-6.dll` / `libwinpthread-1.dll` | GCC/MinGW 运行时 |

系统 DLL（`KERNEL32.dll`、`USER32.dll`、`OPENGL32.dll` 等）由 Windows 自动提供，无需分发。

---

## License

MIT
