FFmpeg 音视频转码工具（Qt + FFmpeg C API）| FFmpeg Audio & Video Transcoder (Qt + FFmpeg C API)

FFmpeg-AAC-MP3-Video-Transcode

基于 Qt + FFmpeg C API 开发的跨平台音视频转码工具，专注解决 FFmpeg 转码过程中的常见崩溃、音视频不同步、格式不兼容等问题，支持视频编码转换与音频格式互转。
A cross-platform audio and video transcoding tool developed based on Qt + FFmpeg C API, focusing on solving common problems such as crashes, audio-video desynchronization, and format incompatibility during FFmpeg transcoding, supporting video codec conversion and audio format mutual conversion.

📌 项目简介 | Project Introduction

本项目是一款轻量、稳定的音视频转码工具，核心解决 FFmpeg 转码中「音频帧长不匹配」「采样格式错误」「刷新编码器崩溃」等经典痛点，适配主流音视频编码格式，输出兼容性强的 MP4 封装文件，适合开发者学习参考或直接用于实际转码场景。
This project is a lightweight and stable audio-video transcoding tool, which mainly solves classic pain points in FFmpeg transcoding such as "audio frame length mismatch", "sampling format error", and "crash when flushing encoder". It is compatible with mainstream audio-video codecs, outputs MP4 encapsulated files with strong compatibility, and is suitable for developers to learn and reference or directly use in practical transcoding scenarios.

✨ 功能特性 | Features

- 视频转码 | Video Transcoding：支持 H.264（libx264）、H.265（libx265）、Xvid（MPEG4）编码自由切换 | Supports free switching of H.264 (libx264), H.265 (libx265), Xvid (MPEG4) codecs

- 音频转码 | Audio Transcoding：高质量 AAC ↔ MP3 互转，自动处理采样格式差异 | High-quality AAC ↔ MP3 mutual conversion, automatically handles sampling format differences

- 视频适配 | Video Adaptation：自动分辨率缩放、像素格式转换（默认 YUV420P） | Automatic resolution scaling and pixel format conversion (default YUV420P)

- 音频优化 | Audio Optimization：内置 AudioFIFO 缓冲，解决 AAC/MP3 帧长不一致问题（1024 ↔ 1152 采样点） | Built-in AudioFIFO buffer to solve the problem of inconsistent AAC/MP3 frame lengths (1024 ↔ 1152 sample points)

- 稳定可靠 | Stable & Reliable：修复「100% 进度闪退」「内存泄漏」「野指针」等崩溃问题 | Fixes crash issues such as "100% progress crash", "memory leak", and "wild pointer"

- 实时反馈 | Real-time Feedback：转码进度条实时显示，转码完成自动提示 | Real-time display of transcoding progress bar, automatic prompt when transcoding is completed

- 跨平台 | Cross-platform：基于 Qt 开发，支持 Windows、Linux、macOS（需自行编译对应平台） | Developed based on Qt, supporting Windows, Linux, macOS (need to compile the corresponding platform by yourself)

🛠️ 解决的 FFmpeg 经典坑点 | Solved Classic FFmpeg Pitfalls

- ✅ AAC 仅支持 FLTP 采样格式、MP3 推荐 S16P 格式，自动完成重采样 | AAC only supports FLTP sampling format, MP3 recommends S16P format, automatic resampling

- ✅ 音频帧长不匹配（AAC 1024 采样 / MP3 1152 采样），用 AudioFIFO 缓冲对齐 | Inconsistent audio frame lengths (AAC 1024 samples / MP3 1152 samples), aligned with AudioFIFO buffer

- ✅ 刷新编码器（flush）时崩溃，采用安全的 AVPacket 初始化与释放逻辑 | Crash when flushing the encoder, using safe AVPacket initialization and release logic

- ✅ PTS/DTS 时间戳错乱，导致音画不同步 | PTS/DTS timestamp disorder causing audio-video desynchronization

- ✅ 视频缩放时内存分配失败、野指针问题 | Memory allocation failure and wild pointer issues during video scaling

- ✅ 转码收尾时音频/视频最后几秒丢失 | Loss of the last few seconds of audio/video during transcoding completion

💻 技术栈 | Technology Stack

- 开发框架：Qt 5 / Qt 6（界面 + 跨平台支持） | Development Framework: Qt 5 / Qt 6 (UI + cross-platform support)

- 核心库：FFmpeg 4.x ~ 6.x（C API） | Core Library: FFmpeg 4.x ~ 6.x (C API)

- 编程语言：C++ | Programming Language: C++

- 关键模块 | Key Modules:

  - swresample：音频重采样 | Audio resampling

  - av_audio_fifo：音频帧缓冲 | Audio frame buffer

  - avcodec：音视频编解码 | Audio and video encoding/decoding

  - avformat：音视频封装/解封装 | Audio and video muxing/demuxing

📋 使用场景 | Application Scenarios

- 短视频转码、压缩（适配不同平台需求） | Short video transcoding and compression (adapting to different platform requirements)

- 音频格式互转（AAC ↔ MP3），节省存储或适配设备 | Audio format mutual conversion (AAC ↔ MP3) to save storage or adapt to devices

- 旧视频编码升级（如 Xvid 转 H.264/H.265） | Old video codec upgrade (e.g., Xvid to H.264/H.265)

- FFmpeg 转码初学者学习参考（避坑实战案例） | Learning reference for FFmpeg transcoding beginners (practical cases to avoid pitfalls)

👥 适合人群 | Target Audience

- FFmpeg C API 初学者，想快速掌握音视频转码流程 | Beginners of FFmpeg C API who want to quickly master the audio-video transcoding process

- 需要开发音视频转码功能的 Qt 开发者 | Qt developers who need to develop audio-video transcoding functions

- 被 FFmpeg 转码崩溃、音画不同步等问题困扰的开发者 | Developers troubled by FFmpeg transcoding crashes, audio-video desynchronization and other issues

🔧 编译说明 | Compilation Instructions

1. 环境准备：安装 Qt 5/6、FFmpeg 4.x~6.x（需开启 libmp3lame、aac 编码器） | Environment Preparation: Install Qt 5/6, FFmpeg 4.x~6.x (need to enable libmp3lame and aac encoders)

2. 配置 FFmpeg 路径：在 Qt 项目文件（.pro）中指定 FFmpeg 的 include 和 lib 路径 | Configure FFmpeg Path: Specify the include and lib paths of FFmpeg in the Qt project file (.pro)

3. 编译项目：Qt Creator 打开项目，选择对应平台（Windows/Linux/macOS），直接编译运行 | Compile Project: Open the project with Qt Creator, select the corresponding platform (Windows/Linux/macOS), and compile and run directly

⚠️ 注意事项 | Notes

- FFmpeg 需开启 libmp3lame 编码器，否则无法支持 MP3 转码 | FFmpeg needs to enable the libmp3lame encoder, otherwise MP3 transcoding is not supported

- 转码时确保输入文件格式合法（支持主流 MP4、AAC、MP3 等格式） | Ensure the input file format is legal during transcoding (supports mainstream formats such as MP4, AAC, MP3)

- 不同平台编译时，需适配对应平台的 FFmpeg 静态库/动态库 | When compiling on different platforms, you need to adapt to the FFmpeg static/dynamic library of the corresponding platform

📄 开源协议 | Open Source License

本项目采用 MIT 开源协议，允许个人和商业使用，可自由修改、分发，无需注明原作者（建议注明）。
This project adopts the MIT Open Source License, which allows personal and commercial use, free modification and distribution, and does not require注明 the original author (it is recommended to注明).

MIT License Full Text (Consistent with the official version):
Copyright <YEAR> <COPYRIGHT HOLDER>
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

💡 备注 | Remarks

项目核心修复了 FFmpeg 转码中最易踩坑的「音频帧缓冲」和「编码器刷新崩溃」问题，代码注释详细，适合初学者学习 FFmpeg 音视频转码的最佳实践。
The core of the project fixes the most error-prone issues in FFmpeg transcoding: "audio frame buffering" and "encoder flushing crash". The code has detailed comments, making it suitable for beginners to learn the best practices of FFmpeg audio-video transcoding.
