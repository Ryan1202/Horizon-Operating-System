#include <fs/fs.h>
#include <kernel/memory.h>
#include <kernel/console.h>
#include <drivers/disk.h>
#include <string.h>
#include <const.h>

void init_fs(struct disk *disk, unsigned int disk_num)
{
	struct MBR mbr;
	unsigned char *data;
	data = kmalloc(512);
	ide_read_sector(0, 0, data);
	memcpy(&mbr, data, sizeof(struct MBR));
	if (mbr.signature == 0xaa55)
	{
		int i;
		for (i = 0; i < 4; i++)
		{
			if(mbr.pt[i].sector_count)
			{
				disk->parts[i].disk = disk;
				disk->parts[i].pt = mbr.pt[i];
				disk->disk_num = disk_num;
				disk->disk_read(0, mbr.pt[i].start_lba, data);
				switch (mbr.pt[i].type)
				{
				case 0x01:		//FAT12
					break;
				case 0x04:		//FAT16 小于32MB
					break;
				case 0x05:		//Extended
					break;
				case 0x06:		//FAT16 大于32MB
					break;
				case 0x07:		//HPFS/NTFS
					break;
				case 0x0b:		//WINDOWS95 FAT32
					if (isFAT32(data))
					{
						init_fat32(&disk->parts[i], data);
					}
					disk->part_cnt++;
					break;
				case 0x0c:		//WINDOWS95 FAT32(使用LBA模式INT13扩展)
					break;
				case 0x0e:		//WINDOWS FAT16
					break;
				case 0x0f:		//WINDOWS95 Extended(大于8G)
					break;
				case 0x82:		//Linux swap
					break;
				case 0x83:		//Linux
					break;
				case 0x85:		//Linux extended
					break;
				case 0x86:		//NTFS volume set
					break;
				case 0x87:		//NTFS volume set
					break;
				
				default:
					break;
				}
			}
		}
	}
	kfree(data);
}

void fs_free(struct disk *disk)
{
	int i;
	for (i = 0; i < disk->part_cnt; i++)
	{
		kfree(disk->parts[i].fs);
	}
}

struct FILE *fs_open(char *path)
{
	int disk_num, part_num, i;
	char buf[512];
	struct disks *disks;
	struct FILE *fp = kmalloc(sizeof(struct fs_file));
	if (*path == '/')
	{
		path++;
		disk_num = atoi(path);
		if (disk_num < 0) return NULL;
		disks = find_member_in_disks(disk_num);
		if (disks == 0) return NULL;
		path = strstr(path, "/");
		if (*path == '/')
		{
			path++;
			part_num = atoi(path);
			if (part_num < 0) return NULL;
			path = strstr(path, "/");
			if (path == 0) return NULL;
			fp->part = &disks->disk.parts[part_num];
			fp->file = fp->part->fs_open(&disks->disk.parts[part_num], path);
			int size = (fp->file.file_size % 512 ? (fp->file.file_size / 512)+1 : fp->file.file_size / 512);
			int pos = fp->file.pos;
			return fp;
		}
		else
		{
			return NULL;
		}
	}
	return NULL;
}

void fs_read(struct FILE *file, unsigned int offset, unsigned int length, char *buf)
{
	char *tmp_buf;
	if (offset + length > file->file.file_size) return;
	tmp_buf = kmalloc(length);
	file->part->fs_read(file->part, &file->file, offset, length, tmp_buf);
	buf = tmp_buf;
}

void fs_write(struct FILE *file, unsigned int offset, unsigned int length, char *buf)
{
	unsigned int i;
	char *tmp_buf;
	tmp_buf = kmalloc((length%SECTOR_SIZE ? length/SECTOR_SIZE + 1 : length/SECTOR_SIZE)*SECTOR_SIZE);
	file->part->fs_read(file->part, &file->file, offset, length, tmp_buf);
	for (i = offset; i < length; i++)
	{
		tmp_buf[i] = buf[i - offset];
	}
	file->part->fs_write(file->part, &file->file, offset, length, tmp_buf);
}

void fs_close(struct FILE *file)
{
	file->part->fs_close(&file->file);
	kfree(file);
}

struct fs_file *find_member_in_file_list(struct fs_file *first_file, int i)
{
	int j;
	struct fs_file *member = first_file;
	for (j = 0; j < i; j++)
	{
		member = member->next;
	}
	return member;
}