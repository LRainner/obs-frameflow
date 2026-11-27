# 编译 FrameFlow

> 占位文档 —— P1 阶段补全。

## 依赖

| 依赖 | 版本 | 说明 |
|---|---|---|
| OBS Studio | 30.0+ | 头文件 + libobs 导入库 |
| CMake | 3.28+ | |
| Visual Studio | 2022 | MSVC x64 工具链 |
| NVIDIA Video Effects SDK | 最新 | Maxine VSR / Artifact Reduction |
| NVIDIA Optical Flow SDK | 5.x | NVOF |
| FFmpeg | 7.x | 仅用于封装 |

## 配置与编译

```bat
cmake -S . -B build_x64 -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_PREFIX_PATH="%OBS_INSTALL_DIR%"
cmake --build build_x64 --config RelWithDebInfo
```

产物：`build_x64/RelWithDebInfo/frameflow.dll`

## 安装到 OBS（开发用）

```bat
xcopy /E /I build_x64\RelWithDebInfo\frameflow.dll ^
      "C:\ProgramData\obs-studio\plugins\frameflow\bin\64bit\"
```
