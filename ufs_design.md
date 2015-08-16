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
    - `-ENAMETOOLONG`: 路径名过长;
    - `-ENFILE`: 文件系统无空间存放新打开的文件;
    - `-ENOENT`: `path` 中的某一前缀目录不存在;
    - `-ENOSPC`: 硬盘空间不足;
    - `-ENOTDIR`: `path` 中的某一前缀不是目录;
    - `-EINVAL`: 含有无效参数;
  + 函数过程:
    - 如果 `path` 为空, 或长度为 0, 返回 `-EINVAL`;
    - 若 `path` 超过最大路径长度, 返回 `-ENAMETOOLONG`;
    - 在 `ufs_open_files` 表中查找空闲项, 若无空闲项, 返回 `-ENFILE`; 若找到,
      记空闲项的索引为 `fd`;
    - 调用 `parpath = dirname(path)` 与 `base = basename(path)`, 获取前缀路径 
      与 新文件名;
    - 如果 `base` 长度超过最大文件名长度, 则会被截断;
    - 调用 `ufs_dir2i(parpath, parinode)` 获取父目录的 i 结点, 若函数出错,
      原样返回错误值;
    - 调用 `ufs_find_entry(parinode, base, entry)`, 在父目录查找是否已存在新文件
      的目录项;
    - 若 `ufs_find_entry` 的返回值为 0, 返回 `-EEXIST`; 若返回值是除了 `-ENOENT`
      之外的其他值, 原样返回错误值.
    - 调用 `inum = ufs_new_inode()` 获取一个空闲的 i 结点, 若返回 值 为0, 返回 `-ENOSPC`;
    - 初始化一个 i 结点 `inode`, 将它的 i 结点号设置为 `inum`, 并调用 `ufs_wr_inode(inode)`,
      若 `ufs_wr_inode` 出错, 原样返回错误值.
    - 初始化一个新文件的目录项 `entry`, 调用 `ufs_add_entry(parinode, entry)`, 在父目录中 
      添加该目录项, 若 `ufs_add_entry` 出错, 原样返回错误值
    - 用新文件的 i 结点 `inode` 初始化 `ufs_open_files[fd]`;
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

* `int mkdir(const char *path, mode_t mode)`
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
    - 从 `path` 中分离出前缀目录与新目录名, `dir = dirname(path); base = basename(path)`, 若路径部分 `path` 过长, 返回 `-ENAMETOOLONG`, 若 `base` 过长,
    则会被截断;
    - 调用 `ufs_dir2i(dir, parent)`, 若调用出错, 原样返回错误值;
    - 调用 `ufs_find_entry(parent, base)`, 在父目录 `parent` 中查找新目录名 `base`. 若函数返回
      值是 `0`, 返回 `-EEXIST`; 若返回值不是 `-ENOENT`, 原样返回错误值.
    - 调用 `ufs_new_inode()` 为新目录申请一个 i 结点号, 若返回值为 0, 返回 `-ENOSPC`;
    - 初始化新目录的 i 结点 `dirinode`, 调用 `ufs_add_entry(dirinode, entry)`, 为新目录添加
      两个目录项 `.` 与 `..`, 若出错, 原样返回错误值;
    - 调用 `ufs_wr_inode(dirinode)` 将新目录的 i 结点写盘, 若出错, 原样返回错误值;
    - 初始化一个新目录的目录项, 调用 `ufs_add_entry(parent, entry)`, 在父目录中新增一个目录项,
      若出错, 原样返回错误值, 更新父目录的 链接数 `i_nlink`,  调用 `ufs_wr_inode(parent)` 将父
      目录的 i 结点写盘; 若出错, 原样返回错误值;
    - 在函数即将返回前, 检查前面的步骤是否有错误发生, 若有, 且已经为新目录申请了 i 结点, 则将
      新申请的 i 结点释放 (`ufs_free_inode(newdirinode->i_ino)`;
    - 返回.

* `int readdir(const char *dirpath)`
  + 功能: 读某个目录中的所有项
  + 输入参数:
    - `dirpath`: 目录的路径;
  + 返回值:
    - 若成功, 返回 0;
    - `-ENOTDIR`: `dirpath` 引用的不是一个目录, 或路径中的某个部分不是一个目录;
    - `-ENOENT`: 路径中的某个目录不存在
    - `-ENAMETOOLONG`: 路径名过长
    - `-EINVAL`: 路径名为空或长度为 0;
    - `-ENOMEM`: 内存不足;
    - `-EIO`: 不知名错误;
  + 函数过程 
    - 若 `dirpath` 为空或长度为0, 返回 `-EINVAL`;
    - 若 `dirpath` 长度大于最大路径名长度, 返回 `-ENAMETOOLONG`;
    - 调用 `ufs_dir2i(dirpath, dirinode)`, 若返回出错, 原样返回错误值;
    - 循环读取 `dirinode` 所有的数据块, 对数据块 `dnum`, 调用 `ufs_dnum2znum`, 若
      返回的逻辑块号 `znum` 为 0, 则递增 `dnum`, 继续循环;
    - 调用 `ufs_rd_zone(znum, buf)`, 从磁盘上读取逻辑块到缓冲区 `buf` 中; 若出错, 原样返回错误值;
    - 将 `buf` 当作 `struct ufs_dir_entry` 的数组来使用; 若读取到一个 `buf[i].de_inum` 不为0 的
      元素, 则输出目录项, 并递增读取的到目录项项数.
    - 若所有的目录项都已读取完毕, 则退出循环.
    - 若所有的目录项都还没有读完, 但是数据块已经读取完了, 说明程序有错, 返回 `-EIO`
    - 返回.
* `int unlink(const char *path)`
  + 功能: 删除一个文件
  + 输入参数:
    - `path`: 文件的路径;
  + 返回值:
    - 成功返回 0;
    - `-EINVAL`: 路径为空, 或长度为 0;
    - `-EACCESS`: 对文件的父目录无写权限, 或对路径中的目录无搜索权限;
    - `-EISDIR`: 路径名引用的是目录文件;
    - `-ENAMETOOLONG`: 路径名过长;
    - `-ENOENT`: 文件不存在, 或路径中的某个目录不存在;
    - `-ENOTDIR`: 路径中的某一前缀目录不是一个目录文件;
  + 函数过程:
    - 若 `path` 为空, 或长度为 0, 返回 `-EINVAL`;
    - 若 `path` 长度大于最大路径名长度, 返回 `-ENAMETOOLONG`;
    - 调用 `dir = dirname(path)` 与 `file = basename(path)` 获取路径中的目录中的目录名,
      与 文件名;
    - 调用 `ufs_path2i(path, inode)` 获取将被删除的文件的 i 结点, 若函数出错, 原样返回错误值;
    - 如果 `inode` 是一个目录文件, 返回 `-EISDIR`;
    - 调用 `ufs_dir2i(dir, parinode)` 获取父目录的的 i 结点, 若函数出错, 原样返回错误值;
    - 调用 `ufs_rm_entry(parinode, entry)`, 从父目录中删除一个目录项, 若函数出错, 原样返回错误值.
    - 调用 `ufs_wr_inode(parinode)` 将父目录 i 结点写盘;
    - 为 `inode.i_nlink` 减一, 减 1 后不为零, 调用 `ufs_wr_inode(inode)` 将 i 结点写盘; 若为 0, 调用 
      `ufs_truncate(inode)` 将文件截断, 若出错, 原样返回错误值, 截断后调用 `ufs_free_inode(inode.i_ino)` 释放
      i 结点.
    - 返回
* `int rmdir(const char *path)`
  + 功能: 删除一个空目录;
  + 输入参数:
    - `path`: 目录的路径名;
  + 返回值:
    - 成功返回 0;
    - `-EINVAL`: 目录为空, 或长度为 0;
    - `-EACCESS`: 对目录的父目录无写权限, 或对路径中的目录无搜索权限;
    - `-ENAMETOOLONG`: `path` 路径名过长;
    - `-ENOENT`: 目录不存在, 或路径中的某个目录不存在;
    - `-ENOTDIR`: `path` 引用的不是一个目录, 或路径中的某个部分不是一个目录;
    - `-ENOTEMPTY`: `path` 引用的不是一个空目录;
    - `-EPERM`: `path` 引用的是根目录;
  + 函数过程:
    - 若 `path` 为空, 或长度为 0, 返回 `-EINVAL`;
    - 若 `path` 长度大于最大路径名长度, 返回 `-ENAMETOOLONG`;
    - 若 `path` 引用是根目录, 返回 `-EPERM`;
    - 调用 `ufs_dir2i(path, inode)`, 若函数出错, 原样返回错误值;
    - 如果 `inode` 不是一个目录, 返回 `-ENOTDIR`;
    - 调用 `ufs_is_empty(inode)`, 如果目录不空, 返回 `-ENOTEMPTY`;
    - 调用 `dir = dirname(path)` 获取 `path` 的目录部分;
    - 调用 `ufs_dir2i(dir, parinode)`, 获取父目录的 i 结点, 若函数出错, 原样返回错误值;
    - 调用 `ufs_rm_entry(parinode, ent)` 移除将被删除的目录的目录项, 若函数出错, 原样返回错误值;
    - 调用 `ufs_wr_inode(parinode)` 将父目录的 i 结点写盘;
    - 调用 `ufs_truncate(inode)`, 释放将被删除的目录占用的数据块;
    - 调用 `ufs_free_inode(inode)` , 释放目录的 i 结点;
    - 返回.

* `int open(const char *path, int flag)`;
  + 功能: 打开一个文件;
  + 输入参数:
    - `path`: 文件路径;
    - `flag`: 打开标志, 包括 `UFS_O_RDWR`, `UFS_O_RDONLY`, `UFS_O_WRONLY`, `UFS_O_APPEND`,
      `UFS_O_DIR`, `UFS_O_TRUNC`;
  + 返回值:
    - 若成功返回非负的文件描述符;
    - `-EINVAL`: `path` 为空或长度为 0;
    - `-ENOTSUP`: `flag` 包含无效标志;
    - `-ENAMETOOLONG`: `path` 长度大于最大路径名长度;
    - `-EACCESS`: 对文件的打开请求不被允许 (例如打开标志指定了 `UFS_O_RDWR`, 但文件不可写), 或路径中的某目录
      无搜索权限;
    - `-EISDIR`: `path` 引用的是目录, 而打开标志包含写;
    - `-ENFILE`: 文件系统无空间存放新打开的文件;
    - `-ENOENT`: 文件不存在, 或路径中的某个目录不存在;
    - `-ENOTDIR`: 路径中的某一前缀目录不是一个目录文件, 或打开标志指定了 `UFS_O_DIR`, 但 `path` 不是一个目录文件;
  + 函数过程:
    - 若 `path` 为空, 或长度为 0, 返回 `-EINVAL`;
    - 若 `flag` 包含无效标志 (`UFS_O_RDWR`, `UFS_O_WRONLY` 与 `UFS_O_RDONLY` 三者有且仅有一个被设置, 其他标志是可
      选的), 返回 `-ENOTSUP`;
    - 若 `path` 长度大于最大路径名长度, 返回 `-ENAMETOOLONG`;
    - 遍历 `open_files[]`, 寻找空闲项, 若没有空闲项, 返回 `-ENFILE`; 若找到, 记空闲项下标为 `fd`;
    - 调用 `ufs_path2i(path, inode)`, 获取文件的 i 结点点, 若函数出错, 原样返回错误值;
    - 若 `flag` 包含写操作 (`UFS_O_WRONLY`, `UFS_O_RDWR` 等), 而 `path` 是一个目录文件, 返回 `-EISDIR`;
    - 若 `flag` 指定了 `UFS_O_DIR`, 但 `path` 不是一个目录文件, 返回 `-ENOTDIR`;
    - 若 `flag` 指定了 `UFS_O_TRUNC`, 则调用 `ufs_truncate(inode)` 与 `ufs_wr_inode(inode)`, 若函数出错, 原样返回错误值;
    - 初始化 `open_files[fd]`, 返回.

* `int write(int fd, const void *buf, size_t size)`
  + 功能: 写一个文件;
  + 输入参数:
    - `fd`: 打开文件描述符;
    - `buf`: 数据缓冲区;
    - `size`: 写入的数据量, 数据量大于 0;
  + 返回值:
    - 若成功返回 写入的数据量;
    - `-EBADF`: `fd` 不是一个有效的文件描述符, 或没有打开, 或打开文件时没有指定写标志;
    - `-EIO`: 底层写函数出错;
    - `-ENOSPC`: 磁盘空间不足;
    - `-EINVAL`: `buf` 为空, 或 `size` 小于等于 0;
    - `-EFBIG`: 文件过大;
  + 函数过程:
    - 若 `fd` 不是一个有效的文件描述符, 或没有打开, 或打开文件时没有指定写标志, 返回 `-EBADF`;
    - 若 `buf` 为空, 或 `size` 小于等于 0, 返回 `-EINVAL`;
    - 获取文件的当前读写偏移量 `pos`, 若打开文件时指定 了 `UFS_O_APPEND`, 将 `pos` 设置为
      文件的当前大小;
    - 当还有剩余的数据未写时, 循环;
    - 利用 除运算与取模运算, 计算当前要写的偏移在文件中的数据块号, 与数据块内的偏移量;
      调用 `ufs_creat_zone(dnum)` 获取数据块号对应的逻辑块号, 若返回 0, 判断是否是因为文件过大,
      若是, 置 `ret = -EFBIG`; 否则置 `ret = -ENOSPC`, 退出循环;
    - 调用 `ufs_rd_zone(znum, buf)` 读一块数据, 并将要写入的数据写入 `buf`, 再调用 
      `ufs_wr_zone(znum, buf)` 将逻辑块写盘;
    - 更新文件读偏移量, 若超过文件大小, 同时更新文件 i 结点的大小, 并写盘;
    - 若还有数据未写完, 则继续循环, 否则退出循环;
    - 在循环结束后, 如果文件打开时没有设置追加写, 则更新文件当前读写偏移量;
    - 若前面没有出错, 返回已写的数据量, 否则返回错误值;
* `int read(int fd, void *buf, size_t size)`
  + 功能: 读一个文件;
  + 输入参数:
    - `fd`: 打开文件的描述符;
    - `buf`: 数据缓冲区;
    - `size`: 期望读到的字节数;
  + 返回值:
    - 若成功返回读取到的字节数, 0 表读到文件末尾;
    - `-EBADF`: 文件描述符无效, 或未打开, 或打开时没有指定读标志;
    - `-EIO`: 发生 I/O 错误;
    - `-EISDIR`: `fd` 引用的是一个目录文件;
  + 函数过程:
    - 若 `fd` 无效, 或未打开, 或打开时没有指定读标志, 返回 `-EBADF`;
    - 若 `buf` 为空, 或 `size` 为 0, 返回 0;
    - 若 `open_files[fd].f_inode` 引用的是一个目录文件, 返回 `-EISDIR`;
    - 获取文件的当前读偏移量 `pos`;
    - 当未遇到文件末尾且未读满 `size` 个字节时, 循环;
    - 将读到的数据复制到 `buf` 中, 若出错, 原样返回错误值;
    - 每读完一个单元的数据, 就更新文件的当前读写偏移量.
    - 退出循环后, 返回读到的数据量.
