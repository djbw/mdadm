/*
 * mdadm - manage Linux "md" devices aka RAID arrays.
 *
 * Copyright (C) 2001-2013 Neil Brown <neilb@suse.de>
 *
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *    Author: Neil Brown
 *    Email: <neilb@suse.de>
 */

#include	"mdadm.h"
#include	"dlink.h"

#if ! defined(__BIG_ENDIAN) && ! defined(__LITTLE_ENDIAN)
#error no endian defined
#endif
#include	"md_u.h"
#include	"md_p.h"

struct array {
	struct supertype *st;
	struct mdinfo info;
	void *devs;
	struct array *next;
	int spares;
	int cache_leg;
};

static struct array *add_cache_legs(struct array *caches, struct supertype *st,
				    struct mdinfo *info, struct array *arrays)
{
	struct mdinfo cache_info;
	struct array *ap;
	int i;

	for (i = 1; i <= info->cache_legs; i++) {
		/* in the case where the cache leg is assembled its uuid
		 * may appear in the arrays list, so we need to check
		 * both the caches list and the arrays list for
		 * duplicates
		 */
		struct array *lists[] = { caches, arrays };
		int j;

		st->cache_leg = i;
		st->ss->getinfo_super(st, &cache_info, NULL);
		st->cache_leg = 0;
		for (j = 0; j < 2; j++) {
			for (ap = lists[j]; ap; ap = ap->next) {
				if (st->ss == ap->st->ss
				    && same_uuid(ap->info.uuid, cache_info.uuid,
						 st->ss->swapuuid))
					break;
			}
			if (ap)
				break;
		}
		if (!ap) {
			ap = xcalloc(1, sizeof(*ap));
			ap->devs = dl_head();
			ap->next = caches;
			ap->st = st;
			ap->cache_leg = i;
			caches = ap;
			memcpy(&ap->info, &cache_info, sizeof(cache_info));
		}
	}

	return caches;
}

static void free_arrays(struct array *arrays)
{
	struct array *ap;

	while (arrays) {
		ap = arrays;
		arrays = ap->next;

		ap->st->ss->free_super(ap->st);
		free(ap);
	}
}


int Examine(struct mddev_dev *devlist,
	    struct context *c,
	    struct supertype *forcest)
{

	/* Read the raid superblock from a device and
	 * display important content.
	 *
	 * If cannot be found, print reason: too small, bad magic
	 *
	 * Print:
	 *   version, ctime, level, size, raid+spare+
	 *   prefered minor
	 *   uuid
	 *
	 *   utime, state etc
	 *
	 * If (brief) gather devices for same array and just print a mdadm.conf
	 * line including devices=
	 * if devlist==NULL, use conf_get_devs()
	 */
	int fd;
	int rv = 0;
	int err = 0;
	struct array *arrays = NULL, *caches = NULL;

	for (; devlist ; devlist = devlist->next) {
		struct supertype *st;
		int have_container = 0;

		fd = dev_open(devlist->devname, O_RDONLY);
		if (fd < 0) {
			if (!c->scan) {
				pr_err("cannot open %s: %s\n",
				       devlist->devname, strerror(errno));
				rv = 1;
			}
			err = 1;
		}
		else {
			int container = 0;
			if (forcest)
				st = dup_super(forcest);
			else if (must_be_container(fd)) {
				/* might be a container */
				st = super_by_fd(fd, NULL);
				container = 1;
			} else
				st = guess_super(fd);
			if (st) {
				err = 1;
				st->ignore_hw_compat = 1;
				if (!container)
					err = st->ss->load_super(st, fd,
								 (c->brief||c->scan) ? NULL
								 :devlist->devname);
				if (err && st->ss->load_container) {
					err = st->ss->load_container(st, fd,
								 (c->brief||c->scan) ? NULL
								 :devlist->devname);
					if (!err)
						have_container = 1;
				}
				st->ignore_hw_compat = 0;
			} else {
				if (!c->brief) {
					pr_err("No md superblock detected on %s.\n", devlist->devname);
					rv = 1;
				}
				err = 1;
			}
			close(fd);
		}
		if (err)
			continue;

		if (c->SparcAdjust)
			st->ss->update_super(st, NULL, "sparc2.2",
					     devlist->devname, 0, 0, NULL);
		/* Ok, its good enough to try, though the checksum could be wrong */

		if (c->brief && st->ss->brief_examine_super == NULL) {
			if (!c->scan)
				pr_err("No brief listing for %s on %s\n",
					st->ss->name, devlist->devname);
		} else if (c->brief) {
			struct array *ap;
			char *d;
			for (ap = arrays; ap; ap = ap->next) {
				if (st->ss == ap->st->ss &&
				    st->ss->compare_super(ap->st, st) == 0)
					break;
			}
			if (!ap) {
				ap = xcalloc(1, sizeof(*ap));
				ap->devs = dl_head();
				ap->next = arrays;
				ap->st = st;
				arrays = ap;
				st->ss->getinfo_super(st, &ap->info, NULL);
				caches = add_cache_legs(caches, st, &ap->info,
							arrays);
			} else
				st->ss->getinfo_super(st, &ap->info, NULL);
			if (!have_container &&
			    !(ap->info.disk.state & (1<<MD_DISK_SYNC)))
				ap->spares++;
			d = dl_strdup(devlist->devname);
			dl_add(ap->devs, d);
		} else if (c->export) {
			if (st->ss->export_examine_super)
				st->ss->export_examine_super(st);
			st->ss->free_super(st);
		} else {
			printf("%s:\n",devlist->devname);
			st->ss->examine_super(st, c->homehost);
			st->ss->free_super(st);
		}
	}
	if (c->brief) {
		struct array *ap;
		for (ap = arrays; ap; ap = ap->next) {
			char sep='=';
			char *d;
			int newline = 0;

			ap->st->ss->brief_examine_super(ap->st, c->verbose > 0);
			if (ap->spares)
				newline += printf("   spares=%d", ap->spares);
			if (c->verbose > 0) {
				newline += printf("   devices");
				for (d = dl_next(ap->devs);
				     d != ap->devs;
				     d=dl_next(d)) {
					printf("%c%s", sep, d);
					sep=',';
				}
			}
			if (ap->st->ss->brief_examine_subarrays) {
				if (newline)
					printf("\n");
				ap->st->ss->brief_examine_subarrays(ap->st, c->verbose);
			}
			if (ap->spares || c->verbose > 0)
				printf("\n");
		}
		/* list container caches after their parent containers
		 * and subarrays
		 */
		for (ap = caches; ap; ap = ap->next)
			if (ap->st->ss->brief_examine_cache)
				ap->st->ss->brief_examine_cache(ap->st, ap->cache_leg);
		free_arrays(arrays);
		free_arrays(caches);

	}
	return rv;
}

int ExamineBadblocks(char *devname, int brief, struct supertype *forcest)
{
	int fd = dev_open(devname, O_RDONLY);
	struct supertype *st = forcest;
	int err = 1;

	if (fd < 0) {
		pr_err("cannot open %s: %s\n", devname, strerror(errno));
		return 1;
	}
	if (!st)
		st = guess_super(fd);
	if (!st) {
		if (!brief)
			pr_err("No md superblock detected on %s\n", devname);
		goto out;
	}
	if (!st->ss->examine_badblocks) {
		pr_err("%s metadata does not support badblocks\n", st->ss->name);
		goto out;
	}
	err = st->ss->load_super(st, fd, brief ? NULL : devname);
	if (err)
		goto out;
	err = st->ss->examine_badblocks(st, fd, devname);

out:
	if (fd >= 0)
		close(fd);
	if (st) {
		st->ss->free_super(st);
		free(st);
	}
	return err;
}
