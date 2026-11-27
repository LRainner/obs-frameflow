# 编译 FrameFlow

## 依赖

| 依赖 | 版本 | 说明 |
|---|---|---|
| OBS Studio | 30.0+ | 头文件 + libobs 导入库 |
| CMake | 3.28+ | |
| Visual Studio | 2022 | MSVC x64 工具链 |
| NVIDIA Video Effects SDK | 最新 | P2 起：Maxine VSR / Artifact Reduction |
| NVIDIA Optical Flow SDK | 5.x | P4 起：NVOF |
| FFmpeg | 7.x | P5 起：仅用于封装 |

P1 只需要 OBS SDK，NVIDIA SDK 与 FFmpeg 等实现对应阶段时再引入。
CI 会从源码构建 OBS SDK，本地构建可复用同一份安装目录。

## 配置与编译

```bat
cmake -S . -B build_x64 -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_PREFIX_PATH="%OBS_INSTALL_DIR%"
cmake --build build_x64 --config RelWithDebInfo
cmake --install build_x64 --config RelWithDebInfo --prefix build_x64/install
```

产物：

- `build_x64/RelWithDebInfo/frameflow.dll`
- `build_x64/install/obs-plugins/64bit/frameflow.dll`
- `build_x64/install/obs-plugins/frameflow/`（`data/` 内容）

## 安装到 OBS（开发用）

OBS 33 起使用单层目录：`<plugin>.dll` 与 `data\` 并排。

```bat
mkdir "C:\ProgramData\obs-studio\plugins\frameflow"
copy build_x64\install\obs-plugins\64bit\frameflow.dll ^
     "C:\ProgramData\obs-studio\plugins\frameflow\"
xcopy /E /I build_x64\install\obs-plugins\frameflow ^
      "C:\ProgramData\obs-studio\plugins\frameflow\data"
```
