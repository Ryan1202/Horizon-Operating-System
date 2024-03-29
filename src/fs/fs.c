#include <const.h>
#include <fs/fs.h>
#include <kernel/console.h>
#include <kernel/memory.h>
#include <math.h>

LIST_HEAD(part_list_head);
LIST_HEAD(fs_list_head);

struct file_operations fs_fops = {
	.open = fs_open, .close = fs_close, .read = NULL, .write = NULL, .seek = fs_seek, .ioctl = NULL};

struct file *fds[FD_MAX_NR];
int			 fd_num;

void init_fs(void) {
	int						i;
	struct index_node	   *inode, *next, *part_inode;
	struct partition_table *pt;
	for (i = 0; i < FD_MAX_NR; i++) {
		fds[i] = NULL;
	}
	char *buffer = kmalloc(SECTOR_SIZE);
	list_for_each_owner_safe (inode, next, &dev->childs, list) {
		if (inode->device->type == DEV_STORAGE) {
			fs_t *fs, *fs_next;
			inode->f_ops.seek(inode, 0, 0);
			inode->f_ops.read(inode, (uint8_t *)buffer, SECTOR_SIZE);
			for (i = 0; i < 4; i++) {
				pt = (struct partition_table *)(buffer + 0x1be + i * sizeof(struct partition_table));
				if (pt->sign != 0x80 && pt->sign != 0x00) continue;
				list_for_each_owner_safe (fs, fs_next, &fs_list_head, list) {
					if (fs->fs_ops->fs_check(pt) == FAILED) continue;
					char *path = kmalloc(inode->name.length + 3);
					path[0]	   = '/';
					strcpy(path + 1, inode->name.text);
					path[inode->name.length + 1] = 'p';
					path[inode->name.length + 2] = i + '1';
					part_inode					 = vfs_mkdir(path);
					part_inode->f_ops			 = fs_fops;
					part_inode->f_ops.read		 = fs->fs_ops->fs_read;
					part_inode->f_ops.write		 = fs->fs_ops->fs_write;
					part_inode->part			 = kmalloc(sizeof(partition_t));
					part_inode->part->fs		 = fs;
					part_inode->part->root		 = part_inode;
					part_inode->part->device	 = inode->device;
					part_inode->part->start_lba	 = pt->start_lba;
					list_add_tail(&part_inode->part->list, &part_list_head);
					char *superblock = kmalloc(SECTOR_SIZE);
					inode->f_ops.seek(inode, pt->start_lba, 0);
					inode->f_ops.read(inode, (uint8_t *)superblock, SECTOR_SIZE);
					fs->fs_ops->fs_read_superblock(part_inode->part, superblock);
				}
			}
		}
	}
}

int fs_register(char *name, fs_operations_t *fs_ops) {
	fs_t *fs = kmalloc(sizeof(fs_t));
	string_init(&fs->name);
	string_new(&fs->name, name, strlen(name));
	fs->fs_ops = fs_ops;
	list_add_tail(&fs->list, &fs_list_head);
	return 0;
}

int alloc_fd(void) {
	if (fd_num >= FD_MAX_NR - 1) {
		int i;
		for (i = 0; i < FD_MAX_NR; i++) {
			if (fds[i] == NULL) {
				fds[i] = (struct file *)-1;
				return i;
			}
		}
		return -1;
	}
	fd_num++;
	return fd_num - 1;
}

struct index_node *fs_opendir(char *path) {
	struct index_node *inode, *parent, *next;
	partition_t		  *part, *pnext;
	uint8_t			   f;
	int				   l	= strlen(path);
	char			  *name = kmalloc(l), *p;
	strcpy(name, path);
	p = name;
	if (p[0] == '/' && p[1] == 0) return root;
	int cnt = l;
	while (p[cnt] != '/')
		cnt--;
	p[cnt] = 0;
	if ((inode = vfs_opendir(p)) != NULL) return inode;

	int length = 0;
	if (*p != '/') return NULL; // 绝对路径以"/"开始
	else if (p[1] == '\0') { return root; }
	p++;
	while (p[length] != '/' && p[length] != '\0')
		length++;
	p[length] = 0;
	f		  = 1;
	list_for_each_owner_safe (part, pnext, &part_list_head, list) {
		if (strncmp(p, part->root->name.text, length) == 0) {
			f	  = 0;
			inode = part->root;
			break;
		}
	}
	if (f) return NULL;
	while (*p != '\0') {
		f = 1;
		p += length + 1;
		parent = inode;
		length = 0;
		while (p[length] != '/' && p[length] != '\0')
			length++;
		p[length] = 0;
		// inode = vfs_find(inode, p);
		list_for_each_owner_safe (inode, next, &parent->childs, list) {
			if (strncmp(p, inode->name.text, length - 1) == 0 && inode->attribute == ATTR_DIR) {
				f = 0;
				break;
			}
		}
		if (f) inode = parent->part->fs->fs_ops->fs_opendir(parent->part, parent, p);
		if (inode == NULL) return NULL;
		if (p + length >= name + cnt && inode->attribute == ATTR_DIR) {
			string_init(&inode->name);
			string_new(&inode->name, p, strlen(p));
			return inode;
		}
	}
	return NULL;
}

struct index_node *fs_open(char *path) {
	struct index_node *inode, *parent, *next;
	inode = vfs_open(path);
	if (inode == NULL) {
		int cnt = strlen(path) - 1;
		while (path[cnt] != '/')
			cnt--;
		parent	  = fs_opendir(path);
		path[cnt] = 0;
		path += cnt + 1;
		list_for_each_owner_safe (inode, next, &parent->childs, list) {
			if (strncmp(path, inode->name.text, strlen(path) - 1) == 0 && inode->attribute != ATTR_DIR) {
				return inode;
			}
		}
		inode = parent->part->fs->fs_ops->fs_open(parent->part, parent, path);
	}
	return inode;
}

int fs_close(struct index_node *inode) {
	inode->part->fs->fs_ops->fs_close(inode);
	kfree(inode->fp);
	string_del(&inode->name);
	return 0;
}

int fs_seek(struct index_node *inode, unsigned int offset, unsigned int origin) {
	inode->fp->offset = origin + offset;
	return 0;
}

int fs_create(struct index_node *parent, char *name) {
	struct index_node *inode = vfs_create(name, ATTR_FILE, parent);
	inode->fp				 = parent->fp;
	inode->part				 = parent->part;
	inode->f_ops			 = parent->f_ops;
	inode->part->fs->fs_ops->fs_create(inode->part, parent, name, strlen(name));
	return 0;
}

int fs_delete(struct index_node *inode) {
	inode->part->fs->fs_ops->fs_delete(inode->part, inode);
	fs_close(inode);
	return 0;
}