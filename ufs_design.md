# u_fs 设计文档

* `int creat(const char *path, mode_t mode)`  
  + 功能: 创建一个新文件
  + 输入参数:
    - `path`: 新文件的路径;
    - `mode`: 新文件的访问权限.
  + 返回值:
    - 若成功, 返回只写打开的文件描述符;
    - `-EACCESS`: 进程无权限搜索目录; 进程无权限写新文件的父目录;
    - `-EEXIST`: 新文件名已存在.
    - `-EISDIR`: `path` 引用的是目录;
    - `-ENAMETOOLONG`: 新文件名过长, 或路径名过长;
    - `-ENFILE`: 文件系统无空间存放新打开的文件;
    - `-ENOENT`: `path` 中的某一前缀目录不存在;
    - `-ENOSPC`: 硬盘空间不足;
    - `-ENOTDIR`: `path` 中的某一前缀不是目录;
    - `-EINVAL`: 含有无效参数;
  + 函数过程:
    - 如果 `path` 为空, 或长度为 0, 返回 `-EINVAL`;
    - 若 `path` 超过最大路径长度, 返回 `-ENAMETOOLONG`;
    - 在 `open_files` 表中查找空闲项, 若无空闲项, 返回 `-ENFILE`; 若找到,
      记空闲项的索引为 `fd`;
    - 调用 `parpath = dirname(path)` 与 `base = basename(path)`, 获取前缀路径 
      与 新文件名;
    - 调用 `dir2i(parpath, parinode)` 获取父目录的 i 结点, 若函数出错,
      原样返回错误值;
    - 调用 `find_entry(parinode, base, entry)`, 在父目录查找是否已存在新文件
      的目录项;
    - 若 `find_entry` 的返回值为 0, 返回 `-EEXIST`; 若返回值是除了 `-ENOENT`
      之外的其他值, 原样返回错误值.
    - 调用 `inum = new_inode()` 获取一个空闲的 i 结点, 若返回 值 为0, 返回 `-ENOSPC`;
    - 初始化一个 i 结点 `inode`, 将它的 i 结点号设置为 `inum`, 并调用 `wr_inode(inode)`,
      若 `wr_inode` 出错, 原样返回错误值.
    - 初始化一个新文件的目录项 `entry`, 调用 `add_entry(parinode, entry)`, 在父目录中 
      添加该目录项, 若 `add_entry` 出错, 原样返回错误值
    - 用新文件的 i 结点 `inode` 初始化 `open_files[fd]`;
    - 在即将返回时, 如果前面发生了错误, 而申请过 i 结点, 则释放申请到的
      i 结点.
  + 注: `creat(const char *path, mode_t mode)` 等价于
`open(path, O_WRONLY | O_CREAT | O_TRUNC, mode)`,  但无法通过 fuse 获取不
定参数, 故无法通过 `open` 实现 `creat`.

* `int release(const char *path, int fd)`
  + 功能: 关闭一个打开文件, 并释放它占用的资源;
  + 输入参数:
    - `path`: 将被释放的文件路径名, 不用;
    - `fd`: 打开文件的描述符;
  + 返回值:
    - 若成功返回 0;
    - `-EBADF`: `fd` 不是一个有效的文件描述符;

* `int open(const char *path, int oflag, ... /* mode_t mode */)`
  + 功能: 打开一个文件, 必要时创建.
  + 输入参数:
    - `path`: 文件的路径名;
    - `oflag`: 打开标志;
    - `mode`: 新文件的访问权限;
  + 返回值:
    - 若成功, 返回文件描述符;
    - `-EACCESS`: 进程无权限搜索目录; 进程无权限写新文件的父目录;
    - `-EISDIR`: `path` 引用的是目录, 而 `oflag` 包含 `O_WRONLY`  或  
    `O_RDWR`
    - `-ENAMETOOLONG`: 新文件名过长;
    - `-ENFILE`: 文件系统无空间存放新打开的文件;
    - `-ENOENT`: `oflag` 不含 `O_CREAT` 而 `path` 引用的文件不存在; 或  
    `path` 中的某一前缀目录不存在;
    - `-ENOME`: 内存不足;
    - `-ENOSPC`: 硬盘空间不足;
    - `-ENOTDIR`: `path` 中的某一前缀不是目录;
    - `-EEXIST`: `oflag` 包含 `O_CREAT` 与 `O_EXCL` 但是文件已存在;
    - `-EINVAL`: 含有无效参数;

