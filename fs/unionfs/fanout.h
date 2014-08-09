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
 * Copyright (c) 2003-2007 The Research Foundation of SUNY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _FANOUT_H_
#define _FANOUT_H_

/*
 * Inode to private data
 *
 * Since we use containers and the struct inode is _inside_ the
 * unionfs_inode_info structure, UNIONFS_I will always (given a non-NULL
 * inode pointer), return a valid non-NULL pointer.
 */
static inline struct unionfs_inode_info *UNIONFS_I(const struct inode *inode)
{
	return container_of(inode, struct unionfs_inode_info, vfs_inode);
}

#define ibstart(ino) (UNIONFS_I(ino)->bstart)
#define ibend(ino) (UNIONFS_I(ino)->bend)

/* Superblock to private data */
#define UNIONFS_SB(super) ((struct unionfs_sb_info *)(super)->s_fs_info)
#define sbstart(sb) 0
#define sbend(sb) (UNIONFS_SB(sb)->bend)
#define sbmax(sb) (UNIONFS_SB(sb)->bend + 1)
#define sbhbid(sb) (UNIONFS_SB(sb)->high_branch_id)

/* File to private Data */
#define UNIONFS_F(file) ((struct unionfs_file_info *)((file)->private_data))
#define fbstart(file) (UNIONFS_F(file)->bstart)
#define fbend(file) (UNIONFS_F(file)->bend)

/* macros to manipulate branch IDs in stored in our superblock */
static inline int branch_id(struct super_block *sb, int index)
{
	BUG_ON(!sb || index < 0);
	return UNIONFS_SB(sb)->data[index].branch_id;
}

static inline void set_branch_id(struct super_block *sb, int index, int val)
{
	BUG_ON(!sb || index < 0);
	UNIONFS_SB(sb)->data[index].branch_id = val;
}

static inline void new_branch_id(struct super_block *sb, int index)
{
	BUG_ON(!sb || index < 0);
	set_branch_id(sb, index, ++UNIONFS_SB(sb)->high_branch_id);
}

/*
 * Find new index of matching branch with an existing superblock of a known
 * (possibly old) id.  This is needed because branches could have been
 * added/deleted causing the branches of any open files to shift.
 *
 * @sb: the new superblock which may have new/different branch IDs
 * @id: the old/existing id we're looking for
 * Returns index of newly found branch (0 or greater), -1 otherwise.
 */
static inline int branch_id_to_idx(struct super_block *sb, int id)
{
	int i;
	for (i = 0; i < sbmax(sb); i++) {
		if (branch_id(sb, i) == id)
			return i;
	}
	/* in the non-ODF code, this should really never happen */
	printk(KERN_WARNING "unionfs: cannot find branch with id %d\n", id);
	return -1;
}

/* File to lower file. */
static inline struct file *unionfs_lower_file(const struct file *f)
{
	BUG_ON(!f);
	return UNIONFS_F(f)->lower_files[fbstart(f)];
}

static inline struct file *unionfs_lower_file_idx(const struct file *f,
						  int index)
{
	BUG_ON(!f || index < 0);
	return UNIONFS_F(f)->lower_files[index];
}

static inline void unionfs_set_lower_file_idx(struct file *f, int index,
					      struct file *val)
{
	BUG_ON(!f || index < 0);
	UNIONFS_F(f)->lower_files[index] = val;
	/* save branch ID (may be redundant?) */
	UNIONFS_F(f)->saved_branch_ids[index] =
		branch_id((f)->f_dentry->d_sb, index);
}

static inline void unionfs_set_lower_file(struct file *f, struct file *val)
{
	BUG_ON(!f);
	unionfs_set_lower_file_idx((f), fbstart(f), (val));
}

/* Inode to lower inode. */
static inline struct inode *unionfs_lower_inode(const struct inode *i)
{
	BUG_ON(!i);
	return UNIONFS_I(i)->lower_inodes[ibstart(i)];
}

static inline struct inode *unionfs_lower_inode_idx(const struct inode *i,
						    int index)
{
	BUG_ON(!i || index < 0);
	return UNIONFS_I(i)->lower_inodes[index];
}

static inline void unionfs_set_lower_inode_idx(struct inode *i, int index,
					       struct inode *val)
{
	BUG_ON(!i || index < 0);
	UNIONFS_I(i)->lower_inodes[index] = val;
}

static inline void unionfs_set_lower_inode(struct inode *i, struct inode *val)
{
	BUG_ON(!i);
	UNIONFS_I(i)->lower_inodes[ibstart(i)] = val;
}

/* Superblock to lower superblock. */
static inline struct super_block *unionfs_lower_super(
					const struct super_block *sb)
{
	BUG_ON(!sb);
	return UNIONFS_SB(sb)->data[sbstart(sb)].sb;
}

static inline struct super_block *unionfs_lower_super_idx(
					const struct super_block *sb,
					int index)
{
	BUG_ON(!sb || index < 0);
	return UNIONFS_SB(sb)->data[index].sb;
}

static inline void unionfs_set_lower_super_idx(struct super_block *sb,
					       int index,
					       struct super_block *val)
{
	BUG_ON(!sb || index < 0);
	UNIONFS_SB(sb)->data[index].sb = val;
}

static inline void unionfs_set_lower_super(struct super_block *sb,
					   struct super_block *val)
{
	BUG_ON(!sb);
	UNIONFS_SB(sb)->data[sbstart(sb)].sb = val;
}

/* Branch count macros. */
static inline int branch_count(const struct super_block *sb, int index)
{
	BUG_ON(!sb || index < 0);
	return atomic_read(&UNIONFS_SB(sb)->data[index].open_files);
}

static inline void set_branch_count(struct super_block *sb, int index, int val)
{
	BUG_ON(!sb || index < 0);
	atomic_set(&UNIONFS_SB(sb)->data[index].open_files, val);
}

static inline void branchget(struct super_block *sb, int index)
{
	BUG_ON(!sb || index < 0);
	atomic_inc(&UNIONFS_SB(sb)->data[index].open_files);
}

static inline void branchput(struct super_block *sb, int index)
{
	BUG_ON(!sb || index < 0);
	atomic_dec(&UNIONFS_SB(sb)->data[index].open_files);
}

/* Dentry macros */
static inline struct unionfs_dentry_info *UNIONFS_D(const struct dentry *dent)
{
	BUG_ON(!dent);
	return dent->d_fsdata;
}

static inline int dbstart(const struct dentry *dent)
{
	BUG_ON(!dent);
	return UNIONFS_D(dent)->bstart;
}

static inline void set_dbstart(struct dentry *dent, int val)
{
	BUG_ON(!dent);
	UNIONFS_D(dent)->bstart = val;
}

static inline int dbend(const struct dentry *dent)
{
	BUG_ON(!dent);
	return UNIONFS_D(dent)->bend;
}

static inline void set_dbend(struct dentry *dent, int val)
{
	BUG_ON(!dent);
	UNIONFS_D(dent)->bend = val;
}

static inline int dbopaque(const struct dentry *dent)
{
	BUG_ON(!dent);
	return UNIONFS_D(dent)->bopaque;
}

static inline void set_dbopaque(struct dentry *dent, int val)
{
	BUG_ON(!dent);
	UNIONFS_D(dent)->bopaque = val;
}

static inline void unionfs_set_lower_dentry_idx(struct dentry *dent, int index,
						struct dentry *val)
{
	BUG_ON(!dent || index < 0);
	UNIONFS_D(dent)->lower_paths[index].dentry = val;
}

static inline struct dentry *unionfs_lower_dentry_idx(
				const struct dentry *dent,
				int index)
{
	BUG_ON(!dent || index < 0);
	return UNIONFS_D(dent)->lower_paths[index].dentry;
}

static inline struct dentry *unionfs_lower_dentry(const struct dentry *dent)
{
	BUG_ON(!dent);
	return unionfs_lower_dentry_idx(dent, dbstart(dent));
}

static inline void unionfs_set_lower_mnt_idx(struct dentry *dent, int index,
					     struct vfsmount *mnt)
{
	BUG_ON(!dent || index < 0);
	UNIONFS_D(dent)->lower_paths[index].mnt = mnt;
}

static inline struct vfsmount *unionfs_lower_mnt_idx(
					const struct dentry *dent,
					int index)
{
	BUG_ON(!dent || index < 0);
	return UNIONFS_D(dent)->lower_paths[index].mnt;
}

static inline struct vfsmount *unionfs_lower_mnt(const struct dentry *dent)
{
	BUG_ON(!dent);
	return unionfs_lower_mnt_idx(dent, dbstart(dent));
}

/* Macros for locking a dentry. */
static inline void unionfs_lock_dentry(struct dentry *d)
{
	BUG_ON(!d);
	mutex_lock(&UNIONFS_D(d)->lock);
}

static inline void unionfs_unlock_dentry(struct dentry *d)
{
	BUG_ON(!d);
	mutex_unlock(&UNIONFS_D(d)->lock);
}

static inline void verify_locked(struct dentry *d)
{
	BUG_ON(!d);
	BUG_ON(!mutex_is_locked(&UNIONFS_D(d)->lock));
}

/* copy a/m/ctime from the lower branch with the newest times */
static inline void unionfs_copy_attr_times(struct inode *upper)
{
	int bindex;
	struct inode *lower;

	if (!upper)
		return;
	for (bindex=ibstart(upper); bindex <= ibend(upper); bindex++) {
		lower = unionfs_lower_inode_idx(upper, bindex);
		if (!lower)
			continue; /* not all lower dir objects may exist */
		if (timespec_compare(&upper->i_mtime, &lower->i_mtime) < 0)
			upper->i_mtime = lower->i_mtime;
		if (timespec_compare(&upper->i_ctime, &lower->i_ctime) < 0)
			upper->i_ctime = lower->i_ctime;
		if (timespec_compare(&upper->i_atime, &lower->i_atime) < 0)
			upper->i_atime = lower->i_atime;
	}
}

/*
 * A unionfs/fanout version of fsstack_copy_attr_all.  Uses a
 * unionfs_get_nlinks to properly calcluate the number of links to a file.
 * Also, copies the max() of all a/m/ctimes for all lower inodes (which is
 * important if the lower inode is a directory type)
 */
static inline void unionfs_copy_attr_all(struct inode *dest,
					 const struct inode *src)
{
	dest->i_mode = src->i_mode;
	dest->i_uid = src->i_uid;
	dest->i_gid = src->i_gid;
	dest->i_rdev = src->i_rdev;

	unionfs_copy_attr_times(dest);

	dest->i_blkbits = src->i_blkbits;
	dest->i_flags = src->i_flags;

	/*
	 * Update the nlinks AFTER updating the above fields, because the
	 * get_links callback may depend on them.
	 */
	dest->i_nlink = unionfs_get_nlinks(dest);
}

#endif	/* not _FANOUT_H */
