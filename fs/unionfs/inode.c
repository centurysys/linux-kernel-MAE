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

static int unionfs_create(struct inode *parent, struct dentry *dentry,
			  int mode, struct nameidata *nd)
{
	int err = 0;
	struct dentry *hidden_dentry = NULL;
	struct dentry *wh_dentry = NULL;
	struct dentry *new_hidden_dentry;
	struct dentry *hidden_parent_dentry = NULL;
	int bindex = 0, bstart;
	char *name = NULL;

	unionfs_lock_dentry(dentry);

	/* We start out in the leftmost branch. */
	bstart = dbstart(dentry);
	hidden_dentry = unionfs_lower_dentry(dentry);

	/* check if whiteout exists in this branch, i.e. lookup .wh.foo first */
	name = alloc_whname(dentry->d_name.name, dentry->d_name.len);
	if (IS_ERR(name)) {
		err = PTR_ERR(name);
		goto out;
	}

	wh_dentry = lookup_one_len(name, hidden_dentry->d_parent,
			   dentry->d_name.len + UNIONFS_WHLEN);
	if (IS_ERR(wh_dentry)) {
		err = PTR_ERR(wh_dentry);
		wh_dentry = NULL;
		goto out;
	}

	if (wh_dentry->d_inode) {
		/* .wh.foo has been found. */
		/* First truncate it and then rename it to foo (hence having
		 * the same overall effect as a normal create.
		 */
		struct dentry *hidden_dir_dentry;
		struct iattr newattrs;

		mutex_lock(&wh_dentry->d_inode->i_mutex);
		newattrs.ia_valid = ATTR_CTIME | ATTR_MODE | ATTR_ATIME
		    | ATTR_MTIME | ATTR_UID | ATTR_GID | ATTR_FORCE
		    | ATTR_KILL_SUID | ATTR_KILL_SGID;

		newattrs.ia_mode = mode & ~current->fs->umask;
		newattrs.ia_uid = current->fsuid;
		newattrs.ia_gid = current->fsgid;

		if (wh_dentry->d_inode->i_size != 0) {
			newattrs.ia_valid |= ATTR_SIZE;
			newattrs.ia_size = 0;
		}

		err = notify_change(wh_dentry, &newattrs);

		mutex_unlock(&wh_dentry->d_inode->i_mutex);

		if (err)
			printk(KERN_WARNING "unionfs: %s:%d: notify_change "
				"failed: %d, ignoring..\n",
				__FILE__, __LINE__, err);

		new_hidden_dentry = unionfs_lower_dentry(dentry);
		dget(new_hidden_dentry);

		hidden_dir_dentry = dget_parent(wh_dentry);
		lock_rename(hidden_dir_dentry, hidden_dir_dentry);

		if (!(err = is_robranch_super(dentry->d_sb, bstart))) {
			err = vfs_rename(hidden_dir_dentry->d_inode,
					 wh_dentry,
					 hidden_dir_dentry->d_inode,
					 new_hidden_dentry);
		}
		if (!err) {
			fsstack_copy_attr_times(parent,
					new_hidden_dentry->d_parent->d_inode);
			fsstack_copy_inode_size(parent,
					new_hidden_dentry->d_parent->d_inode);
			parent->i_nlink = unionfs_get_nlinks(parent);
		}

		unlock_rename(hidden_dir_dentry, hidden_dir_dentry);
		dput(hidden_dir_dentry);

		dput(new_hidden_dentry);

		if (err) {
			/* exit if the error returned was NOT -EROFS */
			if (!IS_COPYUP_ERR(err))
				goto out;
			/* We were not able to create the file in this
			 * branch, so, we try to create it in one branch to
			 * left
			 */
			bstart--;
		} else {
			/* reset the unionfs dentry to point to the .wh.foo
			 * entry.
			 */

			/* Discard any old reference. */
			dput(unionfs_lower_dentry(dentry));

			/* Trade one reference to another. */
			unionfs_set_lower_dentry_idx(dentry, bstart, wh_dentry);
			wh_dentry = NULL;

			err = unionfs_interpose(dentry, parent->i_sb, 0);
			goto out;
		}
	}

	for (bindex = bstart; bindex >= 0; bindex--) {
		hidden_dentry = unionfs_lower_dentry_idx(dentry, bindex);
		if (!hidden_dentry) {
			/* if hidden_dentry is NULL, create the entire
			 * dentry directory structure in branch 'bindex'.
			 * hidden_dentry will NOT be null when bindex == bstart
			 * because lookup passed as a negative unionfs dentry
			 * pointing to a lone negative underlying dentry */
			hidden_dentry = create_parents(parent, dentry, bindex);
			if (!hidden_dentry || IS_ERR(hidden_dentry)) {
				if (IS_ERR(hidden_dentry))
					err = PTR_ERR(hidden_dentry);
				continue;
			}
		}

		hidden_parent_dentry = lock_parent(hidden_dentry);
		if (IS_ERR(hidden_parent_dentry)) {
			err = PTR_ERR(hidden_parent_dentry);
			goto out;
		}
		/* We shouldn't create things in a read-only branch. */
		if (!(err = is_robranch_super(dentry->d_sb, bindex)))
			err = vfs_create(hidden_parent_dentry->d_inode,
					 hidden_dentry, mode, nd);

		if (err || !hidden_dentry->d_inode) {
			unlock_dir(hidden_parent_dentry);

			/* break out of for loop if the error wasn't  -EROFS */
			if (!IS_COPYUP_ERR(err))
				break;
		} else {
			err = unionfs_interpose(dentry, parent->i_sb, 0);
			if (!err) {
				fsstack_copy_attr_times(parent,
						hidden_parent_dentry->d_inode);
				fsstack_copy_inode_size(parent,
						hidden_parent_dentry->d_inode);
				/* update number of links on parent directory */
				parent->i_nlink = unionfs_get_nlinks(parent);
			}
			unlock_dir(hidden_parent_dentry);
			break;
		}
	}

out:
	dput(wh_dentry);
	kfree(name);

	unionfs_unlock_dentry(dentry);
	return err;
}

static struct dentry *unionfs_lookup(struct inode *parent,
				     struct dentry *dentry,
				     struct nameidata *nd)
{
	struct path path_save;
	struct dentry *ret;

	/* save the dentry & vfsmnt from namei */
	if (nd) {
		path_save.dentry = nd->dentry;
		path_save.mnt = nd->mnt;
	}

	/* The locking is done by unionfs_lookup_backend. */
	ret = unionfs_lookup_backend(dentry, nd, INTERPOSE_LOOKUP);

	/* restore the dentry & vfsmnt in namei */
	if (nd) {
		nd->dentry = path_save.dentry;
		nd->mnt = path_save.mnt;
	}

	return ret;
}

static int unionfs_link(struct dentry *old_dentry, struct inode *dir,
			struct dentry *new_dentry)
{
	int err = 0;
	struct dentry *hidden_old_dentry = NULL;
	struct dentry *hidden_new_dentry = NULL;
	struct dentry *hidden_dir_dentry = NULL;
	struct dentry *whiteout_dentry;
	char *name = NULL;

	double_lock_dentry(new_dentry, old_dentry);

	hidden_new_dentry = unionfs_lower_dentry(new_dentry);

	/* check if whiteout exists in the branch of new dentry, i.e. lookup
	 * .wh.foo first. If present, delete it
	 */
	name = alloc_whname(new_dentry->d_name.name, new_dentry->d_name.len);
	if (IS_ERR(name)) {
		err = PTR_ERR(name);
		goto out;
	}

	whiteout_dentry = lookup_one_len(name, hidden_new_dentry->d_parent,
				new_dentry->d_name.len + UNIONFS_WHLEN);
	if (IS_ERR(whiteout_dentry)) {
		err = PTR_ERR(whiteout_dentry);
		goto out;
	}

	if (!whiteout_dentry->d_inode) {
		dput(whiteout_dentry);
		whiteout_dentry = NULL;
	} else {
		/* found a .wh.foo entry, unlink it and then call vfs_link() */
		hidden_dir_dentry = lock_parent(whiteout_dentry);
		err = is_robranch_super(new_dentry->d_sb, dbstart(new_dentry));
		if (!err)
			err = vfs_unlink(hidden_dir_dentry->d_inode,
				       whiteout_dentry);

		fsstack_copy_attr_times(dir, hidden_dir_dentry->d_inode);
		dir->i_nlink = unionfs_get_nlinks(dir);
		unlock_dir(hidden_dir_dentry);
		hidden_dir_dentry = NULL;
		dput(whiteout_dentry);
		if (err)
			goto out;
	}

	if (dbstart(old_dentry) != dbstart(new_dentry)) {
		hidden_new_dentry =
		    create_parents(dir, new_dentry, dbstart(old_dentry));
		err = PTR_ERR(hidden_new_dentry);
		if (IS_COPYUP_ERR(err))
			goto docopyup;
		if (!hidden_new_dentry || IS_ERR(hidden_new_dentry))
			goto out;
	}
	hidden_new_dentry = unionfs_lower_dentry(new_dentry);
	hidden_old_dentry = unionfs_lower_dentry(old_dentry);

	BUG_ON(dbstart(old_dentry) != dbstart(new_dentry));
	hidden_dir_dentry = lock_parent(hidden_new_dentry);
	if (!(err = is_robranch(old_dentry)))
		err = vfs_link(hidden_old_dentry, hidden_dir_dentry->d_inode,
			     hidden_new_dentry);
	unlock_dir(hidden_dir_dentry);

docopyup:
	if (IS_COPYUP_ERR(err)) {
		int old_bstart = dbstart(old_dentry);
		int bindex;

		for (bindex = old_bstart - 1; bindex >= 0; bindex--) {
			err =
			    copyup_dentry(old_dentry->d_parent->
					  d_inode, old_dentry,
					  old_bstart, bindex, NULL,
					  old_dentry->d_inode->i_size);
			if (!err) {
				hidden_new_dentry =
				    create_parents(dir, new_dentry, bindex);
				hidden_old_dentry = unionfs_lower_dentry(old_dentry);
				hidden_dir_dentry =
				    lock_parent(hidden_new_dentry);
				/* do vfs_link */
				err = vfs_link(hidden_old_dentry,
					     hidden_dir_dentry->d_inode,
					     hidden_new_dentry);
				unlock_dir(hidden_dir_dentry);
				goto check_link;
			}
		}
		goto out;
	}

check_link:
	if (err || !hidden_new_dentry->d_inode)
		goto out;

	/* Its a hard link, so use the same inode */
	new_dentry->d_inode = igrab(old_dentry->d_inode);
	d_instantiate(new_dentry, new_dentry->d_inode);
	fsstack_copy_attr_all(dir, hidden_new_dentry->d_parent->d_inode,
					unionfs_get_nlinks);
	fsstack_copy_inode_size(dir, hidden_new_dentry->d_parent->d_inode);

	/* propagate number of hard-links */
	old_dentry->d_inode->i_nlink = unionfs_get_nlinks(old_dentry->d_inode);

out:
	if (!new_dentry->d_inode)
		d_drop(new_dentry);

	kfree(name);

	unionfs_unlock_dentry(new_dentry);
	unionfs_unlock_dentry(old_dentry);

	return err;
}

static int unionfs_symlink(struct inode *dir, struct dentry *dentry,
			   const char *symname)
{
	int err = 0;
	struct dentry *hidden_dentry = NULL;
	struct dentry *whiteout_dentry = NULL;
	struct dentry *hidden_dir_dentry = NULL;
	umode_t mode;
	int bindex = 0, bstart;
	char *name = NULL;

	unionfs_lock_dentry(dentry);

	/* We start out in the leftmost branch. */
	bstart = dbstart(dentry);

	hidden_dentry = unionfs_lower_dentry(dentry);

	/* check if whiteout exists in this branch, i.e. lookup .wh.foo
	 * first. If present, delete it
	 */
	name = alloc_whname(dentry->d_name.name, dentry->d_name.len);
	if (IS_ERR(name)) {
		err = PTR_ERR(name);
		goto out;
	}

	whiteout_dentry =
	    lookup_one_len(name, hidden_dentry->d_parent,
			   dentry->d_name.len + UNIONFS_WHLEN);
	if (IS_ERR(whiteout_dentry)) {
		err = PTR_ERR(whiteout_dentry);
		goto out;
	}

	if (!whiteout_dentry->d_inode) {
		dput(whiteout_dentry);
		whiteout_dentry = NULL;
	} else {
		/* found a .wh.foo entry, unlink it and then call vfs_symlink() */
		hidden_dir_dentry = lock_parent(whiteout_dentry);

		if (!(err = is_robranch_super(dentry->d_sb, bstart)))
			err = vfs_unlink(hidden_dir_dentry->d_inode,
				       whiteout_dentry);
		dput(whiteout_dentry);

		fsstack_copy_attr_times(dir, hidden_dir_dentry->d_inode);
		/* propagate number of hard-links */
		dir->i_nlink = unionfs_get_nlinks(dir);

		unlock_dir(hidden_dir_dentry);

		if (err) {
			/* exit if the error returned was NOT -EROFS */
			if (!IS_COPYUP_ERR(err))
				goto out;
			/* should now try to create symlink in the another branch */
			bstart--;
		}
	}

	/* deleted whiteout if it was present, now do a normal vfs_symlink()
	 * with possible recursive directory creation
	 */
	for (bindex = bstart; bindex >= 0; bindex--) {
		hidden_dentry = unionfs_lower_dentry_idx(dentry, bindex);
		if (!hidden_dentry) {
			/* if hidden_dentry is NULL, create the entire
			 * dentry directory structure in branch 'bindex'.
			 * hidden_dentry will NOT be null when bindex ==
			 * bstart because lookup passed as a negative
			 * unionfs dentry pointing to a lone negative
			 * underlying dentry
			 */
			hidden_dentry = create_parents(dir, dentry, bindex);
			if (!hidden_dentry || IS_ERR(hidden_dentry)) {
				if (IS_ERR(hidden_dentry))
					err = PTR_ERR(hidden_dentry);

				printk(KERN_DEBUG "hidden dentry NULL (or error)"
					"for bindex = %d\n", bindex);
				continue;
			}
		}

		hidden_dir_dentry = lock_parent(hidden_dentry);

		if (!(err = is_robranch_super(dentry->d_sb, bindex))) {
			mode = S_IALLUGO;
			err =
			    vfs_symlink(hidden_dir_dentry->d_inode,
					hidden_dentry, symname, mode);
		}
		unlock_dir(hidden_dir_dentry);

		if (err || !hidden_dentry->d_inode) {
			/* break out of for loop if error returned was NOT -EROFS */
			if (!IS_COPYUP_ERR(err))
				break;
		} else {
			err = unionfs_interpose(dentry, dir->i_sb, 0);
			if (!err) {
				fsstack_copy_attr_times(dir,
						hidden_dir_dentry->d_inode);
				fsstack_copy_inode_size(dir,
						hidden_dir_dentry->d_inode);
				/* update number of links on parent directory */
				dir->i_nlink = unionfs_get_nlinks(dir);
			}
			break;
		}
	}

out:
	if (!dentry->d_inode)
		d_drop(dentry);

	kfree(name);
	unionfs_unlock_dentry(dentry);
	return err;
}

static int unionfs_mkdir(struct inode *parent, struct dentry *dentry, int mode)
{
	int err = 0;
	struct dentry *hidden_dentry = NULL, *whiteout_dentry = NULL;
	struct dentry *hidden_parent_dentry = NULL;
	int bindex = 0, bstart;
	char *name = NULL;
	int whiteout_unlinked = 0;
	struct sioq_args args;

	unionfs_lock_dentry(dentry);
	bstart = dbstart(dentry);

	hidden_dentry = unionfs_lower_dentry(dentry);

	/* check if whiteout exists in this branch, i.e. lookup .wh.foo first */
	name = alloc_whname(dentry->d_name.name, dentry->d_name.len);
	if (IS_ERR(name)) {
		err = PTR_ERR(name);
		goto out;
	}

	whiteout_dentry = lookup_one_len(name, hidden_dentry->d_parent,
			   dentry->d_name.len + UNIONFS_WHLEN);
	if (IS_ERR(whiteout_dentry)) {
		err = PTR_ERR(whiteout_dentry);
		goto out;
	}

	if (!whiteout_dentry->d_inode) {
		dput(whiteout_dentry);
		whiteout_dentry = NULL;
	} else {
		hidden_parent_dentry = lock_parent(whiteout_dentry);

		/* found a.wh.foo entry, remove it then do vfs_mkdir */
		if (!(err = is_robranch_super(dentry->d_sb, bstart))) {
			args.unlink.parent = hidden_parent_dentry->d_inode;
			args.unlink.dentry = whiteout_dentry;
			run_sioq(__unionfs_unlink, &args);
			err = args.err;
		}
		dput(whiteout_dentry);

		unlock_dir(hidden_parent_dentry);

		if (err) {
			/* exit if the error returned was NOT -EROFS */
			if (!IS_COPYUP_ERR(err))
				goto out;
			bstart--;
		} else
			whiteout_unlinked = 1;
	}

	for (bindex = bstart; bindex >= 0; bindex--) {
		int i;
		int bend = dbend(dentry);

		if (is_robranch_super(dentry->d_sb, bindex))
			continue;

		hidden_dentry = unionfs_lower_dentry_idx(dentry, bindex);
		if (!hidden_dentry) {
			hidden_dentry = create_parents(parent, dentry, bindex);
			if (!hidden_dentry || IS_ERR(hidden_dentry)) {
				printk(KERN_DEBUG "hidden dentry NULL for "
					"bindex = %d\n", bindex);
				continue;
			}
		}

		hidden_parent_dentry = lock_parent(hidden_dentry);

		if (IS_ERR(hidden_parent_dentry)) {
			err = PTR_ERR(hidden_parent_dentry);
			goto out;
		}

		err = vfs_mkdir(hidden_parent_dentry->d_inode, hidden_dentry, mode);

		unlock_dir(hidden_parent_dentry);

		/* did the mkdir suceed? */
		if (err)
			break;

		for (i = bindex + 1; i < bend; i++) {
			if (unionfs_lower_dentry_idx(dentry, i)) {
				dput(unionfs_lower_dentry_idx(dentry, i));
				unionfs_set_lower_dentry_idx(dentry, i, NULL);
			}
		}
		set_dbend(dentry, bindex);

		err = unionfs_interpose(dentry, parent->i_sb, 0);
		if (!err) {
			fsstack_copy_attr_times(parent,
					hidden_parent_dentry->d_inode);
			fsstack_copy_inode_size(parent,
					hidden_parent_dentry->d_inode);

			/* update number of links on parent directory */
			parent->i_nlink = unionfs_get_nlinks(parent);
		}

		err = make_dir_opaque(dentry, dbstart(dentry));
		if (err) {
			printk(KERN_ERR "mkdir: error creating "
				".wh.__dir_opaque: %d\n", err);
			goto out;
		}

		/* we are done! */
		break;
	}

out:
	if (!dentry->d_inode)
		d_drop(dentry);

	kfree(name);

	unionfs_unlock_dentry(dentry);
	return err;
}

static int unionfs_mknod(struct inode *dir, struct dentry *dentry, int mode,
			 dev_t dev)
{
	int err = 0;
	struct dentry *hidden_dentry = NULL, *whiteout_dentry = NULL;
	struct dentry *hidden_parent_dentry = NULL;
	int bindex = 0, bstart;
	char *name = NULL;
	int whiteout_unlinked = 0;

	unionfs_lock_dentry(dentry);
	bstart = dbstart(dentry);

	hidden_dentry = unionfs_lower_dentry(dentry);

	/* check if whiteout exists in this branch, i.e. lookup .wh.foo first */
	name = alloc_whname(dentry->d_name.name, dentry->d_name.len);
	if (IS_ERR(name)) {
		err = PTR_ERR(name);
		goto out;
	}

	whiteout_dentry = lookup_one_len(name, hidden_dentry->d_parent,
				dentry->d_name.len + UNIONFS_WHLEN);
	if (IS_ERR(whiteout_dentry)) {
		err = PTR_ERR(whiteout_dentry);
		goto out;
	}

	if (!whiteout_dentry->d_inode) {
		dput(whiteout_dentry);
		whiteout_dentry = NULL;
	} else {
		/* found .wh.foo, unlink it */
		hidden_parent_dentry = lock_parent(whiteout_dentry);

		/* found a.wh.foo entry, remove it then do vfs_mkdir */
		if (!(err = is_robranch_super(dentry->d_sb, bstart)))
			err = vfs_unlink(hidden_parent_dentry->d_inode,
					 whiteout_dentry);
		dput(whiteout_dentry);

		unlock_dir(hidden_parent_dentry);

		if (err) {
			if (!IS_COPYUP_ERR(err))
				goto out;

			bstart--;
		} else
			whiteout_unlinked = 1;
	}

	for (bindex = bstart; bindex >= 0; bindex--) {
		if (is_robranch_super(dentry->d_sb, bindex))
			continue;

		hidden_dentry = unionfs_lower_dentry_idx(dentry, bindex);
		if (!hidden_dentry) {
			hidden_dentry = create_parents(dir, dentry, bindex);
			if (IS_ERR(hidden_dentry)) {
				printk(KERN_DEBUG
				       "failed to create parents on %d, err = %ld\n",
				       bindex, PTR_ERR(hidden_dentry));
				continue;
			}
		}

		hidden_parent_dentry = lock_parent(hidden_dentry);
		if (IS_ERR(hidden_parent_dentry)) {
			err = PTR_ERR(hidden_parent_dentry);
			goto out;
		}

		err = vfs_mknod(hidden_parent_dentry->d_inode,
				hidden_dentry, mode, dev);

		if (err) {
			unlock_dir(hidden_parent_dentry);
			break;
		}

		err = unionfs_interpose(dentry, dir->i_sb, 0);
		if (!err) {
			fsstack_copy_attr_times(dir,
					hidden_parent_dentry->d_inode);
			fsstack_copy_inode_size(dir,
					hidden_parent_dentry->d_inode);
			/* update number of links on parent directory */
			dir->i_nlink = unionfs_get_nlinks(dir);
		}
		unlock_dir(hidden_parent_dentry);

		break;
	}

out:
	if (!dentry->d_inode)
		d_drop(dentry);

	kfree(name);

	unionfs_unlock_dentry(dentry);
	return err;
}

static int unionfs_readlink(struct dentry *dentry, char __user * buf,
			    int bufsiz)
{
	int err;
	struct dentry *hidden_dentry;

	unionfs_lock_dentry(dentry);
	hidden_dentry = unionfs_lower_dentry(dentry);

	if (!hidden_dentry->d_inode->i_op ||
	    !hidden_dentry->d_inode->i_op->readlink) {
		err = -EINVAL;
		goto out;
	}

	err = hidden_dentry->d_inode->i_op->readlink(hidden_dentry, buf, bufsiz);
	if (err > 0)
		fsstack_copy_attr_atime(dentry->d_inode, hidden_dentry->d_inode);

out:
	unionfs_unlock_dentry(dentry);
	return err;
}

/* We don't lock the dentry here, because readlink does the heavy lifting. */
static void *unionfs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	char *buf;
	int len = PAGE_SIZE, err;
	mm_segment_t old_fs;

	/* This is freed by the put_link method assuming a successful call. */
	buf = kmalloc(len, GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto out;
	}

	/* read the symlink, and then we will follow it */
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = dentry->d_inode->i_op->readlink(dentry, (char __user *)buf, len);
	set_fs(old_fs);
	if (err < 0) {
		kfree(buf);
		buf = NULL;
		goto out;
	}
	buf[err] = 0;
	nd_set_link(nd, buf);
	err = 0;

out:
	return ERR_PTR(err);
}

static void unionfs_put_link(struct dentry *dentry, struct nameidata *nd,
			     void *cookie)
{
	kfree(nd_get_link(nd));
}

/* Basically copied from the kernel vfs permission(), but we've changed
 * the following:
 *   (1) the IS_RDONLY check is skipped, and 
 *   (2) if you set the mount option `mode=nfsro', we assume that -EACCES
 *   means that the export is read-only and we should check standard Unix
 *   permissions.  This means that NFS ACL checks (or other advanced
 *   permission features) are bypassed. Note however, that we do call
 *   security_inode_permission, and therefore security inside SELinux, etc.
 *   are performed.
 */
static int inode_permission(struct inode *inode, int mask, struct nameidata *nd,
			    int bindex)
{
	int retval, submask;

	if (mask & MAY_WRITE) {
		/* The first branch is allowed to be really readonly. */
		if (bindex == 0) {
			umode_t mode = inode->i_mode;
			if (IS_RDONLY(inode) &&
			   (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
				return -EROFS;
		}
		/*
		 * Nobody gets write access to an immutable file.
		 */
		if (IS_IMMUTABLE(inode))
			return -EACCES;
	}

	/* Ordinary permission routines do not understand MAY_APPEND. */
	submask = mask & ~MAY_APPEND;
	if (inode->i_op && inode->i_op->permission) {
		retval = inode->i_op->permission(inode, submask, nd);
		if ((retval == -EACCES) && (submask & MAY_WRITE) &&
		    (!strcmp("nfs", (inode)->i_sb->s_type->name)) &&
		    (nd) && (nd->mnt) && (nd->mnt->mnt_sb) &&
		    (branchperms(nd->mnt->mnt_sb, bindex) & MAY_NFSRO)) {
			retval = generic_permission(inode, submask, NULL);
		}
	} else
		retval = generic_permission(inode, submask, NULL);

	if (retval && retval != -EROFS)	/* ignore EROFS */
		return retval;

	retval = security_inode_permission(inode, mask, nd);
	return ((retval == -EROFS) ? 0 : retval);	/* ignore EROFS */
}

static int unionfs_permission(struct inode *inode, int mask,
			      struct nameidata *nd)
{
	struct inode *hidden_inode = NULL;
	int err = 0;
	int bindex, bstart, bend;
	const int is_file = !S_ISDIR(inode->i_mode);
	const int write_mask = (mask & MAY_WRITE) && !(mask & MAY_READ);

	bstart = ibstart(inode);
	bend = ibend(inode);

	for (bindex = bstart; bindex <= bend; bindex++) {
		hidden_inode = unionfs_lower_inode_idx(inode, bindex);
		if (!hidden_inode)
			continue;

		/* check the condition for D-F-D underlying files/directories,
		 * we dont have to check for files, if we are checking for
		 * directories.
		 */
		if (!is_file && !S_ISDIR(hidden_inode->i_mode))
			continue;

		/* We use our own special version of permission, such that
		 * only the first branch returns -EROFS.
		 */
		err = inode_permission(hidden_inode, mask, nd, bindex);

		/* The permissions are an intersection of the overall directory
		 * permissions, so we fail if one fails.
		 */
		if (err)
			goto out;

		/* only the leftmost file matters. */
		if (is_file || write_mask) {
			if (is_file && write_mask) {
				err = get_write_access(hidden_inode);
				if (!err)
					put_write_access(hidden_inode);
			}
			break;
		}
	}

out:
	return err;
}

static int unionfs_setattr(struct dentry *dentry, struct iattr *ia)
{
	int err = 0;
	struct dentry *hidden_dentry;
	struct inode *inode = NULL;
	struct inode *hidden_inode = NULL;
	int bstart, bend, bindex;
	int i;
	int copyup = 0;

	unionfs_lock_dentry(dentry);
	bstart = dbstart(dentry);
	bend = dbend(dentry);
	inode = dentry->d_inode;

	for (bindex = bstart; (bindex <= bend) || (bindex == bstart); bindex++) {
		hidden_dentry = unionfs_lower_dentry_idx(dentry, bindex);
		if (!hidden_dentry)
			continue;
		BUG_ON(hidden_dentry->d_inode == NULL);

		/* If the file is on a read only branch */
		if (is_robranch_super(dentry->d_sb, bindex)
		    || IS_RDONLY(hidden_dentry->d_inode)) {
			if (copyup || (bindex != bstart))
				continue;
			/* Only if its the leftmost file, copyup the file */
			for (i = bstart - 1; i >= 0; i--) {
				loff_t size = dentry->d_inode->i_size;
				if (ia->ia_valid & ATTR_SIZE)
					size = ia->ia_size;
				err = copyup_dentry(dentry->d_parent->d_inode,
						    dentry, bstart, i, NULL,
						    size);

				if (!err) {
					copyup = 1;
					hidden_dentry = unionfs_lower_dentry(dentry);
					break;
				}
				/* if error is in the leftmost branch, pass it up */
				if (i == 0)
					goto out;
			}

		}
		err = notify_change(hidden_dentry, ia);
		if (err)
			goto out;
		break;
	}

	/* get the size from the first hidden inode */
	hidden_inode = unionfs_lower_inode(dentry->d_inode);
	fsstack_copy_attr_all(inode, hidden_inode, unionfs_get_nlinks);
	fsstack_copy_inode_size(inode, hidden_inode);

out:
	unionfs_unlock_dentry(dentry);
	return err;
}

struct inode_operations unionfs_symlink_iops = {
	.readlink	= unionfs_readlink,
	.permission	= unionfs_permission,
	.follow_link	= unionfs_follow_link,
	.setattr	= unionfs_setattr,
	.put_link	= unionfs_put_link,
};

struct inode_operations unionfs_dir_iops = {
	.create		= unionfs_create,
	.lookup		= unionfs_lookup,
	.link		= unionfs_link,
	.unlink		= unionfs_unlink,
	.symlink	= unionfs_symlink,
	.mkdir		= unionfs_mkdir,
	.rmdir		= unionfs_rmdir,
	.mknod		= unionfs_mknod,
	.rename		= unionfs_rename,
	.permission	= unionfs_permission,
	.setattr	= unionfs_setattr,
#ifdef CONFIG_UNION_FS_XATTR
	.setxattr	= unionfs_setxattr,
	.getxattr	= unionfs_getxattr,
	.removexattr	= unionfs_removexattr,
	.listxattr	= unionfs_listxattr,
#endif
};

struct inode_operations unionfs_main_iops = {
	.permission	= unionfs_permission,
	.setattr	= unionfs_setattr,
#ifdef CONFIG_UNION_FS_XATTR
	.setxattr	= unionfs_setxattr,
	.getxattr	= unionfs_getxattr,
	.removexattr	= unionfs_removexattr,
	.listxattr	= unionfs_listxattr,
#endif
};

