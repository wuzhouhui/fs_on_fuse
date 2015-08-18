# fuse_doc
fuse 培训资料

该文件系统没有考虑并发条件下的竞争, 所以请以单线程模式运行.

不解之处:
	调用 int open(pathname, flag) 时, 若 flag 设置的不正确 (例如将
	flag 设置为 0), 当 open() 返回后, fuse 会紧接着调用 flush()
	与 release(), release() 会释放文件描述符占用的项, 使得后续的以
	文件描述符作为参数的系统调用 (例如 write()) 失败.
