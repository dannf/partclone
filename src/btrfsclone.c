/**
 * btrfsclone.c - part of Partclone project
 *
 * Copyright (c) 2007~ Thomas Tsai <thomas at nchc org tw>
 *
 * read btrfs super block and extent
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdio.h>
#include "btrfs/version.h"
#include "btrfs/ctree.h"
#include "btrfs/disk-io.h"
#include "btrfs/volumes.h"

#include "partclone.h"
#include "btrfsclone.h"
#include "progress.h"
#include "fs_common.h"

char *EXECNAME = "partclone.btrfs";

struct btrfs_root *root;
struct btrfs_path *path;
int block_size = 0;

///set useb block
static void set_bitmap(unsigned long* bitmap, uint64_t pos, uint64_t length){
    uint64_t block;
    uint64_t pos_block;
    uint64_t count;

    pos_block = pos/block_size;
    count = length/block_size;

    for(block = pos_block; block < (pos_block+count); block++){
	pc_set_bit(block, bitmap);
	log_mesg(3, 0, 0, fs_opt.debug, "%s: block %i is used\n",__FILE__,  block);
    }
}

/// open device
static void fs_open(char* device){
    log_mesg(0, 0, 0, fs_opt.debug, "\n%s: btrfs library version = %s\n", __FILE__, BTRFS_BUILD_VERSION);
    radix_tree_init();
    root = open_ctree(device, 0, 0);
    if(!root){
	log_mesg(0, 1, 1, fs_opt.debug, "%s: Unable to open %s\n", __FILE__, device);	
    }
    path = btrfs_alloc_path();

}

/// close device
static void fs_close(){
    int ret = 0;
    btrfs_free_path(path);
    ret = close_ctree(root);
    if(ret)
	log_mesg(0, 1, 1, fs_opt.debug, "%s: close ctree fail\n", __FILE__);

}

void read_bitmap(char* device, file_system_info fs_info, unsigned long* bitmap, int pui)
{
    u64 super_block = 0;
    struct btrfs_root *extent_root;
    struct btrfs_extent_item *ei;
    struct extent_buffer *leaf;
    struct extent_buffer *eb;
    struct btrfs_key key;
    u64 bytenr;
    u64 num_bytes;
    u64 length;
    u64 block_offset;
    int leafsize = 0;
    struct btrfs_multi_bio *multi = NULL;
    int ret;
    int i;

    fs_open(device);
    block_size = fs_info.block_size;
    //initial all block as free
    pc_init_bitmap(bitmap, 0x00, fs_info.totalblock);

    // set super block and tree as used
    super_block = BTRFS_SUPER_INFO_OFFSET / block_size;
    leafsize = btrfs_super_leafsize(root->fs_info->super_copy);
    log_mesg(1, 0, 0, fs_opt.debug, "%s: leafsize %i\n", __FILE__, leafsize);
    log_mesg(1, 0, 0, fs_opt.debug, "%s: super block %llu\n", __FILE__, super_block);
    pc_set_bit(super_block, bitmap);
    for (i = 1; i < 7; i++) {
	bytenr = (BTRFS_SUPER_INFO_OFFSET + 1024 * 1024 + leafsize * i ) / block_size;
	log_mesg(1, 0, 0, fs_opt.debug, "%s: tree block %llu\n", __FILE__, bytenr);
	pc_set_bit(bytenr, bitmap);
    }


    extent_root = root->fs_info->extent_root;
    bytenr = BTRFS_SUPER_INFO_OFFSET + 4096;
    key.objectid = bytenr;
    key.type = BTRFS_EXTENT_ITEM_KEY;
    key.offset = 0;

    ret = btrfs_search_slot(NULL, extent_root, &key, path, 0, 0);
    while(1){
	leaf = path->nodes[0];
	if (path->slots[0] >= btrfs_header_nritems(leaf)){
	    ret = btrfs_next_leaf(extent_root, path);
	    if(ret > 0)
		break;
	    leaf = path->nodes[0];
	}

	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	if(key.objectid < bytenr || key.type != BTRFS_EXTENT_ITEM_KEY){
	    path->slots[0]++;
	    continue;
	}

	bytenr = key.objectid;
        block_offset = bytenr;
	num_bytes = key.offset;
        log_mesg(2, 0, 0, fs_opt.debug, "%s: bytenr = %llu, size = %llu\n", __FILE__, bytenr, num_bytes);

        if (btrfs_item_size_nr(leaf, path->slots[0]) > sizeof(*ei)) {
	    ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	    if (btrfs_extent_flags(leaf, ei) & BTRFS_EXTENT_FLAG_TREE_BLOCK) {

                /*disk_io.c*/
		eb = btrfs_find_tree_block(root, bytenr, num_bytes);
		if (eb && btrfs_buffer_uptodate(eb, 0)) {
			free_extent_buffer(eb);
		}

		/*READ=0, mirror=0*/
		ret = btrfs_map_block(&root->fs_info->mapping_tree, 0, bytenr, &length, &multi, 0, NULL);
                block_offset = multi->stripes[0].physical;
		log_mesg(2, 0, 0, fs_opt.debug, "%s: 1 bytenr = %llu, size = %llu physical=%llu \n", __FILE__, bytenr, num_bytes, block_offset);
		set_bitmap(bitmap, block_offset, num_bytes);
	    } else {
		/*READ=0, mirror=0*/
		ret = btrfs_map_block(&root->fs_info->mapping_tree, 0, bytenr, &length, &multi, 0, NULL);
                block_offset = multi->stripes[0].physical;
		log_mesg(2, 0, 0, fs_opt.debug, "%s: 2 bytenr = %llu, size = %llu physical=%llu \n", __FILE__, bytenr, num_bytes, block_offset);
		set_bitmap(bitmap, block_offset, num_bytes);
            }
	} else {
	    /*READ=0, mirror=0*/
	    ret = btrfs_map_block(&root->fs_info->mapping_tree, 0, bytenr, &length, &multi, 0, NULL);
	    block_offset = multi->stripes[0].physical;
	    log_mesg(2, 0, 0, fs_opt.debug, "%s: 3 bytenr = %llu, size = %llu physical=%llu \n", __FILE__, bytenr, num_bytes, block_offset);
	    set_bitmap(bitmap, block_offset, num_bytes);
	}
	bytenr+=num_bytes;
    }
    fs_close();
}

void read_super_blocks(char* device, file_system_info* fs_info)
{    
    fs_open(device);

    strncpy(fs_info->fs, btrfs_MAGIC, FS_MAGIC_SIZE);

    fs_info->block_size  = btrfs_super_nodesize(root->fs_info->super_copy);
    fs_info->usedblocks  = btrfs_super_bytes_used(root->fs_info->super_copy) / fs_info->block_size;
    fs_info->device_size = btrfs_super_total_bytes(root->fs_info->super_copy);
    fs_info->totalblock  = fs_info->device_size / fs_info->block_size;
    log_mesg(0, 0, 0, fs_opt.debug, "block_size = %i\n", fs_info->block_size);
    log_mesg(0, 0, 0, fs_opt.debug, "usedblock = %lli\n", fs_info->usedblocks);
    log_mesg(0, 0, 0, fs_opt.debug, "device_size = %llu\n", fs_info->device_size);
    log_mesg(0, 0, 0, fs_opt.debug, "totalblock = %lli\n", fs_info->totalblock);

    fs_close();
}

