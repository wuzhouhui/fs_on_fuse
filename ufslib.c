#include "ufs.h"

extern struct m_super_block sb;

static blkcnt_t _dnum2znum(struct m_inode *, blkcnt_t, int);

/* determine whether the inode inum is valid */
static inline int is_ivalid(ino_t inum)
{
	return(inum >= 1 && inum < (sb.s_inode_blocks << (BLK_SIZE_SHIFT + 3)));
}

static inline int is_zvalid(blkcnt_t znum)
{
	return(znum >= 1 && znum <= sb.s_zone_blocks);
}

static inline int is_bvalid(blkcnt_t bnum)
{
	return(bnum >= sb.s_1st_inode_block &&
			bnum < (sb.s_1st_zone_block + sb.s_zone_blocks));
}

static inline int is_dvalid(blkcnt_t dnum)
{
	return(dnum >= 0  && dnum < (6 + ZNUM_PER_BLK + ZNUM_PER_BLK *
				ZNUM_PER_BLK));
}

static blkcnt_t creat_zone(struct m_inode *inode, blkcnt_t dnum)
{
	blkcnt_t ret = 0;
	log_msg("creat_zone called, inum = %u, dnum = %u", inode->i_ino, dnum);
	ret = _dnum2znum(inode, dnum, 1);
	log_msg("creat_zone return %u", ret);
	return(ret);
}

blkcnt_t dnum2znum(struct m_inode *inode, blkcnt_t dnum)
{
	blkcnt_t ret = 0;
	log_msg("dnum2znum called, inum = %u, dnum = %u", inode->i_ino, dnum);
	ret = _dnum2znum(inode, dnum, 0);
	log_msg("dnum2znum return %u", ret);
	return(ret);
}

/*
 * read super block from disk, return the disk file's file descriptor
 */
int read_sb(const char *disk_name)
{
	int	fd;

	if (disk_name == NULL || disk_name[0] == 0)
		return(-1);
	if ((fd = open(disk_name, O_RDWR)) < 0)
		err_sys("read_sb: open %s error.", disk_name);
	if (read(fd, &sb, sizeof(struct d_super_block)) !=
			sizeof(struct d_super_block))
		err_sys("read_sb: read %s error.", disk_name);
	if (sb.s_magic != MAGIC) {
		err_msg("filesystem magic %x isn't right.", sb.s_magic);
		return(-1);
	}
	return(fd);
}

ino_t new_inode(void)
{
	char	*p = sb.s_imap;
	int	n = sb.s_imap_blocks << BLK_SIZE_SHIFT;
	int	i, j;
	ino_t	ret = 0;

	log_msg("new_inode called");

	for (i = 0; i < n; i++) {
		if (p[i] == 0xff)
			continue;
		for (j = 0; j < 8; j++) {
			if (p[i] & (1 << j))
				continue;

			/* find a free inode bit */

			/* test if there is available inode */
			if (((i << 3) + j - 1) >=
					sb.s_inode_blocks * INUM_PER_BLK)
				goto out;

			p[i] |= 1 << j;
			ret = (i << 3) + j;
			goto out;
		}
	}
out:
	log_msg("new_inode return %d", (int)ret);
	return(ret);
}

int free_inode(ino_t inum)
{
	ino_t	n;
	int	ret;

	log_msg("free_inode called, inum = %u", inum);
	if (!is_ivalid(inum)) {
		log_msg("free_inode: inum out of range, inum = %u", inum);
		ret = -1;
		goto out;
	}

	n = inum >> 3;
	if ((sb.s_imap[n] & (1 << (inum & 7))) == 0) {
		log_msg("free_inode: inode %d already freed", (int)inum);
		ret = -1;
	} else {
		sb.s_imap[n] &= ~(1 << (inum & 7));
		ret = 0;
	}

out:
	log_msg("free_inode return %d", ret);
	return(ret);
}

int rd_inode(ino_t inum, struct d_inode *inode)
{
	int	ret;
	blkcnt_t bnum;
	char	buf[BLK_SIZE];

	log_msg("rd_inode called, inode num = %u", inum);
	if (inode == NULL) {
		ret = -EINVAL;
		log_msg("rd_inode: inode is NULL");
		goto out;
	}
	if (!is_ivalid(inum)) {
		ret = -EINVAL;
		log_msg("rd_inode: inode number %u out of range", inum);
		goto out;
	}
	if ((bnum = inum2bnum(inum)) == 0) {
		ret = -EINVAL;
		log_msg("rd_inode: block number of inode %u is zero", inum);
		goto out;
	}
	if ((ret = rd_blk(bnum, buf, sizeof(buf))) < 0) {
		log_msg("rd_inode: rd_blk error for block %u", bnum);
		goto out;
	}
	/* inode started from 1, so substract by 1 */
	*inode = ((struct d_inode *)buf)[(inum - 1) % INUM_PER_BLK];
	ret = 0;
out:
	log_msg("rd_inode return %d", ret);
	return(ret);
}

int wr_inode(const struct m_inode *inode)
{
	int	ret;
	blkcnt_t bnum;
	char	buf[BLK_SIZE];

	log_msg("wr_inode called");
	if (inode == NULL) {
		ret = -EINVAL;
		log_msg("wr_inode: inode is NULL");
		goto out;
	}
	if (!is_ivalid(inode->i_ino)) {
		ret = -EINVAL;
		log_msg("wr_inode: inode number %u out of range",
				inode->i_ino);
		goto out;
	}
	if ((bnum = inum2bnum(inode->i_ino)) == 0) {
		ret = -EINVAL;
		log_msg("wr_inode: block number of inode %u is zero",
				inode->i_ino);
		goto out;
	}
	if ((ret = rd_blk(bnum, buf, sizeof(buf))) < 0) {
		log_msg("wr_inode: rd_blk error for block %u", bnum);
		goto out;
	}
	/* inode started from 1, so substract by 1 */
	((struct d_inode *)buf)[(inode->i_ino - 1) % INUM_PER_BLK] =
		*(struct d_inode *)inode;
	if ((ret = wr_blk(bnum, buf, sizeof(buf))) < 0) {
		log_msg("wr_inode: wr_blk error for block %u", bnum);
		goto out;
	}
	ret = 0;
out:
	log_msg("wr_inode return %d", ret);
	return(ret);
}

blkcnt_t new_zone(void)
{
	char	*p = sb.s_zmap;
	int	n = sb.s_zmap_blocks << BLK_SIZE_SHIFT;
	int	i, j;
	char	buf[BLK_SIZE];
	ino_t	ret = 0;

	log_msg("new_zone called");

	/* n in bytes, not bit */
	for (i = 0; i < n; i++) {
		if (p[i] == 0xff)
			continue;
		for (j = 0; j < 8; j++) {
			if (p[i] & (1 << j))
				continue;

			/* find a free zone bit */

			/* test if there is available zone in disk */
			ret = (i << 3) + j;
			if ((ret - 1) >= sb.s_zone_blocks) {
				ret = 0;
				goto out;
			}
			p[i] |= 1 << j;

			memset(buf, 0, sizeof(buf));
			if (wr_zone(ret, buf, sizeof(buf)) < 0)
				log_msg("new_zone: wr_zone error");
			goto out;
		}
	}
out:
	log_msg("new_zone return %d", (int)ret);
	return(ret);
}

int free_zone(blkcnt_t znum)
{
	ino_t	n;
	int	ret;

	log_msg("free_zone called, znum = %u", znum);
	if (!is_zvalid(znum)) {
		log_msg("free_zone: znum out of range, znum = %u", znum);
		ret = -1;
		goto out;
	}

	n = znum >> 3;
	if ((sb.s_zmap[n] & (1 << (znum & 7))) == 0) {
		log_msg("free_zone: zone %d already freed", (int)znum);
		ret = -1;
	} else {
		sb.s_zmap[n] &= ~(1 << (znum & 7));
		ret = 0;
	}

out:
	log_msg("free_znode return %d", ret);
	return(ret);
}

int rd_zone(blkcnt_t zone_num, void *buf, size_t size)
{
	int	ret;
	blkcnt_t bnum;

	log_msg("rd_zone called, zone_num = %u", zone_num);
	if (!is_zvalid(zone_num)) {
		log_msg("rd_zone: znum out of range, znum = %u", zone_num);
		ret = -EINVAL;
		goto out;
	}
	if (buf == NULL) {
		log_msg("rd_zone: buf is NULL");
		ret = -EINVAL;
		goto out;
	}
	if (size != BLK_SIZE) {
		log_msg("rd_zone: size = %z, do not equals to %d",
				size, BLK_SIZE);
		ret = -EINVAL;
		goto out;
	}
	if ((bnum = znum2bnum(zone_num)) == 0) {
		log_msg("rd_zone: zone %u's block number is zero", zone_num);
		ret = -EINVAL;
		goto out;
	}
	ret = rd_blk(bnum, buf, size);
out:
	log_msg("rd_zone return %d", ret);
	return(ret);
}

int wr_zone(blkcnt_t zone_num, const void *buf, size_t size)
{
	int	ret;
	blkcnt_t bnum;

	log_msg("wr_zone called, zone_num = %u", zone_num);
	if (!is_zvalid(zone_num)) {
		log_msg("wr_zone: znum out of range, znum = %u", zone_num);
		ret = -1;
		goto out;
	}
	if (buf == NULL) {
		log_msg("wr_zone: buf is NULL");
		ret = -1;
		goto out;
	}
	if (size != BLK_SIZE) {
		log_msg("wr_zone: size = %z, do not equals to %d",
				size, BLK_SIZE);
		ret = -1;
		goto out;
	}
	if ((bnum = znum2bnum(zone_num)) == 0) {
		log_msg("wr_zone: zone %u's block number is zero", zone_num);
		ret = -1;
		goto out;
	}
	ret = wr_blk(bnum, buf, size);
out:
	log_msg("wr_zone return %d", ret);
	return(ret);
}

blkcnt_t inum2bnum(ino_t inum)
{
	blkcnt_t ret = 0;

	log_msg("inum2bnum called, inum = %u", inum);
	if (!is_ivalid(inum))
		log_msg("inum2bnum: inode %u is not valid", inum);
	else
		ret = (inum - 1) / INUM_PER_BLK + sb.s_1st_inode_block;
	log_msg("inum2bnum return %u", ret);
	return(ret);
}

blkcnt_t znum2bnum(blkcnt_t zone_num)
{
	blkcnt_t ret = 0;

	log_msg("znum2bnum called, zone_num = %u", zone_num);
	if (!is_zvalid(zone_num)) {
		log_msg("znum2bnum: zone %s is not valid", zone_num);
		goto out;
	}
	ret = (zone_num - 1) + sb.s_1st_zone_block;
out:
	log_msg("znum2bnum return %u", ret);
	return(ret);
}

static blkcnt_t _dnum2znum(struct m_inode *inode, blkcnt_t dnum, int creat)
{
	blkcnt_t ret = 0, znum;
	char	buf[BLK_SIZE];

	log_msg("_dnum2znum called, inum = %u, dnum = %u, creat = %d",
			inode->i_ino, dnum, creat);
	if (!is_ivalid(inode->i_ino)) {
		log_msg("_dnum2znum: inum %u is not valid", inode->i_ino);
		goto out;
	}
	if (!is_dvalid(dnum)) {
		log_msg("_dnum2znum: dnum %u is not valid",
				dnum);
		goto out;
	}

	if (dnum < 6) {
		ret = inode->i_zones[dnum];
		if (ret == 0 && creat) {
			if ((ret = new_zone()) == 0)
				goto out;
			inode->i_zones[dnum] = ret;
			if (wr_inode(inode) < 0) {
				log_msg("_dnum2znum: wr_inode error");
				ret = 0;
				goto out;
			}
		}
		goto out;
	}

	dnum -= 6;
	if (dnum < ZNUM_PER_BLK) {
		ret = inode->i_zones[6];
		if (ret == 0 && creat) {
			if ((ret = new_zone()) == 0) {
				log_msg("_dnum2znum: new_zone return 0");
				goto out;
			}
			inode->i_zones[6] = ret;
			if (wr_inode(inode) < 0) {
				log_msg("_dnum2znum: wr_inode error");
				ret = 0;
				goto out;
			}
		}
		if (inode->i_zones[6] == 0)
			goto out;
		ret = ((blkcnt_t *)buf)[dnum];
		if (ret == 0 && creat) {
			if ((ret = new_zone()) == 0)
				goto out;
			((blkcnt_t *)buf)[dnum] = ret;
			if (wr_zone(inode->i_zones[6], buf, sizeof(buf)) < 0) {
				log_msg("_dnum2znum: wr_inode error");
				ret = 0;
				goto out;
			}
		}
		goto out;
	}

	/* double indirect */
	dnum -= ZNUM_PER_BLK;
	ret = inode->i_zones[7];
	if (ret == 0 && creat) {
		if ((ret = new_zone()) == 0) {
			log_msg("_dnum2znum: new_inode return 0");
			goto out;
		}
		inode->i_zones[7] = ret;
		if (wr_inode(inode) < 0) {
			log_msg("_dnum2znum: wr_inode error for inode"
					" %u", inode->i_ino);
			goto out;
		}
	}
	if (inode->i_zones[7] == 0)
		goto out;
	if (rd_zone(inode->i_zones[7], buf, sizeof(buf)) < 0) {
		log_msg("dnum2znum: rd_zone error for %u",
				inode->i_zones[7]);
		ret = 0;
		goto out;
	}
	ret = ((blkcnt_t *)buf)[dnum / ZNUM_PER_BLK];
	if (ret == 0 && creat) {
		if ((ret = new_zone()) == 0) {
			log_msg("_dnum2znum: new_inode return 0 for "
					"1st level of double");
			goto out;
		}
		((blkcnt_t *)buf)[dnum / ZNUM_PER_BLK] = ret;
		if (wr_zone(inode->i_zones[7], buf, sizeof(buf)) < 0) {
			log_msg("_dnum2znum: wr_zone error");
			goto out;
		}
	}
	if (ret == 0)
		goto out;
	if (rd_zone(ret, buf, sizeof(buf)) < 0) {
		log_msg("_dnum2znum: rd_zone error");
		goto out;
	}
	znum = ret;
	ret = ((blkcnt_t *)buf)[dnum % ZNUM_PER_BLK];
	if (ret == 0 && creat) {
		if ((ret = new_zone()) == 0) {
			log_msg("_dnum2znum: new_inode return 0 for "
					"2nd level of double");
			goto out;
		}
		((blkcnt_t *)buf)[dnum % ZNUM_PER_BLK] = ret;
		if (wr_zone(znum, buf, sizeof(buf)) < 0) {
			log_msg("_dnum2znum: wr_zone error");
			goto out;
		}
	}
out:
	log_msg("dnum2znum return %u", ret);
	return(ret);
}

int rd_blk(blkcnt_t blk_num, void *buf, size_t size)
{
	int	ret = 0;

	log_msg("rd_blk called, blk_num = %u", blk_num);
	if (!is_bvalid(blk_num)) {
		log_msg("rd_blk: block num %u is not valid", blk_num);
		ret = -EINVAL;
		goto out;
	}
	if (buf == NULL) {
		log_msg("rd_blk: buf is NULL");
		ret = -EINVAL;
		goto out;
	}
	if (size != BLK_SIZE) {
		log_msg("rd_blk: size = %u", size);
		ret = -EINVAL;
		goto out;
	}
	if (pread(sb.s_fd, buf, size, blk_num << BLK_SIZE_SHIFT) != size) {
		log_ret("rd_blk: pread error");
		ret = -errno;
		goto out;
	}
	ret = 0;
out:
	log_msg("rd_blk return %d", ret);
	return(ret);
}

int wr_blk(blkcnt_t blk_num, const void *buf, size_t size)
{
	int	ret = -1;

	log_msg("wr_blk called, blk_num = %u", blk_num);
	if (!is_bvalid(blk_num)) {
		log_msg("wr_blk: block num %u is not valid", blk_num);
		goto out;
	}
	if (buf == NULL) {
		log_msg("wr_blk: buf is NULL");
		goto out;
	}
	if (size != BLK_SIZE) {
		log_msg("wr_blk: size = %u", size);
		goto out;
	}
	if (pwrite(sb.s_fd, buf, size, blk_num << BLK_SIZE_SHIFT) != size) {
		log_ret("wr_blk: pwrite error");
		goto out;
	}
	ret = 0;
out:
	log_msg("wr_blk return %d", ret);
	return(ret);
}

int dir2i(const char *dirpath, struct m_inode *dirinode)
{
	int	ret;

	log_msg("dir2i called, dirpath = %s", (dirpath == NULL ? "NULL" :
				dirpath));
	if ((ret = path2i(dirpath, dirinode)) < 0) {
		log_msg("dir2i: path2i error");
		goto out;
	}
	if (!UFS_ISDIR(dirinode->i_mode)) {
		ret = -ENOTDIR;
		goto out;
	}
	ret = 0;
out:
	log_msg("dir2i return %d", ret);
	return(ret);
}

int add_entry(struct m_inode *dirinode, const char *file,
		struct dir_entry *entry)
{
	int	ret, i;
	struct m_inode inode;
	blkcnt_t dnum, znum;
	char	buf[BLK_SIZE];
	struct dir_entry *de;

	log_msg("add_entry called, adding %s in %u",
			(file == NULL ? "NULL" : file), dirinode->i_ino);
	if (dirinode == NULL || !is_ivalid(dirinode->i_ino)) {
		log_msg("add_entry: dirinode not valid");
		ret = -EINVAL;
		goto out;
	}
	if (file == NULL) {
		log_msg("add_entry: file is NULL");
		ret = -EINVAL;
		goto out;
	}
	if (entry == NULL) {
		log_msg("add_entry: entry is NULL");
		ret = -EINVAL;
		goto out;
	}

	/*
	 * allocate a new inode and initialize it,
	 * then write it.
	 */
	if ((entry->de_inum = new_inode()) == 0) {
		log_msg("add_entry: new_inode return zero");
		ret = -ENOSPC;
		goto out;
	}
	memset(&inode, 0, sizeof(inode));
	inode.i_nlink = 1;
	inode.i_ino = entry->de_inum;
	inode.i_uid = getuid();
	inode.i_gid = getgid();
	if ((ret = wr_inode(&inode)) < 0) {
		log_msg("add_entry: wr_inode error");
		goto out;
	}

	/*
	 * find a available entry in parent directory. write new entry and
	 * update parent directory's inode.
	 */
	dnum = 0;
	while (1) {
		if ((znum = creat_zone(dirinode, dnum)) == 0) {
			log_msg("add_entry: creat_zone return 0 for %u",
					znum);
			ret = -ENOSPC;
			goto out;
		}
		if ((ret = rd_zone(znum, buf, sizeof(buf))) < 0) {
			log_msg("add_entry: rd_zone error");
			goto out;
		}
		for (de = (struct dir_entry *)buf, i = 0;
				i < ENTRYNUM_PER_BLK; i++)
			if (de[i].de_inum == 0)
				break;
		if (i < ENTRYNUM_PER_BLK)
			break;
	}
	/* find a available entry in directory */
	de[i].de_inum = entry->de_inum;
	strncpy(de[i].de_name, file, NAME_LEN);
	de[i].de_name[NAME_LEN] = 0;
	memcpy(entry, &de[i], sizeof(de[i]));
	if ((ret = wr_zone(znum, buf, sizeof(buf))) < 0) {
		log_msg("add_entry: wr_zone error");
		goto out;
	}
	/*
	 * update parent directory's inode.
	 */
	dirinode->i_size += sizeof(de[i]);
	if ((ret = wr_inode(dirinode)) < 0) {
		log_msg("add_entry: wr_inode error");
		goto out;
	}
	ret = 0;

out:
	if (ret && entry->de_inum)
		free_inode(entry->de_inum);
	log_msg("add_entry return %d", ret);
	return(ret);
}

int path2i(const char *path, struct m_inode *inode)
{
	struct dir_entry ent;
	char	file[NAME_LEN + 1];
	int	i, start, ret;

	log_msg("path2i called, path = %s", (path == NULL ? "NULL" :
				path));
	if (path == NULL) {
		ret = -EINVAL;
		goto out;
	}
	inode->i_ino = ROOT_INO;
	if ((ret = rd_inode(ROOT_INO, (struct d_inode *)inode)) < 0) {
		log_msg("path2i: rd_inode error");
		goto out;
	}

	i = 1;
	while (path[i]) {
		if (path[i] == '/')
			start = ++i;
		else
			start = i++;
		while (path[i] != '/' && path[i])
			i++;
		if (path[start] == 0) /* /usr/bin/vim/ */
			break;
		memcpy(file, path + start, i - start);
		file[i - start] = 0;
		if ((ret = find_entry(inode, file, &ent)) < 0) {
			log_msg("path2i: find_entry error for %s", file);
			goto out;
		}
		inode->i_ino = ent.de_inum;
		ret = rd_inode(inode->i_ino, (struct d_inode *)inode);
		if (ret  < 0) {
			log_msg("path2i: rd_inode error");
			goto out;
		}
	}
	ret = 0;

out:
	log_msg("path2i return %d, inum = %u", ret, inode->i_ino);
	return(ret);
}

int find_entry(struct m_inode *par, const char *file,
		struct dir_entry *res)
{
	off_t	i, j;
	int	ret;
	blkcnt_t dnum, znum;
	char	buf[BLK_SIZE];
	struct dir_entry *de;

	log_msg("find_entry called");
	if (par == NULL || file == NULL || res == NULL)
		log_msg("find_entry: arguments is NULL");
	log_msg("find_entry: par->i_ino = %u, file = %s", par->i_ino,
			file);

	if (!UFS_ISDIR(par->i_mode)) {
		ret = -ENOTDIR;
		goto out;
	}

	i = 0;
	dnum = 0;
	while (i < par->i_size) {
		if ((znum = dnum2znum(par, dnum)) == 0) {
			log_msg("find_entry: dnum2znum return "
					"zero for data %u", dnum);
			goto out;
		}
		if (rd_zone(znum, buf, sizeof(buf)) < 0) {
			log_msg("find_entry: rd_zone error for data"
					" %u", znum);
			goto out;
		}
		for (de = (struct dir_entry *)buf, j = 0;
				j < ENTRYNUM_PER_BLK &&
				i < par->i_size; j++) {
			if (de[j].de_inum == 0)
				continue;
			i += sizeof(*de);
			if (strncmp(de[j].de_name, file, NAME_LEN) == 0) {
				res->de_inum = de[j].de_inum;
				strncpy(res->de_name, de[j].de_name,
						NAME_LEN);
				res->de_name[NAME_LEN] = 0;
				ret = 0;
				goto out;
			}
		}
		dnum++;
	}
	ret = -ENOENT;
out:
	log_msg("find_entry return %d", ret);
	return(ret);
}
/*
 * convert file mode and permission from ufs to
 * UNIX.
 */
mode_t conv_fmode(mode_t mode)
{
	mode_t ret = mode & 0x1ff;
	if (UFS_ISREG(mode))
		ret |= S_IFREG;
	if (UFS_ISDIR(mode))
		ret |= S_IFDIR;
	return(ret);
}
