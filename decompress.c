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
#include "decompress.h"

#include "squashfuse.h"

#include <string.h>

#ifdef HAVE_ZLIB_H
#include <zlib.h>

static sqfs_err sqfs_decompressor_zlib(void *in, size_t insz,
		void *out, size_t *outsz) {
	uLongf zout = *outsz;
	int zerr = uncompress((Bytef*)out, &zout, in, insz);
	if (zerr != Z_OK)
		return SQFS_ERR;
	*outsz = zout;
	return SQFS_OK;
}
#endif


#ifdef HAVE_LZMA_H
#include <lzma.h>

static sqfs_err sqfs_decompressor_xz(void *in, size_t insz,
		void *out, size_t *outsz) {
	/* FIXME: Save stream state, to minimize setup time? */
	uint64_t memlimit = UINT64_MAX;
	size_t inpos = 0, outpos = 0;
	lzma_ret err = lzma_stream_buffer_decode(&memlimit, 0, NULL, in, &inpos, insz,
		out, &outpos, *outsz);
	if (err != LZMA_OK)
		return SQFS_ERR;
	*outsz = outpos;
	return SQFS_OK;
}
#endif


#ifdef HAVE_LZO_LZO1X_H
#include <lzo/lzo1x.h>

static sqfs_err sqfs_decompressor_lzo(void *in, size_t insz,
		void *out, size_t *outsz) {
	lzo_uint lzout = *outsz;
	int err = lzo1x_decompress_safe(in, insz, out, &lzout, NULL);
	if (err != LZO_E_OK)
		return SQFS_ERR;
	*outsz = lzout;
	return SQFS_OK;
}
#endif


sqfs_decompressor sqfs_decompressor_get(sqfs_compression_type type) {
	switch (type) {
#ifdef HAVE_ZLIB_H
		case ZLIB_COMPRESSION: return &sqfs_decompressor_zlib;
#endif
#ifdef HAVE_LZMA_H
		case XZ_COMPRESSION: return &sqfs_decompressor_xz;
#endif
#ifdef HAVE_LZO_LZO1X_H
		case LZO_COMPRESSION: return &sqfs_decompressor_lzo;
#endif
		default: return NULL;
	}
}

static char *const sqfs_compression_names[SQFS_COMP_MAX] = {
	NULL, "zlib", "lzma", "lzo", "xz",
};

char *sqfs_compression_name(sqfs_compression_type type) {
	if (type < 0 || type >= SQFS_COMP_MAX)
		return NULL;
	return sqfs_compression_names[type];
}

void sqfs_compression_supported(sqfs_compression_type *types) {
	size_t i = 0;
	memset(types, SQFS_COMP_UNKNOWN, SQFS_COMP_MAX * sizeof(*types));
#ifdef HAVE_LZO_LZO1X_H
	types[i++] = LZO_COMPRESSION;
#endif
#ifdef HAVE_LZMA_H
	types[i++] = XZ_COMPRESSION;
#endif
#ifdef HAVE_ZLIB_H
	types[i++] = ZLIB_COMPRESSION;
#endif
}
