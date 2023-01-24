#ifndef _VFS_H
#define _VFS_H

#include <kernel/driver.h>
#include <kernel/list.h>
#include <kernel/time.h>
#include <stddef.h>
#include <string.h>

// 实

struct directory {
	struct index_node *inode;

	unsigned int dir_start;	 // 文件夹目录项所在的簇号
	unsigned int dir_offset; // 文件夹目录项在簇内的位置

	unsigned int start; // 文件夹数据所在的簇号
};

struct file {
	struct index_node *inode;

	unsigned int dir_start;	 // 文件目录项所在的簇号
	unsigned int dir_offset; // 文件目录项在簇内的位置

	unsigned int start; // 文件数据所在的簇号
	unsigned int offset;
	unsigned int cur_pos;
	unsigned int mode;
	size_t		 size;

	uint32_t *index_table;

	void *private_data;
};

// 虚

struct file_operations {
	struct index_node *(*open)(char *path);
	int (*close)(struct index_node *inode);
	int (*read)(struct index_node *inode, uint8_t *buffer, uint32_t length);
	int (*write)(struct index_node *inode, uint8_t *buffer, uint32_t length);
	int (*seek)(struct index_node *inode, unsigned int offset, unsigned int origin);
	int (*ioctl)(struct index_node *inode, uint32_t cmd, uint32_t arg);
};

/*
struct index_node_operations
{
	struct index_node *	(*create)(struct index_node *inode, struct dir_block *dir, int mode);
	int					(*delete)(struct index_node *inode, struct dir_block *dir);
	struct index_node	(*find)(struct index_node *parent_inode, struct dir_block *dir);
	struct index_node *	(*mkdir)(struct dir_block *dir, int mode, char *name);
	int					(*rmdir)(struct index_node *inode, struct dir_block *dir);
	int					(*rename)(struct index_node *inode, struct dir_block *dir, char *name);
};
*/
typedef enum { ATTR_FILE, ATTR_DIR, ATTR_DEV } inode_attr_t;

struct index_node {
	inode_attr_t attribute;

	struct index_node *parent;
	list_t			   list, childs;
	string_t		   name;
	union {
		struct file		 *fp;
		struct directory *dir;
	};
	struct _device_s	  *device;
	struct _partition_s	  *part;
	struct file_operations f_ops;

	date create_date;	   // 创建日期
	time create_time;	   // 创建时间
	date write_date;	   // 修改日期
	time write_time;	   // 修改时间
	date last_access_date; // 最后访问日期
	time last_access_time; // 最后访问时间
};

typedef struct _file_request {
	list_t list;
	void  *buffer;
	union {
		struct {
			uint32_t offset;
			uint32_t length;
		} rw;
		struct {
			uint32_t offset;
			uint32_t origin;
		} seek;
		struct {
			uint32_t cmd;
			uint32_t arg;
		} ioctl;
	};

} file_request_t;

extern struct index_node *root;

void init_vfs(void);

// 以下函数只对虚拟节点进行操作，不对实际文件造成影响
struct index_node *vfs_find(struct index_node *parent, char *name);
struct index_node *vfs_mkdir(char *path);
void			   vfs_rm(struct index_node *inode);
void			   vfs_rename(struct index_node *inode, char *name);
struct index_node *vfs_open(char *path);
struct index_node *vfs_opendir(char *path);
struct index_node *vfs_create(char *name, inode_attr_t attr, struct index_node *parent);
void			   vfs_close(struct index_node *inode);

#endif