# 第三方组件声明

OpenBoard 希望学定制版基于 OpenBoard 开发，整体以 GNU GPL v3 发布。

发行包使用了 Qt、FFmpeg、OpenSSL、Poppler、QuaZip、Boost 等第三方组件。各组件仍适用其各自的版权和许可证条款，相关许可文本保留在源码树、构建依赖或安装目录中。

## FFmpeg

定制版的 Windows MP4 录制功能使用 FFmpeg 9.0.1 shared build，输出 H.264/AAC MP4。构建时使用的开发包位于 `thirdparty/ffmpeg/ffmpeg-9.0.1-full_build-shared`，由于包含大型预编译二进制文件，不直接提交到 Git 历史。

重新构建时需准备兼容的 FFmpeg 9.0.1 shared development build，并保持上述目录结构。FFmpeg 及其启用组件的完整许可信息以所使用构建包内的 `LICENSE` 文件为准。

## OpenSSL 特别连接例外

OpenBoard 源码文件声明包含针对 OpenSSL 项目库的特别连接例外，本定制版保留该声明。

如果发现发行包中第三方声明有遗漏，请通过 GitHub Issues 或联系 W000210 车禹泽反馈。
