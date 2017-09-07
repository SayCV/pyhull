/* Use funopen(3) to provide open_memstream(3) like functionality. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(__APPLE__)
struct memstream {
	char **cp;
	size_t *lenp;
	size_t offset;
};

static void
memstream_grow(struct memstream *ms, size_t newsize)
{
	char *buf;

	if (newsize > *ms->lenp) {
		buf = realloc(*ms->cp, newsize + 1);
		if (buf != NULL) {
#ifdef DEBUG
			fprintf(stderr, "MS: %p growing from %zd to %zd\n",
			    ms, *ms->lenp, newsize);
#endif
			memset(buf + *ms->lenp + 1, 0, newsize - *ms->lenp);
			*ms->cp = buf;
			*ms->lenp = newsize;
		}
	}
}

static int
memstream_read(void *cookie, char *buf, int len)
{
	struct memstream *ms;
	int tocopy;

	ms = cookie;
	memstream_grow(ms, ms->offset + len);
	tocopy = *ms->lenp - ms->offset;
	if (len < tocopy)
		tocopy = len;
	memcpy(buf, *ms->cp + ms->offset, tocopy);
	ms->offset += tocopy;
#ifdef DEBUG
	fprintf(stderr, "MS: read(%p, %d) = %d\n", ms, len, tocopy);
#endif
	return (tocopy);
}

static int
memstream_write(void *cookie, const char *buf, int len)
{
	struct memstream *ms;
	int tocopy;

	ms = cookie;
	memstream_grow(ms, ms->offset + len);
	tocopy = *ms->lenp - ms->offset;
	if (len < tocopy)
		tocopy = len;
	memcpy(*ms->cp + ms->offset, buf, tocopy);
	ms->offset += tocopy;
#ifdef DEBUG
	fprintf(stderr, "MS: write(%p, %d) = %d\n", ms, len, tocopy);
#endif
	return (tocopy);
}

static fpos_t
memstream_seek(void *cookie, fpos_t pos, int whence)
{
	struct memstream *ms;
#ifdef DEBUG
	size_t old;
#endif

	ms = cookie;
#ifdef DEBUG
	old = ms->offset;
#endif
	switch (whence) {
	case SEEK_SET:
		ms->offset = pos;
		break;
	case SEEK_CUR:
		ms->offset += pos;
		break;
	case SEEK_END:
		ms->offset = *ms->lenp + pos;
		break;
	}
#ifdef DEBUG
	fprintf(stderr, "MS: seek(%p, %zd, %d) %zd -> %zd\n", ms, pos, whence,
	    old, ms->offset);
#endif
	return (ms->offset);
}

static int
memstream_close(void *cookie)
{

	free(cookie);
	return (0);
}

FILE *
open_memstream(char **cp, size_t *lenp)
{
	struct memstream *ms;
	int save_errno;
	FILE *fp;

	*cp = NULL;
	*lenp = 0;
	ms = malloc(sizeof(*ms));
	ms->cp = cp;
	ms->lenp = lenp;
	ms->offset = 0;
	fp = funopen(ms, memstream_read, memstream_write, memstream_seek,
	    memstream_close);
	if (fp == NULL) {
		save_errno = errno;
		free(ms);
		errno = save_errno;
	}
	return (fp);
}

#elif defined(_WIN32)
/* -*- mode: c -*- */
/* $Id: open_memstream.c 5575 2009-07-20 15:32:03Z cher $ */

/* Copyright (C) 2008-2009 Alexander Chernov <cher@ejudge.ru> */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef HAVE_OPEN_MEMSTREAM /* win32 version */

static void
addONode(int o_stream_number, FILE *file, char **buf, size_t *length);
static void delONode(FILE *file);
static int get_o_stream_number(void);
static void setODirName(char *str);
static void setOFileName(char *str, int stream_number);

struct oListNode
{
  int o_stream_number;
  FILE *file;
  char **buf;
  size_t *length;
  struct oListNode *pnext;
};

static struct oListNode *oList = NULL;

static void addONode(
        int o_stream_number,
        FILE *file,
        char **buf,
        size_t *length)
{
  struct oListNode **pcur = &oList;
  struct oListNode *node = calloc(1, sizeof(struct oListNode));
  
  if(node == NULL)
    abort();
  
  while((*pcur) && (*pcur)->o_stream_number < o_stream_number)
    pcur = &((*pcur)->pnext);
        
  node->pnext = *pcur;
  node->o_stream_number = o_stream_number;
  node->file = file;
  node->buf = buf;
  node->length = length;
  (*pcur) = node;
}

static void delONode(FILE *file)
{
  struct oListNode **pcur = &oList;
  struct oListNode *todel;
  char file_name[30];

  while((*pcur) && (*pcur)->file != file)
    pcur = &((*pcur)->pnext);

  todel = (*pcur);
  if(todel == NULL){ //not found
    // WARNING(("Trying to close a simple FILE* with close_memstream()"));
  } else {
    if(EOF == fflush(file))
      abort();
    if((*(todel->length) = ftell(file)) == -1)
      abort();
    if((*(todel->buf) = calloc(1, *(todel->length) + 1)) == NULL)
      abort();
    if(EOF == fseek(file, 0, SEEK_SET))
      abort();
    fread(*(todel->buf), 1, *(todel->length), file);

    fclose(file);
    setOFileName(file_name,todel->o_stream_number);
    if(-1 == remove(file_name))
      abort();

    (*pcur) = todel->pnext;
    free(todel);
  }
}


static int get_o_stream_number(void)
{
  int o_stream_number = 1;
  struct oListNode *cur = oList;
  
  while(cur && o_stream_number >= cur->o_stream_number){
    o_stream_number++;
        cur = cur->pnext;
  }
  return o_stream_number;
}

static void setODirName(char *str)
{
  sprintf(str, "ostr_job_%d", _getpid());
}
 
static void setOFileName(char *str, int stream_number)
{
  setODirName(str);
  char fname[30];
  sprintf(fname,"/o_stream_%d",stream_number);
  strcat(str,fname);
}

FILE *
open_memstream(char **ptr, size_t *sizeloc)
{
  FILE *f;
  char file_name[30];
  int o_stream_number;
  
  if(oList == NULL){
    setODirName(file_name);
    mkdir(file_name);
  }

  o_stream_number = get_o_stream_number();
  setOFileName(file_name,o_stream_number);
  f = fopen(file_name,"w+");
  
  if(!f)
    return NULL;
  
  addONode(o_stream_number, f, ptr, sizeloc);
  
  return f;
}


void
close_memstream(FILE *f)
{
  char file_name[30];
  delONode(f);

  if(oList == NULL){
    setODirName(file_name);
    rmdir(file_name);
  }
}

#else

void
close_memstream(FILE *f)
{
  fclose(f);
}

#endif /* HAVE_OPEN_MEMSTREAM */

/*
 * Local variables:
 *  compile-command: "make -C .."
 *  c-font-lock-extra-types: ("\\sw+_t" "FILE")
 * End:
 */
#endif