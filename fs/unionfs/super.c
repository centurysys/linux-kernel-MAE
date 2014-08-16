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

/* The inode cache is used with alloc_inode for both our inode info and the
 * vfs inode.
 */
static struct kmem_cache *unionfs_inode_cachep;

static void unionfs_read_inode(struct inode *inode)
{
	static struct address_space_operations unionfs_empty_aops;
	int size;
	struct unionfs_inode_info *info = UNIONFS_I(inode);

	if (!info) {
		printk(KERN_ERR "No kernel memory when allocating inode "
				"private data!\n");
		BUG();
	}

	memset(info, 0, offsetof(struct unionfs_inode_info, vfs_inode));
	info->bstart = -1;
	info->bend = -1;
	atomic_set(&info->generation,
		   atomic_read(&UNIONFS_SB(inode->i_sb)->generation));
	spin_lock_init(&info->rdlock);
	info->rdcount = 1;
	info->hashsize = -1;
	INIT_LIST_HEAD(&info->readdircache);

	size = sbmax(inode->i_sb) * sizeof(struct inode *);
	info->lower_inodes = kzalloc(size, GFP_KERNEL);
	if (!info->lower_inodes) {
		printk(KERN_ERR "No kernel memory when allocating lower-"
				"pointer array!\n");
		BUG();
	}

	inode->i_version++;
	inode->i_op = &unionfs_main_iops;
	inode->i_fop = &unionfs_main_fops;

	/* I don't think ->a_ops is ever allowed to be NULL */
	inode->i_mapping->a_ops = &unionfs_empty_aops;
}

static void unionfs_put_inode(struct inode *inode)
{
	/*
	 * This is really funky stuff:
	 * Basically, if i_count == 1, iput will then decrement it and this
	 * inode will be destroyed.  It is currently holding a reference to the
	 * hidden inode.  Therefore, it needs to release that reference by
	 * calling iput on the hidden inode.  iput() _will_ do it for us (by
	 * calling our clear_inode), but _only_ if i_nlink == 0.  The problem
	 * is, NFS keeps i_nlink == 1 for silly_rename'd files.  So we must for
	 * our i_nlink to 0 here to trick iput() into calling our clear_inode.
	 */

	if (atomic_read(&inode->i_count) == 1)
		inode->i_nlink = 0;
}

/*
 * we now define delete_inode, because there are two VFS paths that may
 * destroy an inode: one of them calls clear inode before doing everything
 * else that's needed, and the other is fine.  This way we truncate the inode
 * size (and its pages) and then clear our own inode, which will do an iput
 * on our and the lower inode.
 */
static void unionfs_delete_inode(struct inode *inode)
{
	inode->i_size = 0;	/* every f/s seems to do that */

	clear_inode(inode);
}

/* final actions when unmounting a file system */
static void unionfs_put_super(struct super_block *sb)
{
	int bindex, bstart, bend;
	struct unionfs_sb_info *spd;

	spd = UNIONFS_SB(sb);
	if (!spd)
		return;
		
	bstart = sbstart(sb);
	bend = sbend(sb);

	/* Make sure we have no leaks of branchget/branchput. */
	for (bindex = bstart; bindex <= bend; bindex++)
		BUG_ON(branch_count(sb, bindex) != 0);

	kfree(spd->data);
	kfree(spd);
	sb->s_fs_info = NULL;
}

/* Since people use this to answer the "How big of a file can I write?"
 * question, we report the size of the highest priority branch as the size of
 * the union.
 */
static int unionfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	int err	= 0;
	struct super_block *sb, *hidden_sb;

	sb = dentry->d_sb;

	hidden_sb = unionfs_lower_super_idx(sb, sbstart(sb));
	err = vfs_statfs(hidden_sb->s_root, buf);

	buf->f_type = UNIONFS_SUPER_MAGIC;
	buf->f_namelen -= UNIONFS_WHLEN;

	memset(&buf->f_fsid, 0, sizeof(__kernel_fsid_t));
	memset(&buf->f_spare, 0, sizeof(buf->f_spare));

	return err;
}

/* We don't support a standard text remount. Eventually it would be nice to
 * support a full-on remount, so that you can have all of the directories
 * change at once, but that would require some pretty complicated matching
 * code.
 */
static int unionfs_remount_fs(struct super_block *sb, int *flags, char *data)
{
	return -ENOSYS;
}

/*
 * Called by iput() when the inode reference count reached zero
 * and the inode is not hashed anywhere.  Used to clear anything
 * that needs to be, before the inode is completely destroyed and put
 * on the inode free list.
 */
static void unionfs_clear_inode(struct inode *inode)
{
	int bindex, bstart, bend;
	struct inode *hidden_inode;
	struct list_head *pos, *n;
	struct unionfs_dir_state *rdstate;

	list_for_each_safe(pos, n, &UNIONFS_I(inode)->readdircache) {
		rdstate = list_entry(pos, struct unionfs_dir_state, cache);
		list_del(&rdstate->cache);
		free_rdstate(rdstate);
	}

	/* Decrement a reference to a hidden_inode, which was incremented
	 * by our read_inode when it was created initially.
	 */
	bstart = ibstart(inode);
	bend = ibend(inode);
	if (bstart >= 0) {
		for (bindex = bstart; bindex <= bend; bindex++) {
			hidden_inode = unionfs_lower_inode_idx(inode, bindex);
			if (!hidden_inode)
				continue;
			iput(hidden_inode);
		}
	}

	kfree(UNIONFS_I(inode)->lower_inodes);
	UNIONFS_I(inode)->lower_inodes = NULL;
}

static struct inode *unionfs_alloc_inode(struct super_block *sb)
{
	struct unionfs_inode_info *i;

	i = kmem_cache_alloc(unionfs_inode_cachep, GFP_KERNEL);
	if (!i)
		return NULL;

	/* memset everything up to the inode to 0 */
	memset(i, 0, offsetof(struct unionfs_inode_info, vfs_inode));

	i->vfs_inode.i_version = 1;
	return &i->vfs_inode;
}

static void unionfs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(unionfs_inode_cachep, UNIONFS_I(inode));
}

/* unionfs inode cache constructor */
static void init_once(void *v, struct kmem_cache * cachep, unsigned long flags)
{
	struct unionfs_inode_info *i = v;

	if ((flags & (SLAB_CTOR_VERIFY | SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR)
		inode_init_once(&i->vfs_inode);
}

int unionfs_init_inode_cache(void)
{
	int err = 0;

	unionfs_inode_cachep =
	    kmem_cache_create("unionfs_inode_cache",
			      sizeof(struct unionfs_inode_info), 0,
			      SLAB_RECLAIM_ACCOUNT, init_once, NULL);
	if (!unionfs_inode_cachep)
		err = -ENOMEM;
	return err;
}

void unionfs_destroy_inode_cache(void)
{
	if (unionfs_inode_cachep)
		kmem_cache_destroy(unionfs_inode_cachep);
}

/* Called when we have a dirty inode, right here we only throw out
 * parts of our readdir list that are too old.
 */
static int unionfs_write_inode(struct inode *inode, int sync)
{
	struct list_head *pos, *n;
	struct unionfs_dir_state *rdstate;

	spin_lock(&UNIONFS_I(inode)->rdlock);
	list_for_each_safe(pos, n, &UNIONFS_I(inode)->readdircache) {
		rdstate = list_entry(pos, struct unionfs_dir_state, cache);
		/* We keep this list in LRU order. */
		if ((rdstate->access + RDCACHE_JIFFIES) > jiffies)
			break;
		UNIONFS_I(inode)->rdcount--;
		list_del(&rdstate->cache);
		free_rdstate(rdstate);
	}
	spin_unlock(&UNIONFS_I(inode)->rdlock);

	return 0;
}

/*
 * Used only in nfs, to kill any pending RPC tasks, so that subsequent
 * code can actually succeed and won't leave tasks that need handling.
 */
static void unionfs_umount_begin(struct vfsmount *mnt, int flags)
{
	struct super_block *sb, *hidden_sb;
	struct vfsmount *hidden_mnt;
	int bindex, bstart, bend;

	if (!(flags & MNT_FORCE))
		/* we are not being MNT_FORCEd, therefore we should emulate
		 * old behaviour
		 */
		return;

	sb = mnt->mnt_sb;

	bstart = sbstart(sb);
	bend = sbend(sb);
	for (bindex = bstart; bindex <= bend; bindex++) {
		hidden_mnt = unionfs_lower_mnt_idx(sb->s_root, bindex);
		hidden_sb = unionfs_lower_super_idx(sb, bindex);

		if (hidden_mnt && hidden_sb && hidden_sb->s_op &&
		    hidden_sb->s_op->umount_begin)
			hidden_sb->s_op->umount_begin(hidden_mnt, flags);
	}
}

static int unionfs_show_options(struct seq_file *m, struct vfsmount *mnt)
{
	struct super_block *sb = mnt->mnt_sb;
	int ret = 0;
	char *tmp_page;
	char *path;
	int bindex, bstart, bend;
	int perms;

	unionfs_lock_dentry(sb->s_root);

	tmp_page = (char*) __get_free_page(GFP_KERNEL);
	if (!tmp_page) {
		ret = -ENOMEM;
		goto out;
	}

	bstart = sbstart(sb);
	bend = sbend(sb);

	seq_printf(m, ",dirs=");
	for (bindex = bstart; bindex <= bend; bindex++) {
		path = d_path(unionfs_lower_dentry_idx(sb->s_root, bindex),
			   unionfs_lower_mnt_idx(sb->s_root, bindex), tmp_page,
			   PAGE_SIZE);
		perms = branchperms(sb, bindex);

		seq_printf(m, "%s=%s", path,
			   perms & MAY_WRITE ? "rw" : "ro");
		if (bindex != bend) {
			seq_printf(m, ":");
		}
	}

out:
	free_page((unsigned long) tmp_page);

	unionfs_unlock_dentry(sb->s_root);

	return ret;
}

struct super_operations unionfs_sops = {
	.read_inode	= unionfs_read_inode,
	.put_inode	= unionfs_put_inode,
	.delete_inode	= unionfs_delete_inode,
	.put_super	= unionfs_put_super,
	.statfs		= unionfs_statfs,
	.remount_fs	= unionfs_remount_fs,
	.clear_inode	= unionfs_clear_inode,
	.umount_begin	= unionfs_umount_begin,
	.show_options	= unionfs_show_options,
	.write_inode	= unionfs_write_inode,
	.alloc_inode	= unionfs_alloc_inode,
	.destroy_inode	= unionfs_destroy_inode,
};

