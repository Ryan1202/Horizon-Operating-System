#ifndef _FS_H
#define _FS_H

#include <fs/fat32.h>
#include <kernel/time.h>
#include <drivers/disk.h>

#define part_read(part, lba, buf) part->disk->disk_read(part->disk->disk_num, lba, buf)
#define part_write(part, lba, buf) part->disk->disk_write(part->disk->disk_num, lba, buf)

struct MBR {
	unsigned char	bootcode[446];
	struct partition_table pt[4];
	unsigned short	signature;
}__attribute__((packed));

struct fs_folder {
	struct fs_file *first_file;	//第一个文件
	unsigned int file_num;		//文件数量
	char *name;					//文件夹名
	unsigned int pos;			//文件夹位置
	date create_date;			//创建日期
	time create_time;			//创建时间
	date write_date;			//修改日期
	time write_time;			//修改时间
};

struct fs_file {
	struct fs_file *prev;		//上一个文件
	char *name;					//文件名
	unsigned int pos;			//文件位置
	date create_date;			//创建日期
	time create_time;			//创建时间
	unsigned int create_timeT;	//创建时间（10ms位）
	unsigned int last_acctime;	//最后访问时间
	date write_date;			//修改日期
	time write_time;			//修改时间
	unsigned int file_size;		//文件大小
	struct fs_folder parent_fld;//所在的文件夹
	struct fs_file *next;		//下一个文件
};

struct FILE {
	struct fs_file file;
	struct partition *part;
};

void init_fs(struct disk *disk, unsigned int disk_num);
void fs_free(struct disk *disk);
struct FILE *fs_open(char *path);
void fs_read(struct FILE *file, unsigned int offset, unsigned int length, char *buf);
void fs_write(struct FILE *file, unsigned int offset, unsigned int length, char *buf);
void fs_close(struct FILE *file);
struct fs_file *find_member_in_file_list(struct fs_file *first_file, int i);

#endif