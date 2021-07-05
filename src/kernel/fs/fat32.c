#include <kernel/fs/fat32.h>
#include <device/ide.h>
#include <kernel/memory.h>
#include <kernel/time.h>
#include <kernel/console.h>
#include <ctype.h>
#include <math.h>

struct FAT_clus_list *clus_list;

void init_fat32(struct partition *part, char *data)
{
	char buf[512];
	struct pt_fat32 *fat32 = kmalloc(sizeof(struct pt_fat32));
	memcpy(fat32, data, SECTOR_SIZE);
	part->disk->disk_read(part->disk->disk_num, part->pt.start_lba + 1, buf);
	
	memcpy(&fat32->FSInfo, buf, sizeof(struct FS_Info));
	if(fat32->FSInfo.FSI_LeadSig == 0x41615252)
	{
		fat32->fat_start = part->pt.start_lba + fs_FAT32(part->fs)->BPB_RevdSecCnt;
		part->fs = fat32;
		part->fs_open = FAT32_open;
		part->fs_read = FAT32_read;
		part->fs_write = FAT32_write;
		part->fs_close = FAT32_close;
		part->data_start = fat32->fat_start + fat32->BPB_NumFATs*fat32->BPB_FATSz32;
	}
	else
	{
		kfree(fat32);
	}
	return;
}

struct fs_file FAT32_open(struct partition *part, char *path)
{
	int len = strlen(path) - 1;
	struct fs_folder fld;
	struct fs_file file;
	char *fld_name;
	while(path[len] != '/' && len >= 0) len--;
	if (len == 0)
	{
		printk(COLOR_LRED"FAT32_open:error!");
	}
	fld_name = kmalloc(len);
	strncpy(fld_name, path, len);
	fld = FAT32_find_fld(part, fld_name);
	kfree(fld_name);
	path += len+1;
	file = FAT32_find_file(part, fld, path);
	file.parent_fld = fld;
	return file;
}

void FAT32_read(struct partition *part, struct fs_file *file, unsigned int offset, unsigned int length, char *buf)
{
	int i, j;
	int pos;
	pos = file->pos + offset/fs_FAT32(part->fs)->BPB_SecPerClus;
	for (j = 0; j < (length%SECTOR_SIZE ? length/SECTOR_SIZE + 1 : length/SECTOR_SIZE);)
	{
		for (i = 0; i < fs_FAT32(part->fs)->BPB_SecPerClus && j < (length%SECTOR_SIZE ? length/SECTOR_SIZE + 1 : length/SECTOR_SIZE); i++, j++)
		{
			part->disk->disk_read(part->disk->disk_num, part->data_start + (pos - 2) * ((struct pt_fat32 *)part->fs)->BPB_SecPerClus + i, buf + j*SECTOR_SIZE);
		}
		pos = find_member_in_fat(part, pos);
	}
}

void FAT32_write(struct partition *part, struct fs_file *file, unsigned int offset, unsigned int length, char *buf)
{
	int i, j;
	int pos, tmp;
	pos = file->pos + offset/fs_FAT32(part->fs)->BPB_SecPerClus;
	for (j = 0; j < (length%SECTOR_SIZE ? length/SECTOR_SIZE + 1 : length/SECTOR_SIZE);)
	{
		for (i = 0; i < fs_FAT32(part->fs)->BPB_SecPerClus && j < (length%SECTOR_SIZE ? length/SECTOR_SIZE + 1 : length/SECTOR_SIZE); i++, j++)
		{
			part->disk->disk_write(part->disk->disk_num, part->data_start + (pos - 2) * ((struct pt_fat32 *)part->fs)->BPB_SecPerClus + i, buf + j*SECTOR_SIZE);
		}
		tmp = find_member_in_fat(part, pos);
		if (tmp >= 0x0fffffff) pos = fat32_alloc_clus(part, pos);
		else pos = tmp;
	}
}

void FAT32_create_file(struct partition *part, struct fs_folder *fld, struct fs_file *file)
{
	char buf[512], filename_short[11];
	unsigned int pos, i = 0, j, len1, len2, name_count = 0, count = 0, name_len, tmp;
	unsigned char checksum;
	int flag;
	pos = fld->pos;
	do
	{
		if (i % SECTOR_SIZE == 0) part_read(part, part->data_start + (pos - 2) * fs_FAT32(part->fs)->BPB_SecPerClus + i/SECTOR_SIZE, buf);
		if (i/SECTOR_SIZE/fs_FAT32(part->fs)->BPB_SecPerClus) tmp = find_member_in_fat(part, pos);
		if (tmp >= 0x0fffffff) pos = fat32_alloc_clus(part, pos);
		else pos = tmp;
		if (buf[i%SECTOR_SIZE + 11] != FAT32_ATTR_LONG_NAME)
		{
			flag = 1;
			for (j = 0; j < 8 && buf[i%SECTOR_SIZE + j] != '~'; j++)
			{
				if (buf[i%SECTOR_SIZE + j] != file->name[j]) flag = 0;
			}
			if (flag)
			{
				name_count++;
			}
		}
		i+=0x20;
	} while (buf[i]);
	pos = ((pos - 2) * fs_FAT32(part->fs)->BPB_SecPerClus)*SECTOR_SIZE + i;
	len1 = strlen(file->name);
	flag = FAT32_BASE_L | FAT32_EXT_L;
	for (i = 0; i < len1 && file->name[i] != '.'; i++) if(!isupper(file->name[i])) flag |= 0x1;
	for (; i < len1; i++) if(!isupper(file->name[i])) flag |= 0x4;
	for (i = 0; i < len1 && file->name[i] != '.'; i++) if(!islower(file->name[i])) flag |= 0x2;
	for (; i < len1; i++) if(!islower(file->name[i])) flag |= 0x8;
	if (flag&0x03 && flag&0x0c) goto short_dir;
	else if (len1>11 || (file->name + len1 - strstr(file->name, ".")) > 3)
	{
		len2 = (len1%13 ? len1%13+1 : len1%13);
		int tmp = name_count;
		while(tmp)
		{
			count++;
			tmp/=10;
		}
		name_len = min(name_count, 8-count);
		for (i = 0; i < 11; i++)
		{
			if (i < name_len) filename_short[i] = toupper(file->name ? file->name : ' ');
			else if (i == name_len) filename_short[i] = '~';
			else if (i > name_len && i < name_len+count) filename_short[i] = ((name_count/pow(10, 7-i))%10 + '0');
			else if (i < 8) filename_short[i] = ' ';
			else filename_short[i] = file->name[len1 - 3 + i];
		}
		FAT32_checksum(filename_short, checksum);
		for (i = 0; i < len2; i++)
		{
			if ((i*0x20)%SECTOR_SIZE == 0) part_read(part, part->data_start + (pos + i*0x20)/SECTOR_SIZE, buf);
			buf[(pos + i*0x20)%SECTOR_SIZE + 0] = len2 - i;
			if (i == 0) buf[pos%SECTOR_SIZE] |= 0x40;
			for(;(j%13) <  5; j++) buf[(pos + i*0x20)%SECTOR_SIZE +  1 + j] = (j < len1 ? file->name[j] : 0xff);
			buf[pos + i*0x20 + 11] = FAT32_ATTR_LONG_NAME;
			buf[pos + i*0x20 + 13] = checksum;
			for(;(j%13) < 11; j++) buf[(pos + i*0x20)%SECTOR_SIZE + 14 + j] = (j < len1 ? file->name[j] : 0xff);
			for(;(j%13) < 13; j++) buf[(pos + i*0x20)%SECTOR_SIZE + 28 + j] = (j < len1 ? file->name[j] : 0xff);
		}
		pos += i*0x20;
		if (pos/SECTOR_SIZE == 0) part_read(part, part->data_start + pos/SECTOR_SIZE, buf);
		for (i = 0; i < 11; i++)
		{
			buf[pos%SECTOR_SIZE + i] = filename_short[i];
		}
		buf[pos%SECTOR_SIZE + 11] = FAT32_ATTR_ARCHIVE;
		//时间信息未添加
		int file_clus = fat32_alloc_clus(part, 0);
		buf[pos%SECTOR_SIZE + 21] = file_clus>>16;
		buf[pos%SECTOR_SIZE + 27] = file_clus&0xff;
	}
	else
	{
short_dir:
		for (i = 0; i < 11; i++)
		{
			buf[pos%SECTOR_SIZE + i] = toupper(filename_short[i]);
		}
		if (!(flag&0x01) && (flag&0x02)) buf[pos%SECTOR_SIZE + 12] = FAT32_BASE_L;
		else if (!(flag&0x04) && (flag&0x08)) buf[pos%SECTOR_SIZE + 12] = FAT32_EXT_L;
		buf[pos%SECTOR_SIZE + 11] = FAT32_ATTR_ARCHIVE;
		//时间信息未添加
		int file_clus = fat32_alloc_clus(part, 0);
		buf[pos%SECTOR_SIZE + 21] = file_clus>>16;
		buf[pos%SECTOR_SIZE + 27] = file_clus&0xff;
	}
	
}

void FAT32_delete_file(struct partition *part, struct fs_file *file)
{
	char buf[512];
	unsigned int pos, i;
	bool f = true;
	pos = FAT32_lookup_file(part, file->parent_fld, file->name, &i);
	part_read(part, part->data_start + pos/SECTOR_SIZE, buf);
	do
	{
		if(pos%SECTOR_SIZE == (SECTOR_SIZE-0x20))
		{
			part_write(part, part->data_start + pos/SECTOR_SIZE + 1, buf);
			part_read(part, part->data_start + pos/SECTOR_SIZE, buf);
		}
		buf[pos%SECTOR_SIZE] = 0xe5;
		pos-=0x20;
	} while(!(buf[pos%SECTOR_SIZE + 11] & FAT32_ATTR_LONG_NAME));
	pos = file->pos;
	while(f)
	{
		i = find_member_in_fat(part, pos);
		if (i >= 0x0fffffff) f = false;
		fat32_free_clus(part, pos, 0);
		pos = i;
	}
}

void FAT32_close(struct fs_file *file)
{
	kfree(file->name);
}

int fat32_alloc_clus(struct partition *part, int last_clus)
{
	int buf[128];
	int i = 3, j;
	while(buf[i%128])
	{
		if(i % 128 == 0) part_read(part, fs_FAT32(part->fs)->fat_start + (i/128), buf);
		i++;
	}
	for(j = 0; j < fs_FAT32(part->fs)->BPB_NumFATs; j++)
	{
		part_read(part, fs_FAT32(part->fs)->fat_start + j*fs_FAT32(part->fs)->BPB_FATSz32 + (i/128), buf);
		buf[i%128] = 0x0fffffff;
		part_write(part, fs_FAT32(part->fs)->fat_start + j*fs_FAT32(part->fs)->BPB_FATSz32 + i/128, buf);
		if (last_clus >= 3)
		{
			part_read(part, fs_FAT32(part->fs)->fat_start + j*fs_FAT32(part->fs)->BPB_FATSz32 + (last_clus/128), buf);
			buf[last_clus%128] = i;
			part_write(part, fs_FAT32(part->fs)->fat_start + j*fs_FAT32(part->fs)->BPB_FATSz32 + (last_clus/128), buf);
		}
	}
	return i;
}

int fat32_free_clus(struct partition *part, int last_clus, int clus)
{
	int buf1[128], buf2[128];
	int j;
	if (last_clus < 3 || clus < 3) return -1;
	for(j = 0; j < fs_FAT32(part->fs)->BPB_NumFATs; j++)
	{
		if (last_clus > 2 && clus > 2)
		{
			part_read(part, fs_FAT32(part->fs)->fat_start + j*fs_FAT32(part->fs)->BPB_FATSz32 + last_clus/128, buf1);
			if (clus/128 != last_clus/128)
			{
				part_read(part, fs_FAT32(part->fs)->fat_start + j*fs_FAT32(part->fs)->BPB_FATSz32 + clus/128, buf2);
				buf1[last_clus%128] = buf2[clus%128];
				buf2[clus%128] = 0x00;
				part_write(part, fs_FAT32(part->fs)->fat_start + j*fs_FAT32(part->fs)->BPB_FATSz32 + clus/128, buf2);
			}
			else
			{
				buf1[last_clus%128] = buf1[clus%128];
				buf1[clus%128] = 0x00;
			}
			part_write(part, fs_FAT32(part->fs)->fat_start + j*fs_FAT32(part->fs)->BPB_FATSz32 + last_clus/128, buf1);
		}
		else if (clus > 2)
		{
			part_read(part, fs_FAT32(part->fs)->fat_start + j*fs_FAT32(part->fs)->BPB_FATSz32 + clus/128, buf1);
			buf1[clus%128] = 0x00;
			part_write(part, fs_FAT32(part->fs)->fat_start + j*fs_FAT32(part->fs)->BPB_FATSz32 + clus/128, buf1);
		}
	}
}

unsigned int FAT32_lookup_file(struct partition *part, struct fs_folder fld, char *filename, int *len)
{
	int i, j, x;
	struct FAT32_long_dir *ldir;
	struct FAT32_dir *sdir;
	bool flag = 0, f = 1;
	unsigned int cc;
	char buf[((struct pt_fat32 *)part->fs)->BPB_SecPerClus*SECTOR_SIZE];
	*len = strlen(filename);
	cc = fld.pos;
	
	while (f){
		if (find_member_in_fat(part, cc) >= 0x0fffffff) f=0;
		for (i = 0; i < ((struct pt_fat32 *)part->fs)->BPB_SecPerClus; i++)
		{
			part->disk->disk_read(part->disk->disk_num, part->data_start + (cc - 2) * ((struct pt_fat32 *)part->fs)->BPB_SecPerClus + i, buf + i*SECTOR_SIZE);
		}
		for (i = 0x00; i < SECTOR_SIZE * ((struct pt_fat32 *)part->fs)->BPB_SecPerClus; i+=0x20)
		{
			if (buf[i + 11] == FAT32_ATTR_LONG_NAME) continue;
			if (buf[i + 1] == 0xe5 || buf[i + 1] == 0x00 || buf[i + 1] == 0x05) continue;
			ldir = (struct FAT32_long_dir *)(buf + i) - 1;
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
					flag = true;
					goto cmp_success;
				}
				
				ldir--;
			}
			
			//如果是短目录项
			j = 0;
			sdir = (struct FAT32_long_dir *)(buf + i);
			if (sdir->DIR_Attr == FAT32_ATTR_VOLUME_ID)
			{
				memcpy(part->vol_name, sdir, 11);
			}
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
			flag = true;
			goto cmp_success;
cmp_fail:;
		}
		cc = find_member_in_fat(part, cc);
	};
cmp_success:
	if(flag)
	{
		return (cc - 2)*fs_FAT32(part->fs)->BPB_SecPerClus*SECTOR_SIZE + i;
	}
}

struct fs_file FAT32_find_file(struct partition *part, struct fs_folder fld, char *filename)
{
	int len;
	struct fs_file file;
	struct FAT32_dir *sdir;
	char buf[512];
	memset(&file, 0, sizeof(file));
	unsigned int ret = FAT32_lookup_file(part, fld, filename, &len);
	if(ret)
	{
		part_read(part, part->data_start + ret/SECTOR_SIZE, buf);
		sdir = (struct FAT32_long_dir *)(buf + ret%512);
		file.name = kmalloc(len);
		memcpy(file.name, filename, len);
		file.pos = (sdir->DIR_FstClusHI<<16 | sdir->DIR_FstClusLO) & 0x0fffffff;
		file.create_date.year = 1980 + ((sdir->DIR_CrtDate)>>9);
		file.create_date.month = (sdir->DIR_CrtDate&0x01e0)>>5;
		file.create_date.day = sdir->DIR_CrtDate&0x001f;
		file.create_time.hour = (sdir->DIR_CrtTime&0xf800)>>11;
		file.create_time.minute = (sdir->DIR_CrtTime&0x07e0)>>5;
		file.create_time.second = sdir->DIR_CrtTime&0x001f;
		file.write_date.year = 1980 + ((sdir->DIR_WrtDate)>>9);
		file.write_date.month = (sdir->DIR_WrtDate&0x01e0)>>5;
		file.write_date.day = sdir->DIR_WrtDate&0x001f;
		file.write_time.hour = (sdir->DIR_WrtTime&0xf800)>>11;
		file.write_time.minute = (sdir->DIR_WrtTime&0x07e0)>>5;
		file.write_time.second = sdir->DIR_WrtTime&0x001f;
		file.last_acctime = sdir->DIR_LastAccData;
		file.file_size = sdir->DIR_FileSize;
	}
	return file;
}

struct fs_folder FAT32_find_fld(struct partition *part, char *path)
{
	unsigned int i, j, cc, x, pos;
	bool flag, f;
	char buf[((struct pt_fat32 *)part->fs)->BPB_SecPerClus*SECTOR_SIZE];
	struct fs_folder fld;
	struct FAT32_long_dir *ldir;
	struct FAT32_dir *sdir;
	struct fs_file file;
	fld.name = kmalloc(1);
	strcpy(fld.name, "/");
	fld.pos = ((struct pt_fat32 *)part->fs)->BPB_RootClus;
	while (*path == '/')
	{
		flag = 0;
		f = 1;
		path++;
		if (*path == 0) break;
		cc = fld.pos;
		int len = (strstr(path, "/") - path);
		if (len <= 0) len = strlen(path);
		char *folder_name = kmalloc(len);
		memcpy(folder_name, path, len);
		while (f) {
			if (find_member_in_fat(part, cc) >= 0x0fffffff) f=0;
			for (i = 0; i < ((struct pt_fat32 *)part->fs)->BPB_SecPerClus; i++)
			{
				part->disk->disk_read(part->disk->disk_num, part->data_start + (cc - 2) * ((struct pt_fat32 *)part->fs)->BPB_SecPerClus + i, buf + i*SECTOR_SIZE);
			}
			for (i = 0x00; i < SECTOR_SIZE * ((struct pt_fat32 *)part->fs)->BPB_SecPerClus; i+=0x20)
			{
				if (buf[i + 11] == FAT32_ATTR_LONG_NAME) continue;
				if (buf[i + 1] == 0xe5 || buf[i + 1] == 0x00 || buf[i + 1] == 0x05) continue;
				ldir = (struct FAT32_long_dir *)(buf + i) - 1;
				j = 0;
				
				//如果是长目录项
				while (ldir->LDIR_Attr == FAT32_ATTR_LONG_NAME && ldir->LDIR_Ord != 0xe5)
				{
					for (x = 0; x < 5; x++)
					{
						if (j > len && ldir->LDIR_Name1[x] == 0xffff) continue;
						else if (j > len || ldir->LDIR_Name1[x] != (unsigned short)(folder_name[j++])) goto cmp_fail;
					}
					for (x = 0; x < 6; x++)
					{
						if (j > len && ldir->LDIR_Name2[x] == 0xffff) continue;
						else if (j > len || ldir->LDIR_Name2[x] != (unsigned short)(folder_name[j++])) goto cmp_fail;
					}
					for (x = 0; x < 2; x++)
					{
						if (j > len && ldir->LDIR_Name3[x] == 0xffff) continue;
						else if (j > len || ldir->LDIR_Name3[x] != (unsigned short)(folder_name[j++])) goto cmp_fail;
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
				if (sdir->DIR_Attr == FAT32_ATTR_VOLUME_ID)
				{
					memcpy(part->vol_name, sdir, 11);
				}
				for (x = 0; x < 11; x++)
				{
					if (sdir->DIR_Name[x] == ' ')
					{
						if (sdir->DIR_Attr & FAT32_ATTR_DIRECTORY)
						{
							if (sdir->DIR_Name[x] == folder_name[j])
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
							if (j < len  && sdir->DIR_Name[x] + 32 == folder_name[j])
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
							if (j < len  && sdir->DIR_Name[x] == folder_name[j])
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
					else if (j < len && sdir->DIR_Name[x] == folder_name[j])
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
			fld.pos = (sdir->DIR_FstClusHI<<16 | sdir->DIR_FstClusLO);
			kfree(fld.name);
			fld.name = kmalloc(len);
			memcpy(fld.name, folder_name, len);
			fld.create_date.year = 1980 + ((sdir->DIR_CrtDate)>>9);
			fld.create_date.month = (sdir->DIR_CrtDate&0x01e0)>>5;
			fld.create_date.day = sdir->DIR_CrtDate&0x001f;
			fld.create_time.hour = (sdir->DIR_CrtTime&0xf800)>>11;
			fld.create_time.minute = (sdir->DIR_CrtTime&0x07e0)>>5;
			fld.create_time.second = sdir->DIR_CrtTime&0x001f;
			fld.write_date.year = 1980 + ((sdir->DIR_WrtDate)>>9);
			fld.write_date.month = (sdir->DIR_WrtDate&0x01e0)>>5;
			fld.write_date.day = sdir->DIR_WrtDate&0x001f;
			fld.write_time.hour = (sdir->DIR_WrtTime&0xf800)>>11;
			fld.write_time.minute = (sdir->DIR_WrtTime&0x07e0)>>5;
			fld.write_time.second = sdir->DIR_WrtTime&0x001f;
			path += len;
		}
		kfree(folder_name);
	}
	return fld;
}

unsigned int find_member_in_fat(struct partition *part, int i)
{
	unsigned int buf[128], next_clus;
	part->disk->disk_read(0, part->pt.start_lba + ((struct pt_fat32 *)part->fs)->BPB_RevdSecCnt + (i/128), buf);
	next_clus = buf[i%128];
	return next_clus;
}
