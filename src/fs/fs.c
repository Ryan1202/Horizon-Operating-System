#include <fs/fs.h>
#include <kernel/memory.h>
#include <kernel/console.h>
#include <const.h>
#include <math.h>

LIST_HEAD(part_list_head);
LIST_HEAD(fs_list_head);

struct file_operations fs_fops = {
	.open = fs_open,
	.close = fs_close,
	.read = fs_read,
	.write = NULL,
	.seek = NULL,
	.ioctl = NULL
};

void init_fs(void)
{
	int i;
	uint8_t f;
	struct index_node *inode, *next, *part_inode;
	struct partition_table *pt;
	file_request_t req;
	char *buffer = kmalloc(SECTOR_SIZE);
	list_for_each_owner_safe(inode, next, &dev->childs, list)
	{
		if (inode->device->type == DEV_STORAGE)
		{
			fs_t *fs, *fs_next;
			inode->f_ops.read(inode, buffer, 0, SECTOR_SIZE);
			for (i = 0; i < 4; i++)
			{
				pt = buffer + 0x1be + i*sizeof(struct partition_table);
				list_for_each_owner_safe(fs, fs_next, &fs_list_head, list)
				{
					if (fs->fs_type == pt->fs_type)
					{
						char *path = kmalloc(inode->name.length + 3);
						path[0] = '/';
						strcpy(path+1, inode->name.text);
						path[inode->name.length + 1] = 'p';
						path[inode->name.length + 2] = i + '0';
						part_inode = vfs_mkdir(path);
						part_inode->f_ops = fs_fops;
						part_inode->part = kmalloc(sizeof(partition_t));
						part_inode->part->fs = fs;
						part_inode->part->root = part_inode;
						part_inode->part->device = inode->device;
						part_inode->part->start_lba = pt->start_lba;
						list_add_tail(&part_inode->part->list, &part_list_head);
						char *superblock = kmalloc(SECTOR_SIZE);
						inode->f_ops.read(inode, superblock, pt->start_lba, SECTOR_SIZE);
						fs->fs_ops->fs_read_superblock(part_inode->part, superblock);
					}
					else
					{
						kfree(pt);
					}
				}
			}
		}
	}
}

int fs_register(char *name, int fs_type, fs_operations_t *fs_ops)
{
	fs_t *fs = kmalloc(sizeof(fs_t));
	list_add_tail(&fs->list, &fs_list_head);
	string_init(&fs->name);
	string_new(&fs->name, name, strlen(name));
	fs->fs_type = fs_type;
	fs->fs_ops = fs_ops;
}

struct index_node *fs_opendir(char *path)
{
	struct index_node *inode, *parent, *next;
	partition_t *part, *pnext;
	uint8_t f;
	int l = strlen(path);
	char *name = kmalloc(l), *p;
	strcpy(name, path);
	p = name;
	if (p[0] == '/' && p[1] == 0) return root;
	int cnt = l;
	while (p[cnt] != '/') cnt--;
	p[cnt] = 0;
	if ((inode = vfs_opendir(p)) != NULL) return inode;
	
	int length = 0, flag;
	if(*p != '/') return NULL;	//绝对路径以"/"开始
	else if(p[1] == '\0')
	{
		return root;
	}
	p++;
	while(p[length] != '/' && p[length] != '\0') length++;
	p[length] = 0;
	f = 1;
	list_for_each_owner_safe(part, pnext, &part_list_head, list)
	{
		if (strncmp(p, part->root->name.text, length) == 0)
		{
			f = 0;
			inode = part->root;
			break;
		}
	}
	if (f) return NULL;
	while(*p != '\0')
	{
		f = 1;
		p += length + 1;
		parent = inode;
		length = 0;
		while(p[length] != '/' && p[length] != '\0') length++;
		p[length] = 0;
		// inode = vfs_find(inode, p);
		list_for_each_owner_safe(inode, next, &parent->childs, list)
		{
			if (strncmp(p, inode->name.text, length - 1) == 0 && inode->attribute == ATTR_DIR)
			{
				f = 0;
				break;
			}
		}
		if (f) inode = parent->part->fs->fs_ops->fs_opendir(parent->part, parent, p);
		if (inode == NULL) return NULL;
		if (p+length >= name + cnt && inode->attribute == ATTR_DIR)
		{
			string_init(&inode->name);
			string_new(&inode->name, p, strlen(p));
			return inode;
		}
	}
	return NULL;
}

struct index_node *fs_open(char *path)
{
	struct index_node *inode, *parent, *next;
	inode = vfs_open(path);
	if (inode != NULL) return inode;
	int cnt = strlen(path) - 1;
	while (path[cnt] != '/') cnt--;
	parent = fs_opendir(path);
	path[cnt] = 0;
	path += cnt + 1;
	list_for_each_owner_safe(inode, next, &parent->childs, list)
	{
		if (strncmp(path, inode->name.text, strlen(path) - 1) == 0 && inode->attribute != ATTR_DIR)
		{
			return inode;
		}
	}
	inode = parent->part->fs->fs_ops->fs_open(parent->part, parent, path);
	if (inode != NULL)
	{
		inode->fp->rw_buf = kmalloc(SECTOR_SIZE);
		inode->fp->rw_buf_changed = 0;
		inode->fp->offset = 0;
	}
	return inode;
}

int fs_close(struct index_node *inode)
{
	if (inode->fp->rw_buf_changed)
	{
		inode->part->fs->fs_ops->fs_write(inode);
	}
	inode->part->fs->fs_ops->fs_close(inode);
	kfree(inode->fp->rw_buf);
	kfree(inode->fp);
	string_del(&inode->name);
	vfs_close(inode);
}

int fs_read(struct index_node *inode, char *buffer, uint32_t length)
{
	int i, n;
	uint32_t l = length;
	uint32_t offset = 0, pos = inode->fp->offset/SECTOR_SIZE;
	int size = SECTOR_SIZE - inode->fp->offset%SECTOR_SIZE;
	if (inode->fp->offset%SECTOR_SIZE && inode->fp->offset + size < inode->fp->size)
	{
		memcpy(buffer, inode->fp->rw_buf + inode->fp->offset%SECTOR_SIZE, size);
		if(inode->fp->rw_buf_changed) inode->part->fs->fs_ops->fs_write(inode);
		offset += size;
		l -= size;
		inode->fp->offset += size;
	}
	n = DIV_ROUND_UP(l, SECTOR_SIZE);
	for (i = 0; i < n; i++)
	{
		inode->part->fs->fs_ops->fs_read(inode);
		if (i == n-1)
		{
			memcpy(buffer + offset, inode->fp->rw_buf, l);
			inode->fp->offset += l;
		}
		else
		{
			memcpy(buffer + offset, inode->fp->rw_buf, SECTOR_SIZE);
			l -= SECTOR_SIZE;
			offset += SECTOR_SIZE;
			inode->fp->offset += SECTOR_SIZE;
		}
		if (inode->fp->offset > inode->fp->size)
		{
			inode->fp->offset = 0;
			memset(buffer + offset, 0xff, length - offset);
			break;
		}
	}
}

int fs_write(struct index_node *inode, char *buffer, uint32_t length)
{
	int i, n;
	uint32_t l = length;
	uint32_t offset = 0, pos = inode->fp->offset/SECTOR_SIZE;
	int size = SECTOR_SIZE - inode->fp->offset%SECTOR_SIZE;
	if (inode->fp->offset%SECTOR_SIZE && inode->fp->offset + size < inode->fp->size)
	{
		memcpy(inode->fp->rw_buf + inode->fp->offset%SECTOR_SIZE, buffer, size);
		inode->part->fs->fs_ops->fs_write(inode);
		offset += size;
		l -= size;
		inode->fp->offset += size;
	}
	n = DIV_ROUND_UP(l, SECTOR_SIZE);
	for (i = 0; i < n; i++)
	{
		inode->part->fs->fs_ops->fs_read(inode);
		if (i == n-1)
		{
			memcpy(inode->fp->rw_buf + inode->fp->offset%SECTOR_SIZE, buffer + offset, l);
			inode->fp->offset += l;
			if (inode->fp->offset%SECTOR_SIZE == 0) inode->part->fs->fs_ops->fs_write(inode);
			else inode->fp->rw_buf_changed = 1;
		}
		else
		{
			memcpy(inode->fp->rw_buf + inode->fp->offset%SECTOR_SIZE, buffer+offset, SECTOR_SIZE);
			l -= SECTOR_SIZE;
			offset += SECTOR_SIZE;
			inode->fp->offset += SECTOR_SIZE;
			inode->part->fs->fs_ops->fs_write(inode);
		}
	}
}

int fs_create(struct index_node *parent, char *name)
{
	struct index_node *inode = vfs_create(name, ATTR_FILE, parent);
	inode->fp = parent->fp;
	inode->part = parent->part;
	inode->f_ops = parent->f_ops;
	inode->part->fs->fs_ops->fs_create(inode->part, parent, name, strlen(name));
}

int fs_delete(struct index_node *inode)
{
	inode->part->fs->fs_ops->fs_delete(inode->part, inode);
	fs_close(inode);
}