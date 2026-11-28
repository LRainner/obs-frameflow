<div align="center">

# FrameFlow

**OBS Studio 实时视频增强插件**
超分辨率 · 降噪 · 补帧 —— 对任意 OBS 源实时生效

`1080p30 → 1440p60` · `延迟约 80ms` · `纯 GPU 处理` · `Windows 11`

[![许可证: GPL-2.0](https://img.shields.io/badge/%E8%AE%B8%E5%8F%AF%E8%AF%81-GPL--2.0-blue.svg)](LICENSE)
[![OBS](https://img.shields.io/badge/OBS-30%2B-302E31)](https://obsproject.com)
[![状态](https://img.shields.io/badge/%E7%8A%B6%E6%80%81-pre--alpha-orange)](docs/ROADMAP.md)

[功能](#功能) · [安装](#安装) · [使用](#使用) · [环境要求](#环境要求) · [编译](#从源码编译) · [文档](docs/README.md)

</div>

<!-- TODO(P1): 补 docs/images/screenshot.png -->
<!-- ![FrameFlow 在 OBS 中](docs/images/screenshot.png) -->

## 功能

| | |
|---|---|
| **超分辨率** | 1080p → 1440p / 4K，基于 NVIDIA Maxine VSR |
| **降噪** | 超分前去除视频压缩伪影 |
| **补帧** | 30 → 60 fps，基于光流插值 |
| **A/B 对比** | 分屏对比 + 按住热键看原图 |
| **任意源** | 采集卡、媒体、浏览器、场景、分组 |

## 安装

1. 从 [Releases](../../releases) 下载 `frameflow-<版本>-win64.zip`
2. 把压缩包里的 `frameflow` 文件夹解压到 `C:\ProgramData\obs-studio\plugins\`
   （目录不存在就新建）
3. 重启 OBS

## 使用

1. **来源** → **+** → **FrameFlow**
2. **输入源**：选择要增强的那个源
3. **输出分辨率**：设为 `2560x1440`
4. 完成 —— FrameFlow 源直接渲染增强后的画面

> FrameFlow 是**源**，不是滤镜。它自己渲染你选中的那个源，
> 所以场景里只放 FrameFlow。被选中的源保留在「来源」列表即可，可以隐藏；
> 如果列表里没有，先创建一个（例如「视频采集设备」）。
> 用「显示器采集」测试时，不要把它投影或全屏显示到被采集的那块屏幕上，
> 否则会形成采集回环（画面嵌套、光标残影）。

### 启用 Maxine VSR（可选）

1. 下载 `frameflow-maxine-*.zip` 组件包
2. 解压到 `C:\ProgramData\obs-studio\plugins\frameflow\data\maxine\`
3. 属性里把「缩放方式」设为 `自动` 或 `Maxine VSR`

组件包缺失时自动使用内置 Lanczos，不影响使用。

## 环境要求

| | |
|---|---|
| 系统 | Windows 11 x64 |
| OBS | 30.0+ |
| 显卡 | NVIDIA RTX 20 系及以上（建议 12GB 显存） |
| 驱动 | 570+ |

## 从源码编译

```bat
cmake -S . -B build_x64 -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_PREFIX_PATH="%OBS_INSTALL_DIR%"
cmake --build build_x64 --config RelWithDebInfo
```

依赖说明与打包见 [编译文档](docs/BUILDING.md)。

## 状态

Pre-alpha —— 管线正在分阶段搭建。进度见 [ROADMAP.md](docs/ROADMAP.md)。

## 许可证

GPL-2.0，与 OBS Studio 一致。
第三方组件清单见 [THIRD_PARTY_NOTICES.md](docs/THIRD_PARTY_NOTICES.md)。
