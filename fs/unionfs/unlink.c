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

/* unlink a file by creating a whiteout */
static int unionfs_unlink_whiteout(struct inode *dir, struct dentry *dentry)
{
	struct dentry *hidden_dentry;
	struct dentry *hidden_dir_dentry;
	int bindex;
	int err = 0;

	if ((err = unionfs_partial_lookup(dentry)))
		goto out;

	bindex = dbstart(dentry);

	hidden_dentry = unionfs_lower_dentry_idx(dentry, bindex);
	if (!hidden_dentry)
		goto out;

	hidden_dir_dentry = lock_parent(hidden_dentry);

	/* avoid destroying the hidden inode if the file is in use */
	dget(hidden_dentry);
	if (!(err = is_robranch_super(dentry->d_sb, bindex)))
		err = vfs_unlink(hidden_dir_dentry->d_inode, hidden_dentry);
	dput(hidden_dentry);
	fsstack_copy_attr_times(dir, hidden_dir_dentry->d_inode);
	unlock_dir(hidden_dir_dentry);

	if (err && !IS_COPYUP_ERR(err))
		goto out;

	if (err) {
		if (dbstart(dentry) == 0)
			goto out;

		err = create_whiteout(dentry, dbstart(dentry) - 1);
	} else if (dbopaque(dentry) != -1)
		/* There is a hidden lower-priority file with the same name. */
		err = create_whiteout(dentry, dbopaque(dentry));
	else
		err = create_whiteout(dentry, dbstart(dentry));

out:
	if (!err)
		dentry->d_inode->i_nlink--;

	/* We don't want to leave negative leftover dentries for revalidate. */
	if (!err && (dbopaque(dentry) != -1))
		update_bstart(dentry);

	return err;
}

int unionfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int err = 0;

	unionfs_lock_dentry(dentry);

	err = unionfs_unlink_whiteout(dir, dentry);
	/* call d_drop so the system "forgets" about us */
	if (!err)
		d_drop(dentry);

	unionfs_unlock_dentry(dentry);
	return err;
}

static int unionfs_rmdir_first(struct inode *dir, struct dentry *dentry,
			       struct unionfs_dir_state *namelist)
{
	int err;
	struct dentry *hidden_dentry;
	struct dentry *hidden_dir_dentry = NULL;

	/* Here we need to remove whiteout entries. */
	err = delete_whiteouts(dentry, dbstart(dentry), namelist);
	if (err)
		goto out;

	hidden_dentry = unionfs_lower_dentry(dentry);

	hidden_dir_dentry = lock_parent(hidden_dentry);

	/* avoid destroying the hidden inode if the file is in use */
	dget(hidden_dentry);
	if (!(err = is_robranch(dentry)))
		err = vfs_rmdir(hidden_dir_dentry->d_inode, hidden_dentry);
	dput(hidden_dentry);

	fsstack_copy_attr_times(dir, hidden_dir_dentry->d_inode);
	/* propagate number of hard-links */
	dentry->d_inode->i_nlink = unionfs_get_nlinks(dentry->d_inode);

out:
	if (hidden_dir_dentry)
		unlock_dir(hidden_dir_dentry);
	return err;
}

int unionfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int err = 0;
	struct unionfs_dir_state *namelist = NULL;

	unionfs_lock_dentry(dentry);

	/* check if this unionfs directory is empty or not */
	err = check_empty(dentry, &namelist);
	if (err)
		goto out;

	err = unionfs_rmdir_first(dir, dentry, namelist);
	/* create whiteout */
	if (!err)
		err = create_whiteout(dentry, dbstart(dentry));
	else {
		int new_err;

		if (dbstart(dentry) == 0)
			goto out;

		/* exit if the error returned was NOT -EROFS */
		if (!IS_COPYUP_ERR(err))
			goto out;

		new_err = create_whiteout(dentry, dbstart(dentry) - 1);
		if (new_err != -EEXIST)
			err = new_err;
	}

out:
	/* call d_drop so the system "forgets" about us */
	if (!err)
		d_drop(dentry);

	if (namelist)
		free_rdstate(namelist);

	unionfs_unlock_dentry(dentry);
	return err;
}

