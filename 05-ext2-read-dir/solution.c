#include <solution.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <ext2fs/ext2fs.h>

int dir_report(int img, void* buf, const int block_size, const int block, int* left) {

    if (pread(img, buf, block_size, block_size * block) < block_size) {
        return -errno;
    }

    int write_size;

    if (block_size < *left) {
        write_size = block_size;
    } else {
        write_size = *left;
    }

    *left -= write_size;

    char name[EXT2_NAME_LEN];
    struct ext2_dir_entry_2* entry = (struct ext2_dir_entry_2*) (buf);
    int shift = 0;
    while (entry && entry->inode != 0 && write_size > 0) {

        memcpy(name, entry->name, entry->name_len);
        name[entry->name_len] = '\0';

        if (entry->file_type == EXT2_FT_DIR) {
            report_file(entry->inode, 'd', name);
        }
        if (entry->file_type == EXT2_FT_REG_FILE) {
            report_file(entry->inode, 'f', name);
        }

        shift += entry->rec_len;
        write_size -= entry->rec_len;

        if (block_size <= shift) {
            entry = 0;
        } else {
            entry = (struct ext2_dir_entry_2*) (buf + shift);
        }
    }
    return 0;
}

int in_dir_report(int img, int* buf, const int block_size, const int block, int* left, int type) {

    if (pread(img, buf, block_size, block_size * block) < block_size) {
        return -errno;
    }

    void* another_buf = malloc(block_size);

    int k = 0;
    while (k < (block_size / (int)sizeof(int)) && buf[k] != 0 && *left > 0) {
        if (type != EXT2_IND_BLOCK) {
            int result;
            if ((result = in_dir_report(img, (int*)another_buf, block_size, buf[k], left, k - 1)) < 0) {
                free(another_buf);
                return result;
            }
        } else {
            int result;
            if ((result = dir_report(img, another_buf, block_size, buf[k], left)) < 0) {
                free(another_buf);
                return result;
            }
        }
        ++k;
    }
    free(another_buf);
    return 0;
}

int dump_dir(int img, int inode_nr) {

    struct ext2_super_block super_block;
    int result = pread(img, &super_block, SUPERBLOCK_SIZE, SUPERBLOCK_OFFSET);
    if (result < 0) {
        return -errno;
    }

    int block_size = EXT2_BLOCK_SIZE(&super_block);

    struct ext2_group_desc group_block;
    int id_group = (inode_nr - 1) / super_block.s_inodes_per_group;

    result = pread(img, &group_block, sizeof(group_block), block_size * (super_block.s_first_data_block + 1) + sizeof(group_block) * id_group);
    if (result < 0) {
        return -errno;
    }

    int id_inode = (inode_nr - 1) % super_block.s_inodes_per_group;

    struct ext2_inode inode;
    result = pread(img, &inode, sizeof(inode), block_size * group_block.bg_inode_table + super_block.s_inode_size * id_inode);
    if (result < 0) {
        return -errno;
    }

    int copy_left = inode.i_size;
    void* buf = malloc(block_size);
    int k = 0;
    while (inode.i_block[k] != 0 && copy_left > 0 && k < EXT2_N_BLOCKS) {
        int result = -1;
        if (k == EXT2_DIND_BLOCK || k == EXT2_IND_BLOCK || k == EXT2_TIND_BLOCK) {
            result = in_dir_report(img, (int*)buf, block_size, inode.i_block[k], &copy_left, k);
        } else if (k < EXT2_NDIR_BLOCKS) {
            result = dir_report(img, buf, block_size, inode.i_block[k], &copy_left);
        }
        if (result < 0) {
            free(buf);
            return result;
        }
        ++k;
    }
    free(buf);
    return 0;
}