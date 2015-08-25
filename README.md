### 基于 fuse 的用户空间文件系统

#### 编译与运行环境
Ubuntu 14.04 32bits, fuse-2.9.4 (Ubuntu 14.04 预装了 fuse).

#### 编译
	cd src; make

#### 创建一个普通文件 `disk`, 并把它格式化
	dd if=/dev/zero of=disk bs=1M count=30
	src/format disk

#### 将文件系统挂到 `mnt` 目录
	src/ufs mnt disk

现在, 文件系统就开始运行了, 你可以像访问普通文件系统那样访问它.

#### 注意事项
本文件系统还未完全支持所有的与文件系统有关的系统调用 (例如符号链接), 目前
为止支持的系统调用请参考 `src/ufs.c` 的 `struct fuse_operations ufs_oper`
变量.

本文件系统没有考虑并发条件下的文件访问, 竞争条件会带来不可预知的效果.

完整文档请参考 `src/fs_based_on_fuse.tex`, 如果你的系统上没有安装 LaTeX
编译环境, 请给我留言, 我把编译好的 PDF 发送给您 (因为文档可能会经常变化,
所以我只把 LaTeX 源文件纳入版本管理).
