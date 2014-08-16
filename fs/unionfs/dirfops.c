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

/* Make sure our rdstate is playing by the rules. */
static void verify_rdstate_offset(struct unionfs_dir_state *rdstate)
{
	BUG_ON(rdstate->offset >= DIREOF);
	BUG_ON(rdstate->cookie >= MAXRDCOOKIE);
}

struct unionfs_getdents_callback {
	struct unionfs_dir_state *rdstate;
	void *dirent;
	int entries_written;
	int filldir_called;
	int filldir_error;
	filldir_t filldir;
	struct super_block *sb;
};

/* based on generic filldir in fs/readir.c */
static int unionfs_filldir(void *dirent, const char *name, int namelen,
			   loff_t offset, u64 ino, unsigned int d_type)
{
	struct unionfs_getdents_callback *buf = dirent;
	struct filldir_node *found = NULL;
	int err = 0;
	int is_wh_entry = 0;

	buf->filldir_called++;

	if ((namelen > UNIONFS_WHLEN) &&
	    !strncmp(name, UNIONFS_WHPFX, UNIONFS_WHLEN)) {
		name += UNIONFS_WHLEN;
		namelen -= UNIONFS_WHLEN;
		is_wh_entry = 1;
	}

	found = find_filldir_node(buf->rdstate, name, namelen);

	if (found)
		goto out;

	/* if 'name' isn't a whiteout, filldir it. */
	if (!is_wh_entry) {
		off_t pos = rdstate2offset(buf->rdstate);
		u64 unionfs_ino = ino;

		if (!err) {
			err = buf->filldir(buf->dirent, name, namelen, pos,
					   unionfs_ino, d_type);
			buf->rdstate->offset++;
			verify_rdstate_offset(buf->rdstate);
		}
	}
	/* If we did fill it, stuff it in our hash, otherwise return an error */
	if (err) {
		buf->filldir_error = err;
		goto out;
	}
	buf->entries_written++;
	if ((err = add_filldir_node(buf->rdstate, name, namelen,
				    buf->rdstate->bindex, is_wh_entry)))
		buf->filldir_error = err;

out:
	return err;
}

static int unionfs_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	int err = 0;
	struct file *hidden_file = NULL;
	struct inode *inode = NULL;
	struct unionfs_getdents_callback buf;
	struct unionfs_dir_state *uds;
	int bend;
	loff_t offset;

	if ((err = unionfs_file_revalidate(file, 0)))
		goto out;

	inode = file->f_dentry->d_inode;

	uds = UNIONFS_F(file)->rdstate;
	if (!uds) {
		if (file->f_pos == DIREOF) {
			goto out;
		} else if (file->f_pos > 0) {
			uds = find_rdstate(inode, file->f_pos);
			if (!uds) {
				err = -ESTALE;
				goto out;
			}
			UNIONFS_F(file)->rdstate = uds;
		} else {
			init_rdstate(file);
			uds = UNIONFS_F(file)->rdstate;
		}
	}
	bend = fbend(file);

	while (uds->bindex <= bend) {
		hidden_file = unionfs_lower_file_idx(file, uds->bindex);
		if (!hidden_file) {
			uds->bindex++;
			uds->dirpos = 0;
			continue;
		}

		/* prepare callback buffer */
		buf.filldir_called = 0;
		buf.filldir_error = 0;
		buf.entries_written = 0;
		buf.dirent = dirent;
		buf.filldir = filldir;
		buf.rdstate = uds;
		buf.sb = inode->i_sb;

		/* Read starting from where we last left off. */
		offset = vfs_llseek(hidden_file, uds->dirpos, 0);
		if (offset < 0) {
			err = offset;
			goto out;
		}
		err = vfs_readdir(hidden_file, unionfs_filldir, &buf);
		/* Save the position for when we continue. */

		offset = vfs_llseek(hidden_file, 0, 1);
		if (offset < 0) {
			err = offset;
			goto out;
		}
		uds->dirpos = offset;

		/* Copy the atime. */
		fsstack_copy_attr_atime(inode, hidden_file->f_dentry->d_inode);

		if (err < 0)
			goto out;

		if (buf.filldir_error)
			break;

		if (!buf.entries_written) {
			uds->bindex++;
			uds->dirpos = 0;
		}
	}

	if (!buf.filldir_error && uds->bindex >= bend) {
		/* Save the number of hash entries for next time. */
		UNIONFS_I(inode)->hashsize = uds->hashentries;
		free_rdstate(uds);
		UNIONFS_F(file)->rdstate = NULL;
		file->f_pos = DIREOF;
	} else
		file->f_pos = rdstate2offset(uds);

out:
	return err;
}

/* This is not meant to be a generic repositioning function.  If you do
 * things that aren't supported, then we return EINVAL.
 *
 * What is allowed:
 *  (1) seeking to the same position that you are currently at
 *	This really has no effect, but returns where you are.
 *  (2) seeking to the beginning of the file
 *	This throws out all state, and lets you begin again.
 */
static loff_t unionfs_dir_llseek(struct file *file, loff_t offset, int origin)
{
	struct unionfs_dir_state *rdstate;
	loff_t err;

	if ((err = unionfs_file_revalidate(file, 0)))
		goto out;

	rdstate = UNIONFS_F(file)->rdstate;

	/* We let users seek to their current position, but not anywhere else. */
	if (!offset) {
		switch (origin) {
		case SEEK_SET:
			if (rdstate) {
				free_rdstate(rdstate);
				UNIONFS_F(file)->rdstate = NULL;
			}
			init_rdstate(file);
			err = 0;
			break;
		case SEEK_CUR:
			err = file->f_pos;
			break;
		case SEEK_END:
			/* Unsupported, because we would break everything.  */
			err = -EINVAL;
			break;
		}
	} else {
		switch (origin) {
		case SEEK_SET:
			if (rdstate) {
				if (offset == rdstate2offset(rdstate))
					err = offset;
				else if (file->f_pos == DIREOF)
					err = DIREOF;
				else
					err = -EINVAL;
			} else {
				rdstate = find_rdstate(file->f_dentry->d_inode,
							offset);
				if (rdstate) {
					UNIONFS_F(file)->rdstate = rdstate;
					err = rdstate->offset;
				} else
					err = -EINVAL;
			}
			break;
		case SEEK_CUR:
		case SEEK_END:
			/* Unsupported, because we would break everything.  */
			err = -EINVAL;
			break;
		}
	}

out:
	return err;
}

/* Trimmed directory options, we shouldn't pass everything down since
 * we don't want to operate on partial directories.
 */
struct file_operations unionfs_dir_fops = {
	.llseek		= unionfs_dir_llseek,
	.read		= generic_read_dir,
	.readdir	= unionfs_readdir,
	.unlocked_ioctl	= unionfs_ioctl,
	.open		= unionfs_open,
	.release	= unionfs_file_release,
	.flush		= unionfs_flush,
};

