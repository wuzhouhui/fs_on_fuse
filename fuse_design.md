# 文件系统存储与组织详细设计

## 1.1 文件系统在磁盘上的布局

![](./layout.png)  
图 1-1 文件系统各个字段在磁盘上的布局

## 1.2 常量说明

	#define NAME_LEN        27	/* 文件名的最大长度, 不包括结尾的空字符 */
	#define BLK_SIZE        512	/* 磁盘块大小 */
	#define OPEN_MAX        64	/* 同时打开文件数最大值 */
	#define DISK_MAX_SIZE   32	/* 磁盘文件最大值 (MB) */
	#define DISK_MIN_SIZE	1	/* 磁盘文件最小值 (MB) */
	#define ROOT_INO	1	/* 根目录的 i 结点号 */

## 1.3 主要数据结构

### 1.3.1 定义

* 逻辑块: 文件系统看待磁盘的方式, 文件系统分配磁盘的最小单位,
1 个逻辑块等于 1 个磁盘块的大小. 逻辑块编号从 1 开始.
* 磁盘块: 访问磁盘的最小单位, 编号从 0 开始, 每一个磁盘块大小为 512 字节.
* 数据块: 单个文件内部数据组成的块, 大小与逻辑块相同, 从 0 开始编号,
数据块号仅在包含它的文件内部有效.

在外部表现上, 逻辑块与磁盘块唯一的不同在于在磁盘上的起始位置不同. 磁盘块从磁盘的第
1 块 512
字节块 就开始, 一直到磁盘的最后一块 512 字节块. 而逻辑块的第 1 块从磁盘的数据区
开始, 该磁盘块前面的几块要预留给超级块, i 结点位图, 逻辑块位图,
与 i 结点使用, 逻辑块从它们之后的第 1 块开始, 从 1 开始编号. 在图 1-1 中, 从左到右,
磁盘块从第 1 块方格开始, 而逻辑块从第 9 块方格开始. 数据块指的是分配给文件, 用来
存储文件数据 (非元数据) 的逻辑块, 如果一个文件的大小为 678 KB,
那么它就拥有 2 块数据块, 数据块号按出现顺序依次为 1, 2.

### 1.3.2 文件系统超级块
超级块含有整个文件系统的配置信息

	/*
	 * super block in disk.
	 * sizeof(struct d_super_block) <= 512
	 */
	struct d_super_block {
		unsigned short s_magic; /* 文件系统魔数 */
		blkcnt_t s_imap_blocks;	/* i 结点位图所占块数, 以逻辑块计 */
		blkcnt_t s_zmap_blocks;	/* 逻辑块位图所占块数, 以逻辑块计 */
		blkcnt_t s_inode_blocks; /* i 结点块数, 以逻辑块计 */
		blkcnt_t s_zone_blocks;	/* 逻辑块块数 */
		off_t	s_max_size;	/* 最大文件长度 */
	};
	
	/* super block in memeory */
	struct m_super_block {
                unsigned short s_magic; /* 文件系统魔数 */
		blkcnt_t s_imap_blocks;	/* i 结点位图所占块数, 以逻辑块计 */
		blkcnt_t s_zmap_blocks;	/* 逻辑块位图所占块数, 以逻辑块计 */
		blkcnt_t s_inode_blocks; /* i 结点块数, 以逻辑块计 */
		blkcnt_t s_zone_blocks;	/* 逻辑块块数 */
		off_t	s_max_size;	/* 最大文件长度 */

		/* 下面的字段仅存在于内存中 */
		char *s_imap;		/* i 结点位图 */
		char *s_zmap;		/* 逻辑块位图 */
		blkcnt_t s_1st_inode_block; /* 第 1 块 i 结点块的磁盘块号 */
		blkcnt_t s_1st_zone_block; /* 第 1 块逻辑块的磁盘块号 */
		ino_t	s_inode_left;	/* 剩余 i 结点数 */
		blkcnt_t s_block_left;	/* 剩余 逻辑块数 */
		int	s_fd;		/* 磁盘文件描述符 */
		void	*s_addr;	/* 磁盘文件在内存中的地址 */
	};

磁盘上的超级块大小要小于 512 字节, 这是因为超级块要完全放入磁盘的第一个磁盘块中.
`s_magic` 用于唯一地识别一个文件系统, 从磁盘上加载了超级块结构后, 如果魔
数正确, 那么就可以断定这是我们想要的文件系统. `s_max_size` 依赖于 i 结点所 
能支持的逻辑块块号数组大小, 以及逻辑块块大小, 在讲解 i 结点时会提到.
`s_zmap_blocks` 决定了文件系统所能支持的最大磁盘大小. 假设 `s_zmap_blocks` 为2,
且 逻辑块大小 为 512 字节, 那么文件系统最多支持 `2 * 512 * 8 * 512 = 4 MB` 大小 
的磁盘. i 结点位图的一个二进制位为 0,
表示相应的 i 结点位空闲; 为 1 则表示相应的 i 结点被占用. "i 结点号" 其实就
是 i 结点相应的位在位图中的下标. 逻辑块位图除了每一个位表示一个逻辑块外,
其他的与 i 结点位图相同.

之所以分成 "磁盘上的超级块" 与 "内存中的超级块" 是为了兼顾使用的方便与信息的最
小化. `struct m_super_block` 结构中多出来的信息可以通过 "磁盘上的超级块" 计算得到,
但如果每次使用时都重新计算比较浪费时间, 所以将这些辅助信息事先计算并存储起来.

为了分辨出 "所有 i 结点都被占用" 与 "所有逻辑块都被占用" 的情况, i 结点位图与
逻辑块位图的第 1 个位不用, 这样就可以通过返回 0 来表示无空闲 i 结点或 
逻辑块, 在初始化文件系统时, 这两位被初始化为 1.

### 1.3.3 i 结点

	/*
	 * 磁盘上的 i 结点.
	 * sizeof(struct d_inode) == 64 in 32-bit
	 */
	struct d_inode {
		nlink_t	i_nlink;	/* 链接数 */
		mode_t	i_mode;		/* 文件类型和访问权限 */
		off_t	i_size;		/* 文件长度, 以字节计 */
		time_t	i_atime;	/* 文件内容最后一次被访问的时间 */
		time_t	i_mtime;	/* 文件内容最后一次被修改的时间 */
		time_t	i_ctime;	/* i 结点最后一次被修改的时间 */
		uid_t	i_uid;		/* 拥有此文件的用户 id */
		gid_t	i_gid;		/* 拥有此文件的用户的组 id */
		/*
		 * 文件内容用到的逻辑块块号数组.
		 *
		 * 0-5: 直接块;
		 * 6: 一次间接块
		 * 7: 二次间接块
		 */
		blkcnt_t i_zones[8];
	};

	/* 内存中的 i 结点. */
	struct m_inode {
		nlink_t	i_nlink;	/* 链接数 */
		mode_t	i_mode;		/* 文件类型和访问权限 */
		off_t	i_size;		/* 文件长度, 以字节计 */
		time_t	i_atime;	/* 文件内容最后一次被访问的时间 */
		time_t	i_mtime;	/* 文件内容最后一次被修改的时间 */
		time_t	i_ctime;	/* i 结点最后一次被修改的时间 */
		uid_t	i_uid;		/* 拥有此文件的用户 id */
		gid_t	i_gid;		/* 拥有此文件的用户的组 id */
		/*
		 * 文件内容用到的逻辑块块号数组.
		 *
		 * 0-5: 直接块;
		 * 6: 间接块
		 * 7: 间间接块
		 */
		blkcnt_t i_zones[8];

		/* 下面的字段仅存在于内存中 */
		ino_t	i_ino;		/* i 结点号, 从 1 开始, 等价于与该 i 结点对应的二进制位在 i 结点位图中的下标 */
		int	i_refcnt;	/* i 结点被引用的次数 */
	}

`i_nlink` 是指向该 i 结点的目录项的个数, 可用于实现硬链接. `i_mode` 包含了有关文件属性与
权限的信息, 该字段包含的信息如下图

![](./imode.png)  
图 1-2 `i_mode` 字段中每一位的作用.

`i_mode` 的高位在左, 低位在右. 低 9 位用于判断三类用户对该文件的读写权限,
位 9 用于判断文件类型, 其余位不用.

![](./i_zones_array.png)  
图 1-3 i 结点逻辑块块号数组的功能

如果存放逻辑块号的存储单元的值为 0, 说明该单元空闲.
很容易可以算出一个本文件系统在 32 位平台上所能支持的最大文件为
`(6 + 512/4 + (512/4) * (512/4)) * 512 = 8259 kB`. `struct m_inode`
结构中多出来的信息一方面是为了避免重复计算, 另一方面是为完成某些功能, 例如当 i 结点 
的链接数为 0 且引用次数为 0 时, 才可删除文件并回收资源, "引用次数" 指的是
打开文件表 `open_files` 中指向该 i 结点的项数.

### 1.3.4 目录项

	/* sizeof(struct dir_entry) == 32 in 32-bit */
	struct dir_entry {
		ino_t	de_inum;		/* 文件的 i 结点号 */
		char	de_name[NAME_LEN + 1];	/* 文件名, 以空字符结尾 */
	};

每个目录至少含有 2 个目录项: `.` 与 `..`, 文件系统格式化后只含有一个根目录,
且只有 `.` 与 `..` 这两个目录项.

### 1.3.5 打开文件表

	struct file {
		struct m_inode *f_inode;	/* 与该文件对应的 i 结点 */
		mode_t	f_mode;			/* 文件类型与访问权限 */
		int	f_flag;			/* 文件打开和控制标志 */
		int	f_count;		/* 对应文件句柄引用次数 */
		off_t	f_pos;			/* 当前文件偏移量 */
	};
	
	struct file open_files[OPEN_MAX]; /* 打开文件表 */

`open_files` 是文件系统存储打开文件信息的表格, 打开文件在表格中的索引将作
为文件描述符使用. `f_mode` 字段的含义与 i 结点的 `i_mode` 字段相同. 
`f_flag` 的标志包括:  
* 文件访问模式:
  + `O_RDONLY`: 只读打开
  + `O_WRONLY`: 只写打开
  + `O_RDWR`: 读写打开
* 文件创建与控制标志:
  + `O_CREAT`: 如果文件不存在则创建;
  + `O_EXCL`: 如果同时指定了 `O_CREAT`, 若文件已存在则出错;
  + `O_APPEND`: 追加写;
  + `O_TRUNC`: 若打开方式包含写, 则截断文件;

![](./file_mode.png)  
图 1-4 `f_mode` 字段各位的作用, 高位在左, 低位在右.

3 种文件访问模式必须指定且只能指定一种, 但是文件创建与控制标志都是
可选的.

`f_count` 表示该文件被多少个描述符引用, 例如 `dup()` 函数就可以增加
`f_count` 的值, 当 `f_count` 为 0 时, 该文件从打开文件表中清除, 并
将该项置为空闲状态, 表项是否空闲的依据是 `f_count` 是否为 0.

## 1.4 功能函数 (或命令)

### `format`
* 功能: 格式化一个文件系统
* 命令行参数:
  + 磁盘文件路径, 在挂载文件系统之后, 磁盘文件必须存放在另一个
  文件系统上, 否则行为是未定义的.
* 程序执行结果: 由参数指定的文件被格式化成一个文件系统, 将作为磁盘
使用.
* 注: 本文提到的磁盘都是指利用普通文件模拟的磁盘, "普通文件" 来源于外部
文件系统 (例如 Ext4), 磁盘作为文件系统的私有数据使用. 一个文件或作为
磁盘使用的条件是: 
  + 大小在 1 兆字节到 `DISK_MAX_SIZE` 字节之间
  + 是普通文件
  + 用户可读写

### `int read_sb(const char *diskname)`
* 功能: 从磁盘上读超级块
* 输入参数:
  + `diskname`: 磁盘文件名
* 返回值: 成功返回 磁盘文件描述符; 失败返回 -1
* 注: 超级块作为文件系统的私有数据使用, 所以未在函数签名中显式给出. 
磁盘文件必须曾被 `format` 程序格式化过.

### `ino_t new_inode(void)`
* 功能: 获取一个空闲的 i 结点
* 返回值: 若找到一个空闲的 i 结点, 返回它的 i 结点号; 否则返回 0
* 注: i 结点号从 1 开始

### `int free_inode(ino_t inum)`
* 功能: 释放一个指定的 i 结点
* 输入参数:
  + `inum`: 将被释放的 i 结点的编号
* 返回值: 若成功返回 0; 若失败返回 -1. 以下情况返回失败:
  + i 节点号超出范围;
  + i 节点原来就处理空闲状态.

### `int rd_inode(ino_t inum, struct d_inode *inode)`
* 功能: 读取指定的 i 结点
* 输入参数:
  + `inum`: 被读取的 i 结点的编号
  + `inode`: 存放 i 结点的缓冲区
* 返回值: 若成功返回 0; 失败返回 -1. 以下情况返回失败:
  + i 节点号超出范围;
  + `inode` 为空.

### `int wr_inode(struct d_inode *inode)`
* 功能: 将指定的 i 结点写回磁盘
* 输入参数:
  + `inode`: 需写回磁盘的 i 结点
* 返回值: 若成功返回 0; 若失败返回 -1. 若 `inode` 为空, 则什么
也不做, 返回成功.

### `blkcnt_t new_zone(void)`
* 功能: 获取一块空闲的逻辑块
* 返回值: 若找到一块空闲的逻辑块, 返回它的逻辑块号; 否则返回 0
* 注: 逻辑块用于存储文件数据 (不包括元数据), 是针对于文件系统的; 而磁盘上
的每 512 字节为一个磁盘块, 磁盘块中可以存储任意的内容 (无论是 i 结点, 还是
文件数据)

### `int free_zone(blkcnt_t zone_num)`
* 功能: 释放一个指定的逻辑块
* 输入参数: 
  + `zone_num`: 将被释放的逻辑块块号
* 返回值: 成功时返回 0; 若失败返回 -1. 以下情况返回失败:
  + `zone_num` 超出范围;
  + `zone_num` 原来就处于已释放状态.

### `int rd_zone(blkcnt_t zone_num, void *buf, size_t size)`
* 功能: 读一个指定的逻辑块
* 输入参数: 
  + `zone_num` :将被读取的逻辑块块号
  + `buf`: 存储逻辑块数据的缓冲区
  + `size`: 缓冲区大小, 必须等于逻辑块大小
* 返回值: 若成功, 返回0; 若失败, 返回 -1. 以下情况返回失败:
  + `zone_num` 超出范围;
  + `buf` 为空;
  + `size` 不等于 逻辑块大小

### `int wr_zone(blkcnt_t zone_num, void *buf, size_t size)`
* 功能: 将一个指定的逻辑块写入磁盘
* 输入参数:
  + `zone_num`: 逻辑块块号
  + `buf`:写入逻辑块的缓冲区
  + `size`: 缓冲区大小, 必须等于逻辑块大小
* 返回值: 成功返回 0; 失败返回 -1. 以下情况返回失败:
  + `zone_num` 超出范围;
  + `buf` 为空;
  + `size` 不等于 逻辑块大小

### `blkcnt_t inum2blknum(ino_t inum)`
* 功能: 计算指定的 i 结点所在的磁盘块块号
* 输入参数:
  + `inum`: 待计算的 i 结点号
* 返回值: 编号为 `inum` 的 i 结点所在的磁盘块块号. 若 `inum` 无效,
返回 0.
* 注: 磁盘的第一个块被超级块占用, 故 0 号磁盘块不会被用到, 可用
作错误的返回值.

### `blkcnt_t zonenum2blknum(blkcnt_t zone_num)`
* 功能: 计算指定的逻辑块所在的磁盘块块号
* 输入参数:
  + `zone_num`: 待计算的逻辑块块号
* 返回值: 编号为 `zone_num` 的逻辑块所在的磁盘块块号. 若 `zone_num`
无效则返回 0.

### `blkcnt_t datanum2zonenum(ino_t inum, blkcnt_t data_num)`
* 功能: 计算指定的数据块所在逻辑块号
* 输入参数:
  + `inum`: 数据块所在 i 结点号;
  + `data_num`: 数据块号
* 返回值: 若数据块号与 i 结点号有效, 返回对应的逻辑块号; 否则返回 0.
* 注: "数据块" 是相对于单个文件的, 从 1 开始编号, 数据块号最大值
受限于 i 结点所能支持的最大文件大小.

### `int rd_blk(blkcnt_t blk_num, void *buf, size_t size)`
* 功能: 从磁盘上读一块指定的磁盘块
* 输入参数:
  + `blk_num`: 将被读的磁盘块块号
  + `buf`: 存储磁盘块的缓冲区
  + `size`: 缓冲区大小, 必须等于磁盘块大小
* 返回值: 若成功返回 0; 失败返回 -1. 以下情况返回失败:
 + `blk_num` 无效;
 + `buf` 为空;
 + `size` 不等于磁盘块大小.
* 注: 磁盘文件上的每 512 字节都算作一个磁盘块, 而不管该磁盘块存放的是什么
内容, 下同.

### `int wr_blk(blkcnt_t blk_num, void *buf, size_t size)`
* 功能: 写一块指定的磁盘块
* 输入参数:
  + `blk_num`: 将被写入的磁盘块的块号
  + `buf`: 写入磁盘块的缓冲区
  + `size`: 缓冲区大小, 必须等于磁盘块大小
* 返回值: 若成功返回 0; 失败返回 -1, 以下情况返回失败:
 + `blk_num` 无效;
 + `buf` 为空;
 + `size` 不等于磁盘块大小.

### `int path2dir_ent(const char *path, struct dir_entry *buf)`
* 功能: 查找指定路径名的目录项
* 参数: 
  + `path`: 文件的路径;
  + `buf`: 存放目录项的缓冲区
* 返回值: 若找到返回 1; 若 `path` 为空, 或未找到, 返回 0.

### `ino_t path2inum(const char *path)`
* 功能: 将路径名映射为 i 结点
* 输入参数:
  + `path`: 被映射的路径名
* 返回值: 成功返回 i 结点号; 失败返回 0

### `int srch_dir_entry(struct m_inode *parent, const char *filename, struct dir_entry *ent)`
* 功能: 在指定的目录中查找具有指定文件名的文件
* 输入参数:
  + `parent`: 查找的位置, 必须是目录;
  + `filename`: 待查找的文件名;
  + `ent`: 若查找成功, 存放被查找文件的目录项
* 返回值: 查找成功返回 1; 失败返回 0

### `int add_dir_entry(struct m_inode *dir, const struct dir_entry *entry)`
* 功能: 在指定的目录中新增一个目录项
* 输入参数:
  + `dir`: 在该目录中新增一个目录项;
  + `entry`: 新增的目录项.
* 返回值: 添加成功返回 0; 失败返回 -1. 在以下情况返回失败：
  + `dir` 为空或者不是一个目录；
  + `entry` 为空, 若 `entry` 的成员具有非法值, 或出现重得.

## 1.5 功能函数详细流程

	format diskfile
		// 打开文件 
		// 获取文件的各种元信息, 以判断该文件是否可作为磁盘使用
		// 初始一个超级块, 根据文件的大小决定文件系统各个部分的大小
		// 将超级块写到磁盘文件的第一个块上
		// 将 i 结点位图与逻辑块位图对应的磁盘块清零, 但将各自的第 1 个位置 1
		// 创建根目录
		// 关闭文件并退出

	int read_sb(const char *diskname)
		// 打开文件, 并读文件的前 512 字节 (超级块)
		// 判断超级块的合法性 (魔数)
		// 若超级块合法, 初始化 `struct m_super_block` 的其他字段
		// 若没有任何问题发生就返回成功; 否则返回失败.

	ino_t new_inode(void)
		// 先判断磁盘上是否有剩余的 i 结点 (inode_left)
		// 从 i 结点位图的第 1 个位开始查找.
		// 如果找到值为 0 的位或超出位图范围, 则退出循环; 否则继续.
		// 在循环之外, 若循环是因为越界而退出, 则返回 0; 否则 
		// 置位, 更新 inode_left 并返回该位在位图中的下标.

	int free_inode(ino_t inum)
		// 判断 inum 的合法性.
		// 测试 bit[inum] 是否置位
		// 若未置位则出错; 否则清零并返回成功.

	int rd_inode(ino_t inum struct d_inode *inode)
		// 判断 inum 与 inode 的有效性
		// 计算 inode 所在的磁盘块号.
		// 读磁盘块, 提取指定的 i 结点 
		// 将读取到的 i 结点写入 inode,
		// 返回

	int wr_inode(struct m_inode *inode)
		// 计算 inode 所在的磁盘块号
		// 读磁盘块, 修改指定的 i 指点
		// 写回磁盘

	blkcnt_t new_zone(void)
		// 先查看磁盘上是否有剩余的逻辑块 (block_left)
		// 从逻辑块位图的第 1 个位开始查找.
		// 如果找到值为 0 的位或超出位图范围, 则退出; 否则继续.
		// 在循环之外, 若循环是因为越界而退出, 则返回 0; 否则 
		// 置位并返回该位在位图中的下标

	int free_zone(blkcnt_t blk_num)
		// 判断 blk_num 的合法性.
		// 测试 bit[inum] 是否置位
		// 若未置位则出错; 否则清零并返回.

	int rd_zone(blkcnt_t zone_num, void *buf, size_t size)
		// 检查 逻辑块号与 缓冲区, 及其大小 的有效性
		// 由逻辑块号计算磁盘块号
		// 调用 读磁盘块函数
		// 返回由 读磁盘函数返回的值

	int wr_zone(blkcnt_t zone_num, void *buf, size_t size)
		// 检查逻辑块号与缓冲区, 及其大小的有效性
		// 由逻辑块号计算磁盘块号
		// 调用 写磁盘块函数
		// 返回由 写磁盘块函数返回的值

	blkcnt_t inum2blknum(ino_t inum)
		// 判断 `inum` 的有效性
		// 根据超级块, i 结点位图, 逻辑块位图占用的逻辑块数,
		// 以及每块逻辑块包含的 i 结点数计算包含该 i 结点的 
		// 逻辑块号, 如果逻辑块号有效, 则返回对应的磁盘块号;
		// 否则返回 0

	blkcnt_t zonenum2blknum(blkcnt_t zone_num)
		// 判断 zone_num 的有效性
		// 根据第 1 块逻辑块的磁盘块号 计算与 zone_num 对应的
		// 磁盘块号, 并返回
	
	blkcnt_t datanum2zonenum(ino_t inum, blkcnt_t data_num)
		// 判断 `inum` 与 `data_num` 的有效性.
		// 根据 `data_num` 的值判断它是在直接块, 一次间接块, 
		// 还是在二次间接块中.
		// 读相应的块并获取与数据块对应的逻辑块号并返回.

	int rd_blk(blkcnt_t blk_num, void *buf, size_t size)
		// 判断 blk_num, buf, size 的有效性.
		// 根据块的大小与磁盘块号计算块的起始偏移量,
		// 调用底层文件系统的读函数

	int wr_blk(blkcnt_t blk_num, void *buf, size_t size)
		// 判断 blk_num, buf, size 的有效性.
		// 根据块的大小与磁盘块号计算块的起始偏移量,
		// 调用底层文件系统的写函数.

	int path2dir_ent(const char *path, struct dir_entry *buf)
		// 先判断参数的有效性.
		// 若 path 的长度大于 1 且末尾有 '/', 则删除该字符,
		// 这是为了避免 "/usr/bin/vi/" 这种情况.
		// 从根结点开始查找, 遍历根目录的每一个目录项, 当找到期望
		// 的中间目录项时, 再从该目录项指向的 i 结点开始查找下一
		// 目标, 一直到查找成功或失败为止.

	int srch_dir_entry(struct m_inode *parent, const char *filename, struct dir_entry *ent)
		// 检查参数的有效性
		// 测试 parent 是否是一个目录
		// 寻索该目录下的每一个目录项, 查看是否有与 filename 相同的项,
		// 若找到, 将目录项复制到 ent 中, 并返回成功; 否则返回 失败.

	int add_dir_entry(struct m_inode *dir, const struct dir_entry *entry)
		// 检查参数的有效性
		// 测试 parent 是否是一个目录
		// 寻索该目录下的每一个目录项, 查看是否有与 filename 相同的项,
		// 若未找到, 则搜索一个空闲目录基, 空闲目录项指的是 `de_inum` 为 0 的项
		// 将新的目录项写到父目录的空闲项中, 并返回

