# fuse 原理学习与理解
## 1 [fuse-1.3 的工作方式](http://fuse.sourceforge.net/doxygen/index.html#section1)
![fuse 与其他模块的关系](./FUSE_structure.svg.png)  
图片来源 http://en.wikipedia.org/wiki/File:FUSE_structure.svg  

### 1.1 内核模块
内核模块由两个部分组成, 第一个是位于 `kernel/dev.c` 的 `proc` 文件系统, 第
二个是位于 `kernel/file.c`, `kernel/inode.c`, `kernel/dir.c` 的文件系统
调用.

`kernel/file.c`, `kernel/inode.c`, `kernel/dir.c` 中的所有系统调用都会
调用 `request_send()`, 或者是 `request_send_noreply()`, 又或者是
`request_send_noblock()`. 大多数系统调用 (有2个除外) 调用的是
`request_send()`. `request_send()` 将请求放入 "请求队列" (`fc->pending`),
等待响应. `request_send_noreply()` 不会等待响应, `request_send_noblock()`
不会阻塞, 除此之外, 这两个函数与 `request_send()` 的功能相同.

`proc` 文件系统对应于 `/dev/fuse` 的文件 I/O 请求. `fuse_dev_read()` 处理
文件的读操作, 并将 "请求队列" 的命令返回给主调程序. `fuse_dev_write()`
处理文件的写操作, 该函数接收要被写入的数据, 将其放入 `req->out` 结构中,
这些要被写入的数据会通过 "请求队列"  与 `request_send()` 返回给系统调用.

### 1.2 fuse 库函数
* 当你的用户态程序调用 `fuse_main()` (`lib/helper.c`) 时, `fuse_main()` 开始
解析传递给程序的参数, 然后调用 `fuse_mount()` (`lib/mount.c`).
* `fuse_mount()` 创建一对 UNIX 域套接字, 然后 `fork()` 并 `exec()`
`fusermount`
(`util/fusermount.c`), 通过环境变量 `FUSE_COMMFD_ENV` 将其中一个套接字传递
给 `fusermount`.
* `fusermount` (`util/fusermount.c`) 先确定 fuse 模块已加载进内核. 然后,
`fusermount` 打开 `/dev/fuse` 并将文件描述符通过 UNIX 域套接字发回给 
`fuse_mount()`.
* `fuse_mount()` 收到 `/dev/fuse` 的文件描述符后将其返回给 `fuse_main()`.
* `fuse_main()` 调用 `fuse_new()` (`lib/fuse.c`), 后者分配一个结构体
`struct fuse`, 该结构用来存放并维护文件系统数据的一个缓冲映像.
* 最后, `fuse_main()` 或者调用  `fuse_loop()`, 又或者是 `fuse_loop_mt()`
(`lib/fuse.c`), 这两个函数都会从 `/dev/fuse` 中读取用户文件系统从内核收
到的请求 (比如 `read`, `stat` 等), 然后调用 `struct fuse_operation` 中对应
的用户态函数, 函数调用的结果会写回到 `/dev/fuse`, 通过它返回给内核.

## 2 [fuse 文件系统简介](http://lxr.free-electrons.com/source/Documentation/filesystems/fuse.txt)

### 2.1 定义
#### 用户态文件系统

