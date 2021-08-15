#ifndef _FS_H
#define _FS_H

#include <kernel/list.h>
#include <fs/vfs.h>
#include <string.h>

struct partition_table {
	uint8_t sign;
	uint8_t start_chs[3];
	uint8_t fs_type;
	uint8_t end_chs[3];
	uint32_t start_lba;
	uint32_t size;
};

typedef struct {
	status_t (*fs_read_superblock)(struct _partition_s *partition, char *data);
	struct index_node *(*fs_open)(struct _partition_s *part, struct index_node *parent, char *filename);
	struct index_node *(*fs_opendir)(struct _partition_s *part, struct index_node *parent, char *name);
	void (*fs_close)(struct index_node *inode);
	void (*fs_read)(struct index_node *inode);
	void (*fs_write)(struct index_node *inode);
	struct index_node *(*fs_create)(struct _partition_s *part, struct index_node *parent, char *name, int len);
	void (*fs_delete)(struct _partition_s *part, struct index_node *inode);
} fs_operations_t;

typedef struct {
	list_t list;
	string_t name;
	int fs_type;
	fs_operations_t *fs_ops;
} fs_t;

typedef struct _partition_s {
	list_t list;
	string_t name;
	device_t *device;
	fs_t *fs;
	struct index_node *root;
	int start_lba;
	void *private_data;
} partition_t;


void init_fs(void);
struct index_node *fs_open(char *path);
int fs_close(struct index_node *inode);
int fs_read(struct index_node *inode, char *buffer, uint32_t length);
int fs_write(struct index_node *inode, char *buffer, uint32_t length);
int fs_create(struct index_node *parent, char *name);
int fs_delete(struct index_node *inode);

#endif