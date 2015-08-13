# u_fs 设计文档

* `int creat(const char *path, mode_t mode)`  
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

* `int release(const char *path, int fd)`
  + 功能: 关闭一个打开文件, 并释放它占用的资源;
  + 输入参数:
    - `path`: 将被释放的文件路径名, 不用;
    - `fd`: 打开文件的描述符;
  + 返回值:
    - 若成功返回 0;
    - `-EBADF`: `fd` 不是一个有效的文件描述符;

* `int mkdir(const char *path, mode_t mode)
  + 功能: 创建一个空目录;
  + 输入参数:
    - `path`: 新目录的路径名;
    - `mode`: 新目录的访问权限;
  + 返回值:
    - 若成功, 返回 0;
    - `-EINVAL`: `path` 为空, 或长度为 0;
    - `-EACCESS`: 用户对新目录的父目录无写权限, 或者, 对路径中的某一目录
      无搜索权限;
    - `-EEXIST`: `path` 引用的文件 (不一定非是目录) 已存在;
    - `-ENAMETOOLONG`: `path` 过长;
    - `-ENOENT`: `path` 中的某个前缀目录不存在;
    - `-ENOSPC`: 磁盘空间不足, 或父目录无空闲空间;
    - `-ENOTDIR`: `path` 的某个前缀目录不是一个目录文件;
  + 函数过程:
    - 若 `path` 为 `NULL`, 或长度为 0, 返回 `-EINVAL`;
    - 若 `path` 长度大于最大路径名长度, 返回 `-ENAMETOOLONG`;
    - 从 `path` 中分离出前缀目录与新目录名, `dir = dirname(path); base = basename(path);`
    - 调用 `dir2i(dir, parent)`, 若调用出错, 原样返回错误值;
    - 调用 `find_entry(parent, base)`, 在父目录 `parent` 中查找新目录名 `base`. 若函数返回
      值是 `0`, 返回 `-EEXIST`; 若返回值不是 `-ENOENT`, 原样返回错误值.
    - 调用 `new_inode()` 为新目录申请一个 i 结点号, 若返回值为 0, 返回 `-ENOSPC`;
    - 初始化新目录的 i 结点 `dirinode`, 调用 `add_entry(dirinode, entry)`, 为新目录添加
      两个目录项 `.` 与 `..`, 若出错, 原样返回错误值;
    - 调用 `wr_inode(dirinode)` 将新目录的 i 结点写盘, 若出错, 原样返回错误值;
    - 初始化一个新目录的目录项, 调用 `add_entry(parent, entry)`, 在父目录中新增一个目录项,
      若出错, 原样返回错误值.
    - 在函数即将返回前, 检查前面的步骤是否有错误发生, 若有, 且已经为新目录申请了 i 结点, 则将
      新申请的 i 结点释放 (`free_inode(newdirinode->i_ino)`;
    - 返回.

