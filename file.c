/*
 * Copyright (c) 2012 Dave Vasilevsky <dave@vasilevsky.ca>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "file.h"

#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>

#include "squashfuse.h"

sqfs_err sqfs_frag_entry(sqfs *fs, struct squashfs_fragment_entry *frag,
		uint32_t idx) {
	sqfs_err err = SQFS_OK;
	
	if (idx == SQUASHFS_INVALID_FRAG)
		return SQFS_ERR;
	
	err = sqfs_table_get(&fs->frag_table, fs, idx, frag);
	sqfs_swapin_fragment_entry(frag);
	return err;
}

sqfs_err sqfs_frag_block(sqfs *fs, sqfs_inode *inode,
		size_t *offset, size_t *size, sqfs_block **block) {
	struct squashfs_fragment_entry frag;
	sqfs_err err = SQFS_OK;
	
	if (!S_ISREG(inode->base.mode))
		return SQFS_ERR;
	
	err = sqfs_frag_entry(fs, &frag, inode->xtra.reg.frag_idx);
	if (err)
		return err;
	
	err = sqfs_data_cache(fs, &fs->frag_cache, frag.start_block,
		frag.size, block);
	if (err)
		return SQFS_ERR;
	
	*offset = inode->xtra.reg.frag_off;
	*size = inode->xtra.reg.file_size % fs->sb.block_size;
	return SQFS_OK;
}

size_t sqfs_blocklist_count(sqfs *fs, sqfs_inode *inode) {
	uint64_t size = inode->xtra.reg.file_size, block = fs->sb.block_size;
	if (inode->xtra.reg.frag_idx == SQUASHFS_INVALID_FRAG) {
		return sqfs_divceil(size, block);
	} else {
		return size / block;
	}
}

void sqfs_blocklist_init(sqfs *fs, sqfs_inode *inode, sqfs_blocklist *bl) {
	bl->fs = fs;
	bl->remain = sqfs_blocklist_count(fs, inode);
	bl->cur = inode->next;
	bl->started = false;
	bl->pos = 0;
	bl->block = inode->xtra.reg.start_block;
	bl->input_size = 0;
}

sqfs_err sqfs_blocklist_next(sqfs_blocklist *bl) {
	sqfs_err err = SQFS_OK;
	bool compressed;
	
	if (bl->remain == 0)
		return SQFS_ERR;
	--(bl->remain);
	
	err = sqfs_md_read(bl->fs, &bl->cur, &bl->header,
		sizeof(bl->header));
	if (err)
		return err;
	sqfs_swapin32(&bl->header);
	
	bl->block += bl->input_size;
	sqfs_data_header(bl->header, &compressed, &bl->input_size);
	
	if (bl->started)
		bl->pos += bl->fs->sb.block_size;
	bl->started = true;
	
	return SQFS_OK;
}

sqfs_err sqfs_read_range(sqfs *fs, sqfs_inode *inode, off_t start,
		off_t *size, void *buf) {
	sqfs_err err = SQFS_OK;
	
	off_t file_size;
	size_t block_size;
	sqfs_blocklist bl;
	
	size_t read_off;
	char *buf_orig;
	
	if (!S_ISREG(inode->base.mode))
		return SQFS_ERR;
	
	file_size = inode->xtra.reg.file_size;
	block_size = fs->sb.block_size;
	
	if (*size < 0 || start > file_size)
		return SQFS_ERR;
	if (start == file_size) {
		*size = 0;
		return SQFS_OK;
	}
	
	err = sqfs_blockidx_blocklist(fs, inode, &bl, start);
	if (err)
		return err;
	
	read_off = start % block_size;
	buf_orig = buf;
	while (*size > 0) {
		sqfs_block *block = NULL;
		size_t data_off, data_size;
		size_t take;
		
		bool fragment = (bl.remain == 0);
		if (fragment) { /* fragment */
			if (inode->xtra.reg.frag_idx == SQUASHFS_INVALID_FRAG)
				break;
			err = sqfs_frag_block(fs, inode, &data_off, &data_size, &block);
			if (err)
				return err;
		} else {			
			if ((err = sqfs_blocklist_next(&bl)))
				return err;
			if (bl.pos + block_size <= start)
				continue;
			
			data_off = 0;
			if (bl.input_size == 0) { /* Hole! */
				data_size = file_size - bl.pos;
				if (data_size > block_size)
					data_size = block_size;
			} else {
				err = sqfs_data_cache(fs, &fs->data_cache, bl.block,
					bl.header, &block);
				if (err)
					return err;
				data_size = block->size;
			}
		}
		
		take = data_size - read_off;
		if (take > *size)
			take = *size;
		if (block) {
			memcpy(buf, (char*)block->data + data_off + read_off, take);
			/* BLOCK CACHED, DON'T DISPOSE */
		} else {
			memset(buf, 0, take);
		}
		read_off = 0;
		*size -= take;
		buf = (char*)buf + take;
		
		if (fragment)
			break;
	}
	
	*size = (char*)buf - buf_orig;
	return *size ? SQFS_OK : SQFS_ERR;
}


static void sqfs_blockidx_dispose(void *data) {
	free(*(sqfs_blockidx_entry**)data);
}

sqfs_err sqfs_blockidx_init(sqfs_cache *cache) {
	return sqfs_cache_init(cache, sizeof(sqfs_blockidx_entry*),
		SQUASHFS_META_SLOTS, &sqfs_blockidx_dispose);
}

sqfs_err sqfs_blockidx_add(sqfs *fs, sqfs_inode *inode,
		sqfs_blockidx_entry **out) {
	size_t blocks, md_size, count;
	
	sqfs_blockidx_entry *blockidx;
	sqfs_blocklist bl;
	
	sqfs_cache_idx idx;
	sqfs_blockidx_entry **cachep;

	size_t i = 0;
	bool first = true;
	
	blocks = sqfs_blocklist_count(fs, inode);
	md_size = blocks * sizeof(sqfs_blocklist_entry);
	*out = NULL;
	if (md_size < SQUASHFS_METADATA_SIZE)
		return SQFS_OK; /* not worth indexing */
	
	count = (inode->next.offset + md_size - 1)
		/ SQUASHFS_METADATA_SIZE;
	blockidx = malloc(count * sizeof(sqfs_blockidx_entry));
	if (!blockidx)
		return SQFS_ERR;
	
	sqfs_blocklist_init(fs, inode, &bl);
	while (bl.remain && i < count) {
		sqfs_err err = SQFS_OK;
		if (bl.cur.offset < sizeof(sqfs_blocklist_entry) && !first) {
			blockidx[i].data_block = bl.block + bl.input_size;
			blockidx[i++].md_block = bl.cur.block - fs->sb.inode_table_start;
		}
		first = false;
		
		err = sqfs_blocklist_next(&bl);
		if (err) {
			free(blockidx);
			return SQFS_ERR;
		}
	}

	idx = inode->base.inode_number + 1;
	cachep = sqfs_cache_add(&fs->blockidx, idx);
	*out = *cachep = blockidx;
	return SQFS_OK;
}

sqfs_err sqfs_blockidx_blocklist(sqfs *fs, sqfs_inode *inode,
		sqfs_blocklist *bl, off_t start) {
	size_t block, metablock, skipped;
	sqfs_blockidx_entry *blockidx, **bp;
	sqfs_cache_idx idx;
	
	sqfs_blocklist_init(fs, inode, bl);
	block = start / fs->sb.block_size;
	if (block > bl->remain) { /* fragment */
		bl->remain = 0;
		return SQFS_OK;
	}
	
	metablock = (bl->cur.offset + block * sizeof(sqfs_blocklist_entry))
		/ SQUASHFS_METADATA_SIZE;
	if (metablock == 0)
		return SQFS_OK; /* don't want an index */
	
	/* Get the index, creating it if necessary */
	idx = inode->base.inode_number + 1;
	if ((bp = sqfs_cache_get(&fs->blockidx, idx))) {
		blockidx = *bp;
	} else {
		sqfs_err err = sqfs_blockidx_add(fs, inode, &blockidx);
		if (err)
			return err;
	}
	if (!blockidx)
		return SQFS_OK; /* not indexable */
	
	skipped = (metablock * SQUASHFS_META_INDEXES)
		- (bl->cur.offset / sizeof(sqfs_blocklist_entry));
	
	blockidx += metablock - 1;
	bl->cur.block = blockidx->md_block + fs->sb.inode_table_start;
	bl->cur.offset %= sizeof(sqfs_blocklist_entry);
	bl->remain -= skipped;
	bl->pos = skipped * fs->sb.block_size;
	bl->block = blockidx->data_block;
	return SQFS_OK;
}

