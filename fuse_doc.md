# fuse 原理学习与理解
## 1 [fuse-1.3 的工作方式](http://fuse.sourceforge.net/doxygen/index.html#section1)
![fuse 与其他模块的关系](./FUSE_structure.svg.png)  
图 1-1, 来源 http://en.wikipedia.org/wiki/File:FUSE_structure.svg  

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
这些要被写入的数据会通过 "请求队列" 与 `request_send()` 返回给系统调用.

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
#### 用户空间文件系统
一种文件系统, 它的数据与元数据由一个普通的用户态进程提供. 该文件系统可以
通过内核接口 (系统调用) 访问.

#### 文件系统守护进程
提供文件系统的数据与元数据的进程

#### 非特权挂载 (或 用户挂载)
一个被非特权用户 (非 root 用户) 挂载的用户空间文件系统. 文件系统守护进程的
特权级是执行挂载操作的用户的特权级. **注意**: 这与 `/etc/fstab` 的 `user`
选项不同, 该选项允许一个普通用户挂载文件系统, 在这里我们不讨论
`/etc/fstab`.

#### 挂载属主
执行挂载操作的用户.

#### 用户
执行文件系统操作的用户.

### 2.2 什么是 FUSE?
FUSE 是一个用户空间文件系统框架. 它由一个内核模块 (`fuse.ko`), 用户空间 
库函数 (`libfuse.*`), 一个挂载实用程序 (`fusermount`) 组成.

FUSE 一个最重要的特点是允许安全的非特权挂载. 这个特点开启了文件系统
的一个新的用法. 一个好的例子是 sshfs: 一个安全的, 使用 sftp 协议的网络
文件系统.

用户空间的库函数与实用程序可以从 FUSE 的首页获取 http://fuse.sourceforge.net

### 2.3 文件系统类型
传递给 `mount`(2) 的文件系统类型参数可以是以下两种:
* `fuse`  
这是挂载 FUSE  文件系统的通常方式. 传递给 `mount` 系统调用的第一个参数可
以是任意的字符串, 内核不会对该字符串进行解释.
* `fuseblk`  
文件系统是基于块设备的. 传递给 `mount` 系统调用的第一个参数被内核解释
成设备名.

### 2.4 挂载参数
* `fd=N`  
用于用户空间文件系统与内核之间通信的文件描述符, 该文件描述符必须事先通过
打开 `/dev/fuse` 获取.

* `rootmode=M`  
文件系统根目录的访问权限, 以 8 进制表示.

* `user_id=N`  
挂载属主的用户 ID.

* `group_id=N`  
挂载属主的组 ID.

* `default_permissions`   
FUSE 默认不会检查文件访问权限, 文件系统可以自由地实现自身的访问策略, 或者 
将工作交给底层的文件访问机制 (例如, 网络文件系统). 这个选项开启权限检查,
根据文件的访问权限设置来限制用户的访问. 这个选项通常与 `allow_other` 选项 
配合使用.

* `allow_other`  
这个选项解除了 "只有挂载文件系统的用户才有权利访问文件" 的限制. 默认该
选项只对 root 开放, 但这个限制可以通过 (用户空间的) 配置文件来移除.

* `max_read=N`  
`read` 操作一次所能读取的最大字节数, 默认值是无限制. **注意**, 无论如何,
`read` 请求的大小最多被限制为 32 页 (在 i386 架构中是 128 字节).

* `blksize=N`  
设置文件系统的块大小. 默认值是 512. 这个选项只对 `fuseblk` 类型的文件
系统有效.

### 2.5 控制文件系统
这里有一个 FUSE 的控制文件系统, 可以通过下面这个命令挂载:

	mount -t fusectl none /sys/fs/fuse/connections

将它挂载到 `/sys/fs/fuse/connections` 是为了向下兼容.

在控制文件系统中, 每一个连接都对应有一个目录, 目录用互不相同的数字命名.

对每一个连接, 它对应的目录中都至少存在这两个文件:
* `waiting`  
等待被传送到用户空间, 或等待被文件系统守护进程处理的请求个数. 如果文件
系统没有活动在进行, 而 `waiting` 的内容不为零, 那么肯定是文件系统被挂
起, 或者是发生了死锁.

* `abort`  
往这个文件写任何数据都会终止文件系统连接, 此时等待中的请求都会被终止并返回
一个错误, 新请求也会如此.

只有挂载属主才会写或读这些文件.

### 2.6 中断文件系统操作
如果一个正在发送 FUSE 文件系统请求的进程被某个信号中断了, 会发生下面几件事:

1. 如果请求还未被发送至用户空间**并且**信号是致命的 (`SIGKILL`, 或者是未捕
捉的致命信号), 那么请求就会被移出队列, 并马上返回.
2. 如果请求还未被发送至用户空间**并且**信号是非致命的, 那么就会为该请求设置
一个 `interrupted` 标记. 当请求成功传送到用户空间, 并且这个标记被设置, 一
个 `INTERRUPT` 请求会被排入队列.
3. 如果请求已经被传送至用户空间, 那么一个 `INTERRUPT` 请求会被排入队列.

`INTERRUPT` 请求优先于其他请求, 所以用户空间文件系统会优先接收已排队的
`INTERRUPT` 请求.

用户空间文件系统可以完全忽略 `INTERRUPT` 请求, 也可以向请求的 _来源_ 发送
一个回复来提醒它发生了一个中断, 并将 `errno` 设置为 `EINTR`.

在处理原始请求与 `INTERRUPT` 之间可能存在竞争状态, 这时候有两种可能:

1. `INTERRUPT` 请求在原始请求之前被处理.
2. `INTERRUPT` 请求在原始请求得到响应之后被处理.

如果文件系统无法找到原始请求, 它应该等待一定的时间, 和 (或) 等待一定数量
的新请求到来, 在这之后, 它应该向 `INTERRUPT` 请求返回 `EAGAIN` 错误. 在第 
1 种情况 (指上面提到的两种可能的竞争状态中的第1种) 中, `INTERRUPT` 请求会被 
重新排队, 在第 2 种情况下, `INTERRUPT` 
的回复会被忽略.

### 2.7 中断一个文件系统连接
文件系统无法响应是有可能的. 可能的原因包括:

1. 有缺陷的用户空间文件系统实现.
*  网络连接断开.
*  偶然性的死锁.
*  恶意的死锁.

(关于第 3 点与第 4 点更多的信息请看后面的小节)

在任何一种情况下, 中断文件系统连接都是一种比较好的解决方案. 中断连接有下
面几种方案:

* 杀死文件系统守护进程, 对第 1 种与第2 种情况有效.
* 杀死文件系统守护进程与所有的文件系统用户. 对所有情况有效, 除了某些恶意
的死锁.
* 强制卸载 (`umount -f`). 对所有情况有效, 但前提是文件系统仍然处于挂接
状态 (未被懒惰卸载).
* 通过 FUSE 控制文件系统 终止. 这是最有效的方案, 总能奏效.

### 2.8 非特权挂载如何起作用
因为 `mount()` 系统调用是一个特权操作, 所以需要 `fusermount` 的帮助,
`fusermount` 的 SUID 为 root.

提供非特权挂载的目的是防止挂载属主利用该能力来危害系统. 为了做到这一点,
下面这些需求是显然的:

1. 挂载属主不能在被挂载文件系统的帮助下, 提升自己的权限.
* 挂载属主不能非法获取其他用户或超级用户的进程信息.
* 挂载属主不能向其他用户或超级用户的进程施加未期望的行为.

### 2.9 如何满足需求?
* A) 挂载属主可以通过下面2种方式之一提升权限:

  + 1) 创建一个包含设备文件的文件系统, 然后打开该设备文件.  
  + 2) 创建一个包含 SUID 或 SGID 程序的文件系统, 然后运行该程序.  

解决方案是禁止打开设备文件, 在执行程序时忽略 SGID 与 SUID.
为了达到这两个 目的, 对于非特权挂载, `fusermount` 总是为 `mount`
设置添加 `nosuid` 与 `nodev` 这两个选项.

* B) 如果有另外一个用户正在访问文件系统的文件或目录, 此时, 为请求提供服务的
文件系统守护进程会记录下操作的执行顺序与时间. 这些信息对挂载属主来说是不
可访问的, 所以如果挂载属主知晓了其他用户的操作信息, 这种现象叫做
**信息泄漏**.

这个问题的解决方案在在 C) 的 2) 中介绍.

* C) 挂载属主可以用多种方式向其他用户进程施加未期望的行为, 例如:

  + 1) 在一个文件或目录之上挂载文件系统, 而这个文件或目录对挂载属主来说
  是不可修改的 (或者只能做非常有限的修改).  
这一点可以用 `fusermount` 解决. `fusermount` 检查挂载点的访问权限, 只
有在挂载属主有权利对挂载点作无限制的修改 (对挂载点拥有写权限, 挂载点没有
设置粘置位) 的前提下, 才允许挂载.

  + 2) 即使情况 1) 被解决了, 挂载属主依然有能力改变其他用户进程的行为:

    - i) 通过发起一个针对用户或整个系统的 DoS (拒绝服务攻击),
    挂载属主可以减慢或无限期地推迟一个文件系统操作. 一个设置了 SUID
    的程序锁住了一个系统文件,
    然后该程序访问挂载属主的文件系统中的一个文件, 而这个访问可以被暂停, 于是
    这就造成了系统文件被永远地锁住.

    - ii) 用户可以创建大小不受限制的文件或目录, 或者是层次非常深的目录, 
    这会造成磁盘空间与内存被消耗殆尽, 同样会造成 DoS.

解决 C/2 (以及 B) 的办法是禁止进程访问不受挂载属主控制的文件系统.
因为如果挂载属主可以 `ptrace` 一个进程, 它可以在不使用 FUSE 挂载的前提下,
完成上面提到的那些事情. 同样的条件也可以运用在检查一个进程是否允许访问
文件系统.

**注意**, 使用 `ptrace` 检查对 C/2/i 来说并不是必须的, 只需要作这个检查就
足够了: 查看挂载属主是否有足够的权限向访问文件系统的进程发送信号. 这是因为
`SIGSTOP` 可以达到类似的效果.

### 2.10 我认为这些限制是不可接受的?
如果系统管理员足够相信用户 (或者可以通过某些方式来保证), 确保系统进程永远
不会进入一个非特权挂载点, 那么可以使用 `user_allow_other`
配置选项来放松最后一
条限制条件. 如果这个配置选项被设置了, 那么挂载属主可以设置 `allow_other`
选项来禁止掉对其他用户进程的检查.

### 2.11 内核--用户空间 接口
下面这张图展示了一个文件系统操作 (在这里是 `unlink`) 在 FUSE 中如何执行

**注意**, 这里的讨论是经过简化了的.

    |  "rm /mnt/fuse/file"               |  FUSE filesystem daemon
    |                                    |
    |                                    |  >sys_read()
    |                                    |    >fuse_dev_read()
    |                                    |      >request_wait()
    |                                    |        [sleep on fc->waitq]
    |                                    |
    |  >sys_unlink()                     |
    |    >fuse_unlink()                  |
    |      [get request from             |
    |       fc->unused_list]             |
    |      >request_send()               |
    |        [queue req on fc->pending]  |
    |        [wake up fc->waitq]         |        [woken up]
    |        >request_wait_answer()      |
    |          [sleep on req->waitq]     |
    |                                    |      <request_wait()
    |                                    |      [remove req from fc->pending]
    |                                    |      [copy req to read buffer]
    |                                    |      [add req to fc->processing]
    |                                    |    <fuse_dev_read()
    |                                    |  <sys_read()
    |                                    |
    |                                    |  [perform unlink]
    |                                    |
    |                                    |  >sys_write()
    |                                    |    >fuse_dev_write()
    |                                    |      [look up req in fc->processing]
    |                                    |      [remove from fc->processing]
    |                                    |      [copy write buffer to req]
    |          [woken up]                |      [wake up req->waitq]
    |                                    |    <fuse_dev_write()
    |                                    |  <sys_write()
    |        <request_wait_answer()      |
    |      <request_send()               |
    |      [add request to               |
    |       fc->unused_list]             |
    |    <fuse_unlink()                  |
    |  <sys_unlink()                     |

给一个 FUSE 文件系统造成死锁有几种方式, 因为我们讨论的是非特权的用户空间
程序, 关于这些我们必须做些什么.

#### 情景 1 -- 简单的死锁

    |  "rm /mnt/fuse/file"               |  FUSE filesystem daemon
    |                                    |
    |  >sys_unlink("/mnt/fuse/file")     |
    |    [acquire inode semaphore        |
    |     for "file"]                    |
    |    >fuse_unlink()                  |
    |      [sleep on req->waitq]         |
    |                                    |  <sys_read()
    |                                    |  >sys_unlink("/mnt/fuse/file")
    |                                    |    [acquire inode semaphore
    |                                    |     for "file"]
    |                                    |    *DEADLOCK*

解决方案是终止文件系统.

#### 情景 2 -- 狡猾的死锁
这一个需要精心制作的文件系统. 这是上面情景的变形, 只有对文件系统的回调不是
显式的, 但这里的死锁是由页错误造成的.

    |  Kamikaze filesystem thread 1      |  Kamikaze filesystem thread 2
    |                                    |
    |  [fd = open("/mnt/fuse/file")]     |  [request served normally]
    |  [mmap fd to 'addr']               |
    |  [close fd]                        |  [FLUSH triggers 'magic' flag]
    |  [read a byte from addr]           |
    |    >do_page_fault()                |
    |      [find or create page]         |
    |      [lock page]                   |
    |      >fuse_readpage()              |
    |         [queue READ request]       |
    |         [sleep on req->waitq]      |
    |                                    |  [read request to buffer]
    |                                    |  [create reply header before addr]
    |                                    |  >sys_write(addr - headerlength)
    |                                    |    >fuse_dev_write()
    |                                    |      [look up req in fc->processing]
    |                                    |      [remove from fc->processing]
    |                                    |      [copy write buffer to req]
    |                                    |        >do_page_fault()
    |                                    |           [find or create page]
    |                                    |           [lock page]
    |                                    |           * DEADLOCK *

解决方案与上面的相同.

另一个问题是, 当写缓冲区正在被复制给请求, 那么请求不能被中断或终止. 这是因 
为在请求返回之后, 复制操作的目标地址可能不再有效.

问题的解决办法是令复制操作成为一个原子操作, 当属于写缓冲区的页被 
`get_user_pages()` 弄错时, 允许中断. 标志 `req->flag` 指出复制正在发生,
终止操作会一直延迟到该标志被解除为止.

## 3 我理解的 fuse 工作方式
我们以图 1-1 为例, 说明一下 fuse 的工作流程.

1. 用户在 shell 中输入 `ls` 命令, shell `fork()` 一个子进程执行 `ls`
* `ls` 调用 `stat` (glibc)
* glibc  的 `stat` 调用系统调用 `sys_stat`
* fuse 内核模块事先在 VFS 中注册了函数接口, 所以 VFS 收到 fuse 文件系统的 
`stat` 请求后, 会交由 fuse 模块处理.
* fuse 模块收到 `stat` 请求, 将请求写至 `/dev/fuse`
* 用户文件系统守护进程 (在图 1-1 是 `hello`) 不断读取 `/dev/fuse`, 若发现
是发送给自己的请求, 则处理该请求.
* `hello` 文件系统根据用户在 `struct fuse_operations` 注册的回调函数, 调用 
相应的 `stat` 实现.
* `hello` 文件系统处理完 `stat` 请求后, 将处理结果写回给 `/dev/fuse`
* fuse 内核模块从 `/dev/fuse` 读取到 `stat` 请求的处理结果, 将结果返回给
VFS 
* VFS 再将结果依次上传, 最后显示在终端中.

由此可见, fuse 内核模块负责与内核交互, 而用户文件系统通过 fuse 库, 利用
`/dev/fuse` 与 fuse 内核模块交互.
