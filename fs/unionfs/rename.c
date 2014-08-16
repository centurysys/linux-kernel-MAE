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

static int do_rename(struct inode *old_dir, struct dentry *old_dentry,
		     struct inode *new_dir, struct dentry *new_dentry,
		     int bindex, struct dentry **wh_old)
{
	int err = 0;
	struct dentry *hidden_old_dentry;
	struct dentry *hidden_new_dentry;
	struct dentry *hidden_old_dir_dentry;
	struct dentry *hidden_new_dir_dentry;
	struct dentry *hidden_wh_dentry;
	struct dentry *hidden_wh_dir_dentry;
	char *wh_name = NULL;

	hidden_new_dentry = unionfs_lower_dentry_idx(new_dentry, bindex);
	hidden_old_dentry = unionfs_lower_dentry_idx(old_dentry, bindex);

	if (!hidden_new_dentry) {
		hidden_new_dentry =
		    create_parents(new_dentry->d_parent->d_inode, new_dentry, bindex);
		if (IS_ERR(hidden_new_dentry)) {
			printk(KERN_DEBUG "error creating directory tree for"
					  " rename, bindex = %d, err = %ld\n",
				          bindex, PTR_ERR(hidden_new_dentry));
			err = PTR_ERR(hidden_new_dentry);
			goto out;
		}
	}

	wh_name = alloc_whname(new_dentry->d_name.name, new_dentry->d_name.len);
	if (IS_ERR(wh_name)) {
		err = PTR_ERR(wh_name);
		goto out;
	}

	hidden_wh_dentry = lookup_one_len(wh_name, hidden_new_dentry->d_parent,
				new_dentry->d_name.len + UNIONFS_WHLEN);
	if (IS_ERR(hidden_wh_dentry)) {
		err = PTR_ERR(hidden_wh_dentry);
		goto out;
	}

	if (hidden_wh_dentry->d_inode) {
		/* get rid of the whiteout that is existing */
		if (hidden_new_dentry->d_inode) {
			printk(KERN_WARNING "Both a whiteout and a dentry"
					" exist when doing a rename!\n");
			err = -EIO;

			dput(hidden_wh_dentry);
			goto out;
		}

		hidden_wh_dir_dentry = lock_parent(hidden_wh_dentry);
		if (!(err = is_robranch_super(old_dentry->d_sb, bindex)))
			err = vfs_unlink(hidden_wh_dir_dentry->d_inode,
					       hidden_wh_dentry);

		dput(hidden_wh_dentry);
		unlock_dir(hidden_wh_dir_dentry);
		if (err)
			goto out;
	} else
		dput(hidden_wh_dentry);

	dget(hidden_old_dentry);
	hidden_old_dir_dentry = dget_parent(hidden_old_dentry);
	hidden_new_dir_dentry = dget_parent(hidden_new_dentry);

	lock_rename(hidden_old_dir_dentry, hidden_new_dir_dentry);

	err = is_robranch_super(old_dentry->d_sb, bindex);
	if (err)
		goto out_unlock;

	/* ready to whiteout for old_dentry. caller will create the actual
	 * whiteout, and must dput(*wh_old)
	 */
	if (wh_old) {
		char *whname;
		whname = alloc_whname(old_dentry->d_name.name,
				      old_dentry->d_name.len);
		err = PTR_ERR(whname);
		if (IS_ERR(whname))
			goto out_unlock;
		*wh_old = lookup_one_len(whname, hidden_old_dir_dentry,
					 old_dentry->d_name.len + UNIONFS_WHLEN);
		kfree(whname);
		err = PTR_ERR(*wh_old);
		if (IS_ERR(*wh_old)) {
			*wh_old = NULL;
			goto out_unlock;
		}
	}

	err = vfs_rename(hidden_old_dir_dentry->d_inode, hidden_old_dentry,
			 hidden_new_dir_dentry->d_inode, hidden_new_dentry);

out_unlock:
	unlock_rename(hidden_old_dir_dentry, hidden_new_dir_dentry);

	dput(hidden_old_dir_dentry);
	dput(hidden_new_dir_dentry);
	dput(hidden_old_dentry);

out:
	if (!err) {
		/* Fixup the newdentry. */
		if (bindex < dbstart(new_dentry))
			set_dbstart(new_dentry, bindex);
		else if (bindex > dbend(new_dentry))
			set_dbend(new_dentry, bindex);
	}

	kfree(wh_name);

	return err;
}

static int do_unionfs_rename(struct inode *old_dir,
				   struct dentry *old_dentry,
				   struct inode *new_dir,
				   struct dentry *new_dentry)
{
	int err = 0;
	int bindex, bwh_old;
	int old_bstart, old_bend;
	int new_bstart, new_bend;
	int do_copyup = -1;
	struct dentry *parent_dentry;
	int local_err = 0;
	int eio = 0;
	int revert = 0;
	struct dentry *wh_old = NULL;

	old_bstart = dbstart(old_dentry);
	bwh_old = old_bstart;
	old_bend = dbend(old_dentry);
	parent_dentry = old_dentry->d_parent;

	new_bstart = dbstart(new_dentry);
	new_bend = dbend(new_dentry);

	/* Rename source to destination. */
	err = do_rename(old_dir, old_dentry, new_dir, new_dentry, old_bstart,
			&wh_old);
	if (err) {
		if (!IS_COPYUP_ERR(err))
			goto out;
		do_copyup = old_bstart - 1;
	} else
		revert = 1;

	/* Unlink all instances of destination that exist to the left of
	 * bstart of source. On error, revert back, goto out.
	 */
	for (bindex = old_bstart - 1; bindex >= new_bstart; bindex--) {
		struct dentry *unlink_dentry;
		struct dentry *unlink_dir_dentry;

		unlink_dentry = unionfs_lower_dentry_idx(new_dentry, bindex);
		if (!unlink_dentry)
			continue;

		unlink_dir_dentry = lock_parent(unlink_dentry);
		if (!(err = is_robranch_super(old_dir->i_sb, bindex)))
			err = vfs_unlink(unlink_dir_dentry->d_inode,
				       unlink_dentry);

		fsstack_copy_attr_times(new_dentry->d_parent->d_inode,
				     unlink_dir_dentry->d_inode);
		/* propagate number of hard-links */
		new_dentry->d_parent->d_inode->i_nlink =
		    unionfs_get_nlinks(new_dentry->d_parent->d_inode);

		unlock_dir(unlink_dir_dentry);
		if (!err) {
			if (bindex != new_bstart) {
				dput(unlink_dentry);
				unionfs_set_lower_dentry_idx(new_dentry, bindex, NULL);
			}
		} else if (IS_COPYUP_ERR(err)) {
			do_copyup = bindex - 1;
		} else if (revert) {
			dput(wh_old);
			goto revert;
		}
	}

	if (do_copyup != -1) {
		for (bindex = do_copyup; bindex >= 0; bindex--) {
			/* copyup the file into some left directory, so that
			 * you can rename it
			 */
			err = copyup_dentry(old_dentry->d_parent->d_inode,
					    old_dentry, old_bstart, bindex, NULL,
					    old_dentry->d_inode->i_size);
			if (!err) {
				dput(wh_old);
				bwh_old = bindex;
				err = do_rename(old_dir, old_dentry, new_dir,
						new_dentry, bindex, &wh_old);
				break;
			}
		}
	}

	/* make it opaque */
	if (S_ISDIR(old_dentry->d_inode->i_mode)) {
		err = make_dir_opaque(old_dentry, dbstart(old_dentry));
		if (err)
			goto revert;
	}

	/* Create whiteout for source, only if:
	 * (1) There is more than one underlying instance of source.
	 * (2) We did a copy_up
	 */
	if ((old_bstart != old_bend) || (do_copyup != -1)) {
		struct dentry *hidden_parent;
		BUG_ON(!wh_old || wh_old->d_inode || bwh_old < 0);
		hidden_parent = lock_parent(wh_old);
		local_err = vfs_create(hidden_parent->d_inode, wh_old, S_IRUGO,
				       NULL);
		unlock_dir(hidden_parent);
		if (!local_err)
			set_dbopaque(old_dentry, bwh_old);
		else {
			/* We can't fix anything now, so we cop-out and use -EIO. */
			printk(KERN_ERR "We can't create a whiteout for the "
					"source in rename!\n");
			err = -EIO;
		}
	}

out:
	dput(wh_old);
	return err;

revert:
	/* Do revert here. */
	local_err = unionfs_refresh_hidden_dentry(new_dentry, old_bstart);
	if (local_err) {
		printk(KERN_WARNING "Revert failed in rename: the new refresh "
				"failed.\n");
		eio = -EIO;
	}

	local_err = unionfs_refresh_hidden_dentry(old_dentry, old_bstart);
	if (local_err) {
		printk(KERN_WARNING "Revert failed in rename: the old refresh "
				"failed.\n");
		eio = -EIO;
		goto revert_out;
	}

	if (!unionfs_lower_dentry_idx(new_dentry, bindex) ||
	    !unionfs_lower_dentry_idx(new_dentry, bindex)->d_inode) {
		printk(KERN_WARNING "Revert failed in rename: the object "
				"disappeared from under us!\n");
		eio = -EIO;
		goto revert_out;
	}

	if (unionfs_lower_dentry_idx(old_dentry, bindex) &&
	    unionfs_lower_dentry_idx(old_dentry, bindex)->d_inode) {
		printk(KERN_WARNING "Revert failed in rename: the object was "
				"created underneath us!\n");
		eio = -EIO;
		goto revert_out;
	}

	local_err = do_rename(new_dir, new_dentry, old_dir, old_dentry, old_bstart,
					NULL);

	/* If we can't fix it, then we cop-out with -EIO. */
	if (local_err) {
		printk(KERN_WARNING "Revert failed in rename!\n");
		eio = -EIO;
	}

	local_err = unionfs_refresh_hidden_dentry(new_dentry, bindex);
	if (local_err)
		eio = -EIO;
	local_err = unionfs_refresh_hidden_dentry(old_dentry, bindex);
	if (local_err)
		eio = -EIO;

revert_out:
	if (eio)
		err = eio;
	return err;
}

static struct dentry *lookup_whiteout(struct dentry *dentry)
{
	char *whname;
	int bindex = -1, bstart = -1, bend = -1;
	struct dentry *parent, *hidden_parent, *wh_dentry;

	whname = alloc_whname(dentry->d_name.name, dentry->d_name.len);
	if (IS_ERR(whname))
		return (void *)whname;

	parent = dget_parent(dentry);
	unionfs_lock_dentry(parent);
	bstart = dbstart(parent);
	bend = dbend(parent);
	wh_dentry = ERR_PTR(-ENOENT);
	for (bindex = bstart; bindex <= bend; bindex++) {
		hidden_parent = unionfs_lower_dentry_idx(parent, bindex);
		if (!hidden_parent)
			continue;
		wh_dentry = lookup_one_len(whname, hidden_parent,
				   dentry->d_name.len + UNIONFS_WHLEN);
		if (IS_ERR(wh_dentry))
			continue;
		if (wh_dentry->d_inode)
			break;
		dput(wh_dentry);
		wh_dentry = ERR_PTR(-ENOENT);
	}
	unionfs_unlock_dentry(parent);
	dput(parent);
	kfree(whname);
	return wh_dentry;
}

/* We can't copyup a directory, because it may involve huge
 * numbers of children, etc.  Doing that in the kernel would
 * be bad, so instead we let the userspace recurse and ask us
 * to copy up each file separately
 */
static int may_rename_dir(struct dentry *dentry)
{
	int err, bstart;

	err = check_empty(dentry, NULL);
	if (err == -ENOTEMPTY) {
		if (is_robranch(dentry))
			return -EXDEV;
	} else if (err)
		return err;

	bstart = dbstart(dentry);
	if (dbend(dentry) == bstart || dbopaque(dentry) == bstart)
		return 0;

	set_dbstart(dentry, bstart + 1);
	err = check_empty(dentry, NULL);
	set_dbstart(dentry, bstart);
	if (err == -ENOTEMPTY)
		err = -EXDEV;
	return err;
}

int unionfs_rename(struct inode *old_dir, struct dentry *old_dentry,
		   struct inode *new_dir, struct dentry *new_dentry)
{
	int err = 0;
	struct dentry *wh_dentry;

	double_lock_dentry(old_dentry, new_dentry);

	if (!S_ISDIR(old_dentry->d_inode->i_mode))
		err = unionfs_partial_lookup(old_dentry);
	else
		err = may_rename_dir(old_dentry);

	if (err)
		goto out;

	err = unionfs_partial_lookup(new_dentry);
	if (err)
		goto out;

	/*
	 * if new_dentry is already hidden because of whiteout,
	 * simply override it even if the whiteouted dir is not empty.
	 */
	wh_dentry = lookup_whiteout(new_dentry);
	if (!IS_ERR(wh_dentry))
		dput(wh_dentry);
	else if (new_dentry->d_inode) {
		if (S_ISDIR(old_dentry->d_inode->i_mode) !=
		    S_ISDIR(new_dentry->d_inode->i_mode)) {
			err = S_ISDIR(old_dentry->d_inode->i_mode) ?
					-ENOTDIR : -EISDIR;
			goto out;
		}

		if (S_ISDIR(new_dentry->d_inode->i_mode)) {
			struct unionfs_dir_state *namelist;
			/* check if this unionfs directory is empty or not */
			err = check_empty(new_dentry, &namelist);
			if (err)
				goto out;

			if (!is_robranch(new_dentry))
				err = delete_whiteouts(new_dentry,
						       dbstart(new_dentry),
						       namelist);

			free_rdstate(namelist);

			if (err)
				goto out;
		}
	}
	err = do_unionfs_rename(old_dir, old_dentry, new_dir, new_dentry);

out:
	if (err)
		/* clear the new_dentry stuff created */
		d_drop(new_dentry);
	else
		/* force re-lookup since the dir on ro branch is not renamed,
		   and hidden dentries still indicate the un-renamed ones. */
		if (S_ISDIR(old_dentry->d_inode->i_mode))
			atomic_dec(&UNIONFS_D(old_dentry)->generation);

	unionfs_unlock_dentry(new_dentry);
	unionfs_unlock_dentry(old_dentry);
	return err;
}

