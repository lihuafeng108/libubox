/*
 * ustream - library for stream buffer management
 *
 * Copyright (C) 2012 Felix Fietkau <nbd@openwrt.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include "ustream.h"

static void ustream_fd_set_uloop(struct ustream *s)
{
	struct ustream_fd *sf = container_of(s, struct ustream_fd, stream);
	struct ustream_buf *buf;
	unsigned int flags = ULOOP_EDGE_TRIGGER;

	if (!s->read_blocked && !s->eof)
		flags |= ULOOP_READ;

	buf = s->w.head;
	if (buf && s->w.data_bytes && !s->write_error)
		flags |= ULOOP_WRITE;

	uloop_fd_add(&sf->fd, flags);

	if (flags & ULOOP_READ)
		sf->fd.cb(&sf->fd, ULOOP_READ);
}

static void ustream_fd_read_pending(struct ustream_fd *sf, bool *update)
{
	struct ustream *s = &sf->stream;
	int buflen = 0;
	ssize_t len;
	char *buf;

	do {
		buf = ustream_reserve(s, 1, &buflen);
		if (!buf)
			break;

		len = read(sf->fd.fd, buf, buflen);
		if (!len) {
			sf->fd.eof = true;
			return;
		}

		if (len < 0) {
			if (errno == EINTR)
				continue;

			if (errno == EAGAIN)
				return;
		}

		ustream_fill_read(s, len);
	} while (1);
}

static int ustream_fd_write(struct ustream *s, const char *buf, int buflen, bool more)
{
	struct ustream_fd *sf = container_of(s, struct ustream_fd, stream);
	ssize_t len;

	if (!buflen)
		return 0;

retry:
	len = write(sf->fd.fd, buf, buflen);
	if (!len)
		goto retry;

	if (len < 0) {
		if (errno == EINTR)
			goto retry;

		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;
	}

	return len;
}

static void ustream_uloop_cb(struct uloop_fd *fd, unsigned int events)
{
	struct ustream_fd *sf = container_of(fd, struct ustream_fd, fd);
	struct ustream *s = &sf->stream;
	bool update = false;

	if (events & ULOOP_READ)
		ustream_fd_read_pending(sf, &update);

	if (events & ULOOP_WRITE) {
		if (ustream_write_pending(s))
			ustream_fd_set_uloop(s);
	}

	if (!s->eof && fd->eof) {
		s->eof = true;
		ustream_fd_set_uloop(s);
		ustream_state_change(s);
	}
}


static void ustream_fd_free(struct ustream *s)
{
	struct ustream_fd *sf = container_of(s, struct ustream_fd, stream);

	uloop_fd_delete(&sf->fd);
}

void ustream_fd_init(struct ustream_fd *sf, int fd)
{
	struct ustream *s = &sf->stream;

	ustream_init_defaults(s);

	sf->fd.fd = fd;
	sf->fd.cb = ustream_uloop_cb;
	s->set_read_blocked = ustream_fd_set_uloop;
	s->write = ustream_fd_write;
	s->free = ustream_fd_free;
	ustream_fd_set_uloop(s);
}
