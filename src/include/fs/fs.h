#ifndef _FS_H
#define _FS_H

#include <fs/vfs.h>
#include <kernel/list.h>
#include <string.h>

#define FD_MAX_NR 128

struct partition_table {
    uint8_t  sign;
    uint8_t  start_chs[3];
    uint8_t  fs_type;
    uint8_t  end_chs[3];
    uint32_t start_lba;
    uint32_t size;
};

typedef struct {
    status_t (*fs_check)(struct partition_table *pt);
    status_t (*fs_read_superblock)(struct _partition_s *partition, char *data);
    struct index_node *(*fs_open)(struct _partition_s *part, struct index_node *parent, char *filename);
    struct index_node *(*fs_opendir)(struct _partition_s *part, struct index_node *parent, char *name);
    void (*fs_close)(struct index_node *inode);
    int (*fs_read)(struct index_node *inode, uint8_t *buffer, uint32_t length);
    int (*fs_write)(struct index_node *inode, uint8_t *buffer, uint32_t length);
    struct index_node *(*fs_create)(struct _partition_s *part, struct index_node *parent, char *name, int len);
    void (*fs_delete)(struct _partition_s *part, struct index_node *inode);
} fs_operations_t;

typedef struct {
    list_t           list;
    string_t         name;
    fs_operations_t *fs_ops;
} fs_t;

typedef struct _partition_s {
    list_t             list;
    string_t           name;
    device_t          *device;
    fs_t              *fs;
    struct index_node *root;
    int                start_lba;
    void              *private_data;
} partition_t;

extern struct file *fds[FD_MAX_NR];
extern int          fd_num;

void               init_fs(void);
int                alloc_fd(void);
struct index_node *fs_open(char *path);
int                fs_close(struct index_node *inode);
int                fs_read(struct index_node *inode, uint8_t *buffer, uint32_t length);
int                fs_write(struct index_node *inode, uint8_t *buffer, uint32_t length);
int                fs_seek(struct index_node *inode, unsigned int offset, unsigned int origin);
int                fs_create(struct index_node *parent, char *name);
int                fs_delete(struct index_node *inode);

#endif