/*
 * mdadm - Intel(R) Smart Response Technology Support
 *
 * Copyright (C) 2011-2014 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#ifndef __ISRT_INTEL_H__
#define __ISRT_INTEL_H__

enum {
	/* for a given cache device how many volumes can be associated */
	MAX_NV_CACHE_VOLS = 1,
	/* likely should be dynamically configurable when this driver is
	 * made more generic
	 */
	ISRT_FRAME_SIZE = 8192,
	VOL_CONFIG_RESERVED = 32,
	MD_HEADER_RESERVED = 32,
	MAX_RAID_SERIAL_LEN = 16,
	NVC_SIG_LEN = 32,
	ISRT_DEV_IDX = 0,
	ISRT_TARGET_DEV_IDX = 1,

	NV_CACHE_MODE_OFF          = 0,
	NV_CACHE_MODE_OFF_TO_SAFE  = 1, /* powerfail recovery state */
	NV_CACHE_MODE_OFF_TO_PERF  = 2, /* powerfail recovery state */
	NV_CACHE_MODE_SAFE         = 3,
	NV_CACHE_MODE_SAFE_TO_OFF  = 4,
	NV_CACHE_MODE_PERF         = 5,
	NV_CACHE_MODE_PERF_TO_OFF  = 6,
	NV_CACHE_MODE_PERF_TO_SAFE = 7,
	NV_CACHE_MODE_IS_FAILING   = 8,
	NV_CACHE_MODE_HAS_FAILED   = 9,
	NV_CACHE_MODE_DIS_PERF     = 10, /* caching on volume or nv cache disabled */
	NV_CACHE_MODE_DIS_SAFE     = 11, /* volume or NV cache not associated */
};

static inline int nvc_enabled(__u8 mode)
{
	switch (mode) {
	case NV_CACHE_MODE_OFF:
	case NV_CACHE_MODE_DIS_PERF:
	case NV_CACHE_MODE_DIS_SAFE:
		return 0;
	default:
		return 1;
	}
}

struct segment_index_pair {
	__u32 segment;
	__u32 index;
};

#define NV_CACHE_CONFIG_SIG "Intel IMSM NV Cache Cfg. Sig.   "
#define MAX_NVC_SIZE_GB            128UL      /* Max NvCache we can support is 128GB */
#define NVC_FRAME_SIZE             8192UL
#define NVC_FRAME_SIZE_IN_KB       (NVC_FRAME_SIZE / 1024UL)                  /* 8 */
#define NVC_FRAMES_PER_GB          (1024UL * (1024UL / NVC_FRAME_SIZE_IN_KB))   /* 128k */
#define MAX_NVC_FRAMES             (MAX_NVC_SIZE_GB * NVC_FRAMES_PER_GB)    /* 16m */
#define SEGIDX_PAIRS_PER_NVC_FRAME (NVC_FRAME_SIZE / sizeof(struct segment_index_pair)) /* 1k */
#define SEGHEAP_SEGS_PER_NVC_FRAME (NVC_FRAME_SIZE / sizeof(__u32)) /* 2k */
#define FRAMES_PER_SEGHEAP_FRAME   (SEGIDX_PAIRS_PER_NVC_FRAME \
				    * SEGHEAP_SEGS_PER_NVC_FRAME) /* 2m */
#define MAX_SEGHEAP_NVC_FRAMES     (MAX_NVC_FRAMES/FRAMES_PER_SEGHEAP_FRAME)  /* 8 */
#define MAX_SEGHEAP_TOC_ENTRIES    (MAX_SEGHEAP_NVC_FRAMES + 1)


/* XXX: size of enum guarantees? */
enum nvc_shutdown_state {
	ShutdownStateNormal,
	ShutdownStateS4CrashDmpStart,
	ShutdownStateS4CrashDmpEnd,
	ShutdownStateS4CrashDmpFailed
};

struct isrt_mpb {
	/*
	 * Metadata array (packed_md0_nba or packed_md1_nba).  is the base for
	 * the Metadata Delta Log changes.  The current contents of the Metadata
	 * Delta Log applied to this packed metadata base becomes the working
	 * packed metadata upon recovery from a power failure.  The alternate
	 * packed metadata array, indicated by (md_base_for_delta_log ^1) is
	 * where the next complete write of packed metadata from DRAM will be
	 * written. On a clean shutdown, packed metadata will also be written to
	 * the alternate array.
	 */
	__u32 packed_md0_nba; /* Start of primary packed metadata array */
	__u32 packed_md1_nba; /* Start of secondary packed metadata array */
	__u32 md_base_for_delta_log; /* 0 or 1. Indicates which packed */
	__u32 packed_md_size; /* Size of packed metadata array in bytes */
	__u32 aux_packed_md_nba; /* Start of array of extra metadata for driver use */
	__u32 aux_packed_md_size; /* Size of array of extra metadata for driver use */
	__u32 cache_frame0_nba; /* Start of actual cache frames */
	__u32 seg_num_index_nba; /* Start of the Seg_num_index array */
	__u32 seg_num_heap_nba; /* Start of the Seg_num_heap */
	__u32 seg_num_heap_size; /* Size of the Seg_num Heap in bytes (always a */
	/*
	 * Multiple of NVM_PAGE_SIZE bytes. The Seg_nums in the tail of the last
	 * page are all set to 0xFFFFFFFF
	 */
	__u32 seg_heap_toc[MAX_SEGHEAP_TOC_ENTRIES];
	__u32 md_delta_log_nba; /* Start of the Metadata Delta Log region */
	/*  The Delta Log is a circular buffer */
	__u32 md_delta_log_max_size; /* Size of the Metadata Delta Log region in bytes */
	__u32 orom_frames_to_sync_nba; /* Start of the orom_frames_to_sync record */
	__u32 num_cache_frames; /* Total number of cache frames */
	__u32 cache_frame_size; /* Size of each cache frame in bytes */
	__u32 lba_alignment; /* Offset to add to host I/O request LBA before
			       * shifting to form the segment number
			       */
	__u32 valid_frame_gen_num; /* Valid cache frame generation number */
	/*
	 * If the cache frame metadata contains a smaller generation number,
	 * that frame's contents are considered invalid.
	 */
	__u32 packed_md_frame_gen_num; /* Packed metadata frame generation number */
	/*
	 * This is the frame generation number associated with all frames in the
	 * packed metadata array. If this is < valid_frame_gen_num, then all
	 * frames in packed metadata are considered invalid.
	 */
	__u32 curr_clean_batch_num; /* Initialized to 0, incremented whenever
				      * the cache goes clean. If this value is
				      * greater than the Nv_cache_metadata
				      * dirty_batch_num in the atomic metadata
				      * of the cache frame, the frame is
				      * considered clean.
				      */
	__u32 total_used_sectors; /* Total number of NVM sectors of size
				    * NVM_SECTOR_SIZE used by cache frames and
				    * metadata.
				    */
	/* OROM I/O Log fields */
	__u32 orom_log_nba; /* OROM I/O Log area for next boot */
	__u32 orom_log_size; /* OROM I/O Log size in 512-byte blocks */

	/* Hibernate/Crashdump Extent_log */
	__u32 s4_crash_dmp_extent_log_nba; /* I/O Extent Log area created by the */
					   /* hibernate/crashdump driver for OROM */
	/* Driver shutdown state utilized by the OROM */
	enum nvc_shutdown_state driver_shutdown_state;

	__u32 validity_bits;
	__u64 nvc_hdr_array_in_dram;

	/* The following fields are used in managing the Metadata Delta Log. */

	/*
	 * Every delta record in the Metadata Delta Log  has a copy of the value
	 * of this field at the time the record was written. This gen num is
	 * incremented by 1 every time the log fills up, and allows powerfail
	 * recovery to easily find the end of the log (it's the first record
	 * whose gen num field is < curr_delta_log_gen_num.)
	 */
	__u32 curr_delta_log_gen_num;
	/*
	 * This is the Nba to the start of the current generation of delta
	 * records in the log.  Since the log is circular, the currentlog
	 * extends from md_delta_log_first up to and including
	 * (md_delta_log_first +max_records-2) % max_records) NOTE: when reading
	 * the delta log, the actual end of the log is indicated by the first
	 * record whose gen num field is <curr_delta_log_gen_num, so the
	 * 'max_records-2' guarantees we'll have at least one delta record whose
	 * gen num field will qualify to mark the end of the log.
	 */
	__u32 md_delta_log_first;
	/*
	 * How many free frames are used in the Metadata Delta Log. After every
	 * write of a delta log record that contains at least one
	 * Md_delta_log_entry, there must always be exactly
	 */

	__u32 md_delta_log_num_free_frames;
	__u32 num_dirty_frames; /* Number of dirty frames in cache when this
				  * isrt_mpb was written.
				  */
	__u32 num_dirty_frames_at_mode_trans; /* Number of dirty frames from
						* the start of the most recent
						* transition out of Performance
						* mode (Perf_to_safe/Perf_to_off)
						*/
} __attribute__((packed));


struct nv_cache_vol_config_md {
	__u32 acc_vol_orig_family_num; /* Unique Volume Id of the accelerated
					 * volume caching to the NVC Volume
					 */
	__u16 acc_vol_dev_id; /* (original family + dev_id ) if there is no
				* volume associated with Nv_cache, both of these
				* fields are 0.
				*/
	__u16 nv_cache_mode; /* NV Cache mode of this volume */
	/*
	 * The serial_no of the accelerated volume associated with Nv_cache.  If
	 * there is no volume associated with Nv_cache, acc_vol_name[0] = 0
	 */
	char acc_vol_name[MAX_RAID_SERIAL_LEN];
	__u32 flags;
	__u32 power_cycle_count; /* Power Cycle Count of the underlying disk or
				   * volume from the last device enumeration.
				   */
	/* Used to determine separation case. */
	__u32  expansion_space[VOL_CONFIG_RESERVED];
} __attribute__((packed));

struct nv_cache_config_md_header {
	char signature[NVC_SIG_LEN]; /* "Intel IMSM NV Cache Cfg. Sig.   " */
	__u16  version_number; /* NV_CACHE_CFG_MD_VERSION */
	__u16  header_length; /* Length by bytes */
	__u32  total_length; /* Length of the entire Config Metadata including
			       * header and volume(s) in bytes
			       */
	/* Elements above here will never change even in new versions */
	__u16  num_volumes; /* Number of volumes that have config metadata. in
			      * 9.0 it's either 0 or 1
			      */
	__u32 expansion_space[MD_HEADER_RESERVED];
	struct nv_cache_vol_config_md vol_config_md[MAX_NV_CACHE_VOLS]; /* Array of Volume */
	/* Config Metadata entries. Contains "num_volumes" */
	/* entries. In 9.0 'MAX_NV_CACHE_VOLS' = 1. */
} __attribute__((packed));

struct nv_cache_control_data {
	struct nv_cache_config_md_header hdr;
	struct isrt_mpb mpb;
} __attribute__((packed));

/* One or more sectors in NAND page are bad */
#define NVC_PACKED_SECTORS_BAD (1 << 0)
#define NVC_PACKED_DIRTY (1 << 1)
#define NVC_PACKED_FRAME_TYPE_SHIFT (2)
/* If set, frame is in clean area of LRU list */
#define NVC_PACKED_IN_CLEAN_AREA (1 << 5)
/*
 * This frame was TRIMMed (OROM shouldn't expect the delta log rebuild to match
 * the packed metadata stored on a clean shutdown.
 */
#define NVC_PACKED_TRIMMED (1 << 6)

struct nv_cache_packed_md {
	__u32 seg_num; /* Disk Segment currently assigned to frame */
	__u16 per_sector_validity; /* Per sector validity */
	__u8 flags;
	union {
		__u8 pad;
		/* repurpose padding for driver state */
		__u8 locked;
	};
} __attribute__((packed));

#define SEGMENTS_PER_PAGE_SHIFT 6
#define SEGMENTS_PER_PAGE (1 << SEGMENTS_PER_PAGE_SHIFT)
#define SEGMENTS_PER_PAGE_MASK (SEGMENTS_PER_PAGE-1)
#define FRAME_SHIFT 4
#define SECTORS_PER_FRAME (1 << FRAME_SHIFT)
#define FRAME_MASK (SECTORS_PER_FRAME-1)

#endif /* __ISRT_INTEL_H__ */
