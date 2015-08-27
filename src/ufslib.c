#include "ufs.h"

extern struct ufs_msuper_block sb;

static unsigned int _ufs_dnum2znum(struct ufs_minode *, unsigned int, int);

/* determine whether the inode inum is valid */
static inline int ufs_is_ivalid(unsigned int inum)
{
	return(inum >= 1 && inum < (sb.s_inode_blocks << (UFS_BLK_SIZE_SHIFT + 3)));
}

static inline int ufs_is_zvalid(unsigned int znum)
{
	return(znum >= 1 && znum <= sb.s_zone_blocks);
}

static inline int ufs_is_bvalid(unsigned int bnum)
{
	return(bnum >= sb.s_1st_inode_block &&
			bnum < (sb.s_1st_zone_block + sb.s_zone_blocks));
}

static inline int ufs_is_dvalid(unsigned int dnum)
{
	return(dnum >= 0  && dnum < (6 + UFS_ZNUM_PER_BLK + UFS_ZNUM_PER_BLK *
				UFS_ZNUM_PER_BLK));
}

static int ufs_free_ind(unsigned int znum)
{
	int	ret, i;
	char	buf[UFS_BLK_SIZE];
	unsigned int *zptr;

	log_msg("ufs_free_ind called, znum = %d", (unsigned int)znum);
	if (znum == 0) {
		ret = 0;
		goto out;
	}
	if ((ret = ufs_rd_zone(znum, buf, sizeof(buf))) < 0) {
		log_msg("ufs_free_ind: ufs_rd_zone error for %d", (int)znum);
		goto out;
	}

	zptr = (unsigned int *)buf;
	for (i = 0; i < UFS_ZNUM_PER_BLK; i++) {
		if (zptr[i] == 0)
			continue;
		if ((ret = ufs_free_zone(zptr[i])) < 0) {
			log_msg("ufs_free_ind: ufs_free_zone error for znum"
					" %u", (unsigned int)zptr[i]);
			goto out;
		}
	}

	if ((ret = ufs_free_zone(znum)) < 0) {
		log_msg("ufs_free_ind: ufs_free_zone error for znum %u",
				(unsigned int)znum);
		goto out;
	}
	ret = 0;

out:
	log_msg("ufs_free_ind return %d");
	return(ret);
}

static int ufs_free_dind(unsigned int znum)
{
	int	ret, i;
	char	buf[UFS_BLK_SIZE];
	unsigned int *zptr;

	log_msg("ufs_free_dind called, znum = %u", (unsigned int)znum);
	if (znum == 0) {
		ret = 0;
		goto out;
	}

	if ((ret = ufs_rd_zone(znum, buf, sizeof(buf))) < 0) {
		log_msg("ufs_free_dind: ufs_rd_zone error for znum %u",
				(unsigned int)znum);
		goto out;
	}
	zptr = (unsigned int *)buf;
	for (i = 0; i < UFS_ZNUM_PER_BLK; i++) {
		if (zptr[i] == 0)
			continue;
		if ((ret = ufs_free_ind(zptr[i])) < 0) {
			log_msg("ufs_free_dind: ufs_free_ind error for znum"
					" %u", (unsigned int)zptr[i]);
			goto out;
		}
	}

	if ((ret = ufs_free_zone(znum)) < 0) {
		log_msg("ufs_free_dind: ufs_free_zone error for znum %u",
				(unsigned int)znum);
		goto out;
	}
	ret = 0;

out:
	log_msg("ufs_free_dind return %d", ret);
	return(ret);
}

unsigned int ufs_creat_zone(struct ufs_minode *inode, unsigned int dnum)
{
	unsigned int ret = 0;
	log_msg("ufs_creat_zone called, inum = %d, dnum = %d",
			(int)inode->i_ino, (int)dnum);
	ret = _ufs_dnum2znum(inode, dnum, 1);
	log_msg("ufs_creat_zone return %d", (int)ret);
	return(ret);
}

unsigned int ufs_dnum2znum(struct ufs_minode *inode, unsigned int dnum)
{
	unsigned int ret = 0;
	log_msg("ufs_dnum2znum called, inum = %u, dnum = %u",
			(unsigned int)inode->i_ino, (unsigned int)dnum);
	ret = _ufs_dnum2znum(inode, dnum, 0);
	log_msg("ufs_dnum2znum return %u", (unsigned int)ret);
	return(ret);
}

/*
 * read super block from disk, return the disk file's file descriptor
 */
int ufs_read_sb(const char *disk_name)
{
	int	fd;

	if (disk_name == NULL || disk_name[0] == 0)
		return(-1);
	if ((fd = open(disk_name, O_RDWR)) < 0)
		err_sys("ufs_read_sb: open %s error.", disk_name);
	if (read(fd, &sb, sizeof(struct ufs_dsuper_block)) !=
			sizeof(struct ufs_dsuper_block))
		err_sys("ufs_read_sb: read %s error.", disk_name);
	if (sb.s_magic != UFS_MAGIC) {
		err_msg("filesystem magic %x isn't right.", sb.s_magic);
		return(-1);
	}
	return(fd);
}

unsigned int ufs_new_inode(void)
{
	char	*p = sb.s_imap;
	int	n = sb.s_imap_blocks << UFS_BLK_SIZE_SHIFT;
	int	i, j;
	unsigned int	ret = 0;

	log_msg("ufs_new_inode called");

	for (i = 0; i < n; i++) {
		if (p[i] == 0xff)
			continue;
		for (j = 0; j < 8; j++) {
			if (p[i] & (1 << j))
				continue;

			/* find a free inode bit */

			/* test if there is available inode */
			if (((i << 3) + j - 1) >=
					sb.s_inode_blocks * UFS_INUM_PER_BLK)
				goto out;

			p[i] |= 1 << j;
			ret = (i << 3) + j;
			goto out;
		}
	}
out:
	log_msg("ufs_new_inode return %d", (int)ret);
	return(ret);
}

int ufs_free_inode(unsigned int inum)
{
	unsigned int	n;
	int	ret;

	log_msg("ufs_free_inode called, inum = %u", (unsigned int)inum);
	if (!ufs_is_ivalid(inum)) {
		log_msg("ufs_free_inode: inum out of range, inum = %u",
				(unsigned int)inum);
		ret = -EINVAL;
		goto out;
	}

	n = inum >> 3;
	if ((sb.s_imap[n] & (1 << (inum & 7))) == 0) {
		log_msg("ufs_free_inode: inode %d already freed", (int)inum);
		ret = -EAGAIN;
	} else {
		sb.s_imap[n] &= ~(1 << (inum & 7));
		ret = 0;
	}

out:
	log_msg("ufs_free_inode return %d", ret);
	return(ret);
}

int ufs_rd_inode(unsigned int inum, struct ufs_dinode *inode)
{
	int	ret;
	unsigned int bnum;
	char	buf[UFS_BLK_SIZE];

	log_msg("ufs_rd_inode called, inode num = %u", (unsigned int)inum);
	if (inode == NULL) {
		ret = -EINVAL;
		log_msg("ufs_rd_inode: inode is NULL");
		goto out;
	}
	if (!ufs_is_ivalid(inum)) {
		ret = -EINVAL;
		log_msg("ufs_rd_inode: inode number %u out of range",
				(unsigned int)inum);
		goto out;
	}
	if ((bnum = ufs_inum2bnum(inum)) == 0) {
		ret = -EINVAL;
		log_msg("ufs_rd_inode: block number of inode %u is zero",
				(unsigned int)inum);
		goto out;
	}
	if ((ret = ufs_rd_blk(bnum, buf, sizeof(buf))) < 0) {
		log_msg("ufs_rd_inode: ufs_rd_blk error for block %u",
				(unsigned int)bnum);
		goto out;
	}
	/* inode started from 1, so substract by 1 */
	*inode = ((struct ufs_dinode *)buf)[(inum - 1) % UFS_INUM_PER_BLK];
	ret = 0;
out:
	log_msg("ufs_rd_inode return %d", ret);
	return(ret);
}

int ufs_wr_inode(const struct ufs_minode *inode)
{
	int	ret;
	unsigned int bnum;
	char	buf[UFS_BLK_SIZE];

	log_msg("ufs_wr_inode called");
	if (inode == NULL) {
		ret = -EINVAL;
		log_msg("ufs_wr_inode: inode is NULL");
		goto out;
	}
	if (!ufs_is_ivalid(inode->i_ino)) {
		ret = -EINVAL;
		log_msg("ufs_wr_inode: inode number %u out of range",
				(unsigned int)inode->i_ino);
		goto out;
	}
	if ((bnum = ufs_inum2bnum(inode->i_ino)) == 0) {
		ret = -EINVAL;
		log_msg("ufs_wr_inode: block number of inode %u is zero",
				(unsigned int)inode->i_ino);
		goto out;
	}
	if ((ret = ufs_rd_blk(bnum, buf, sizeof(buf))) < 0) {
		log_msg("ufs_wr_inode: ufs_rd_blk error for block %u",
				(unsigned int)bnum);
		goto out;
	}
	/* inode started from 1, so substract by 1 */
	((struct ufs_dinode *)buf)[(inode->i_ino - 1) % UFS_INUM_PER_BLK] =
		*(struct ufs_dinode *)inode;
	if ((ret = ufs_wr_blk(bnum, buf, sizeof(buf))) < 0) {
		log_msg("ufs_wr_inode: ufs_wr_blk error for block %u",
				(unsigned int)bnum);
		goto out;
	}
	ret = 0;
out:
	log_msg("ufs_wr_inode return %d", ret);
	return(ret);
}

unsigned int ufs_new_zone(void)
{
	char	*p = sb.s_zmap;
	int	n = sb.s_zmap_blocks << UFS_BLK_SIZE_SHIFT;
	int	i, j;
	char	buf[UFS_BLK_SIZE];
	unsigned int	ret = 0;

	log_msg("ufs_new_zone called");

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
			if (ufs_wr_zone(ret, buf, sizeof(buf)) < 0)
				log_msg("ufs_new_zone: ufs_wr_zone error");
			goto out;
		}
	}
out:
	log_msg("ufs_new_zone return %d", (int)ret);
	return(ret);
}

int ufs_free_zone(unsigned int znum)
{
	unsigned int	n;
	int	ret;

	log_msg("ufs_free_zone called, znum = %u", (unsigned int)znum);
	if (!ufs_is_zvalid(znum)) {
		log_msg("ufs_free_zone: znum out of range, znum = %u",
				(unsigned int)znum);
		ret = -EINVAL;
		goto out;
	}

	n = znum >> 3;
	if ((sb.s_zmap[n] & (1 << (znum & 7))) == 0) {
		log_msg("ufs_free_zone: zone %d already freed", (int)znum);
		ret = -EAGAIN;
	} else {
		sb.s_zmap[n] &= ~(1 << (znum & 7));
		ret = 0;
	}

out:
	log_msg("free_zone return %d", ret);
	return(ret);
}

int ufs_rd_zone(unsigned int zone_num, void *buf, size_t size)
{
	int	ret;
	unsigned int bnum;

	log_msg("ufs_rd_zone called, zone_num = %u", (unsigned int)zone_num);
	if (!ufs_is_zvalid(zone_num)) {
		log_msg("ufs_rd_zone: znum out of range, znum = %u",
				(unsigned int)zone_num);
		ret = -EINVAL;
		goto out;
	}
	if (buf == NULL) {
		log_msg("ufs_rd_zone: buf is NULL");
		ret = -EINVAL;
		goto out;
	}
	if (size != UFS_BLK_SIZE) {
		log_msg("ufs_rd_zone: size = %d, do not equals to %d",
				(int)size, UFS_BLK_SIZE);
		ret = -EINVAL;
		goto out;
	}
	if ((bnum = ufs_znum2bnum(zone_num)) == 0) {
		log_msg("ufs_rd_zone: zone %u's block number is zero",
				(unsigned int)zone_num);
		ret = -EINVAL;
		goto out;
	}
	ret = ufs_rd_blk(bnum, buf, size);
out:
	log_msg("ufs_rd_zone return %d", ret);
	return(ret);
}

int ufs_wr_zone(unsigned int zone_num, const void *buf, size_t size)
{
	int	ret;
	unsigned int bnum;

	log_msg("ufs_wr_zone called, zone_num = %u",
			(unsigned int)zone_num);
	if (!ufs_is_zvalid(zone_num)) {
		log_msg("ufs_wr_zone: znum out of range, znum = %u",
				(unsigned int)zone_num);
		ret = -EINVAL;
		goto out;
	}
	if (buf == NULL) {
		log_msg("ufs_wr_zone: buf is NULL");
		ret = -EINVAL;
		goto out;
	}
	if (size != UFS_BLK_SIZE) {
		log_msg("ufs_wr_zone: size = %d, do not equals to %d",
				(int)size, UFS_BLK_SIZE);
		ret = -EINVAL;
		goto out;
	}
	if ((bnum = ufs_znum2bnum(zone_num)) == 0) {
		log_msg("ufs_wr_zone: zone %u's block number is zero",
				(unsigned int)zone_num);
		ret = -EINVAL;
		goto out;
	}
	ret = ufs_wr_blk(bnum, buf, size);
out:
	log_msg("ufs_wr_zone return %d", ret);
	return(ret);
}

unsigned int ufs_inum2bnum(unsigned int inum)
{
	unsigned int ret = 0;

	log_msg("ufs_inum2bnum called, inum = %u", (unsigned int)inum);
	if (!ufs_is_ivalid(inum))
		log_msg("ufs_inum2bnum: inode %u is not valid",
				(unsigned int)inum);
	else
		ret = (inum - 1) / UFS_INUM_PER_BLK + sb.s_1st_inode_block;
	log_msg("ufs_inum2bnum return %u", (unsigned int)ret);
	return(ret);
}

unsigned int ufs_znum2bnum(unsigned int zone_num)
{
	unsigned int ret = 0;

	log_msg("ufs_znum2bnum called, zone_num = %u", (unsigned int)zone_num);
	if (!ufs_is_zvalid(zone_num)) {
		log_msg("ufs_znum2bnum: zone %u is not valid",
				(unsigned int)zone_num);
		goto out;
	}
	ret = (zone_num - 1) + sb.s_1st_zone_block;
out:
	log_msg("ufs_znum2bnum return %u", (unsigned int)ret);
	return(ret);
}

static unsigned int _ufs_dnum2znum(struct ufs_minode *inode, unsigned int dnum, int creat)
{
	unsigned int ret = 0, znum;
	char	buf[UFS_BLK_SIZE];

	log_msg("_ufs_dnum2znum called, inum = %d, dnum = %d, creat = %d",
			(int)inode->i_ino, (int)dnum, (int)creat);
	if (!ufs_is_ivalid(inode->i_ino)) {
		log_msg("_ufs_dnum2znum: inum %u is not valid",
				(unsigned int)inode->i_ino);
		goto out;
	}
	if (!ufs_is_dvalid(dnum)) {
		log_msg("_ufs_dnum2znum: dnum %u is not valid",
				(unsigned int)dnum);
		goto out;
	}

	if (dnum < 6) {
		ret = inode->i_zones[dnum];
		if (ret == 0 && creat) {
			if ((ret = ufs_new_zone()) == 0)
				goto out;
			inode->i_blocks++;
			inode->i_zones[dnum] = ret;
			if (ufs_wr_inode(inode) < 0) {
				log_msg("_ufs_dnum2znum: ufs_wr_inode error");
				ret = 0;
				goto out;
			}
		}
		goto out;
	}

	dnum -= 6;
	if (dnum < UFS_ZNUM_PER_BLK) {
		ret = inode->i_zones[6];
		if (ret == 0 && creat) {
			if ((ret = ufs_new_zone()) == 0) {
				log_msg("_ufs_dnum2znum: ufs_new_zone return 0");
				goto out;
			}
			inode->i_blocks++;
			inode->i_zones[6] = ret;
			if (ufs_wr_inode(inode) < 0) {
				log_msg("_ufs_dnum2znum: ufs_wr_inode error");
				ret = 0;
				goto out;
			}
		}
		if ((ret = inode->i_zones[6]) == 0)
			goto out;
		if (ufs_rd_zone(ret, buf, sizeof(buf)) < 0) {
			log_msg("_ufs_dnum2znum: ufs_rd_zone error");
			ret = 0;
			goto out;
		}
		ret = ((unsigned int *)buf)[dnum];
		if (ret == 0 && creat) {
			if ((ret = ufs_new_zone()) == 0)
				goto out;
			inode->i_blocks++;
			((unsigned int *)buf)[dnum] = ret;
			if (ufs_wr_zone(inode->i_zones[6], buf, sizeof(buf)) < 0) {
				log_msg("_ufs_dnum2znum: ufs_wr_inode error");
				ret = 0;
				goto out;
			}
		}
		goto out;
	}

	/* double indirect */
	dnum -= UFS_ZNUM_PER_BLK;
	ret = inode->i_zones[7];
	if (ret == 0 && creat) {
		if ((ret = ufs_new_zone()) == 0) {
			log_msg("_ufs_dnum2znum: ufs_new_inode return 0");
			goto out;
		}
		inode->i_blocks++;
		inode->i_zones[7] = ret;
		if (ufs_wr_inode(inode) < 0) {
			log_msg("_ufs_dnum2znum: ufs_wr_inode error for inode"
					" %u", (unsigned int)inode->i_ino);
			goto out;
		}
	}
	if (inode->i_zones[7] == 0)
		goto out;
	if (ufs_rd_zone(inode->i_zones[7], buf, sizeof(buf)) < 0) {
		log_msg("ufs_dnum2znum: ufs_rd_zone error for %u",
				(unsigned int)inode->i_zones[7]);
		ret = 0;
		goto out;
	}
	ret = ((unsigned int *)buf)[dnum / UFS_ZNUM_PER_BLK];
	if (ret == 0 && creat) {
		if ((ret = ufs_new_zone()) == 0) {
			log_msg("_ufs_dnum2znum: ufs_new_inode return 0 for "
					"1st level of double");
			goto out;
		}
		inode->i_blocks++;
		((unsigned int *)buf)[dnum / UFS_ZNUM_PER_BLK] = ret;
		if (ufs_wr_zone(inode->i_zones[7], buf, sizeof(buf)) < 0) {
			log_msg("_ufs_dnum2znum: ufs_wr_zone error");
			goto out;
		}
	}
	if (ret == 0)
		goto out;
	if (ufs_rd_zone(ret, buf, sizeof(buf)) < 0) {
		log_msg("_ufs_dnum2znum: ufs_rd_zone error");
		goto out;
	}
	znum = ret;
	ret = ((unsigned int *)buf)[dnum % UFS_ZNUM_PER_BLK];
	if (ret == 0 && creat) {
		if ((ret = ufs_new_zone()) == 0) {
			log_msg("_ufs_dnum2znum: ufs_new_inode return 0 for "
					"2nd level of double");
			goto out;
		}
		inode->i_blocks++;
		((unsigned int *)buf)[dnum % UFS_ZNUM_PER_BLK] = ret;
		if (ufs_wr_zone(znum, buf, sizeof(buf)) < 0) {
			log_msg("_ufs_dnum2znum: ufs_wr_zone error");
			goto out;
		}
	}
out:
	log_msg("_ufs_dnum2znum return %u", (unsigned int)ret);
	return(ret);
}

int ufs_rd_blk(unsigned int blk_num, void *buf, size_t size)
{
	int	ret = 0;

	log_msg("ufs_rd_blk called, blk_num = %u", (unsigned int)blk_num);
	if (!ufs_is_bvalid(blk_num)) {
		log_msg("ufs_rd_blk: block num %u is not valid",
				(unsigned int)blk_num);
		ret = -EINVAL;
		goto out;
	}
	if (buf == NULL) {
		log_msg("ufs_rd_blk: buf is NULL");
		ret = -EINVAL;
		goto out;
	}
	if (size != UFS_BLK_SIZE) {
		log_msg("ufs_rd_blk: size = %u", (unsigned int)size);
		ret = -EINVAL;
		goto out;
	}
	memcpy(buf, sb.s_addr + (blk_num << UFS_BLK_SIZE_SHIFT), size);
	ret = 0;
out:
	log_msg("ufs_rd_blk return %d", ret);
	return(ret);
}

int ufs_wr_blk(unsigned int blk_num, const void *buf, size_t size)
{
	int	ret;

	log_msg("ufs_wr_blk called, blk_num = %u",
			(unsigned int)blk_num);
	if (!ufs_is_bvalid(blk_num)) {
		log_msg("ufs_wr_blk: block num %u is not valid",
				(unsigned int)blk_num);
		ret = -EINVAL;
		goto out;
	}
	if (buf == NULL) {
		log_msg("ufs_wr_blk: buf is NULL");
		ret = -EINVAL;
		goto out;
	}
	if (size != UFS_BLK_SIZE) {
		log_msg("ufs_wr_blk: size = %u", (unsigned int)size);
		ret = -EINVAL;
		goto out;
	}
	memcpy(sb.s_addr + (blk_num << UFS_BLK_SIZE_SHIFT), buf, size);
	ret = 0;
out:
	log_msg("ufs_wr_blk return %d", ret);
	return(ret);
}

int ufs_dir2i(const char *dirpath, struct ufs_minode *dirinode)
{
	int	ret;

	log_msg("ufs_dir2i called, dirpath = %s", (dirpath == NULL ? "NULL" :
				dirpath));
	if ((ret = ufs_path2i(dirpath, dirinode)) < 0) {
		log_msg("ufs_dir2i: ufs_path2i error");
		goto out;
	}
	if (!UFS_ISDIR(dirinode->i_mode)) {
		ret = -ENOTDIR;
		goto out;
	}
	ret = 0;
out:
	log_msg("ufs_dir2i return %d", ret);
	return(ret);
}

int ufs_rm_entry(struct ufs_minode *dir, const struct ufs_dir_entry *ent)
{
	int	ret, i;
	mode_t acc;
	unsigned int dnum, znum;
	char	buf[UFS_BLK_SIZE];
	off_t	size;
	struct ufs_dir_entry *de;

	log_msg("ufs_rm_entry called, removing %s in %u",
			(ent == NULL ? "NULL" : ent->de_name),
			(unsigned int)dir->i_ino);
	if (dir == NULL || !ufs_is_ivalid(dir->i_ino)) {
		log_msg("ufs_rm_entry: dirinode not valid");
		ret = -EINVAL;
		goto out;
	}
	if (ent == NULL) {
		log_msg("ufs_rm_entry: ent is NULL");
		ret = -EINVAL;
		goto out;
	}
	if (!UFS_ISDIR(dir->i_mode)) {
		log_msg("ufs_rm_entry: inode %u is not diectory",
				(unsigned int)dir->i_ino);
		ret = -ENOTDIR;
		goto out;
	}

	/* access permission check */
	if (getuid() == dir->i_uid)
		acc = dir->i_mode >> 6;
	else if (getgid() == dir->i_gid)
		acc = dir->i_mode >> 3;
	else
		acc = dir->i_mode;
	acc &= 0x7;
	if (!(acc & W_OK)) {
		ret = -EACCES;
		goto out;
	}

	/*
	 * find the entry in parent directory. remove entry and
	 * update parent directory's inode.
	 */
	dnum = 0;
	size = 0;
	ret = -ENOENT;
	while (size < dir->i_size) {
		if ((znum = ufs_dnum2znum(dir, dnum++)) == 0) {
			log_msg("ufs_rm_entry: ufs_creat_zone return 0"
					" for %u", (unsigned int)znum);
			if (size < dir->i_size) {
				log_msg("ufs_rm_entry: the # data blocks "
					" and the size of directory are not"
					" matched");
				ret = -EIO;
				goto out;
			}
			ret = -ENOENT;
			goto out;
		}
		if ((ret = ufs_rd_zone(znum, buf, sizeof(buf))) < 0) {
			log_msg("ufs_rm_entry: ufs_rd_zone error");
			goto out;
		}
		for (de = (struct ufs_dir_entry *)buf, i = 0;
				i < UFS_ENTRYNUM_PER_BLK &&
				size < dir->i_size; i++) {
			if (de[i].de_inum == 0)
				continue;
			size += sizeof(struct ufs_dir_entry);
			if (de[i].de_inum == ent->de_inum &&
				!strcmp(de[i].de_name, ent->de_name)) {
				ret = 7594;
				break;
			}
		}
		if (ret == 7594)
			break;
	}

	if (ret != 7594) /* not found */
		goto out;

	memset(&de[i], 0, sizeof(de[i]));
	if ((ret = ufs_wr_zone(znum, buf, sizeof(buf))) < 0) {
		log_msg("ufs_rm_entry: ufs_wr_zone error");
		goto out;
	}
	/*
	 * update parent directory's inode.
	 */
	dir->i_size -= sizeof(de[i]);
	dir->i_ctime = dir->i_mtime = time(NULL);
	if ((ret = ufs_wr_inode(dir)) < 0) {
		log_msg("ufs_rm_entry: ufs_wr_inode error");
		goto out;
	}
	ret = 0;

out:
	log_msg("ufs_rm_entry return %d", ret);
	return(ret);
}

int ufs_add_entry(struct ufs_minode *dir, const struct ufs_dir_entry *ent)
{
	int	ret, i;
	mode_t	acc;
	unsigned int dnum, znum;
	char	buf[UFS_BLK_SIZE];
	struct ufs_dir_entry *de;

	log_msg("ufs_add_entry called, adding %s in %u",
			(ent == NULL ? "NULL" : ent->de_name),
			(unsigned int)dir->i_ino);
	if (dir == NULL || !ufs_is_ivalid(dir->i_ino)) {
		log_msg("ufs_add_entry: dirinode not valid");
		ret = -EINVAL;
		goto out;
	}
	if (ent == NULL) {
		log_msg("ufs_add_entry: ent is NULL");
		ret = -EINVAL;
		goto out;
	}
	if (!UFS_ISDIR(dir->i_mode)) {
		log_msg("ufs_add_entry: inode %u is not diectory",
				(unsigned int)dir->i_ino);
		ret = -ENOTDIR;
		goto out;
	}

	/* access permission check */
	if (getuid() == dir->i_uid)
		acc = dir->i_mode >> 6;
	else if (getgid() == dir->i_gid)
		acc = dir->i_mode >> 3;
	else
		acc = dir->i_mode;
	acc &= 0x7;
	if (!(acc & W_OK)) {
		ret = -EACCES;
		goto out;
	}

	/*
	 * find a available entry in parent directory. write new entry and
	 * update parent directory's inode.
	 */
	dnum = 0;
	while (1) {
		if ((znum = ufs_creat_zone(dir, dnum++)) == 0) {
			log_msg("ufs_add_entry: ufs_creat_zone return 0 for %u",
					(unsigned int)znum);
			ret = -ENOSPC;
			goto out;
		}
		if ((ret = ufs_rd_zone(znum, buf, sizeof(buf))) < 0) {
			log_msg("ufs_add_entry: ufs_rd_zone error");
			goto out;
		}
		for (de = (struct ufs_dir_entry *)buf, i = 0;
				i < UFS_ENTRYNUM_PER_BLK; i++)
			if (de[i].de_inum == 0)
				break;
		if (i < UFS_ENTRYNUM_PER_BLK)
			break;
	}
	/* find a available entry in directory */
	memcpy(&de[i], ent, sizeof(de[i]));
	if ((ret = ufs_wr_zone(znum, buf, sizeof(buf))) < 0) {
		log_msg("ufs_add_entry: ufs_wr_zone error");
		goto out;
	}
	/*
	 * update parent directory's inode.
	 */
	dir->i_size += sizeof(de[i]);
	dir->i_ctime = dir->i_mtime = time(NULL);
	if ((ret = ufs_wr_inode(dir)) < 0) {
		log_msg("ufs_add_entry: ufs_wr_inode error");
		goto out;
	}
	ret = 0;

out:
	log_msg("ufs_add_entry return %d", ret);
	return(ret);
}

int ufs_path2i(const char *path, struct ufs_minode *inode)
{
	struct ufs_dir_entry ent;
	char	file[UFS_NAME_LEN + 1];
	int	i, start, ret;

	log_msg("ufs_path2i called, path = %s", (path == NULL ? "NULL" :
				path));
	if (path == NULL) {
		ret = -EINVAL;
		goto out;
	}
	if (inode == NULL) {
		ret = -EINVAL;
		goto out;
	}
	inode->i_ino = UFS_ROOT_INO;
	if ((ret = ufs_rd_inode(UFS_ROOT_INO, (struct ufs_dinode *)inode)) < 0) {
		log_msg("ufs_path2i: ufs_rd_inode error");
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
		if (i - start >= UFS_NAME_LEN) {
			ret = -ENAMETOOLONG;
			goto out;
		}
		memcpy(file, path + start, i - start);
		file[i - start] = 0;
		if ((ret = ufs_find_entry(inode, file, &ent)) < 0) {
			log_msg("ufs_path2i: ufs_find_entry error for %s", file);
			goto out;
		}
		inode->i_ino = ent.de_inum;
		ret = ufs_rd_inode(inode->i_ino, (struct ufs_dinode *)inode);
		if (ret  < 0) {
			log_msg("ufs_path2i: ufs_rd_inode error");
			goto out;
		}
	}
	ret = 0;

out:
	log_msg("ufs_path2i return %d, inum = %u", ret,
			(unsigned int)inode->i_ino);
	return(ret);
}

int ufs_find_entry(struct ufs_minode *par, const char *file,
		struct ufs_dir_entry *res)
{
	off_t	i, j;
	int	ret;
	mode_t	acc;
	unsigned int dnum, znum;
	char	buf[UFS_BLK_SIZE];
	struct ufs_dir_entry *de;

	log_msg("ufs_find_entry called");
	if (par == NULL || file == NULL || res == NULL) {
		log_msg("ufs_find_entry: arguments is NULL");
		ret = -EINVAL;
		goto out;
	}
	log_msg("ufs_find_entry: par->i_ino = %u, file = %s",
			(unsigned int)par->i_ino, file);

	if (!UFS_ISDIR(par->i_mode)) {
		ret = -ENOTDIR;
		goto out;
	}

	/* access permission check */
	if (getuid() == par->i_uid)
		acc = par->i_mode >> 6;
	else if (getgid() == par->i_gid)
		acc = par->i_mode >> 3;
	else
		acc = par->i_mode;
	acc &= 0x7;
	if ((acc & (R_OK | X_OK)) != (R_OK | X_OK)) {
		ret = -EACCES;
		goto out;
	}

	i = 0;
	dnum = 0;
	while (i < par->i_size) {
		if ((znum = ufs_dnum2znum(par, dnum++)) == 0) {
			log_msg("ufs_find_entry: ufs_dnum2znum return "
					"zero for data %u", (unsigned int)dnum);
			break;
		}
		if ((ret = ufs_rd_zone(znum, buf, sizeof(buf))) < 0) {
			log_msg("ufs_find_entry: ufs_rd_zone error for data"
					" %u", (unsigned int)znum);
			goto out;
		}
		for (de = (struct ufs_dir_entry *)buf, j = 0;
				j < UFS_ENTRYNUM_PER_BLK &&
				i < par->i_size; j++) {
			if (de[j].de_inum == 0)
				continue;
			i += sizeof(*de);
			if (strncmp(de[j].de_name, file, UFS_NAME_LEN) == 0) {
				res->de_inum = de[j].de_inum;
				strncpy(res->de_name, de[j].de_name,
						UFS_NAME_LEN);
				res->de_name[UFS_NAME_LEN] = 0;
				ret = 0;
				goto out;
			}
		}
	}
	ret = -ENOENT;
out:
	log_msg("ufs_find_entry return %d", ret);
	return(ret);
}

int ufs_truncatei(struct ufs_minode *iptr)
{
	int	ret, i;

	log_msg("ufs_truncatei called, iptr->i_ino = %u", iptr == NULL ?
			0 : (unsigned int)iptr->i_ino);

	if (iptr == NULL) {
		log_msg("ufs_truncatei: iptr is NULL");
		ret = -EINVAL;
		goto out;
	}
	for (i = 0; i < 6; i++) {
		if (iptr->i_zones[i] == 0)
			continue;
		if ((ret = ufs_free_zone(iptr->i_zones[i])) < 0) {
			log_msg("ufs_truncatei: ufs_free_zone error for "
					"zone %d", (int)iptr->i_zones[i]);
			goto out;
		}
	}
	if ((ret = ufs_free_ind(iptr->i_zones[6])) < 0) {
		log_msg("ufs_truncatei: ufs_free_ind error");
		goto out;
	}
	if ((ret = ufs_free_dind(iptr->i_zones[7])) < 0) {
		log_msg("ufs_truncatei: ufs_free_dind error");
		goto out;
	}
	memset(&iptr->i_zones, 0, sizeof(iptr->i_zones));
	iptr->i_size = 0;
	iptr->i_mtime = iptr->i_ctime = time(NULL);
	if ((ret = ufs_wr_inode(iptr)) < 0) {
		log_msg("ufs_truncatei: ufs_wr_inode error");
		goto out;
	}
	ret = 0;

out:
	log_msg("ufs_truncatei return %d", ret);
	return(ret);
}

int ufs_is_dirempty(struct ufs_minode *inode)
{
	if (inode == NULL || !UFS_ISDIR(inode->i_mode))
		return(0);
	return(inode->i_size == (2 * sizeof(struct ufs_dir_entry)));
}

/*
 * convert file mode and permission from ufs to
 * UNIX.
 */
mode_t ufs_conv_fmode(mode_t mode)
{
	mode_t ret = mode & 0x1ff; /* low 9 bits are user permission */
	if (UFS_ISREG(mode))
		ret |= S_IFREG;
	if (UFS_ISDIR(mode))
		ret |= S_IFDIR;
	return(ret);
}

/*
 * convert open flags from UNIX to ufs.
 */
int ufs_conv_oflag(int oflag)
{
	int	ufsoflag = 0;

	if ((oflag & O_ACCMODE) == O_RDONLY)
		ufsoflag = UFS_O_RDONLY;
	else if ((oflag & O_ACCMODE) == O_WRONLY)
		ufsoflag = UFS_O_WRONLY;
	else
		ufsoflag = UFS_O_RDWR;
	if (oflag & O_APPEND)
		ufsoflag |= UFS_O_APPEND;
	if (oflag & O_DIRECTORY)
		ufsoflag |= UFS_O_DIR;
	if (oflag & O_TRUNC)
		ufsoflag |= UFS_O_TRUNC;
	return(ufsoflag);
}

int ufs_shrink(struct ufs_minode *inode, off_t length)
{
	int	ret, i, flag, dflag;
	unsigned int dnum, znum, *ptr, *dptr;
	char	buf[UFS_BLK_SIZE], dbuf[UFS_BLK_SIZE];

	log_msg("ufs_shrink called, ino = %d, length = %d",
			(int)inode->i_ino, (int)length);

	if (length >= inode->i_size) {
		ret = 0;
		goto out;
	}

	dnum = length / UFS_BLK_SIZE;
	znum = ufs_dnum2znum(inode, dnum);

	/* maybe hole */
	if (znum) {
		if ((ret = ufs_rd_zone(znum, buf, sizeof(buf))) < 0) {
			log_msg("ufs_shrink: ufs_rd_zone error");
			goto out;
		}
		memset(buf + length % UFS_BLK_SIZE, 0,
				UFS_BLK_SIZE - length % UFS_BLK_SIZE);
		if ((ret = ufs_wr_zone(znum, buf, sizeof(buf))) < 0) {
			log_msg("ufs_shrink: ufs_wr_zone error");
			goto out;
		}
	}

	/* free the rest */
	dnum = (length + UFS_BLK_SIZE - 1) / UFS_BLK_SIZE;

	/* direct */
	for (; dnum < 6; dnum++)
		if (inode->i_zones[dnum]) {
			ret = ufs_free_zone(inode->i_zones[dnum]);
			if (ret < 0)
				goto out;
			inode->i_zones[dnum] = 0;
			inode->i_blocks--;
		}

	/* indirect */
	if (inode->i_zones[6]) {
		ret = ufs_rd_zone(inode->i_zones[6], buf, sizeof(buf));
		if (ret < 0)
			goto out;
		ptr = (unsigned int *)buf;
		flag = (dnum == 6 ? 1 : 0);
		for (; (dnum - 6) < UFS_ZNUM_PER_BLK; dnum++) {
			if (!ptr[dnum - 6])
				continue;
			ret = ufs_free_zone(ptr[dnum - 6]);
			if (ret < 0)
				goto out;
			ptr[dnum - 6] = 0;
			inode->i_blocks--;
		}
		if (flag) {
			ret = ufs_free_zone(inode->i_zones[6]);
			if (ret < 0)
				goto out;
			inode->i_zones[6] = 0;
			inode->i_blocks--;
		} else {
			ret = ufs_wr_zone(inode->i_zones[6], buf, sizeof(buf));
			if (ret < 0)
				goto out;
		}
	}

	/* double indirect */
	if (!inode->i_zones[7]) {
		ret = 0;
		goto out;
	}
	if ((ret = ufs_rd_zone(inode->i_zones[7], dbuf, sizeof(dbuf))) < 0)
		goto out;
	dptr = (unsigned int *)dbuf;
	dnum -= 6 + UFS_ZNUM_PER_BLK;
	dflag = (!dnum) ? 1 : 0;
	while (dnum < UFS_ZNUM_PER_BLK * UFS_ZNUM_PER_BLK) {
		if (!dptr[dnum / UFS_ZNUM_PER_BLK]) {
			dnum = (dnum / UFS_ZNUM_PER_BLK + 1) *
				UFS_ZNUM_PER_BLK;
			continue;
		}
		ret = ufs_rd_zone(dptr[dnum / UFS_ZNUM_PER_BLK], buf,
				sizeof(buf));
		if (ret < 0)
			goto out;
		ptr = (unsigned int *)buf;
		flag = (!(dnum % UFS_ZNUM_PER_BLK)) ? 1 : 0;
		for (i = dnum % UFS_ZNUM_PER_BLK; i < UFS_ZNUM_PER_BLK;
				i++) {
			if (!ptr[i])
				continue;
			ret = ufs_free_zone(ptr[i]);
			inode->i_blocks--;
			ptr[i] = 0;
		}
		if (flag) {
			ret = ufs_free_zone(dptr[dnum / UFS_ZNUM_PER_BLK]);
			if (ret < 0)
				goto out;
			dptr[dnum / UFS_ZNUM_PER_BLK] = 0;
			inode->i_blocks--;
		} else {
			ret = ufs_wr_zone(dptr[dnum / UFS_ZNUM_PER_BLK],
					buf, sizeof(buf));
			if (ret < 0)
				goto out;
		}
		dnum += UFS_ZNUM_PER_BLK - (dnum % UFS_ZNUM_PER_BLK);
	}
	if (dflag) {
		ret = ufs_free_zone(inode->i_zones[7]);
		if (ret < 0)
			goto out;
		inode->i_zones[7] = 0;
		inode->i_blocks--;
	} else {
		ret = ufs_wr_zone(inode->i_zones[7], dbuf, sizeof(dbuf));
		if (ret < 0)
			goto out;
	}
	ret = 0;

out:
	log_msg("ufs_shrink return %d", ret);
	return(ret);
}
