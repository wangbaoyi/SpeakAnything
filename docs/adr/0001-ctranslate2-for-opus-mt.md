# 用 CTranslate2 运行 Opus-MT 翻译模型

识别部分跑在 ggml 上，但 ggml 没有现成的 Marian（Opus-MT 架构）实现。我们选择引入 CTranslate2 作为第二个推理运行时，用 int8 量化的 `opus-mt-zh-en` 和 `opus-mt-en-zh`（每个约 75 到 80 MB），两者都随安装包分发。打开翻译模式时在后台加载当前方向的模型，同时只保留一个方向，关闭翻译模式时卸载；它的内存不计入识别部分的 300 MiB 保护线。

CTranslate2 与 SentencePiece 都作为 submodule 从源码编译，矩阵后端用 Ruy（无外部依赖，int8 友好），不用 oneDNN 或 MKL。模型在开发机上用 `tools/` 中的脚本转换，最终用户机器不需要 Python。CTranslate2 使用 MSVC OpenMP 并随包分发 `vcomp140.dll`：不带 OpenMP 的构建在 Windows 上会在进程退出时卡死。

备选方案：ONNX Runtime 需要自己写解码循环；Python 子进程会让安装包增加数百 MB，违背 "轻量原生" 定位；在 ggml 上自己实现 Marian 工作量最大。代价是项目从此要维护两套推理依赖。
