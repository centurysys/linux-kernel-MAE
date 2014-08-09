/*
 * Copyright (c) 2003-2007 Erez Zadok
 * Copyright (c) 2003-2006 Charles P. Wright
 * Copyright (c) 2005-2007 Josef 'Jeff' Sipek
 * Copyright (c) 2005      Arun M. Krishnakumar
 * Copyright (c) 2004-2006 David P. Quigley
 * Copyright (c) 2003-2004 Mohammad Nayyer Zubair
 * Copyright (c) 2003      Puja Gupta
 * Copyright (c) 2003      Harikesavan Krishnan
 * Copyright (c) 2003-2007 Stony Brook University
 * Copyright (c) 2003-2007 The Research Foundation of State University of New York
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _UNION_H_
#define _UNION_H_

#include <linux/dcache.h>
#include <linux/file.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/page-flags.h>
#include <linux/pagemap.h>
#include <linux/poll.h>
#include <linux/security.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/statfs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/writeback.h>
#include <linux/xattr.h>
#include <linux/fs_stack.h>
#include <linux/magic.h>
#include <linux/log2.h>

#include <asm/mman.h>
#include <asm/system.h>

#include <linux/union_fs.h>

/* the file system name */
#define UNIONFS_NAME "unionfs"

/* unionfs root inode number */
#define UNIONFS_ROOT_INO     1

/* Mount time flags */
#define MOUNT_FLAG(sb)     (UNIONFS_SB(sb)->mount_flag)

/* number of characters while generating unique temporary file names */
#define	UNIONFS_TMPNAM_LEN	12

/* number of times we try to get a unique temporary file name */
#define GET_TMPNAM_MAX_RETRY	5

/* Operations vectors defined in specific files. */
extern struct file_operations unionfs_main_fops;
extern struct file_operations unionfs_dir_fops;
extern struct inode_operations unionfs_main_iops;
extern struct inode_operations unionfs_dir_iops;
extern struct inode_operations unionfs_symlink_iops;
extern struct super_operations unionfs_sops;
extern struct dentry_operations unionfs_dops;

/* How long should an entry be allowed to persist */
#define RDCACHE_JIFFIES	(5*HZ)

/* file private data. */
struct unionfs_file_info {
	int bstart;
	int bend;
	atomic_t generation;

	struct unionfs_dir_state *rdstate;
	struct file **lower_files;
};

/* unionfs inode data in memory */
struct unionfs_inode_info {
	int bstart;
	int bend;
	atomic_t generation;
	int stale;
	/* Stuff for readdir over NFS. */
	spinlock_t rdlock;
	struct list_head readdircache;
	int rdcount;
	int hashsize;
	int cookie;
	
	/* The hidden inodes */
	struct inode **lower_inodes;
	/* to keep track of reads/writes for unlinks before closes */
	atomic_t totalopens;

	struct inode vfs_inode;
};

/* unionfs dentry data in memory */
struct unionfs_dentry_info {
	/* The semaphore is used to lock the dentry as soon as we get into a
	 * unionfs function from the VFS.  Our lock ordering is that children
	 * go before their parents.
	 */
	struct mutex lock;
	int bstart;
	int bend;
	int bopaque;
	int bcount;
	atomic_t generation;
	struct path *lower_paths;
};

/* These are the pointers to our various objects. */
struct unionfs_data {
	struct super_block *sb;
	struct vfsmount *hidden_mnt;
	atomic_t sbcount;
	int branchperms;
};

/* unionfs super-block data in memory */
struct unionfs_sb_info {
	int bend;

	atomic_t generation;
	unsigned long mount_flag;
	struct rw_semaphore rwsem;

	struct unionfs_data *data;
};

/*
 * structure for making the linked list of entries by readdir on left branch
 * to compare with entries on right branch
 */
struct filldir_node {
	struct list_head file_list;	/* list for directory entries */
	char *name;		/* name entry */
	int hash;		/* name hash */
	int namelen;		/* name len since name is not 0 terminated */

	/* we can check for duplicate whiteouts and files in the same branch
	 * in order to return -EIO.
	 */ 
	int bindex;		

	/* is this a whiteout entry? */
	int whiteout;

	/* Inline name, so we don't need to separately kmalloc small ones */
	char iname[DNAME_INLINE_LEN_MIN];
};

/* Directory hash table. */
struct unionfs_dir_state {
	unsigned int cookie;	/* The cookie, which is based off of rdversion */
	unsigned int offset;	/* The entry we have returned. */
	int bindex;
	loff_t dirpos;		/* The offset within the lower level directory. */
	int size;		/* How big is the hash table? */
	int hashentries;	/* How many entries have been inserted? */
	unsigned long access;

	/* This cache list is used when the inode keeps us around. */
	struct list_head cache;
	struct list_head list[0];
};

/* include miscellaneous macros */
#include "fanout.h"
#include "sioq.h"

/* Cache creation/deletion routines. */
void unionfs_destroy_filldir_cache(void);
int unionfs_init_filldir_cache(void);
int unionfs_init_inode_cache(void);
void unionfs_destroy_inode_cache(void);
int unionfs_init_dentry_cache(void);
void unionfs_destroy_dentry_cache(void);

/* Initialize and free readdir-specific  state. */
int init_rdstate(struct file *file);
struct unionfs_dir_state *alloc_rdstate(struct inode *inode, int bindex);
struct unionfs_dir_state *find_rdstate(struct inode *inode, loff_t fpos);
void free_rdstate(struct unionfs_dir_state *state);
int add_filldir_node(struct unionfs_dir_state *rdstate, const char *name,
		     int namelen, int bindex, int whiteout);
struct filldir_node *find_filldir_node(struct unionfs_dir_state *rdstate,
				       const char *name, int namelen);

struct dentry **alloc_new_dentries(int objs);
struct unionfs_data *alloc_new_data(int objs);

/* We can only use 32-bits of offset for rdstate --- blech! */
#define DIREOF (0xfffff)
#define RDOFFBITS 20		/* This is the number of bits in DIREOF. */
#define MAXRDCOOKIE (0xfff)
/* Turn an rdstate into an offset. */
static inline off_t rdstate2offset(struct unionfs_dir_state *buf)
{
	off_t tmp;
	tmp = ((buf->cookie & MAXRDCOOKIE) << RDOFFBITS)
		| (buf->offset & DIREOF);
	return tmp;
}

#define unionfs_read_lock(sb)	 down_read(&UNIONFS_SB(sb)->rwsem)
#define unionfs_read_unlock(sb)	 up_read(&UNIONFS_SB(sb)->rwsem)
#define unionfs_write_lock(sb)	 down_write(&UNIONFS_SB(sb)->rwsem)
#define unionfs_write_unlock(sb) up_write(&UNIONFS_SB(sb)->rwsem)

static inline void double_lock_dentry(struct dentry *d1, struct dentry *d2)
{
	if (d2 < d1) {
		struct dentry *tmp = d1;
		d1 = d2;
		d2 = tmp;
	}
	unionfs_lock_dentry(d1);
	unionfs_lock_dentry(d2);
}

extern int new_dentry_private_data(struct dentry *dentry);
void free_dentry_private_data(struct unionfs_dentry_info *udi);
void update_bstart(struct dentry *dentry);

/*
 * EXTERNALS:
 */

/* replicates the directory structure upto given dentry in given branch */
extern struct dentry *create_parents(struct inode *dir, struct dentry *dentry,
				     int bindex);

/* partial lookup */
extern int unionfs_partial_lookup(struct dentry *dentry);

/* Pass an unionfs dentry and an index and it will try to create a whiteout
 * in branch 'index'.
 *
 * On error, it will proceed to a branch to the left
 */
extern int create_whiteout(struct dentry *dentry, int start);
/* copies a file from dbstart to newbindex branch */
extern int copyup_file(struct inode *dir, struct file *file, int bstart,
		       int newbindex, loff_t size);
extern int copyup_named_file(struct inode *dir, struct file *file,
			     char *name, int bstart, int new_bindex,
			     loff_t len);
/* copies a dentry from dbstart to newbindex branch */
extern int copyup_dentry(struct inode *dir, struct dentry *dentry, int bstart,
			 int new_bindex, struct file **copyup_file, loff_t len);

extern int remove_whiteouts(struct dentry *dentry, struct dentry *hidden_dentry,
			    int bindex);

extern int do_delete_whiteouts(struct dentry *dentry, int bindex,
		     struct unionfs_dir_state *namelist);

extern int unionfs_get_nlinks(struct inode *inode);

/* Is this directory empty: 0 if it is empty, -ENOTEMPTY if not. */
extern int check_empty(struct dentry *dentry,
		       struct unionfs_dir_state **namelist);
/* Delete whiteouts from this directory in branch bindex. */
extern int delete_whiteouts(struct dentry *dentry, int bindex,
			    struct unionfs_dir_state *namelist);

/* Re-lookup a hidden dentry. */
extern int unionfs_refresh_hidden_dentry(struct dentry *dentry, int bindex);

extern void unionfs_reinterpose(struct dentry *this_dentry);
extern struct super_block *unionfs_duplicate_super(struct super_block *sb);

/* Locking functions. */
extern int unionfs_setlk(struct file *file, int cmd, struct file_lock *fl);
extern int unionfs_getlk(struct file *file, struct file_lock *fl);

/* Common file operations. */
extern int unionfs_file_revalidate(struct file *file, int willwrite);
extern int unionfs_open(struct inode *inode, struct file *file);
extern int unionfs_file_release(struct inode *inode, struct file *file);
extern int unionfs_flush(struct file *file, fl_owner_t id);
extern long unionfs_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg);

/* Inode operations */
extern int unionfs_rename(struct inode *old_dir, struct dentry *old_dentry,
			  struct inode *new_dir, struct dentry *new_dentry);
int unionfs_unlink(struct inode *dir, struct dentry *dentry);
int unionfs_rmdir(struct inode *dir, struct dentry *dentry);

int unionfs_d_revalidate(struct dentry *dentry, struct nameidata *nd);

/* The values for unionfs_interpose's flag. */
#define INTERPOSE_DEFAULT	0
#define INTERPOSE_LOOKUP	1
#define INTERPOSE_REVAL		2
#define INTERPOSE_REVAL_NEG	3
#define INTERPOSE_PARTIAL	4

extern int unionfs_interpose(struct dentry *this_dentry, struct super_block *sb,
			     int flag);

/* Branch management ioctls. */
int unionfs_ioctl_incgen(struct file *file, unsigned int cmd,
			 unsigned long arg);
int unionfs_ioctl_queryfile(struct file *file, unsigned int cmd,
			    unsigned long arg);

#ifdef CONFIG_UNION_FS_XATTR
/* Extended attribute functions. */
extern void *unionfs_xattr_alloc(size_t size, size_t limit);
extern void unionfs_xattr_free(void *ptr, size_t size);

extern ssize_t unionfs_getxattr(struct dentry *dentry, const char *name,
		void *value, size_t size);
extern int unionfs_removexattr(struct dentry *dentry, const char *name);
extern ssize_t unionfs_listxattr(struct dentry *dentry, char *list,
		size_t size);
extern int unionfs_setxattr(struct dentry *dentry, const char *name,
		const void *value, size_t size, int flags);
#endif /* CONFIG_UNION_FS_XATTR */

/* The root directory is unhashed, but isn't deleted. */
static inline int d_deleted(struct dentry *d)
{
	return d_unhashed(d) && (d != d->d_sb->s_root);
}

struct dentry *unionfs_lookup_backend(struct dentry *dentry, struct nameidata *nd, int lookupmode);

#define IS_SET(sb, check_flag) ((check_flag) & MOUNT_FLAG(sb))

/* unionfs_permission, check if we should bypass error to facilitate copyup */
#define IS_COPYUP_ERR(err) ((err) == -EROFS)

/* unionfs_open, check if we need to copyup the file */
#define OPEN_WRITE_FLAGS (O_WRONLY | O_RDWR | O_APPEND)
#define IS_WRITE_FLAG(flag) ((flag) & OPEN_WRITE_FLAGS)

static inline int branchperms(const struct super_block *sb, int index)
{
	BUG_ON(index < 0);

	return UNIONFS_SB(sb)->data[index].branchperms;
}

static inline int set_branchperms(struct super_block *sb, int index, int perms)
{
	BUG_ON(index < 0);

	UNIONFS_SB(sb)->data[index].branchperms = perms;

	return perms;
}

/* Is this file on a read-only branch? */
static inline int is_robranch_super(const struct super_block *sb, int index)
{
	return (!(branchperms(sb, index) & MAY_WRITE)) ? -EROFS : 0;
}

/* Is this file on a read-only branch? */
static inline int is_robranch_idx(const struct dentry *dentry, int index)
{
	int err = 0;

	BUG_ON(index < 0);

	if ((!(branchperms(dentry->d_sb, index) & MAY_WRITE)) ||
	    IS_RDONLY(unionfs_lower_dentry_idx(dentry, index)->d_inode))
		err = -EROFS;

	return err;
}

static inline int is_robranch(const struct dentry *dentry)
{
	int index;

	index = UNIONFS_D(dentry)->bstart;
	BUG_ON(index < 0);

	return is_robranch_idx(dentry, index);
}

/* What do we use for whiteouts. */
#define UNIONFS_WHPFX ".wh."
#define UNIONFS_WHLEN 4
/* If a directory contains this file, then it is opaque.  We start with the
 * .wh. flag so that it is blocked by lookup.
 */
#define UNIONFS_DIR_OPAQUE_NAME "__dir_opaque"
#define UNIONFS_DIR_OPAQUE UNIONFS_WHPFX UNIONFS_DIR_OPAQUE_NAME

#define VALID_MOUNT_FLAGS (0)

#ifndef DEFAULT_POLLMASK
#define DEFAULT_POLLMASK (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM)
#endif

/*
 * EXTERNALS:
 */
extern char *alloc_whname(const char *name, int len);

/* These two functions are here because it is kind of daft to copy and paste the
 * contents of the two functions to 32+ places in unionfs
 */
static inline struct dentry *lock_parent(struct dentry *dentry)
{
	struct dentry *dir = dget(dentry->d_parent);

	mutex_lock(&dir->d_inode->i_mutex);
	return dir;
}

static inline void unlock_dir(struct dentry *dir)
{
	mutex_unlock(&dir->d_inode->i_mutex);
	dput(dir);
}

extern int make_dir_opaque(struct dentry *dir, int bindex);

#endif	/* not _UNION_H_ */

