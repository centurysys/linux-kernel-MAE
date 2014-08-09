/*
 * Copyright (c) 2003-2007 Erez Zadok
 * Copyright (c) 2003-2006 Charles P. Wright
 * Copyright (c) 2005-2007 Josef 'Jeff' Sipek
 * Copyright (c) 2005-2006 Junjiro Okajima
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

#include "union.h"

/* 1) Copyup the file
 * 2) Rename the file to '.unionfs<original inode#><counter>' - obviously
 * stolen from NFS's silly rename
 */
static int copyup_deleted_file(struct file *file, struct dentry *dentry,
			       int bstart, int bindex)
{
	static unsigned int counter;
	const int i_inosize = sizeof(dentry->d_inode->i_ino) * 2;
	const int countersize = sizeof(counter) * 2;
	const int nlen = sizeof(".unionfs") + i_inosize + countersize - 1;
	char name[nlen + 1];

	int err;
	struct dentry *tmp_dentry = NULL;
	struct dentry *hidden_dentry;
	struct dentry *hidden_dir_dentry = NULL;

	hidden_dentry = unionfs_lower_dentry_idx(dentry, bstart);

	sprintf(name, ".unionfs%*.*lx",
			i_inosize, i_inosize, hidden_dentry->d_inode->i_ino);

	tmp_dentry = NULL;
	do {
		char *suffix = name + nlen - countersize;

		dput(tmp_dentry);
		counter++;
		sprintf(suffix, "%*.*x", countersize, countersize, counter);

		printk(KERN_DEBUG "unionfs: trying to rename %s to %s\n",
				dentry->d_name.name, name);

		tmp_dentry = lookup_one_len(name, hidden_dentry->d_parent,
					    UNIONFS_TMPNAM_LEN);
		if (IS_ERR(tmp_dentry)) {
			err = PTR_ERR(tmp_dentry);
			goto out;
		}
	} while (tmp_dentry->d_inode != NULL);	/* need negative dentry */

	err = copyup_named_file(dentry->d_parent->d_inode, file, name, bstart,
				bindex, file->f_dentry->d_inode->i_size);
	if (err)
		goto out;

	/* bring it to the same state as an unlinked file */
	hidden_dentry = unionfs_lower_dentry_idx(dentry, dbstart(dentry));
	hidden_dir_dentry = lock_parent(hidden_dentry);
	err = vfs_unlink(hidden_dir_dentry->d_inode, hidden_dentry);
	unlock_dir(hidden_dir_dentry);

out:
	return err;
}

/* put all references held by upper struct file and free lower file pointer
 * array
 */
static void cleanup_file(struct file *file)
{
	int bindex, bstart, bend;
	struct file **lf;

	lf = UNIONFS_F(file)->lower_files;
	bstart = fbstart(file);
	bend = fbend(file);

	for (bindex = bstart; bindex <= bend; bindex++) {
		if (unionfs_lower_file_idx(file, bindex)) {
			branchput(file->f_dentry->d_sb, bindex);
			fput(unionfs_lower_file_idx(file, bindex));
		}
	}

	UNIONFS_F(file)->lower_files = NULL;
	kfree(lf);
}

/* open all lower files for a given file */
static int open_all_files(struct file *file)
{
	int bindex, bstart, bend, err = 0;
	struct file *hidden_file;
	struct dentry *hidden_dentry;
	struct dentry *dentry = file->f_dentry;
	struct super_block *sb = dentry->d_sb;

	bstart = dbstart(dentry);
	bend = dbend(dentry);

	for (bindex = bstart; bindex <= bend; bindex++) {
		hidden_dentry = unionfs_lower_dentry_idx(dentry, bindex);
		if (!hidden_dentry)
			continue;

		dget(hidden_dentry);
		mntget(unionfs_lower_mnt_idx(dentry, bindex));
		branchget(sb, bindex);

		hidden_file = dentry_open(hidden_dentry,
				unionfs_lower_mnt_idx(dentry, bindex),
				file->f_flags);
		if (IS_ERR(hidden_file)) {
			err = PTR_ERR(hidden_file);
			goto out;
		} else
			unionfs_set_lower_file_idx(file, bindex, hidden_file);
	}
out:
	return err;
}

/* open the highest priority file for a given upper file */
static int open_highest_file(struct file *file, int willwrite)
{
	int bindex, bstart, bend, err = 0;
	struct file *hidden_file;
	struct dentry *hidden_dentry;

	struct dentry *dentry = file->f_dentry;
	struct inode *parent_inode = dentry->d_parent->d_inode;
	struct super_block *sb = dentry->d_sb;
	size_t inode_size = dentry->d_inode->i_size;

	bstart = dbstart(dentry);
	bend = dbend(dentry);

	hidden_dentry = unionfs_lower_dentry(dentry);
	if (willwrite && IS_WRITE_FLAG(file->f_flags) && is_robranch(dentry)) {
		for (bindex = bstart - 1; bindex >= 0; bindex--) {
			err = copyup_file(parent_inode, file, bstart, bindex,
					  inode_size);
			if (!err)
				break;
		}
		atomic_set(&UNIONFS_F(file)->generation,
			atomic_read(&UNIONFS_I(dentry->d_inode)->generation));
		goto out;
	}

	dget(hidden_dentry);
	mntget(unionfs_lower_mnt_idx(dentry, bstart));
	branchget(sb, bstart);
	hidden_file = dentry_open(hidden_dentry,
			unionfs_lower_mnt_idx(dentry, bstart), file->f_flags);
	if (IS_ERR(hidden_file)) {
		err = PTR_ERR(hidden_file);
		goto out;
	}
	unionfs_set_lower_file(file, hidden_file);
	/* Fix up the position. */
	hidden_file->f_pos = file->f_pos;

	memcpy(&hidden_file->f_ra, &file->f_ra, sizeof(struct file_ra_state));
out:
	return err;
}

static int do_delayed_copyup(struct file *file, struct dentry *dentry)
{
	int bindex, bstart, bend, err = 0;
	struct inode *parent_inode = dentry->d_parent->d_inode;
	loff_t inode_size = file->f_dentry->d_inode->i_size;

	bstart = fbstart(file);
	bend = fbend(file);

	BUG_ON(!S_ISREG(file->f_dentry->d_inode->i_mode));

	for (bindex = bstart - 1; bindex >= 0; bindex--) {
		if (!d_deleted(file->f_dentry))
			err = copyup_file(parent_inode, file, bstart,
					bindex, inode_size);
		else
			err = copyup_deleted_file(file, dentry, bstart, bindex);

		if (!err)
			break;
	}
	if (!err && (bstart > fbstart(file))) {
		bend = fbend(file);
		for (bindex = bstart; bindex <= bend; bindex++) {
			if (unionfs_lower_file_idx(file, bindex)) {
				branchput(dentry->d_sb, bindex);
				fput(unionfs_lower_file_idx(file, bindex));
				unionfs_set_lower_file_idx(file, bindex, NULL);
			}
		}
		fbend(file) = bend;
	}
	return err;
}

/* revalidate the stuct file */
int unionfs_file_revalidate(struct file *file, int willwrite)
{
	struct super_block *sb;
	struct dentry *dentry;
	int sbgen, fgen, dgen;
	int bstart, bend;
	int size;

	int err = 0;

	dentry = file->f_dentry;
	unionfs_lock_dentry(dentry);
	sb = dentry->d_sb;
	unionfs_read_lock(sb);
	if (!unionfs_d_revalidate(dentry, NULL) && !d_deleted(dentry)) {
		err = -ESTALE;
		goto out;
	}

	sbgen = atomic_read(&UNIONFS_SB(sb)->generation);
	dgen = atomic_read(&UNIONFS_D(dentry)->generation);
	fgen = atomic_read(&UNIONFS_F(file)->generation);

	BUG_ON(sbgen > dgen);

	/* There are two cases we are interested in.  The first is if the
	 * generation is lower than the super-block.  The second is if someone
	 * has copied up this file from underneath us, we also need to refresh
	 * things.
	 */
	if (!d_deleted(dentry) &&
	    (sbgen > fgen || dbstart(dentry) != fbstart(file))) {
		/* First we throw out the existing files. */
		cleanup_file(file);

		/* Now we reopen the file(s) as in unionfs_open. */
		bstart = fbstart(file) = dbstart(dentry);
		bend = fbend(file) = dbend(dentry);

		size = sizeof(struct file *) * sbmax(sb);
		UNIONFS_F(file)->lower_files = kzalloc(size, GFP_KERNEL);
		if (!UNIONFS_F(file)->lower_files) {
			err = -ENOMEM;
			goto out;
		}

		if (S_ISDIR(dentry->d_inode->i_mode)) {
			/* We need to open all the files. */
			err = open_all_files(file);
			if (err)
				goto out;
		} else {
			/* We only open the highest priority branch. */
			err = open_highest_file(file, willwrite);
			if (err)
				goto out;
		}
		atomic_set(&UNIONFS_F(file)->generation,
			   atomic_read(&UNIONFS_I(dentry->d_inode)->
				       generation));
	}

	/* Copyup on the first write to a file on a readonly branch. */
	if (willwrite && IS_WRITE_FLAG(file->f_flags) &&
	    !IS_WRITE_FLAG(unionfs_lower_file(file)->f_flags) &&
	    is_robranch(dentry)) {
		printk(KERN_DEBUG "Doing delayed copyup of a read-write "
				  "file on a read-only branch.\n");
		err = do_delayed_copyup(file, dentry);
	}

out:
	unionfs_unlock_dentry(dentry);
	unionfs_read_unlock(dentry->d_sb);
	return err;
}

/* unionfs_open helper function: open a directory */
static int __open_dir(struct inode *inode, struct file *file)
{
	struct dentry *hidden_dentry;
	struct file *hidden_file;
	int bindex, bstart, bend;

	bstart = fbstart(file) = dbstart(file->f_dentry);
	bend = fbend(file) = dbend(file->f_dentry);

	for (bindex = bstart; bindex <= bend; bindex++) {
		hidden_dentry = unionfs_lower_dentry_idx(file->f_dentry, bindex);
		if (!hidden_dentry)
			continue;

		dget(hidden_dentry);
		mntget(unionfs_lower_mnt_idx(file->f_dentry, bindex));
		hidden_file = dentry_open(hidden_dentry,
				unionfs_lower_mnt_idx(file->f_dentry, bindex),
				file->f_flags);
		if (IS_ERR(hidden_file))
			return PTR_ERR(hidden_file);

		unionfs_set_lower_file_idx(file, bindex, hidden_file);

		/* The branchget goes after the open, because otherwise
		 * we would miss the reference on release.
		 */
		branchget(inode->i_sb, bindex);
	}

	return 0;
}

/* unionfs_open helper function: open a file */
static int __open_file(struct inode *inode, struct file *file)
{
	struct dentry *hidden_dentry;
	struct file *hidden_file;
	int hidden_flags;
	int bindex, bstart, bend;

	hidden_dentry = unionfs_lower_dentry(file->f_dentry);
	hidden_flags = file->f_flags;

	bstart = fbstart(file) = dbstart(file->f_dentry);
	bend = fbend(file) = dbend(file->f_dentry);

	/* check for the permission for hidden file.  If the error is COPYUP_ERR,
	 * copyup the file.
	 */
	if (hidden_dentry->d_inode && is_robranch(file->f_dentry)) {
		/* if the open will change the file, copy it up otherwise
		 * defer it.
		 */
		if (hidden_flags & O_TRUNC) {
			int size = 0;
			int err = -EROFS;

			/* copyup the file */
			for (bindex = bstart - 1; bindex >= 0; bindex--) {
				err = copyup_file(file->f_dentry->d_parent->d_inode,
						file, bstart, bindex, size);
				if (!err)
					break;
			}
			return err;
		} else
			hidden_flags &= ~(OPEN_WRITE_FLAGS);
	}

	dget(hidden_dentry);

	/* dentry_open will decrement mnt refcnt if err.
	 * otherwise fput() will do an mntput() for us upon file close.
	 */
	mntget(unionfs_lower_mnt_idx(file->f_dentry, bstart));
	hidden_file = dentry_open(hidden_dentry,
				  unionfs_lower_mnt_idx(file->f_dentry, bstart),
				  hidden_flags);
	if (IS_ERR(hidden_file))
		return PTR_ERR(hidden_file);

	unionfs_set_lower_file(file, hidden_file);
	branchget(inode->i_sb, bstart);

	return 0;
}

int unionfs_open(struct inode *inode, struct file *file)
{
	int err = 0;
	struct file *hidden_file = NULL;
	struct dentry *dentry = NULL;
	int bindex = 0, bstart = 0, bend = 0;
	int size;

	file->private_data = kzalloc(sizeof(struct unionfs_file_info), GFP_KERNEL);
	if (!UNIONFS_F(file)) {
		err = -ENOMEM;
		goto out;
	}
	fbstart(file) = -1;
	fbend(file) = -1;
	atomic_set(&UNIONFS_F(file)->generation,
		   atomic_read(&UNIONFS_I(inode)->generation));

	size = sizeof(struct file *) * sbmax(inode->i_sb);
	UNIONFS_F(file)->lower_files = kzalloc(size, GFP_KERNEL);
	if (!UNIONFS_F(file)->lower_files) {
		err = -ENOMEM;
		goto out;
	}

	dentry = file->f_dentry;
	unionfs_lock_dentry(dentry);
	unionfs_read_lock(inode->i_sb);

	bstart = fbstart(file) = dbstart(dentry);
	bend = fbend(file) = dbend(dentry);

	/* increment, so that we can flush appropriately */
	atomic_inc(&UNIONFS_I(dentry->d_inode)->totalopens);

	/* open all directories and make the unionfs file struct point to
	 * these hidden file structs
	 */
	if (S_ISDIR(inode->i_mode))
		err = __open_dir(inode, file);	/* open a dir */
	else
		err = __open_file(inode, file);	/* open a file */

	/* freeing the allocated resources, and fput the opened files */
	if (err) {
		for (bindex = bstart; bindex <= bend; bindex++) {
			hidden_file = unionfs_lower_file_idx(file, bindex);
			if (!hidden_file)
				continue;

			branchput(file->f_dentry->d_sb, bindex);
			/* fput calls dput for hidden_dentry */
			fput(hidden_file);
		}
	}

	unionfs_unlock_dentry(dentry);
	unionfs_read_unlock(inode->i_sb);

out:
	if (err) {
		kfree(UNIONFS_F(file)->lower_files);
		kfree(UNIONFS_F(file));
	}

	return err;
}

/* release all lower object references & free the file info structure */
int unionfs_file_release(struct inode *inode, struct file *file)
{
	struct file *hidden_file = NULL;
	struct unionfs_file_info *fileinfo = UNIONFS_F(file);
	struct unionfs_inode_info *inodeinfo = UNIONFS_I(inode);
	int bindex, bstart, bend;
	int fgen;

	/* fput all the hidden files */
	fgen = atomic_read(&fileinfo->generation);
	bstart = fbstart(file);
	bend = fbend(file);

	for (bindex = bstart; bindex <= bend; bindex++) {
		hidden_file = unionfs_lower_file_idx(file, bindex);

		if (hidden_file) {
			fput(hidden_file);
			unionfs_read_lock(inode->i_sb);
			branchput(inode->i_sb, bindex);
			unionfs_read_unlock(inode->i_sb);
		}
	}
	kfree(fileinfo->lower_files);

	if (fileinfo->rdstate) {
		fileinfo->rdstate->access = jiffies;
		printk(KERN_DEBUG "Saving rdstate with cookie %u [%d.%lld]\n",
		       fileinfo->rdstate->cookie,
		       fileinfo->rdstate->bindex,
		       (long long)fileinfo->rdstate->dirpos);
		spin_lock(&inodeinfo->rdlock);
		inodeinfo->rdcount++;
		list_add_tail(&fileinfo->rdstate->cache,
			      &inodeinfo->readdircache);
		mark_inode_dirty(inode);
		spin_unlock(&inodeinfo->rdlock);
		fileinfo->rdstate = NULL;
	}
	kfree(fileinfo);
	return 0;
}

/* pass the ioctl to the lower fs */
static long do_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct file *hidden_file;
	int err;

	hidden_file = unionfs_lower_file(file);

	err = security_file_ioctl(hidden_file, cmd, arg);
	if (err)
		goto out;

	err = -ENOTTY;
	if (!hidden_file || !hidden_file->f_op)
		goto out;
	if (hidden_file->f_op->unlocked_ioctl) {
		err = hidden_file->f_op->unlocked_ioctl(hidden_file, cmd, arg);
	} else if (hidden_file->f_op->ioctl) {
		lock_kernel();
		err = hidden_file->f_op->ioctl(hidden_file->f_dentry->d_inode,
					       hidden_file, cmd, arg);
		unlock_kernel();
	}

out:
	return err;
}

long unionfs_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long err;

	if ((err = unionfs_file_revalidate(file, 1)))
		goto out;

	/* check if asked for local commands */
	switch (cmd) {
	case UNIONFS_IOCTL_INCGEN:
		/* Increment the superblock generation count */
		err = -EACCES;
		if (!capable(CAP_SYS_ADMIN))
			goto out;
		err = unionfs_ioctl_incgen(file, cmd, arg);
		break;

	case UNIONFS_IOCTL_QUERYFILE:
		/* Return list of branches containing the given file */
		err = unionfs_ioctl_queryfile(file, cmd, arg);
		break;

	default:
		/* pass the ioctl down */
		err = do_ioctl(file, cmd, arg);
		break;
	}

out:
	return err;
}

int unionfs_flush(struct file *file, fl_owner_t id)
{
	int err = 0;
	struct file *hidden_file = NULL;
	struct dentry *dentry = file->f_dentry;
	int bindex, bstart, bend;

	if ((err = unionfs_file_revalidate(file, 1)))
		goto out;
	if (!atomic_dec_and_test(&UNIONFS_I(dentry->d_inode)->totalopens))
		goto out;

	unionfs_lock_dentry(dentry);

	bstart = fbstart(file);
	bend = fbend(file);
	for (bindex = bstart; bindex <= bend; bindex++) {
		hidden_file = unionfs_lower_file_idx(file, bindex);

		if (hidden_file && hidden_file->f_op && hidden_file->f_op->flush) {
			err = hidden_file->f_op->flush(hidden_file, id);
			if (err)
				goto out_lock;

			/* if there are no more references to the dentry, dput it */
			if (d_deleted(dentry)) {
				dput(unionfs_lower_dentry_idx(dentry, bindex));
				unionfs_set_lower_dentry_idx(dentry, bindex, NULL);
			}
		}

	}

out_lock:
	unionfs_unlock_dentry(dentry);
out:
	return err;
}

