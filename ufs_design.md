# u_fs 设计文档

* `creat(const char *path, mode_t mode)`  
  + 功能: 创建一个新文件
  + 输入参数:
    - `path`: 新文件的路径;
    - `mode`: 新文件的访问权限.
  + 返回值:
    - 若成功, 返回只写打开的文件描述符;
    - `-EACCESS`: 进程无权限搜索目录; 进程无权限写新文件的父目录;
    - `-EISDIR`: `path` 引用的是目录;
    - `-ENAMETOOLONG`: 新文件名过长;
    - `-ENFILE`: 文件系统无空间存放新打开的文件;
    - `-ENOENT`: `path` 中的某一前缀目录不存在;
    - `-ENOMEM`: 内存不足;
    - `-ENOSPC`: 硬盘空间不足;
    - `-ENOTDIR`: `path` 中的某一前缀不是目录;
    - `-EINVAL`: 含有无效参数;
  + 注: `creat(const char *path, mode_t mode)` 等价于
`open(path, O_WRONLY | O_CREAT | O_TRUNC, mode)`,  但无法通过 fuse 获取不
定参数, 故无法通过 `open` 实现 `creat`.

* `open(const char *path, int oflag, ... /* mode_t mode */)`
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

