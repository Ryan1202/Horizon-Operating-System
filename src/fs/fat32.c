#include <fs/fat32.h>
#include <fs/fs.h>
#include <fs/vfs.h>
#include <kernel/memory.h>
#include <kernel/initcall.h>
#include <drivers/cmos.h>
#include <kernel/console.h>
#include <const.h>
#include <ctype.h>
#include <math.h>

// struct FAT_clus_list *clus_list;

fs_operations_t fat32_fs_ops = {
	.fs_check = fat32_check,
	.fs_read_superblock = fat32_readsuperblock,
	.fs_open = FAT32_open,
	.fs_opendir = FAT32_find_dir,
	.fs_close = FAT32_close,
	.fs_read = FAT32_read,
	.fs_write = FAT32_write,
	.fs_create = FAT32_create_file,
	.fs_delete = FAT32_delete_file
};

status_t fat32_check(struct partition_table *pt)
{
	if(pt->fs_type == 0x0b || pt->fs_type == 0x0c)
	{
		return SUCCUESS;
	}
	return FAILED;
}

status_t fat32_readsuperblock(partition_t *partition, char *data)
{
	struct pt_fat32 *fat32 = kmalloc(sizeof(struct pt_fat32));
	memcpy(fat32, data, SECTOR_SIZE);
	partition->device->inode->f_ops.read(partition->device->inode, &fat32->FSInfo, partition->start_lba + 1, SECTOR_SIZE);
	
	if(fat32->FSInfo.FSI_LeadSig == 0x41615252)
	{
		fat32->fat_start = partition->start_lba + fat32->BPB_RevdSecCnt;
		partition->private_data = fat32;
		fat32->data_start = fat32->fat_start + fat32->BPB_NumFATs*fat32->BPB_FATSz32;
		partition->root->dir->start = 2;		//根目录在2号簇
		struct FAT32_dir sdir;
		partition->device->inode->f_ops.read(partition->device->inode, &sdir, fat32->data_start, sizeof(struct FAT32_dir));
		if (sdir.DIR_Attr == FAT32_ATTR_VOLUME_ID)
		{
			string_del(&partition->name);
			int cnt = 0;
			while (sdir.DIR_Name[cnt] != ' ' && cnt < 11) cnt++;
			string_new(&partition->name, sdir.DIR_Name, cnt);
		}
		return SUCCUESS;
	}
	kfree(fat32);
	return FAILED;
}

void FAT32_read(struct index_node *inode)
{
	struct pt_fat32 *fat32 = fs_FAT32(inode->part->private_data);
	int cnt = inode->fp->offset/(SECTOR_SIZE*fat32->BPB_SecPerClus), i;
	int pos;
	struct file *file = inode->fp;
	if (file->rw_buf_changed)
	{
		FAT32_write(inode);
	}
	file->rw_buf_changed = 0;
	pos = file->start + file->offset/fat32->BPB_SecPerClus*SECTOR_SIZE;
	for (i = 0; i < cnt; i++) pos = find_member_in_fat(inode->part, pos);
	inode->device->inode->f_ops.read(inode->device->inode, file->rw_buf, fat32->data_start + (pos - 2) * fat32->BPB_SecPerClus + (inode->fp->offset/SECTOR_SIZE)%fat32->BPB_SecPerClus, SECTOR_SIZE);
}

void FAT32_write(struct index_node *inode)
{
	int i;
	int pos, tmp;
	char buf[SECTOR_SIZE];
	struct file *file = inode->fp;
	struct pt_fat32 *fat32 = inode->part->private_data;
	struct FAT32_dir *sdir;
	int cnt = inode->fp->offset/(SECTOR_SIZE*fat32->BPB_SecPerClus);
	
	pos = file->start + file->offset/(fat32->BPB_SecPerClus*SECTOR_SIZE);
	for (i = 0; i < cnt; i++) pos = find_member_in_fat(inode->part, pos);
	
	int off_sec = inode->fp->offset/SECTOR_SIZE;
	int size_sec = inode->fp->size/SECTOR_SIZE;
	if (off_sec > size_sec)	//不在一个扇区
	{
		if (off_sec == size_sec + 2) pos = fat32_alloc_clus(inode->part, pos);
	}
	inode->device->inode->f_ops.write(inode, file->rw_buf, fat32->data_start + (pos - 2) * fat32->BPB_SecPerClus + off_sec%fat32->BPB_SecPerClus, SECTOR_SIZE);
	inode->device->inode->f_ops.read(inode, buf, fat32->data_start + (inode->fp->dir_start - 2)*fat32->BPB_SecPerClus + inode->fp->dir_offset/SECTOR_SIZE, SECTOR_SIZE);
	sdir = buf + inode->fp->dir_offset%SECTOR_SIZE;
	if (inode->fp->offset > inode->fp->size)	//超出文件大小
	{
		sdir->DIR_FileSize = inode->fp->offset;
		inode->fp->size = inode->fp->offset;
	}
	inode->last_access_date.year = inode->write_date.year = BCD2BIN(CMOS_READ(CMOS_YEAR));
	inode->last_access_date.month = inode->write_date.month = BCD2BIN(CMOS_READ(CMOS_MONTH));
	inode->last_access_date.day = inode->write_date.day = BCD2BIN(CMOS_READ(CMOS_DAY_OF_MONTH));
	inode->write_time.hour = BCD2BIN(CMOS_READ(CMOS_HOURS));
	inode->write_time.minute = BCD2BIN(CMOS_READ(CMOS_MINUTES));
	inode->write_time.second = BCD2BIN(CMOS_READ(CMOS_SECONDS));
	sdir->DIR_LastAccDate = sdir->DIR_WrtDate = (inode->write_date.year+20)<<9 | inode->write_date.month<<5 | inode->write_date.day;
	sdir->DIR_WrtTime = inode->write_time.hour<<11 | inode->write_time.minute<<5 | inode->write_time.second;
	inode->device->inode->f_ops.write(inode, buf, fat32->data_start + (inode->fp->dir_start - 2)*fat32->BPB_SecPerClus + inode->fp->dir_offset/SECTOR_SIZE, SECTOR_SIZE);
}

struct index_node *FAT32_create_file(partition_t *part, struct index_node *parent, char *name, int len)
{
	char buf[512], filename_short[11];
	unsigned int pos, i = 0, j, len1, len2, name_count = 0, count = 0, name_len, tmp;
	unsigned char checksum;
	struct directory *dir = parent->dir;
	struct pt_fat32 *fat32 = part->private_data;
	struct FAT32_long_dir *ldir;
	struct FAT32_dir *sdir;
	struct index_node *inode = vfs_create(name, ATTR_FILE, parent);
	struct file *file = kmalloc(sizeof(struct file));
	uint32_t offset;
	int flag;
	
	file->inode = inode;
	file->dir_start = parent->dir->start;
	inode->fp = file;
	inode->device = parent->device;
	inode->part = part;
	inode->f_ops = parent->f_ops;
	pos = dir->start;
	do
	{
		if (i % SECTOR_SIZE == 0)
		{
			offset = fat32->data_start + (pos - 2) * fat32->BPB_SecPerClus + i/SECTOR_SIZE;
			part->device->inode->f_ops.read(part->device->inode, buf, offset, SECTOR_SIZE);
		}
		if (i/SECTOR_SIZE/fat32->BPB_SecPerClus) tmp = find_member_in_fat(part, pos);
		if (tmp >= 0x0fffffff) pos = fat32_alloc_clus(part, pos);
		else pos = tmp;
		if (buf[i%SECTOR_SIZE + 11] != FAT32_ATTR_LONG_NAME)
		{
			flag = 1;
			for (j = 0; j < 8 && buf[i%SECTOR_SIZE + j] != '~'; j++)
			{
				if (buf[i%SECTOR_SIZE + j] != name[j]) flag = 0;
			}
			if (flag)
			{
				name_count++;
			}
		}
		i+=0x20;
	} while (buf[i]);
	pos = ((pos - 2) * fat32->BPB_SecPerClus)*SECTOR_SIZE + i;
	len1 = len;
	int name_length = len1;
	while (name[name_length] != '.') name_length--;
	for (i = 0; i < len1 && name[i] != '.'; i++) if(!isupper(name[i])) flag |= 0x1;
	for (; i < len1; i++) if(!isupper(name[i])) flag |= 0x4;
	for (i = 0; i < len1 && name[i] != '.'; i++) if(!islower(name[i])) flag |= 0x2;
	for (; i < len1; i++) if(!islower(name[i])) flag |= 0x8;
	if (flag&0x05 && flag&0x0a && len1 <= 11 && name_length <= 8) goto short_dir;
	else if (len1>11 || (name + len1 - strstr(name, ".")) > 3)
	{
		len2 = DIV_ROUND_UP(len1, 13);
		int tmp = name_count;
		while(tmp)
		{
			count++;
			tmp/=10;
		}
		name_len = min(6, 8-count);
		for (i = 0; i < 11; i++)
		{
			if (i < name_len) filename_short[i] = toupper(name[i] ? name[i] : ' ');
			else if (i == name_len) filename_short[i] = '~';
			else if (i > name_len && i < 8) filename_short[i] = ((name_count/pow(10, 7-i))%10 + '0');
			else if (i < 8) filename_short[i] = ' ';
			else filename_short[i] = toupper(name[name_length - 7 + i]);
		}
		FAT32_checksum(filename_short, checksum);
		for (i = 0; i < len2; i++)
		{
			int k;
			if ((pos + i*0x20)%SECTOR_SIZE == 0)
			{
				part->device->inode->f_ops.write(part->device->inode, buf, offset, SECTOR_SIZE);
				offset = fat32->data_start + (pos + i*0x20)/SECTOR_SIZE;
				part->device->inode->f_ops.read(part->device->inode, buf, offset, SECTOR_SIZE);
			}
			ldir = buf + (pos + i*0x20)%SECTOR_SIZE;
			ldir->LDIR_Ord = len2 - i;
			if (i == 0) ldir->LDIR_Ord |= 0x40;
			for(j = 0; (j%13) <  5; j++) ldir->LDIR_Name1[j] = (j < len1-(len2 - i - 1)*13 ? name[j] : 0);
			ldir->LDIR_Attr = FAT32_ATTR_LONG_NAME;
			ldir->LDIR_Type = 0;
			ldir->LDIR_Chksum = checksum;
			for(;(j%13) < 11; j++) ldir->LDIR_Name2[j-5] = (j < len1-(len2 - i - 1)*13 ? name[j] : 0);
			ldir->LDIR_FstClusLO = 0;
			for(k = (j%13); k < 13; k++) ldir->LDIR_Name3[k-11] = (k < len1-(len2 - i - 1)*13 ? name[k] : 0);
		}
		pos += i*0x20;
		if (pos%SECTOR_SIZE == 0)
		{
			part->device->inode->f_ops.write(part->device->inode, buf, offset, SECTOR_SIZE);
			offset = fat32->data_start + pos/SECTOR_SIZE;
			part->device->inode->f_ops.read(part->device->inode, buf, offset, SECTOR_SIZE);
		}
		for (i = 0; i < 11; i++)
		{
			buf[pos%SECTOR_SIZE + i] = filename_short[i];
		}
		sdir = buf + pos%SECTOR_SIZE;
		sdir->DIR_Attr = FAT32_ATTR_ARCHIVE;
		
		int year = BCD2BIN(CMOS_READ(CMOS_YEAR)) + 20;
		int month = BCD2BIN(CMOS_READ(CMOS_MONTH));
		int day = BCD2BIN(CMOS_READ(CMOS_DAY_OF_MONTH));
		sdir->DIR_LastAccDate = sdir->DIR_CrtDate = sdir->DIR_WrtDate = year<<9 | month<<5 | day;
		int hour = BCD2BIN(CMOS_READ(CMOS_HOURS));
		int minute = BCD2BIN(CMOS_READ(CMOS_MINUTES));
		int second = BCD2BIN(CMOS_READ(CMOS_SECONDS));
		sdir->DIR_CrtTime = sdir->DIR_WrtTime = hour<<11 | minute<<5 | second>>1;
		sdir->DIR_CrtTimeTenth = second*10;
		
		int file_clus = fat32_alloc_clus(part, 0);
		sdir->DIR_FstClusHI = file_clus>>16;
		sdir->DIR_FstClusLO = file_clus&0xffff;
		sdir->DIR_FileSize = 0;
	}
	else
	{
short_dir:
		for (i = 0, j = 0; i < 8 && name[i] != '.'; i++, j++) buf[pos%SECTOR_SIZE + i] = toupper(name[j]);
		for (;i < 8; i++) buf[pos%SECTOR_SIZE + i] = ' ';
		for (j++; i < 11; i++, j++) buf[pos%SECTOR_SIZE + i] = toupper(name[j]);
		sdir = buf + pos%SECTOR_SIZE;
		sdir->DIR_Attr = FAT32_ATTR_ARCHIVE;
		
		sdir->DIR_NTRes = 0;
		if (flag & 0x01) sdir->DIR_NTRes |= FAT32_BASE_L;
		if (flag & 0x04) sdir->DIR_NTRes |= FAT32_EXT_L;
		
		int year = BCD2BIN(CMOS_READ(CMOS_YEAR)) + 20;
		int month = BCD2BIN(CMOS_READ(CMOS_MONTH));
		int day = BCD2BIN(CMOS_READ(CMOS_DAY_OF_MONTH));
		sdir->DIR_LastAccDate = sdir->DIR_CrtDate = sdir->DIR_WrtDate = year<<9 | month<<5 | day;
		int hour = BCD2BIN(CMOS_READ(CMOS_HOURS));
		int minute = BCD2BIN(CMOS_READ(CMOS_MINUTES));
		int second = BCD2BIN(CMOS_READ(CMOS_SECONDS));
		sdir->DIR_CrtTime = sdir->DIR_WrtTime = hour<<11 | minute<<5 | second>>1;
		sdir->DIR_CrtTimeTenth = second*10;
		
		int file_clus = fat32_alloc_clus(part, 0);
		sdir->DIR_FstClusHI = file_clus>>16;
		sdir->DIR_FstClusLO = file_clus&0xffff;
		sdir->DIR_FileSize = 0;
	}
	file->dir_offset = (pos%(SECTOR_SIZE * fat32->BPB_SecPerClus));
	file->start = sdir->DIR_FstClusHI<<16 | sdir->DIR_FstClusLO;
	sdir = kmalloc(sizeof(struct FAT32_dir));
	memcpy(sdir, buf + pos%SECTOR_SIZE, sizeof(struct FAT32_dir));
	file->private_data = sdir;
	part->device->inode->f_ops.write(part->device->inode, buf, offset, SECTOR_SIZE);
	return inode;
}

void FAT32_delete_file(partition_t *part, struct index_node *inode)
{
	char buf[512];
	unsigned int pos, i;
	struct file *file = inode->fp;
	struct pt_fat32 *fat32 = part->private_data;
	uint32_t offset;
	bool f = true;
	offset = fat32->data_start + (inode->fp->dir_start - 2)*fat32->BPB_SecPerClus + inode->fp->dir_offset/SECTOR_SIZE;
	inode->device->inode->f_ops.read(inode, buf, offset, SECTOR_SIZE);
	pos = inode->fp->dir_offset;
	do
	{
		if(pos%SECTOR_SIZE == 0 && pos >= SECTOR_SIZE)
		{
			part->device->inode->f_ops.write(part, buf, offset, SECTOR_SIZE);
			offset -= 1;
			part->device->inode->f_ops.read(part->device->inode, buf, offset, SECTOR_SIZE);
		}
		buf[pos%SECTOR_SIZE] = 0xe5;
		pos-=0x20;
	} while(buf[pos%SECTOR_SIZE + 11] & FAT32_ATTR_LONG_NAME);
	part->device->inode->f_ops.write(part->device->inode, buf, offset, SECTOR_SIZE);
	pos = file->start;		//释放文件在文件分配表中对应的簇
	while(f)
	{
		i = find_member_in_fat(part, pos);
		if (i >= 0x0fffffff) f = 0;
		fat32_free_clus(part, 0, pos);
		pos = i;
	}
}

void FAT32_close(struct index_node *inode)
{
	struct file *file;
	file = inode->fp;
	kfree(file->private_data);
	return;
}

int fat32_alloc_clus(partition_t *part, int last_clus)
{
	int buf[128];
	int i = 3, j;
	struct pt_fat32 *fat32 = part->private_data;
	uint32_t offset = fat32->fat_start;
	part->device->inode->f_ops.read(part->device->inode, buf, offset, SECTOR_SIZE);
	while(buf[i%128])
	{
		if(i % 128 == 0)
		{
			offset = fat32->fat_start + (i/128);
			part->device->inode->f_ops.read(part->device->inode, buf, offset, SECTOR_SIZE);
		}
		i++;
	}
	for(j = 0; j < fat32->BPB_NumFATs; j++)
	{
		offset = fat32->fat_start + j*fat32->BPB_FATSz32 + (i/128);
		part->device->inode->f_ops.read(part->device->inode, buf, offset, SECTOR_SIZE);
		buf[i%128] = 0x0fffffff;
		part->device->inode->f_ops.write(part->device->inode, buf, offset, SECTOR_SIZE);
		if (last_clus >= 3)
		{
			offset = fat32->fat_start + j*fat32->BPB_FATSz32 + (last_clus/128);
			part->device->inode->f_ops.read(part->device->inode, buf, offset, SECTOR_SIZE);
			buf[last_clus%128] = i;
			part->device->inode->f_ops.write(part->device->inode, buf, offset, SECTOR_SIZE);
		}
	}
	return i;
}

int fat32_free_clus(partition_t *part, int last_clus, int clus)
{
	int buf1[128], buf2[128];
	int j;
	struct pt_fat32 *fat32 = part->private_data;
	uint32_t offset;
	if (last_clus < 3 && clus < 3) return -1;
	for(j = 0; j < fat32->BPB_NumFATs; j++)
	{
		if (last_clus > 2 && clus > 2)
		{
			offset = fat32->fat_start + j*fat32->BPB_FATSz32 + last_clus/128;
			part->device->inode->f_ops.read(part->device->inode, buf1, offset, SECTOR_SIZE);
			if (clus/128 != last_clus/128)
			{
				offset = fat32->fat_start + j*fat32->BPB_FATSz32 + clus/128;
				part->device->inode->f_ops.read(part->device->inode, buf2, offset, SECTOR_SIZE);
				buf1[last_clus%128] = buf2[clus%128];
				buf2[clus%128] = 0x00;
				part->device->inode->f_ops.write(part->device->inode, buf2, offset, SECTOR_SIZE);
			}
			else
			{
				buf1[last_clus%128] = buf1[clus%128];
				buf1[clus%128] = 0x00;
			}
			offset = fat32->fat_start + j*fat32->BPB_FATSz32 + last_clus/128;
			part->device->inode->f_ops.write(part->device->inode, buf1, offset, SECTOR_SIZE);
		}
		else if (clus > 2)
		{
			offset = fat32->fat_start + j*fat32->BPB_FATSz32 + clus/128;
			part->device->inode->f_ops.read(part->device->inode, buf1, offset, SECTOR_SIZE);
			buf1[clus%128] = 0x00;
			part->device->inode->f_ops.write(part->device->inode, buf1, offset, SECTOR_SIZE);
		}
	}
}

struct index_node *FAT32_open(struct _partition_s *part, struct index_node *parent, char *filename)
{
	int i, j, x;
	struct FAT32_long_dir *ldir;
	struct FAT32_dir *sdir;
	struct directory *dir = parent->dir;
	struct file *file = kmalloc(sizeof(struct file));
	struct index_node *inode = vfs_create(filename, ATTR_FILE, parent);
	struct pt_fat32 *fat32 = part->private_data;
	uint32_t offset;
	uint8_t flag = 0, f = 1;
	unsigned int cc;
	char buf[fat32->BPB_SecPerClus*SECTOR_SIZE];
	file->inode = inode;
	inode->fp = file;
	inode->part = parent->part;
	inode->f_ops = parent->f_ops;
	inode->device = parent->device;
	int len = strlen(filename);
	cc = dir->start;
	
	while (f){
		if (find_member_in_fat(part, cc) >= 0x0fffffff) f=0;
		offset = fat32->data_start + (cc - 2) * fat32->BPB_SecPerClus;
		dir->inode->device->inode->f_ops.read(dir->inode, buf, offset, fat32->BPB_SecPerClus*SECTOR_SIZE);
		for (i = 0x00; i < SECTOR_SIZE * fat32->BPB_SecPerClus; i+=0x20)
		{
			if (buf[i + 11] == FAT32_ATTR_LONG_NAME) continue;
			if (buf[i] == 0xe5 || buf[i] == 0x00 || buf[i] == 0x05) continue;
			ldir = (struct FAT32_long_dir *)(buf + i) - 1;
			sdir = (struct FAT32_dir *)(buf + i);
			j = 0;
			
			//如果是长目录项
			while (ldir->LDIR_Attr == FAT32_ATTR_LONG_NAME && ldir->LDIR_Ord != 0xe5)
			{
				for (x = 0; x < 5; x++)
				{
					if (j > len && ldir->LDIR_Name1[x] == 0xffff) continue;
					else if (j > len || ldir->LDIR_Name1[x] != (unsigned short)(filename[j++])) goto cmp_fail;
				}
				for (x = 0; x < 6; x++)
				{
					if (j > len && ldir->LDIR_Name2[x] == 0xffff) continue;
					else if (j > len || ldir->LDIR_Name2[x] != (unsigned short)(filename[j++])) goto cmp_fail;
				}
				for (x = 0; x < 2; x++)
				{
					if (j > len && ldir->LDIR_Name3[x] == 0xffff) continue;
					else if (j > len || ldir->LDIR_Name3[x] != (unsigned short)(filename[j++])) goto cmp_fail;
				}
				
				if (j >= len)
				{
					flag = 1;
					goto cmp_success;
				}
				
				ldir--;
			}
			
			//如果是短目录项
			j = 0;
			if (sdir->DIR_Attr & FAT32_ATTR_DIRECTORY) continue;
			for (x = 0; x < 8; x++)
			{
				if (sdir->DIR_Name[x] == ' ')
				{
					if (!(sdir->DIR_Attr & FAT32_ATTR_DIRECTORY))
					{
						if (filename[j] == '.') continue;
						else if (sdir->DIR_Name[x] == filename[j])
						{
							j++;
							continue;
						}
						else goto cmp_fail;
					}
					else goto cmp_fail;
				}
				else if ((sdir->DIR_Name[x] >= 'A' && sdir->DIR_Name[x] <= 'Z') || (sdir->DIR_Name[x] >= 'a' && sdir->DIR_Name[x] <= 'z'))
				{
					if (sdir->DIR_NTRes & FAT32_BASE_L)
					{
						if (j < len  && sdir->DIR_Name[x] + 32 == filename[j])
						{
							j++;
							continue;
						}
						else goto cmp_fail;
					}
					else
					{
						if (j < len  && sdir->DIR_Name[x] == filename[j])
						{
							j++;
							continue;
						}
						else goto cmp_fail;
					}
				}
				else if (sdir->DIR_Name[x] >= '0' && sdir->DIR_Name[x] <= '9')
				{
					if (j < len && sdir->DIR_Name[x] == filename[j])
					{
						j++;
						continue;
					}
					else goto cmp_fail;
				}
				else
				{
					j++;
				}
			}
			j++;
			for (x = 0; x < 3; x++)
			{
				if ((sdir->DIR_Ext[x] >= 'A' && sdir->DIR_Ext[x] <= 'Z') || (sdir->DIR_Ext[x] >= 'a' && sdir->DIR_Ext[x] <= 'z'))
				{
					if (sdir->DIR_NTRes & FAT32_BASE_L)
					{
						if (j < len  && sdir->DIR_Ext[x] + 32 == filename[j])
						{
							j++;
							continue;
						}
						else goto cmp_fail;
					}
					else
					{
						if (j < len  && sdir->DIR_Ext[x] == filename[j])
						{
							j++;
							continue;
						}
						else goto cmp_fail;
					}
				}
				else if (sdir->DIR_Ext[x] >= '0' && sdir->DIR_Ext[x] <= '9')
				{
					if (j < len && sdir->DIR_Ext[x] == filename[j])
					{
						j++;
						continue;
					}
					else goto cmp_fail;
				}
				else if (sdir->DIR_Ext[x] == ' ')
				{
					if (sdir->DIR_Ext[x] == filename[j])
					{
						if (sdir->DIR_Ext[x] == filename[j])
						{
							j++;
							continue;
						}
						else goto cmp_fail;
					}
				}
				else
				{
					goto cmp_fail;
				}
			}
			flag = 1;
			goto cmp_success;
cmp_fail:;
		}
		cc = find_member_in_fat(part, cc);
	};
cmp_success:
	if(flag)
	{
		string_init(&inode->name);
		string_new(&inode->name, filename, len);
		file->dir_start = cc;
		file->dir_offset = i;
		file->start = (sdir->DIR_FstClusHI<<16 | sdir->DIR_FstClusLO) & 0x0fffffff;
		file->private_data = kmalloc(sizeof(struct FAT32_dir));
		file->size = sdir->DIR_FileSize;
		memcpy(file->private_data, sdir, sizeof(struct FAT32_dir));
		
		inode->create_date.year = (sdir->DIR_CrtDate >> 9) - 20;
		inode->create_date.month = (sdir->DIR_CrtDate >> 5) & 0x0f;
		inode->create_date.day = sdir->DIR_CrtDate & 0x1f;
		inode->write_date.year = (sdir->DIR_WrtDate >> 9) - 20;
		inode->write_date.month = (sdir->DIR_WrtDate >> 5) & 0x0f;
		inode->write_date.day = sdir->DIR_WrtDate & 0x1f;
		inode->last_access_date.year = (sdir->DIR_LastAccDate >> 9) - 20;
		inode->last_access_date.month = (sdir->DIR_LastAccDate >> 5) & 0x0f;
		inode->last_access_date.day = sdir->DIR_LastAccDate & 0x1f;
		inode->create_time.hour = sdir->DIR_CrtTime >> 11;
		inode->create_time.minute = (sdir->DIR_CrtTime >> 5) & 0x3f;
		inode->create_time.second = sdir->DIR_CrtTime & 0x1f;
		inode->write_time.hour = sdir->DIR_WrtTime >> 11;
		inode->write_time.minute = (sdir->DIR_WrtTime >> 5) & 0x3f;
		inode->write_time.second = sdir->DIR_WrtTime & 0x1f;
		return inode;
	}
	else
	{
		vfs_close(inode);
		kfree(file);
		return NULL;
	}
}

struct index_node *FAT32_find_dir(struct _partition_s *part, struct index_node *parent, char *name)
{
	unsigned int i, j, cc, x, pos;
	bool flag, f;
	char *buf = kmalloc(((struct pt_fat32 *)part->private_data)->BPB_SecPerClus*SECTOR_SIZE);
	struct directory *dir = kmalloc(sizeof(struct directory));
	struct pt_fat32 *fat32 = part->private_data;
	struct FAT32_long_dir *ldir;
	struct FAT32_dir *sdir;
	struct file file;
	int len = strlen(name);
	uint32_t offset;
	dir->inode = kmalloc(sizeof(struct index_node));
	dir->inode->attribute = ATTR_DIR;
	dir->inode->parent = parent;
	dir->inode->f_ops = parent->f_ops;
	dir->inode->device = part->device;
	dir->inode->part = part;
	list_init(&dir->inode->childs);
	list_add_tail(&dir->inode->list, &parent->childs);
	string_init(&dir->inode->name);
	string_new(&dir->inode->name, name, len);
	flag = 0;
	f = 1;
	cc = parent->dir->start;
	while (f) {
		if (find_member_in_fat(part, cc) >= 0x0fffffff) f=0;
		offset = fat32->data_start + (cc - 2) * fat32->BPB_SecPerClus;
		dir->inode->device->inode->f_ops.read(dir->inode, buf, offset, fat32->BPB_SecPerClus*SECTOR_SIZE);
		for (i = 0x00; i < SECTOR_SIZE * fat32->BPB_SecPerClus; i+=0x20)
		{
			if (buf[i + 11] == FAT32_ATTR_LONG_NAME) continue;
			if (buf[i] == 0xe5 || buf[i] == 0x00 || buf[i] == 0x05) continue;
			ldir = (struct FAT32_long_dir *)(buf + i) - 1;
			j = 0;
			
			//如果是长目录项
			while (ldir->LDIR_Attr == FAT32_ATTR_LONG_NAME && ldir->LDIR_Ord != 0xe5)
			{
				for (x = 0; x < 5; x++)
				{
					if (j > len && ldir->LDIR_Name1[x] == 0xffff) continue;
					else if (j > len || ldir->LDIR_Name1[x] != (unsigned short)(name[j++])) goto cmp_fail;
				}
				for (x = 0; x < 6; x++)
				{
					if (j > len && ldir->LDIR_Name2[x] == 0xffff) continue;
					else if (j > len || ldir->LDIR_Name2[x] != (unsigned short)(name[j++])) goto cmp_fail;
				}
				for (x = 0; x < 2; x++)
				{
					if (j > len && ldir->LDIR_Name3[x] == 0xffff) continue;
					else if (j > len || ldir->LDIR_Name3[x] != (unsigned short)(name[j++])) goto cmp_fail;
				}
				
				if (j >= len)
				{
					flag = true;
					goto cmp_success;
				}
				
				ldir--;
			}
			
			//如果是短目录项
			j = 0;
			sdir = (struct FAT32_long_dir *)(buf + i);
			for (x = 0; x < 11; x++)
			{
				if (sdir->DIR_Name[x] == ' ')
				{
					if (sdir->DIR_Attr & FAT32_ATTR_DIRECTORY)
					{
						if (sdir->DIR_Name[x] == name[j])
						{
							j++;
							continue;
						}
						else
						{
							goto cmp_fail;
						}
					}
				}
				else if ((sdir->DIR_Name[x] >= 'A' && sdir->DIR_Name[x] <= 'Z') || (sdir->DIR_Name[x] >= 'a' && sdir->DIR_Name[x] <= 'z'))
				{
					if (sdir->DIR_NTRes & FAT32_BASE_L)
					{
						if (j < len  && sdir->DIR_Name[x] + 32 == name[j])
						{
							j++;
							continue;
						}
						else
						{
							goto cmp_fail;
						}
					}
					else
					{
						if (j < len  && sdir->DIR_Name[x] == name[j])
						{
							j++;
							continue;
						}
						else
						{
							goto cmp_fail;
						}
					}
					
				}
				else if (j < len && sdir->DIR_Name[x] == name[j])
				{
					j++;
					continue;
				}
				else if (sdir->DIR_Name[x] >= '0' && sdir->DIR_Name[x] <= '9')
				{
					goto cmp_fail;
				}
				else
				{
					goto cmp_fail;
				}
			}
			flag = true;
			goto cmp_success;
cmp_fail:;
		}
		cc = find_member_in_fat(part, cc);
	};
cmp_success:
	if (flag)
	{
		sdir = (struct FAT32_long_dir *)(buf + i);
		dir->dir_start = cc;
		dir->dir_offset = i;
		dir->start = (sdir->DIR_FstClusHI<<16 | sdir->DIR_FstClusLO);
		dir->inode->dir = dir;
		
		dir->inode->create_date.year = (sdir->DIR_CrtDate >> 9) - 20;
		dir->inode->create_date.month = (sdir->DIR_CrtDate >> 5) & 0x0f;
		dir->inode->create_date.day = sdir->DIR_CrtDate & 0x1f;
		dir->inode->write_date.year = (sdir->DIR_WrtDate >> 9) - 20;
		dir->inode->write_date.month = (sdir->DIR_WrtDate >> 5) & 0x0f;
		dir->inode->write_date.day = sdir->DIR_WrtDate & 0x1f;
		dir->inode->last_access_date.year = (sdir->DIR_LastAccDate >> 9) - 20;
		dir->inode->last_access_date.month = (sdir->DIR_LastAccDate >> 5) & 0x0f;
		dir->inode->last_access_date.day = sdir->DIR_LastAccDate & 0x1f;
		dir->inode->create_time.hour = sdir->DIR_CrtTime >> 11;
		dir->inode->create_time.minute = (sdir->DIR_CrtTime >> 5) & 0x3f;
		dir->inode->create_time.second = sdir->DIR_CrtTime & 0x1f;
		dir->inode->write_time.hour = sdir->DIR_WrtTime >> 11;
		dir->inode->write_time.minute = (sdir->DIR_WrtTime >> 5) & 0x3f;
		dir->inode->write_time.second = sdir->DIR_WrtTime & 0x1f;
		kfree(buf);
		return dir->inode;
	}
	else
	{
		vfs_close(dir->inode);
		kfree(dir);
		kfree(buf);
		return NULL;
	}
}

unsigned int find_member_in_fat(struct _partition_s *part, int i)
{
	unsigned int buf[SECTOR_SIZE/sizeof(unsigned int)], next_clus;
	struct pt_fat32 *fat32 = part->private_data;
	uint32_t offset = part->start_lba + fat32->BPB_RevdSecCnt + (i/128);
	part->device->inode->f_ops.read(part->device->inode, buf, offset, SECTOR_SIZE);
	next_clus = buf[i%128];
	return next_clus;
}

static __init void fat32_fs_entry(void)
{
	fs_register("FAT32", &fat32_fs_ops);
}

fs_initcall(fat32_fs_entry);
