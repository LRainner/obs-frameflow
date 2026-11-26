# 第三方组件声明

> 占位文档 —— P6 公开发布前补全逐文件审计。

| 组件 | 许可证 | 可否随包分发 | 说明 |
|---|---|---|---|
| OBS Studio（libobs） | GPL-2.0-or-later | 可以 | 插件链接 libobs，因此插件同样为 GPL-2.0 |
| NVIDIA Video Effects SDK（Maxine） | NVIDIA SDK 许可 | 可以，需附带许可文件 | 随包分发运行库与模型 |
| NVIDIA Optical Flow SDK（NVOF） | NVIDIA SDK 许可 | 驱动自带 | 不随包分发任何文件 |
| NVIDIA Video Codec SDK（NVENC） | NVIDIA SDK 许可 | 驱动自带 | 不随包分发任何文件 |
| FFmpeg | LGPL-2.1+ / GPL-2.0 | 可以 | 仅用于封装，注意编译选项 |
| Qt 6 | LGPL-3.0 | 动态链接 | 由 OBS 提供 |

## 不随包分发的组件

`nvngx_dlssnr.dll`、`nvngx_dlss.dll` 以及其他任何 NVIDIA NGX 运行库文件，
本项目**永远不会**打包或再分发。
