#include <fs/vfs.h>
#include <drivers/cmos.h>
#include <kernel/memory.h>

struct index_node *root;

struct index_node *vfs_mkdir(char *path)
{
	int length = strlen(path) - 1;
	char *name = kmalloc(length);
	strcpy(name, path);
	if (name[length] == '/') name[length] = 0, length--;
	while(name[length] != '/') length--;
	length++;
	name += length;
	path[length] = 0;
	struct index_node *parent = vfs_opendir(path), *inode;
	inode = kmalloc(sizeof(struct index_node));
	memset(inode, 0, sizeof(struct index_node));
	inode->attribute = ATTR_DIR;
	inode->parent = parent;
	list_add_tail(&inode->list, &parent->childs);
	inode->create_date.year = inode->write_date.year = inode->last_access_date.year = BCD2BIN(CMOS_READ(CMOS_YEAR));
	inode->create_date.month = inode->write_date.month = inode->last_access_date.month = BCD2BIN(CMOS_READ(CMOS_MONTH));
	inode->create_date.day = inode->write_date.day = inode->last_access_date.day = BCD2BIN(CMOS_READ(CMOS_DAY_OF_MONTH));
	inode->create_time.hour = inode->write_time.hour = inode->last_access_time.hour = BCD2BIN(CMOS_READ(CMOS_HOURS));
	inode->create_time.minute = inode->write_time.minute = inode->last_access_time.minute = BCD2BIN(CMOS_READ(CMOS_MINUTES));
	inode->create_time.second = inode->write_time.second = inode->last_access_time.second = BCD2BIN(CMOS_READ(CMOS_SECONDS));
	
	string_init(&inode->name);
	string_new(&inode->name, name, strlen(name));
	list_init(&inode->childs);
	return inode;
}

void vfs_rm(struct index_node *inode)
{
	struct index_node *child, *next;
	string_del(&inode->name);
	if (!list_empty(&inode->childs))
	{
		list_for_each_owner_safe(child, next, &inode->childs, childs)
		{
			vfs_rm(child);
		}
	}
	list_del(&inode->list);
	kfree(inode);
}

void vfs_rename(struct index_node *inode, char *name)
{
	string_del(&inode->name);
	string_new(&inode->name, name, strlen(name));
}

/*
 * 函数名：	vfs_find
 * 描述：	找到并返回目录的inode
 * 参数：
 * 		@parent	目录的父目录
 * 		@name	目录的名称
 * 返回：	 inode		 找到目录
 * 			NULL		找不到目录
*/
struct index_node *vfs_find(struct index_node *parent, char *name)
{
	struct index_node *inode, *next;
	list_for_each_owner_safe(inode, next, &parent->childs, list)
	{
		if (strcmp(name, inode->name.text) == 0)
		{
			if (inode->attribute == ATTR_DIR)
			{
				return inode;
			}
		}
	}
	return NULL;
}

struct index_node *vfs_open(char *path)
{
	struct index_node *inode = root;
	
	char *name = kmalloc(strlen(path)), *p;
	strcpy(name, path);
	p = name;
	int length, flag;
	if(*path != '/') return NULL;	//绝对路径以"/"开始
	else if(path[1] == '\0')
	{
		return root;
	}
	p++;
	while(*path != '\0')
	{
		length = 0;
		while(p[length] != '/' && p[length] != '\0') length++;
		p[length] = 0;
		inode = vfs_find(inode, p);
		if (inode == NULL) return NULL;
		if (p[length] == 0 && inode->attribute != ATTR_DIR)
		{
			return inode;
		}
		p += length + 1;
	}
	return NULL;
}

void vfs_close(struct index_node *inode)
{
	struct index_node *parent = inode->parent;
	string_del(&inode->name);
	kfree(inode);
	inode = parent;
	list_del(&inode->list);
	while (list_empty(&inode->childs) && inode->parent != root)
	{
		string_del(&inode->name);
		kfree(inode->dir);
		parent = inode->parent;
		kfree(inode);
		inode = parent;
	}
}

struct index_node *vfs_opendir(char *path)
{
	struct index_node *inode = root;
	int l = strlen(path);
	char *name = kmalloc(l), *p;
	strcpy(name, path);
	p = name;
	int length, flag;
	if(*path != '/') return NULL;	//绝对路径以"/"开始
	else if(path[1] == '\0')
	{
		return root;
	}
	p++;
	while(*path != '\0')
	{
		length = 0;
		while(p[length] != '/' && p[length] != '\0') length++;
		p[length] = 0;
		inode = vfs_find(inode, p);
		if (inode == NULL) return NULL;
		if (p+length >= name+l && inode->attribute == ATTR_DIR)
		{
			return inode;
		}
		p += length + 1;
	}
	return NULL;
}

struct index_node *vfs_create(char *name, inode_attr_t attr, struct index_node *parent)
{
	struct index_node *inode = kmalloc(sizeof(struct index_node));
	list_init(&inode->childs);
	string_init(&inode->name);
	string_new(&inode->name, name, strlen(name));
	inode->attribute = attr;
	inode->parent = parent;
	if (parent != NULL) list_add_tail(&inode->list, &parent->childs);
	return inode;
}

void init_vfs(void)
{
	//根目录作为名称为""的文件夹初始化
	root = kmalloc(sizeof(struct index_node));
	memset(root, 0, sizeof(struct index_node));
	root->attribute = ATTR_DIR;
	
	string_init(&root->name);
	string_new(&root->name, "", strlen(""));
	list_init(&root->childs);
	
	dev = vfs_mkdir("/dev/");
}