# 文件系统存储与组织详细设计

## 常量说明

	#define NAME_LEN 27	/* 文件名的最大长度, 不包括结尾的空字符 */
	#define BLK_SIZE 512	/* 磁盘块大小 */
	#define OPEN_MAX 64	/* 同时打开文件数最大值 */

## 主要数据结构
### 定义
* 逻辑块: 文件系统看待磁盘的方式, 文件系统分配磁盘的最小单位,
1 个逻辑块等于 1 个磁盘块
* 磁盘块: 访问磁盘的最小单位.

### 文件系统超级块
超级块含有整个文件系统的配置信息

	/* sizeof(struct super_block) <= 512 */
	struct super_block {
		blkcnt_t s_imap_blocks;	/* i 结点位图所占块数, 以逻辑块计 */
		blkcnt_t s_zamp_blicks;	/* 逻辑块位图所占块数, 以逻辑块计 */
		blkcnt_t s_inodes;	/* i 结点块数, 以逻辑块计 */
		blkcnt_t s_zones;	/* 逻辑块块数 */
	};

### i 结点

	struct inode {
		ino_t	i_ino;		/* i 结点号 */
		nlink_t	i_nlink;	/* 链接数 */
		mode_t	i_mode;		/* 文件类型 */
		off_t	i_size;		/* 文件大小, 以字节计, 只对普通文件有效 */
		time_t	i_atime;	/* 文件内容最后一次被访问的时间 */
		time_t	i_mtime;	/* 文件内容最后一次被修改的时间 */
		time_t	i_ctime;	/* i 结点最后一次被修改的时间 */
		uid_t	i_uid;		/* 拥有此文件的用户 id */
		gid_t	i_gid;		/* 拥有此文件的用户的组 id */
		blkcnt_t i_blocks;	/* 分配给该文件的逻辑块块数 */
		/*
		 * 文件内容用到的逻辑块块号数组.
		 *
		 * 0-3: 直接块;
		 * 4: 间接块
		 * 5: 间间接块
		 */
		blkcnt_t i_zones[6];
	};

### 目录项

	struct dir_entry {
		ino_t	de_inode;		/* 文件的 i 结点号 */
		char	de_name[NAME_LEN + 1];	/* 文件名, 以空字符结尾 */
	};

### 打开文件表

	struct file {
		struct inode *f_inode;	/* 与该文件对应的 i 结点 */
		mode_t	f_mode;		/* 文件操作模式 */
		int	f_flag;		/* 文件状态标志 */
		off_t	f_pos;		/* 当前文件偏移量 */
	};
	
	struct file open_files[OPEN_MAX]; /* 打开文件表 */

## 功能函数

### `int format(size_t megasize)`
* 功能: 格式化一个文件系统
* 输入参数:
  + `megasize`: 以 `MB` 为单位的磁盘大小
* 返回值: 成功返回 0; 失败返回 -1
* 注: 本文提到的磁盘都是指利用普通文件模拟的磁盘, "普通文件" 来源于外部
文件系统 (例如 Ext4), 磁盘作为文件系统的私有数据使用

### `int read_sb(const char *diskname)`
* 功能: 从磁盘上读超级块
* 输入参数:
  + `diskname`: 磁盘文件名
* 返回值: 成功返回 0; 失败返回  -1
* 注: 超级块作为文件系统的私有数据使用

### `ino_t new_inode(void)`
* 功能: 获取一个空闲的 i 结点
* 返回值: 若找到一个空闲的 i 结点, 返回它的 i 结点号; 否则返回 0
* 注: i 结点号从 0 开始, 但是可用的 i 结点从 1 开始

### `int free_inode(ino_t inum)`
* 功能: 释放一个指定的 i 结点
* 输入参数:
  + `inum`: 将被删除的 i 结点的编号
* 返回值: 若成功返回 0; 若失败返回 -1

### `struct d_inode *rd_inode(ino_t inum)`
* 功能: 读取指定的 i 结点
* 输入参数:
  + `inum`: 被读取的 i 结点的编号
* 返回值: 若成功, 返回磁盘上的 i 结点结构指针; 若失败返回 `NULL`

### `int wr_inode(struct d_inode *inode)`
* 功能: 将指定的 i 结点写回磁盘
* 输入参数:
  + `inode`: 需写回磁盘的 i 结点
* 返回值: 若成功返回 0; 若失败返回 -1

### `blkcnt_t new_zone(void)`
* 功能: 获取一块空闲的逻辑块
* 返回值: 若找到一块空闲的逻辑块, 返回它的块号; 否则返回 0
* 注: 逻辑块用于存储文件数据 (不包括元数据), 是针对于文件系统的; 而磁盘上
的每 512 字节为一个磁盘块, 磁盘块中可以存储任意的内容 (无论是 i 结点, 还是
文件数据)

### `int free_zone(blkcnt_t blk_num)`
* 功能: 删除一个指定的逻辑块
* 输入参数: 
  + `blk_num`: 将被删除的逻辑块块号
* 返回值: 成功时返回 0; 若失败返回 -1

### `int rd_zone(blkcnt_t zone_num, void *buf, size_t size)`
* 功能: 读一个指定的逻辑块
* 输入参数: 
  + `zone_num` :将被读取的逻辑块块号
  + `buf`: 存储逻辑块数据的缓冲区
  + `size`: 缓冲区大小, 必须等于逻辑块大小
* 返回值: 若成功, 返回0; 若失败, 返回 -1

### `int wr_zone(blkcnt_t zone_num, void *buf, size_t size)`
* 功能: 将一个指定的逻辑块写入磁盘
* 输入参数:
  + `zone_num`: 逻辑块块号
  + `buf`:写入逻辑块的缓冲区
  + `size`: 缓冲区大小, 必须等于逻辑块大小
* 返回值: 成功返回 0; 失败返回 -1

### `blkcnt_t inum2blknum(ino_t inum)`
* 功能: 计算指定的 i 结点所在的磁盘块块号
* 输入参数:
  + `inum`: 待计算的 i 结点号
* 返回值: 编号为 `inum` 的 i 结点所在的磁盘块块号

### `blkcnt_t zonenum2blknum(blkcnt_t zone_num)`
* 功能: 计算指定的逻辑块所在的磁盘块块号
* 输入参数:
  + `zone_num`: 待计算的逻辑块块号
* 返回值: 编号为 `zone_num` 的逻辑块所在的磁盘块块号

### `int rd_blk(blkcnt_t blk_num, void *buf, size_t size)`
* 功能: 从磁盘上读一块指定的磁盘块
* 输入参数:
  + `blk_num`: 将被读的磁盘块块号
  + `buf`: 存储磁盘块的缓冲区
  + `size`: 缓冲区大小, 必须等于磁盘块大小
* 返回值: 若成功返回 0; 失败返回 -1
* 注: 磁盘文件上的每 512 字节都算作一个磁盘块, 而不管该磁盘块存放的是什么
内容, 下同.

### `int wr_blk(blkcnt_t blk_num, void *buf, size_t size)`
* 功能: 写一块指定的磁盘块
* 输入参数:
  + `blk_num`: 将被写入的磁盘块的块号
  + `buf`: 写入磁盘块的缓冲区
  + `size`: 缓冲区大小, 必须等于磁盘块大小
* 返回值: 若成功返回 0; 失败返回 -1

### `ino_t path2inum(const char *path)`
* 功能: 将路径名映射为 i 结点
* 输入参数:
  + `path`: 被映射的路径名
* 返回值: 成功返回 i 结点号; 失败返回 -1

## 功能函数详细流程

