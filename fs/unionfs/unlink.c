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
 * Copyright (c) 2003-2007 The Research Foundation of SUNY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "union.h"

/* unlink a file by creating a whiteout */
static int unionfs_unlink_whiteout(struct inode *dir, struct dentry *dentry)
{
	struct dentry *lower_dentry;
	struct dentry *lower_dir_dentry;
	int bindex;
	int err = 0;

	err = unionfs_partial_lookup(dentry);
	if (err)
		goto out;

	bindex = dbstart(dentry);

	lower_dentry = unionfs_lower_dentry_idx(dentry, bindex);
	if (!lower_dentry)
		goto out;

	lower_dir_dentry = lock_parent(lower_dentry);

	/* avoid destroying the lower inode if the file is in use */
	dget(lower_dentry);
	err = is_robranch_super(dentry->d_sb, bindex);
	if (!err)
		err = vfs_unlink(lower_dir_dentry->d_inode, lower_dentry);
	/* if vfs_unlink succeeded, update our inode's times */
	if (!err)
		unionfs_copy_attr_times(dentry->d_inode);
	dput(lower_dentry);
	fsstack_copy_attr_times(dir, lower_dir_dentry->d_inode);
	unlock_dir(lower_dir_dentry);

	if (err && !IS_COPYUP_ERR(err))
		goto out;

	/*
	 * We create whiteouts if (1) there was an error unlinking the main
	 * file; (2) there is a lower priority file with the same name
	 * (dbopaque); (3) the branch in which the file is not the last
	 * (rightmost0 branch.  The last rule is an optimization to avoid
	 * creating all those whiteouts if there's no chance they'd be
	 * masking any lower-priority branch, as well as unionfs is used
	 * with only one branch (using only one branch, while odd, is still
	 * possible).
	 */
	if (err) {
		if (dbstart(dentry) == 0)
			goto out;
		err = create_whiteout(dentry, dbstart(dentry) - 1);
	} else if (dbopaque(dentry) != -1) {
		err = create_whiteout(dentry, dbopaque(dentry));
	} else if (dbstart(dentry) < sbend(dentry->d_sb)) {
		err = create_whiteout(dentry, dbstart(dentry));
	}

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

	unionfs_read_lock(dentry->d_sb);
	unionfs_lock_dentry(dentry);

	if (unlikely(!__unionfs_d_revalidate_chain(dentry, NULL, false))) {
		err = -ESTALE;
		goto out;
	}
	unionfs_check_dentry(dentry);

	err = unionfs_unlink_whiteout(dir, dentry);
	/* call d_drop so the system "forgets" about us */
	if (!err) {
		if (!S_ISDIR(dentry->d_inode->i_mode))
			unionfs_postcopyup_release(dentry);
		d_drop(dentry);
		/*
		 * if unlink/whiteout succeeded, parent dir mtime has
		 * changed
		 */
		unionfs_copy_attr_times(dir);
	}

out:
	if (!err) {
		unionfs_check_dentry(dentry);
		unionfs_check_inode(dir);
	}
	unionfs_unlock_dentry(dentry);
	unionfs_read_unlock(dentry->d_sb);
	return err;
}

static int unionfs_rmdir_first(struct inode *dir, struct dentry *dentry,
			       struct unionfs_dir_state *namelist)
{
	int err;
	struct dentry *lower_dentry;
	struct dentry *lower_dir_dentry = NULL;

	/* Here we need to remove whiteout entries. */
	err = delete_whiteouts(dentry, dbstart(dentry), namelist);
	if (err)
		goto out;

	lower_dentry = unionfs_lower_dentry(dentry);

	lower_dir_dentry = lock_parent(lower_dentry);

	/* avoid destroying the lower inode if the file is in use */
	dget(lower_dentry);
	err = is_robranch(dentry);
	if (!err)
		err = vfs_rmdir(lower_dir_dentry->d_inode, lower_dentry);
	dput(lower_dentry);

	fsstack_copy_attr_times(dir, lower_dir_dentry->d_inode);
	/* propagate number of hard-links */
	dentry->d_inode->i_nlink = unionfs_get_nlinks(dentry->d_inode);

out:
	if (lower_dir_dentry)
		unlock_dir(lower_dir_dentry);
	return err;
}

int unionfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int err = 0;
	struct unionfs_dir_state *namelist = NULL;
	int dstart, dend;

	unionfs_read_lock(dentry->d_sb);
	unionfs_lock_dentry(dentry);

	if (unlikely(!__unionfs_d_revalidate_chain(dentry, NULL, false))) {
		err = -ESTALE;
		goto out;
	}
	unionfs_check_dentry(dentry);

	/* check if this unionfs directory is empty or not */
	err = check_empty(dentry, &namelist);
	if (err)
		goto out;

	err = unionfs_rmdir_first(dir, dentry, namelist);
	dstart = dbstart(dentry);
	dend = dbend(dentry);
	/*
	 * We create a whiteout for the directory if there was an error to
	 * rmdir the first directory entry in the union.  Otherwise, we
	 * create a whiteout only if there is no chance that a lower
	 * priority branch might also have the same named directory.  IOW,
	 * if there is not another same-named directory at a lower priority
	 * branch, then we don't need to create a whiteout for it.
	 */
	if (!err) {
		if (dstart < dend)
			err = create_whiteout(dentry, dstart);
	} else {
		int new_err;

		if (dstart == 0)
			goto out;

		/* exit if the error returned was NOT -EROFS */
		if (!IS_COPYUP_ERR(err))
			goto out;

		new_err = create_whiteout(dentry, dstart - 1);
		if (new_err != -EEXIST)
			err = new_err;
	}

out:
	/*
	 * Drop references to lower dentry/inode so storage space for them
	 * can be reclaimed.  Then, call d_drop so the system "forgets"
	 * about us.
	 */
	if (!err) {
		struct inode *inode = dentry->d_inode;
		BUG_ON(!inode);
		iput(unionfs_lower_inode_idx(inode, dstart));
		unionfs_set_lower_inode_idx(inode, dstart, NULL);
		dput(unionfs_lower_dentry_idx(dentry, dstart));
		unionfs_set_lower_dentry_idx(dentry, dstart, NULL);
		/*
		 * If the last directory is unlinked, then mark istart/end
		 * as -1, (to maintain the invariant that if there are no
		 * lower objects, then branch index start and end are set to
		 * -1).
		 */
		if (!unionfs_lower_inode_idx(inode, dstart) &&
		    !unionfs_lower_inode_idx(inode, dend))
			ibstart(inode) = ibend(inode) = -1;
		d_drop(dentry);
	}

	if (namelist)
		free_rdstate(namelist);

	unionfs_unlock_dentry(dentry);
	unionfs_read_unlock(dentry->d_sb);
	return err;
}
