# fuse 原理学习与理解
## 1 [fuse-1.3 的工作方式](http://fuse.sourceforge.net/doxygen/index.html#section1)
### 1.1 内核模块
内核模块是用户文件系统与内核沟通的桥梁, 有了它的帮助, 用户可以在不理解
内核模块编程的前提下, 开发自己的文件系统. 在用户文件系统出现之前, 文件
系统的开发是掌握在内核开发人员手中的, 而内核模块开发的远远大于应用程序
开发, 但有些人的确需要拥有特殊性质的文件系统.
### 1.2 fuse 库函数
* 当你的用户态程序调用 `fuse_main()` (`lib/helper.c`) 时, `fuse_main()` 开始
解析传递给程序的参数, 然后调用 `fuse_mount()` (`lib/mount.c`).
* `fuse_mount()` 创建一对 UNIX 套接字, 然后 `fork` 并 `exec` `fusermount`
(`util/fusermount.c`), 通过环境变量 `FUSE_COMMFD_ENV` 将其中一个套接字传递
给 `fusermount`.
* `fusermount` (`util/fusermount.c`) 先确定 fuse 模块已加载进内核. 然后,
`fusermount` 打开 `/dev/fuse` 并将文件描述符通过 UNIX 域套接字发回给 
`fuse_mount()`.
* `fuse_mount()` 收到 `/dev/fuse` 的文件描述符后将其返回给 `fuse_main()`.
* `fuse_main()` 调用 `fuse_new()` (`lib/fuse.c`), 后者分配一个结构体
`struct fuse`, 该结构用来存放并维护文件系统数据的一个缓冲映像.
* 最后, `fuse_main()` 或者调用  `fuse_loop()`, 
## 2 fuse 与其他模块的关系
![](./FUSE_structure.svg.png)  
如上图所示, fuse 内核模块负责与 VFS 的交互, 而用户的文件系统 (应用程序)
通过 fuse 的库函数, 完成与 FUSE 内核模式的交互.

图中用户输入 `ls` 命令, 查询目录 `/tmp/fuse/` 下的文件清单. 为了目录中文件
的状态信息, 需要发出系统调用 `stat`, 于是 C 库函数中的 `stat` 被调用.  
Linux 中, 在真实文件系统之上套有一层虚拟文件系统, 它可以将各种不同的文件
系统无关别地展现给上层, 而由自己来处理这些差异. fuse 内核模块事先在
VFS 中注册了自己的信息, VFS 通过文件系统信息, 将 `stat` 系统调用交由 fuse
模块来处理.
