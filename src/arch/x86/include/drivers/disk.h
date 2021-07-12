#ifndef DISK_H
#define DISK_H

struct partition_table {
	unsigned char	status;
	unsigned char	start_chs[3];
	unsigned char	type;
	unsigned char	end_chs[3];
	unsigned int	start_lba;
	unsigned int	sector_count;
}__attribute__((packed));

struct partition {
	struct disk *disk;
	
	unsigned int data_start;
	unsigned char vol_name[11];
	struct partition_table pt;
	void *fs;
	
	struct fs_file (*fs_open)(struct partition *part, char *path);
	void (*fs_read)(struct partition *part, struct fs_file *file, unsigned int offset, unsigned int length, char *buf);
	void (*fs_write)(struct partition *part, struct fs_file *file, unsigned int offset, unsigned int length, char *buf);
	void (*fs_close)(struct fs_file *file);
};

struct disk {
	struct disks *disks;
	
	unsigned int disk_num;
	unsigned int part_cnt;
	struct partition parts[4];
	
	void (*disk_read)(unsigned int disk_num, unsigned int lba, unsigned char *buf);
	void (*disk_write)(unsigned int disk_num, unsigned int lba, unsigned char *buf);
	void (*disk_delete)(unsigned int disk_num);
};

struct disks {
	char *type;
	struct disks *prev;
	struct disk disk;
	struct disks *next;
};

struct disk_manager {
	int disk_num;
	struct disks *disks;
};

struct disk *add_disk(char *type);
void delete_disk(struct disk *disk);
struct disks *find_member_in_disks(int i);

#endif