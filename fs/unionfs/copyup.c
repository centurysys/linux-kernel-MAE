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
 * Copyright (c) 2003-2007 The Research Foundation of State University of New York*
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "union.h"

static int copyup_named_dentry(struct inode *dir, struct dentry *dentry,
			       int bstart, int new_bindex, const char *name,
			       int namelen, struct file **copyup_file,
			       loff_t len);
static struct dentry *create_parents_named(struct inode *dir,
					   struct dentry *dentry,
					   const char *name, int bindex);

/* For detailed explanation of copyup see:
 * Documentation/filesystems/unionfs/concepts.txt
 */

#ifdef CONFIG_UNION_FS_XATTR
/* copyup all extended attrs for a given dentry */
static int copyup_xattrs(struct dentry *old_hidden_dentry,
			 struct dentry *new_hidden_dentry)
{
	int err = 0;
	ssize_t list_size = -1;
	char *name_list = NULL;
	char *attr_value = NULL;
	char *name_list_orig = NULL;

	list_size = vfs_listxattr(old_hidden_dentry, NULL, 0);

	if (list_size <= 0) {
		err = list_size;
		goto out;
	}

	name_list = unionfs_xattr_alloc(list_size + 1, XATTR_LIST_MAX);
	if (!name_list || IS_ERR(name_list)) {
		err = PTR_ERR(name_list);
		goto out;
	}
	list_size = vfs_listxattr(old_hidden_dentry, name_list, list_size);
	attr_value = unionfs_xattr_alloc(XATTR_SIZE_MAX, XATTR_SIZE_MAX);
	if (!attr_value || IS_ERR(attr_value)) {
		err = PTR_ERR(name_list);
		goto out;
	}
	name_list_orig = name_list;
	while (*name_list) {
		ssize_t size;

		/* Lock here since vfs_getxattr doesn't lock for us */
		mutex_lock(&old_hidden_dentry->d_inode->i_mutex);
		size = vfs_getxattr(old_hidden_dentry, name_list,
				    attr_value, XATTR_SIZE_MAX);
		mutex_unlock(&old_hidden_dentry->d_inode->i_mutex);
		if (size < 0) {
			err = size;
			goto out;
		}

		if (size > XATTR_SIZE_MAX) {
			err = -E2BIG;
			goto out;
		}
		/* Don't lock here since vfs_setxattr does it for us. */
		err = vfs_setxattr(new_hidden_dentry, name_list, attr_value,
				   size, 0);

		if (err < 0)
			goto out;
		name_list += strlen(name_list) + 1;
	}
      out:
	name_list = name_list_orig;

	if (name_list)
		unionfs_xattr_free(name_list, list_size + 1);
	if (attr_value)
		unionfs_xattr_free(attr_value, XATTR_SIZE_MAX);
	/* It is no big deal if this fails, we just roll with the punches. */
	if (err == -ENOTSUPP || err == -EOPNOTSUPP)
		err = 0;
	return err;
}
#endif /* CONFIG_UNION_FS_XATTR */

/* Determine the mode based on the copyup flags, and the existing dentry. */
static int copyup_permissions(struct super_block *sb,
			      struct dentry *old_hidden_dentry,
			      struct dentry *new_hidden_dentry)
{
	struct inode *i = old_hidden_dentry->d_inode;
	struct iattr newattrs;
	int err;

	newattrs.ia_atime = i->i_atime;
	newattrs.ia_mtime = i->i_mtime;
	newattrs.ia_ctime = i->i_ctime;

	newattrs.ia_gid = i->i_gid;
	newattrs.ia_uid = i->i_uid;

	newattrs.ia_mode = i->i_mode;

	newattrs.ia_valid = ATTR_CTIME | ATTR_ATIME | ATTR_MTIME |
	    ATTR_ATIME_SET | ATTR_MTIME_SET | ATTR_FORCE |
	    ATTR_GID | ATTR_UID | ATTR_MODE;

	err = notify_change(new_hidden_dentry, &newattrs);

	return err;
}

int copyup_dentry(struct inode *dir, struct dentry *dentry,
		  int bstart, int new_bindex,
		  struct file **copyup_file, loff_t len)
{
	return copyup_named_dentry(dir, dentry, bstart, new_bindex,
				   dentry->d_name.name,
				   dentry->d_name.len, copyup_file, len);
}

/* create the new device/file/directory - use copyup_permission to copyup
 * times, and mode
 *
 * if the object being copied up is a regular file, the file is only created,
 * the contents have to be copied up separately
 */
static int __copyup_ndentry(struct dentry *old_hidden_dentry,
			    struct dentry *new_hidden_dentry,
			    struct dentry *new_hidden_parent_dentry,
			    char *symbuf)
{
	int err = 0;
	umode_t old_mode = old_hidden_dentry->d_inode->i_mode;
	struct sioq_args args;

	if (S_ISDIR(old_mode)) {
		args.mkdir.parent = new_hidden_parent_dentry->d_inode;
		args.mkdir.dentry = new_hidden_dentry;
		args.mkdir.mode = old_mode;

		run_sioq(__unionfs_mkdir, &args);
		err = args.err;
	} else if (S_ISLNK(old_mode)) {
		args.symlink.parent = new_hidden_parent_dentry->d_inode;
		args.symlink.dentry = new_hidden_dentry;
		args.symlink.symbuf = symbuf;
		args.symlink.mode = old_mode;

		run_sioq(__unionfs_symlink, &args);
		err = args.err;
	} else if (S_ISBLK(old_mode) || S_ISCHR(old_mode) ||
		   S_ISFIFO(old_mode) || S_ISSOCK(old_mode)) {
		args.mknod.parent = new_hidden_parent_dentry->d_inode;
		args.mknod.dentry = new_hidden_dentry;
		args.mknod.mode = old_mode;
		args.mknod.dev = old_hidden_dentry->d_inode->i_rdev;

		run_sioq(__unionfs_mknod, &args);
		err = args.err;
	} else if (S_ISREG(old_mode)) {
		args.create.parent = new_hidden_parent_dentry->d_inode;
		args.create.dentry = new_hidden_dentry;
		args.create.mode = old_mode;
		args.create.nd = NULL;

		run_sioq(__unionfs_create, &args);
		err = args.err;
	} else {
		printk(KERN_ERR "Unknown inode type %d\n",
				old_mode);
		BUG();
	}

	return err;
}

static int __copyup_reg_data(struct dentry *dentry,
			     struct dentry *new_hidden_dentry, int new_bindex,
			     struct dentry *old_hidden_dentry, int old_bindex,
			     struct file **copyup_file, loff_t len)
{
	struct super_block *sb = dentry->d_sb;
	struct file *input_file;
	struct file *output_file;
	mm_segment_t old_fs;
	char *buf = NULL;
	ssize_t read_bytes, write_bytes;
	loff_t size;
	int err = 0;

	/* open old file */
	mntget(unionfs_lower_mnt_idx(dentry, old_bindex));
	branchget(sb, old_bindex);
	input_file = dentry_open(old_hidden_dentry,
				unionfs_lower_mnt_idx(dentry, old_bindex),
				O_RDONLY | O_LARGEFILE);
	if (IS_ERR(input_file)) {
		dput(old_hidden_dentry);
		err = PTR_ERR(input_file);
		goto out;
	}
	if (!input_file->f_op || !input_file->f_op->read) {
		err = -EINVAL;
		goto out_close_in;
	}

	/* open new file */
	dget(new_hidden_dentry);
	mntget(unionfs_lower_mnt_idx(dentry, new_bindex));
	branchget(sb, new_bindex);
	output_file = dentry_open(new_hidden_dentry,
				unionfs_lower_mnt_idx(dentry, new_bindex),
				O_WRONLY | O_LARGEFILE);
	if (IS_ERR(output_file)) {
		err = PTR_ERR(output_file);
		goto out_close_in2;
	}
	if (!output_file->f_op || !output_file->f_op->write) {
		err = -EINVAL;
		goto out_close_out;
	}

	/* allocating a buffer */
	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto out_close_out;
	}

	input_file->f_pos = 0;
	output_file->f_pos = 0;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	size = len;
	err = 0;
	do {
		if (len >= PAGE_SIZE)
			size = PAGE_SIZE;
		else if ((len < PAGE_SIZE) && (len > 0))
			size = len;

		len -= PAGE_SIZE;

		read_bytes =
		    input_file->f_op->read(input_file,
					   (char __user *)buf, size,
					   &input_file->f_pos);
		if (read_bytes <= 0) {
			err = read_bytes;
			break;
		}

		write_bytes =
		    output_file->f_op->write(output_file,
					     (char __user *)buf,
					     read_bytes,
					     &output_file->f_pos);
		if ((write_bytes < 0) || (write_bytes < read_bytes)) {
			err = write_bytes;
			break;
		}
	} while ((read_bytes > 0) && (len > 0));

	set_fs(old_fs);

	kfree(buf);

	if (err)
		goto out_close_out;
	if (copyup_file) {
		*copyup_file = output_file;
		goto out_close_in;
	}

out_close_out:
	fput(output_file);

out_close_in2:
	branchput(sb, new_bindex);

out_close_in:
	fput(input_file);

out:
	branchput(sb, old_bindex);

	return err;
}

/* dput the lower references for old and new dentry & clear a lower dentry
 * pointer
 */
static void __clear(struct dentry *dentry, struct dentry *old_hidden_dentry,
		    int old_bstart, int old_bend,
		    struct dentry *new_hidden_dentry, int new_bindex)
{
	/* get rid of the hidden dentry and all its traces */
	unionfs_set_lower_dentry_idx(dentry, new_bindex, NULL);
	set_dbstart(dentry, old_bstart);
	set_dbend(dentry, old_bend);

	dput(new_hidden_dentry);
	dput(old_hidden_dentry);
}

/* copy up a dentry to a file of specified name */
static int copyup_named_dentry(struct inode *dir, struct dentry *dentry,
			       int bstart, int new_bindex, const char *name,
			       int namelen, struct file **copyup_file,
			       loff_t len)
{
	struct dentry *new_hidden_dentry;
	struct dentry *old_hidden_dentry = NULL;
	struct super_block *sb;
	int err = 0;
	int old_bindex;
	int old_bstart;
	int old_bend;
	struct dentry *new_hidden_parent_dentry = NULL;
	mm_segment_t oldfs;
	char *symbuf = NULL;

	verify_locked(dentry);

	old_bindex = bstart;
	old_bstart = dbstart(dentry);
	old_bend = dbend(dentry);

	BUG_ON(new_bindex < 0);
	BUG_ON(new_bindex >= old_bindex);

	sb = dir->i_sb;

	unionfs_read_lock(sb);

	if ((err = is_robranch_super(sb, new_bindex))) {
		dput(old_hidden_dentry);
		goto out;
	}

	/* Create the directory structure above this dentry. */
	new_hidden_dentry = create_parents_named(dir, dentry, name, new_bindex);
	if (IS_ERR(new_hidden_dentry)) {
		dput(old_hidden_dentry);
		err = PTR_ERR(new_hidden_dentry);
		goto out;
	}

	old_hidden_dentry = unionfs_lower_dentry_idx(dentry, old_bindex);
	dget(old_hidden_dentry);

	/* For symlinks, we must read the link before we lock the directory. */
	if (S_ISLNK(old_hidden_dentry->d_inode->i_mode)) {

		symbuf = kmalloc(PATH_MAX, GFP_KERNEL);
		if (!symbuf) {
			__clear(dentry, old_hidden_dentry,
				old_bstart, old_bend,
				new_hidden_dentry, new_bindex);
			err = -ENOMEM;
			goto out_free;
		}

		oldfs = get_fs();
		set_fs(KERNEL_DS);
		err = old_hidden_dentry->d_inode->i_op->readlink(
					old_hidden_dentry,
					(char __user *)symbuf,
					PATH_MAX);
		set_fs(oldfs);
		if (err) {
			__clear(dentry, old_hidden_dentry,
				old_bstart, old_bend,
				new_hidden_dentry, new_bindex);
			goto out_free;
		}
		symbuf[err] = '\0';
	}

	/* Now we lock the parent, and create the object in the new branch. */
	new_hidden_parent_dentry = lock_parent(new_hidden_dentry);

	/* create the new inode */
	err = __copyup_ndentry(old_hidden_dentry, new_hidden_dentry,
			       new_hidden_parent_dentry, symbuf);

	if (err) {
		__clear(dentry, old_hidden_dentry,
			old_bstart, old_bend,
			new_hidden_dentry, new_bindex);
		goto out_unlock;
	}

	/* We actually copyup the file here. */
	if (S_ISREG(old_hidden_dentry->d_inode->i_mode))
		err = __copyup_reg_data(dentry, new_hidden_dentry, new_bindex,
				old_hidden_dentry, old_bindex, copyup_file, len);
	if (err)
		goto out_unlink;

	/* Set permissions. */
	if ((err = copyup_permissions(sb, old_hidden_dentry, new_hidden_dentry)))
		goto out_unlink;

#ifdef CONFIG_UNION_FS_XATTR
	/* Selinux uses extended attributes for permissions. */
	if ((err = copyup_xattrs(old_hidden_dentry, new_hidden_dentry)))
		goto out_unlink;
#endif

	/* do not allow files getting deleted to be reinterposed */
	if (!d_deleted(dentry))
		unionfs_reinterpose(dentry);

	goto out_unlock;
	/****/

out_unlink:
	/* copyup failed, because we possibly ran out of space or
	 * quota, or something else happened so let's unlink; we don't
	 * really care about the return value of vfs_unlink
	 */
	vfs_unlink(new_hidden_parent_dentry->d_inode, new_hidden_dentry);

	if (copyup_file) {
		/* need to close the file */

		fput(*copyup_file);
		branchput(sb, new_bindex);
	}

	/*
	 * TODO: should we reset the error to something like -EIO?
	 *
	 * If we don't reset, the user may get some non-sensical errors, but
	 * on the other hand, if we reset to EIO, we guarantee that the user
	 * will get a "confusing" error message.
	 */

out_unlock:
	unlock_dir(new_hidden_parent_dentry);

out_free:
	kfree(symbuf);

out:
	unionfs_read_unlock(sb);

	return err;
}

/* This function creates a copy of a file represented by 'file' which currently
 * resides in branch 'bstart' to branch 'new_bindex.'  The copy will be named
 * "name".
 */
int copyup_named_file(struct inode *dir, struct file *file, char *name,
		      int bstart, int new_bindex, loff_t len)
{
	int err = 0;
	struct file *output_file = NULL;

	err = copyup_named_dentry(dir, file->f_dentry, bstart,
				  new_bindex, name, strlen(name), &output_file,
				  len);
	if (!err) {
		fbstart(file) = new_bindex;
		unionfs_set_lower_file_idx(file, new_bindex, output_file);
	}

	return err;
}

/* This function creates a copy of a file represented by 'file' which currently
 * resides in branch 'bstart' to branch 'new_bindex'.
 */
int copyup_file(struct inode *dir, struct file *file, int bstart,
		int new_bindex, loff_t len)
{
	int err = 0;
	struct file *output_file = NULL;

	err = copyup_dentry(dir, file->f_dentry, bstart, new_bindex,
			    &output_file, len);
	if (!err) {
		fbstart(file) = new_bindex;
		unionfs_set_lower_file_idx(file, new_bindex, output_file);
	}

	return err;
}

/* This function replicates the directory structure upto given dentry
 * in the bindex branch. Can create directory structure recursively to the right
 * also.
 */
struct dentry *create_parents(struct inode *dir, struct dentry *dentry,
			      int bindex)
{
	return create_parents_named(dir, dentry, dentry->d_name.name, bindex);
}

static void __cleanup_dentry(struct dentry * dentry, int bindex,
			     int old_bstart, int old_bend)
{
	int loop_start;
	int loop_end;
	int new_bstart = -1;
	int new_bend = -1;
	int i;

	loop_start = min(old_bstart, bindex);
	loop_end = max(old_bend, bindex);

	/* This loop sets the bstart and bend for the new dentry by
	 * traversing from left to right.  It also dputs all negative
	 * dentries except bindex
	 */
	for (i = loop_start; i <= loop_end; i++) {
		if (!unionfs_lower_dentry_idx(dentry, i))
			continue;

		if (i == bindex) {
			new_bend = i;
			if (new_bstart < 0)
				new_bstart = i;
			continue;
		}

		if (!unionfs_lower_dentry_idx(dentry, i)->d_inode) {
			dput(unionfs_lower_dentry_idx(dentry, i));
			unionfs_set_lower_dentry_idx(dentry, i, NULL);
		} else {
			if (new_bstart < 0)
				new_bstart = i;
			new_bend = i;
		}
	}

	if (new_bstart < 0)
		new_bstart = bindex;
	if (new_bend < 0)
		new_bend = bindex;
	set_dbstart(dentry, new_bstart);
	set_dbend(dentry, new_bend);

}

/* set lower inode ptr and update bstart & bend if necessary */
static void __set_inode(struct dentry * upper, struct dentry * lower,
			int bindex)
{
	unionfs_set_lower_inode_idx(upper->d_inode, bindex,
			igrab(lower->d_inode));
	if (likely(ibstart(upper->d_inode) > bindex))
		ibstart(upper->d_inode) = bindex;
	if (likely(ibend(upper->d_inode) < bindex))
		ibend(upper->d_inode) = bindex;

}

/* set lower dentry ptr and update bstart & bend if necessary */
static void __set_dentry(struct dentry * upper, struct dentry * lower,
			 int bindex)
{
	unionfs_set_lower_dentry_idx(upper, bindex, lower);
	if (likely(dbstart(upper) > bindex))
		set_dbstart(upper, bindex);
	if (likely(dbend(upper) < bindex))
		set_dbend(upper, bindex);
}

/* This function replicates the directory structure upto given dentry
 * in the bindex branch.
 */
static struct dentry *create_parents_named(struct inode *dir,
					   struct dentry *dentry,
					   const char *name, int bindex)
{
	int err;
	struct dentry *child_dentry;
	struct dentry *parent_dentry;
	struct dentry *hidden_parent_dentry = NULL;
	struct dentry *hidden_dentry = NULL;
	const char *childname;
	unsigned int childnamelen;

	int old_kmalloc_size;
	int kmalloc_size;
	int num_dentry;
	int count;

	int old_bstart;
	int old_bend;
	struct dentry **path = NULL;
	struct dentry **tmp_path;
	struct super_block *sb;

	verify_locked(dentry);

	/* There is no sense allocating any less than the minimum. */
	kmalloc_size = malloc_sizes[0].cs_size;
	num_dentry = kmalloc_size / sizeof(struct dentry *);

	if ((err = is_robranch_super(dir->i_sb, bindex))) {
		hidden_dentry = ERR_PTR(err);
		goto out;
	}

	old_bstart = dbstart(dentry);
	old_bend = dbend(dentry);

	hidden_dentry = ERR_PTR(-ENOMEM);
	path = kzalloc(kmalloc_size, GFP_KERNEL);
	if (!path)
		goto out;

	/* assume the negative dentry of unionfs as the parent dentry */
	parent_dentry = dentry;

	count = 0;
	/* This loop finds the first parent that exists in the given branch.
	 * We start building the directory structure from there.  At the end
	 * of the loop, the following should hold:
	 *  - child_dentry is the first nonexistent child
	 *  - parent_dentry is the first existent parent
	 *  - path[0] is the = deepest child
	 *  - path[count] is the first child to create
	 */
	do {
		child_dentry = parent_dentry;

		/* find the parent directory dentry in unionfs */
		parent_dentry = child_dentry->d_parent;
		unionfs_lock_dentry(parent_dentry);

		/* find out the hidden_parent_dentry in the given branch */
		hidden_parent_dentry = unionfs_lower_dentry_idx(parent_dentry, bindex);

		/* store the child dentry */
		path[count++] = child_dentry;

		/* grow path table */
		if (count == num_dentry) {
			old_kmalloc_size = kmalloc_size;
			kmalloc_size *= 2;
			num_dentry = kmalloc_size / sizeof(struct dentry *);

			tmp_path = kzalloc(kmalloc_size, GFP_KERNEL);
			if (!tmp_path) {
				hidden_dentry = ERR_PTR(-ENOMEM);
				goto out;
			}
			memcpy(tmp_path, path, old_kmalloc_size);
			kfree(path);
			path = tmp_path;
			tmp_path = NULL;
		}

	} while (!hidden_parent_dentry);
	count--;

	sb = dentry->d_sb;

	/* This is basically while(child_dentry != dentry).  This loop is
	 * horrible to follow and should be replaced with cleaner code.
	 */
	while (1) {
		/* get hidden parent dir in the current branch */
		hidden_parent_dentry = unionfs_lower_dentry_idx(parent_dentry, bindex);
		unionfs_unlock_dentry(parent_dentry);

		/* init the values to lookup */
		childname = child_dentry->d_name.name;
		childnamelen = child_dentry->d_name.len;

		if (child_dentry != dentry) {
			/* lookup child in the underlying file system */
			hidden_dentry =
			    lookup_one_len(childname, hidden_parent_dentry,
					   childnamelen);
			if (IS_ERR(hidden_dentry))
				goto out;
		} else {

			/* is the name a whiteout of the childname ?
			 * lookup the whiteout child in the underlying file system
			 */
			hidden_dentry =
			    lookup_one_len(name, hidden_parent_dentry,
					   strlen(name));
			if (IS_ERR(hidden_dentry))
				goto out;

			/* Replace the current dentry (if any) with the new one. */
			dput(unionfs_lower_dentry_idx(dentry, bindex));
			unionfs_set_lower_dentry_idx(dentry, bindex, hidden_dentry);

			__cleanup_dentry(dentry, bindex, old_bstart, old_bend);
			break;
		}

		if (hidden_dentry->d_inode) {
			/* since this already exists we dput to avoid
			 * multiple references on the same dentry
			 */
			dput(hidden_dentry);
		} else {
			struct sioq_args args;

			/* its a negative dentry, create a new dir */
			hidden_parent_dentry = lock_parent(hidden_dentry);

			args.mkdir.parent = hidden_parent_dentry->d_inode;
			args.mkdir.dentry = hidden_dentry;
			args.mkdir.mode = child_dentry->d_inode->i_mode;

			run_sioq(__unionfs_mkdir, &args);
			err = args.err;

			if (!err)
				err = copyup_permissions(dir->i_sb,
						child_dentry, hidden_dentry);
			unlock_dir(hidden_parent_dentry);
			if (err) {
				dput(hidden_dentry);
				hidden_dentry = ERR_PTR(err);
				goto out;
			}

		}

		__set_inode(child_dentry, hidden_dentry, bindex);
		__set_dentry(child_dentry, hidden_dentry, bindex);

		parent_dentry = child_dentry;
		child_dentry = path[--count];
	}
out:
	kfree(path);
	return hidden_dentry;
}

