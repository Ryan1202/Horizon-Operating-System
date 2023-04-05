/**
 * @file fat32.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief FAT32文件系统
 * @version 1.1
 * @date 2023-01-23
 *
 */
#include <const.h>
#include <ctype.h>
#include <drivers/cmos.h>
#include <fs/fat32.h>
#include <fs/fs.h>
#include <fs/vfs.h>
#include <kernel/console.h>
#include <kernel/initcall.h>
#include <kernel/memory.h>
#include <math.h>

#include <drivers/pit.h>
// struct FAT_clus_list *clus_list;

fs_operations_t fat32_fs_ops = {
	.fs_check			= fat32_check,
	.fs_read_superblock = fat32_readsuperblock,
	.fs_open			= FAT32_open,
	.fs_opendir			= FAT32_find_dir,
	.fs_close			= FAT32_close,
	.fs_read			= FAT32_read,
	.fs_write			= FAT32_write,
	.fs_create			= FAT32_create_file,
	.fs_delete			= FAT32_delete_file,
};

status_t fat32_check(struct partition_table *pt) {
	if (pt->fs_type == 0x0b || pt->fs_type == 0x0c) { return SUCCUESS; }
	return FAILED;
}

status_t fat32_readsuperblock(partition_t *partition, char *data) {
	struct FAT32_dir   sdir;
	struct pt_fat32	  *fat32 = kmalloc(sizeof(struct pt_fat32));
	struct index_node *dev	 = partition->device->inode;
	memcpy(fat32, data, SECTOR_SIZE);
	dev->f_ops.seek(dev, partition->start_lba + 1, 0);
	dev->f_ops.read(dev, (uint8_t *)&fat32->FSInfo, SECTOR_SIZE);

	if (fat32->FSInfo.FSI_LeadSig == 0x41615252) {
		partition->root->device = partition->device;
		struct directory *dir	= kmalloc(sizeof(struct directory));
		fat32->fat_buffer		= kmalloc(SECTOR_SIZE);

		dir->start				= 2; // 根目录在2号簇
		dir->inode				= partition->root;
		fat32->fat_start		= partition->start_lba + fat32->BPB_RevdSecCnt;
		fat32->data_start		= fat32->fat_start + fat32->BPB_NumFATs * fat32->BPB_FATSz32;
		fat32->buffer_pos		= 0;
		partition->private_data = fat32;
		partition->root->dir	= dir;

		dev->f_ops.seek(dev, fat32->data_start, 0);
		dev->f_ops.read(dev, (uint8_t *)&sdir, sizeof(struct FAT32_dir));
		if (sdir.DIR_Attr == FAT32_ATTR_VOLUME_ID) {
			string_del(&partition->name);
			int cnt = 0;
			while (sdir.DIR_Name[cnt] != ' ' && cnt < 11)
				cnt++;
			string_new(&partition->name, (char *)sdir.DIR_Name, cnt);
		}

		return SUCCUESS;
	}
	kfree(fat32);
	return FAILED;
}

int FAT32_read(struct index_node *inode, uint8_t *buffer, uint32_t length) {
	struct file		  *fp	 = inode->fp;
	struct index_node *dev	 = inode->part->device->inode;
	struct pt_fat32	  *fat32 = fs_FAT32(inode->part->private_data);
	int				   cnt, off = 0, tmp;
	int				   pos;
	const uint32_t	   clus_size = SECTOR_SIZE * fat32->BPB_SecPerClus;
	const uint8_t	  *buf		 = kmalloc(clus_size);

	cnt = fp->offset / (clus_size);
	pos = fp->index_table[cnt];
	dev->f_ops.seek(dev,
					(fp->offset / SECTOR_SIZE) % fat32->BPB_SecPerClus + fat32->data_start +
						(pos - 2) * fat32->BPB_SecPerClus,
					0);
	if (fp->offset % (clus_size)) {
		tmp = clus_size - fp->offset % (clus_size);
		dev->f_ops.read(dev, (uint8_t *)buf, SECTOR_SIZE);
		memcpy(buffer, buf + fp->offset % (clus_size), tmp);
		off += tmp;
		cnt++;
	}
	while (length - off > clus_size) {
		pos = fp->index_table[cnt++];
		dev->f_ops.seek(dev, fat32->data_start + (pos - 2) * fat32->BPB_SecPerClus, 0);
		dev->f_ops.read(dev, buffer + off, MIN(clus_size, length - off));
		off += clus_size;
	}
	tmp = fp->offset;
	fp->offset += length;
	if (fp->offset >= fp->size) { fp->offset = fp->size; }
	return fp->offset - tmp;
}

int FAT32_write(struct index_node *inode, uint8_t *buffer, uint32_t length) {
	int				   i;
	int				   pos;
	struct file		  *fp	 = inode->fp;
	struct pt_fat32	  *fat32 = inode->part->private_data;
	const uint8_t	  *buf	 = kmalloc(SECTOR_SIZE * fat32->BPB_SecPerClus);
	struct FAT32_dir  *sdir;
	struct index_node *dev = inode->device->inode;

	int cnt	 = DIV_ROUND_UP(fp->offset, (SECTOR_SIZE * fat32->BPB_SecPerClus));
	int off	 = fp->offset % SECTOR_SIZE;
	int off2 = cnt % fat32->BPB_SecPerClus;

	if (cnt > inode->fp->size / SECTOR_SIZE / fat32->BPB_SecPerClus) {
		cnt = inode->fp->size / SECTOR_SIZE / fat32->BPB_SecPerClus;
	}
	pos = inode->fp->index_table[cnt];

	dev->f_ops.seek(dev, off2 + (fat32->data_start + (pos - 2) * fat32->BPB_SecPerClus), 0);
	dev->f_ops.write(dev, (uint8_t *)buf, MIN(SECTOR_SIZE - off, off + length));
	if (fp->offset % SECTOR_SIZE + length <= SECTOR_SIZE) goto done;
	else { length -= SECTOR_SIZE - off; }

	i = 0;
	while (length > 0) {
		if (i % 2 == 0 && i != 0) { pos = fat_next(inode, pos, 1, 1); }
		dev->f_ops.seek(dev, (i % 2 + (pos - 2) * fat32->BPB_SecPerClus + fat32->data_start), 0);
	}

done:
	pos = fat_next(inode, fp->dir_start, fp->dir_offset / (SECTOR_SIZE * fat32->BPB_SecPerClus), 0);
	dev->f_ops.seek(dev, fat32->data_start + (pos - 2) * fat32->BPB_SecPerClus + fp->dir_offset / SECTOR_SIZE,
					0);
	dev->f_ops.read(dev, (uint8_t *)buf, SECTOR_SIZE);
	sdir = (struct FAT32_dir *)(buf + fp->dir_offset % SECTOR_SIZE);

	inode->last_access_date.year = inode->write_date.year = BCD2BIN(CMOS_READ(CMOS_YEAR));
	inode->last_access_date.month = inode->write_date.month = BCD2BIN(CMOS_READ(CMOS_MONTH));
	inode->last_access_date.day = inode->write_date.day = BCD2BIN(CMOS_READ(CMOS_DAY_OF_MONTH));
	inode->write_time.hour								= BCD2BIN(CMOS_READ(CMOS_HOURS));
	inode->write_time.minute							= BCD2BIN(CMOS_READ(CMOS_MINUTES));
	inode->write_time.second							= BCD2BIN(CMOS_READ(CMOS_SECONDS));
	sdir->DIR_LastAccDate								= sdir->DIR_WrtDate =
		(inode->write_date.year + 20) << 9 | inode->write_date.month << 5 | inode->write_date.day;
	sdir->DIR_WrtTime =
		inode->write_time.hour << 11 | inode->write_time.minute << 5 | inode->write_time.second;
	dev->f_ops.seek(dev, fat32->data_start + (pos - 2) * fat32->BPB_SecPerClus + fp->dir_offset / SECTOR_SIZE,
					0);
	dev->f_ops.write(dev, (uint8_t *)buf, SECTOR_SIZE);

	fp->offset += length;
	return length;
}

struct index_node *FAT32_create_file(partition_t *part, struct index_node *parent, char *name, int len) {
	struct index_node	  *dev = part->device->inode;
	char				   buf[SECTOR_SIZE], filename_short[11];
	unsigned int		   pos, i = 0, j, len2, name_count = 1, count = 1, name_len;
	unsigned char		   checksum;
	struct directory	  *dir	 = parent->dir;
	struct pt_fat32		  *fat32 = part->private_data;
	struct FAT32_long_dir *ldir;
	struct FAT32_dir	  *sdir;
	struct index_node	  *inode = vfs_create(name, ATTR_FILE, parent);
	struct file			  *file	 = kmalloc(sizeof(struct file));
	uint32_t			   offset;
	int					   flag;

	file->inode		= inode;
	file->dir_start = parent->dir->start;
	file->cur_pos	= 0;
	file->start		= 0;
	inode->fp		= file;
	inode->device	= parent->device;
	inode->part		= part;
	inode->f_ops	= parent->f_ops;
	pos				= dir->start;
	do {
		// 如果到了下一个扇区，重新读取
		if (i % SECTOR_SIZE == 0) {
			offset = fat32->data_start + (pos - 2) * fat32->BPB_SecPerClus + i / SECTOR_SIZE;
			dev->f_ops.seek(dev, offset, 0);
			dev->f_ops.read(dev, (uint8_t *)buf, SECTOR_SIZE);
		}

		/**
		 * 统计短目录项重名个数（只在目录项是长文件名时使用）
		 * 如有重名，则短目录名以以下格式命名: SAMEFI~N.TXT (假设原名为samefilename.txt)
		 * 如果当前数字不够，则将'~'前移
		 */
		if (buf[i % SECTOR_SIZE + 11] != FAT32_ATTR_LONG_NAME) {
			flag = 1;
			for (j = 0; j < 8 && buf[i % SECTOR_SIZE + j] != '~'; j++) {
				if (buf[i % SECTOR_SIZE + j] != name[j]) {
					flag = 0;
					break;
				}
			}
			if (flag) { name_count++; }
		}
		i += 0x20;

		if (i / SECTOR_SIZE / fat32->BPB_SecPerClus) pos = fat_next(inode, pos, 1, 1);
	} while (buf[i]);

	flag = 0;
	pos	 = ((pos - 2) * fat32->BPB_SecPerClus) * SECTOR_SIZE + i;
	int len_without_ext;
	int len_ext;

	for (i = 0; i < len && name[i] != '.'; i++)
		if (islower(name[i])) flag |= 0x1;		// 文件名含小写
		else if (isupper(name[i])) flag |= 0x2; // 文件名含大写

	len_without_ext = i;
	len_ext			= len - len_without_ext - 1;

	if (i >= len) len_ext++; // 无扩展名文件
	for (; i < len; i++)
		if (islower(name[i])) flag |= 0x4;		// 扩展名含小写
		else if (isupper(name[i])) flag |= 0x8; // 扩展名含大写

	// 文件名和扩展名混杂大小写或长度过长则作为长目录项
	if (len > 11 || len_ext > 3 || len_without_ext > 8 || (flag & 0x03) == 0x03 || (flag & 0x0c) == 0x0c) {
		len2	= DIV_ROUND_UP(len, 13);
		int tmp = name_count / 10;
		while (tmp) {
			count++;
			tmp /= 10;
		}
		name_len = min(6, 8 - count - 1);
		tmp		 = 10000000;
		for (i = 0; i < 8; i++) {
			if (i < name_len) filename_short[i] = toupper(name[i] ? name[i] : ' ');
			else if (i == name_len) filename_short[i] = '~';
			else if (i - name_len < count) filename_short[i] = (name_count / tmp % 10 + '0');
			else filename_short[i] = ' ';
			tmp /= 10;
		}
		for (; i < 11; i++) {
			if (i - 8 < len_ext) filename_short[i] = name[len - len_ext + i - 8];
			else filename_short[i] = ' ';
		}
		FAT32_checksum(filename_short, checksum);
		for (i = 0; i < len2; i++) {
			int k;
			if ((pos + i * 0x20) % SECTOR_SIZE == 0) {
				dev->f_ops.seek(dev, offset, 0);
				dev->f_ops.write(dev, (uint8_t *)buf, SECTOR_SIZE);
				offset = fat32->data_start + (pos + i * 0x20) / SECTOR_SIZE;
				dev->f_ops.seek(dev, offset, 0);
				dev->f_ops.read(dev, (uint8_t *)buf, SECTOR_SIZE);
			}
			ldir		   = (struct FAT32_long_dir *)(buf + (pos + i * 0x20) % SECTOR_SIZE);
			ldir->LDIR_Ord = len2 - i;
			if (i == 0) ldir->LDIR_Ord |= 0x40;
			int f = 0;
			for (j = 0; (j % 13) < 5; j++) {
				if (j < len - (len2 - i - 1) * 13) {
					ldir->LDIR_Name1[j] = name[j];
					f					= 1;
				} else {
					if (f) ldir->LDIR_Name1[j] = 0xffff;
					else {
						f					= 0;
						ldir->LDIR_Name1[j] = 0;
					}
				}
			}
			ldir->LDIR_Attr	  = FAT32_ATTR_LONG_NAME;
			ldir->LDIR_Type	  = 0;
			ldir->LDIR_Chksum = checksum;
			for (; (j % 13) < 11; j++) {
				if (j < len - (len2 - i - 1) * 13) {
					ldir->LDIR_Name2[j - 5] = name[j];
					f						= 1;
				} else {
					if (f) ldir->LDIR_Name2[j - 5] = 0xffff;
					else {
						f						= 0;
						ldir->LDIR_Name2[j - 5] = 0;
					}
				}
			}
			ldir->LDIR_FstClusLO = 0;
			for (k = (j % 13); k < 13; k++) {
				if (k < len - (len2 - i - 1) * 13) {
					ldir->LDIR_Name3[k - 11] = name[j];
					f						 = 1;
				} else {
					if (f) ldir->LDIR_Name3[k - 11] = 0xffff;
					else {
						f						 = 0;
						ldir->LDIR_Name3[k - 11] = 0;
					}
				}
			}
		}
		pos += i * 0x20;
		if (pos % SECTOR_SIZE == 0) {
			dev->f_ops.seek(dev, offset, 0);
			dev->f_ops.write(dev, (uint8_t *)buf, SECTOR_SIZE);
			offset = fat32->data_start + pos / SECTOR_SIZE;
			dev->f_ops.seek(dev, offset, 0);
			dev->f_ops.read(dev, (uint8_t *)buf, SECTOR_SIZE);
		}
		for (i = 0; i < 11; i++) {
			buf[pos % SECTOR_SIZE + i] = filename_short[i];
		}
		sdir		   = (struct FAT32_dir *)(buf + pos % SECTOR_SIZE);
		sdir->DIR_Attr = FAT32_ATTR_ARCHIVE;

		int year			  = BCD2BIN(CMOS_READ(CMOS_YEAR)) + 20;
		int month			  = BCD2BIN(CMOS_READ(CMOS_MONTH));
		int day				  = BCD2BIN(CMOS_READ(CMOS_DAY_OF_MONTH));
		sdir->DIR_LastAccDate = sdir->DIR_CrtDate = sdir->DIR_WrtDate = year << 9 | month << 5 | day;
		int hour													  = BCD2BIN(CMOS_READ(CMOS_HOURS));
		int minute													  = BCD2BIN(CMOS_READ(CMOS_MINUTES));
		int second													  = BCD2BIN(CMOS_READ(CMOS_SECONDS));
		sdir->DIR_CrtTime = sdir->DIR_WrtTime = hour << 11 | minute << 5 | second >> 1;
		sdir->DIR_CrtTimeTenth				  = second * 10;

		int file_clus		= fat32_alloc_clus(part, 0, 1);
		sdir->DIR_FstClusHI = file_clus >> 16;
		sdir->DIR_FstClusLO = file_clus & 0xffff;
		sdir->DIR_FileSize	= 0;
	} else {
		for (i = 0, j = 0; i < 8 && name[i] != '.' && j < len; i++, j++)
			buf[pos % SECTOR_SIZE + i] = toupper(name[j]);
		for (; i < 8; i++)
			buf[pos % SECTOR_SIZE + i] = ' ';
		for (j++; i < 11; i++, j++) {
			if (j < len) buf[pos % SECTOR_SIZE + i] = toupper(name[j]);
			else buf[pos % SECTOR_SIZE + i] = ' ';
		}

		sdir		   = (struct FAT32_dir *)(buf + pos % SECTOR_SIZE);
		sdir->DIR_Attr = FAT32_ATTR_ARCHIVE;

		sdir->DIR_NTRes = 0;
		if (flag & 0x01) sdir->DIR_NTRes |= FAT32_BASE_L;
		if (flag & 0x04) sdir->DIR_NTRes |= FAT32_EXT_L;

		int year			  = BCD2BIN(CMOS_READ(CMOS_YEAR)) + 20;
		int month			  = BCD2BIN(CMOS_READ(CMOS_MONTH));
		int day				  = BCD2BIN(CMOS_READ(CMOS_DAY_OF_MONTH));
		sdir->DIR_LastAccDate = sdir->DIR_CrtDate = sdir->DIR_WrtDate = year << 9 | month << 5 | day;
		int hour													  = BCD2BIN(CMOS_READ(CMOS_HOURS));
		int minute													  = BCD2BIN(CMOS_READ(CMOS_MINUTES));
		int second													  = BCD2BIN(CMOS_READ(CMOS_SECONDS));
		sdir->DIR_CrtTime = sdir->DIR_WrtTime = hour << 11 | minute << 5 | second >> 1;
		sdir->DIR_CrtTimeTenth				  = second * 10;

		int file_clus		= fat32_alloc_clus(part, 0, 1);
		sdir->DIR_FstClusHI = file_clus >> 16;
		sdir->DIR_FstClusLO = file_clus & 0xffff;
		sdir->DIR_FileSize	= 0;
	}
	construct_idxtbl(inode);
	file->dir_offset = (pos % (SECTOR_SIZE * fat32->BPB_SecPerClus));
	file->start		 = sdir->DIR_FstClusHI << 16 | sdir->DIR_FstClusLO;
	sdir			 = kmalloc(sizeof(struct FAT32_dir));
	memcpy(sdir, buf + pos % SECTOR_SIZE, sizeof(struct FAT32_dir));
	file->private_data = sdir;
	dev->f_ops.seek(dev, offset, 0);
	dev->f_ops.write(dev, (uint8_t *)buf, SECTOR_SIZE);
	return inode;
}

void FAT32_delete_file(partition_t *part, struct index_node *inode) {
	char			   buf[SECTOR_SIZE];
	unsigned int	   pos, i;
	struct file		  *file	 = inode->fp;
	struct pt_fat32	  *fat32 = part->private_data;
	struct index_node *dev	 = inode->device->inode;
	uint32_t		   offset;
	bool			   f = true;
	offset				 = fat32->data_start + (inode->fp->dir_start - 2) * fat32->BPB_SecPerClus +
			 inode->fp->dir_offset / SECTOR_SIZE;
	dev->f_ops.seek(dev, offset, 0);
	dev->f_ops.read(dev, (uint8_t *)buf, SECTOR_SIZE);
	pos = inode->fp->dir_offset;
	do {
		if (pos % SECTOR_SIZE == 0 && pos >= SECTOR_SIZE) {
			dev->f_ops.seek(dev, offset, 0);
			dev->f_ops.write(dev, (uint8_t *)buf, SECTOR_SIZE);
			offset -= 1;
			dev->f_ops.seek(dev, offset, 0);
			dev->f_ops.read(dev, (uint8_t *)buf, SECTOR_SIZE);
		}
		buf[pos % SECTOR_SIZE] = 0xe5;
		pos -= 0x20;
	} while (buf[pos % SECTOR_SIZE + 11] & FAT32_ATTR_LONG_NAME);
	dev->f_ops.seek(dev, offset, 0);
	dev->f_ops.write(dev, (uint8_t *)buf, SECTOR_SIZE);
	pos = file->start; // 释放文件在文件分配表中对应的簇
	while (f) {
		i = find_member_in_fat(part, pos);
		if (i >= 0x0fffffff) f = 0;
		fat32_free_clus(part, 0, pos);
		pos = i;
	}
}

void FAT32_close(struct index_node *inode) {
	struct file *file;
	file = inode->fp;
	kfree(file->private_data);
	return;
}

struct index_node *FAT32_open(struct _partition_s *part, struct index_node *parent, char *filename) {
	int					   i, j, x;
	struct FAT32_long_dir *ldir;
	struct FAT32_dir	  *sdir;
	struct directory	  *dir	 = parent->dir;
	struct file			  *file	 = kmalloc(sizeof(struct file));
	struct index_node	  *inode = vfs_create(filename, ATTR_FILE, parent);
	struct index_node	  *dev	 = part->device->inode;
	struct pt_fat32		  *fat32 = part->private_data;
	uint32_t			   offset;
	uint8_t				   flag = 0, f = 1;
	unsigned int		   cc;
	uint8_t				   buf[fat32->BPB_SecPerClus * SECTOR_SIZE];
	file->inode	  = inode;
	inode->fp	  = file;
	inode->part	  = parent->part;
	inode->f_ops  = parent->f_ops;
	inode->device = parent->device;
	int len		  = strlen(filename);
	cc			  = dir->start;

	while (f) {
		if (find_member_in_fat(part, cc) >= 0x0fffffff) f = 0;
		offset = fat32->data_start + (cc - 2) * fat32->BPB_SecPerClus;
		dev->f_ops.seek(dev, offset, 0);
		dev->f_ops.read(dev, (uint8_t *)buf, fat32->BPB_SecPerClus * SECTOR_SIZE);
		for (i = 0x00; i < SECTOR_SIZE * fat32->BPB_SecPerClus; i += 0x20) {
			if (buf[i + 11] == FAT32_ATTR_LONG_NAME) continue;
			if (buf[i] == 0xe5 || buf[i] == 0x00 || buf[i] == 0x05) continue;
			ldir = (struct FAT32_long_dir *)(buf + i) - 1;
			sdir = (struct FAT32_dir *)(buf + i);
			j	 = 0;

			// 如果是长目录项
			while (ldir->LDIR_Attr == FAT32_ATTR_LONG_NAME && ldir->LDIR_Ord != 0xe5) {
				for (x = 0; x < 5; x++) {
					if (j > len && ldir->LDIR_Name1[x] == 0xffff) continue;
					else if (j > len || ldir->LDIR_Name1[x] != (unsigned short)(filename[j++])) goto cmp_fail;
				}
				for (x = 0; x < 6; x++) {
					if (j > len && ldir->LDIR_Name2[x] == 0xffff) continue;
					else if (j > len || ldir->LDIR_Name2[x] != (unsigned short)(filename[j++])) goto cmp_fail;
				}
				for (x = 0; x < 2; x++) {
					if (j > len && ldir->LDIR_Name3[x] == 0xffff) continue;
					else if (j > len || ldir->LDIR_Name3[x] != (unsigned short)(filename[j++])) goto cmp_fail;
				}

				if (j >= len) {
					flag = 1;
					goto cmp_success;
				}

				ldir--;
			}

			// 如果是短目录项
			j = 0;
			if (sdir->DIR_Attr & FAT32_ATTR_DIRECTORY) continue;
			for (x = 0; x < 8; x++) {
				if (sdir->DIR_Name[x] == ' ') {
					if (!(sdir->DIR_Attr & FAT32_ATTR_DIRECTORY)) {
						if (filename[j] == '.' || filename[j] == 0) continue;
						else if (sdir->DIR_Name[x] == filename[j]) {
							j++;
							continue;
						} else goto cmp_fail;
					} else goto cmp_fail;
				} else if ((sdir->DIR_Name[x] >= 'A' && sdir->DIR_Name[x] <= 'Z') ||
						   (sdir->DIR_Name[x] >= 'a' && sdir->DIR_Name[x] <= 'z')) {
					if (sdir->DIR_NTRes & FAT32_BASE_L) {
						if (j < len && sdir->DIR_Name[x] + 32 == filename[j]) {
							j++;
							continue;
						} else goto cmp_fail;
					} else {
						if (j < len && sdir->DIR_Name[x] == filename[j]) {
							j++;
							continue;
						} else goto cmp_fail;
					}
				} else if (sdir->DIR_Name[x] >= '0' && sdir->DIR_Name[x] <= '9') {
					if (j < len && sdir->DIR_Name[x] == filename[j]) {
						j++;
						continue;
					} else goto cmp_fail;
				} else {
					j++;
				}
			}
			j++;
			for (x = 0; x < 3; x++) {
				if ((sdir->DIR_Ext[x] >= 'A' && sdir->DIR_Ext[x] <= 'Z') ||
					(sdir->DIR_Ext[x] >= 'a' && sdir->DIR_Ext[x] <= 'z')) {
					if (sdir->DIR_NTRes & FAT32_BASE_L) {
						if (j < len && sdir->DIR_Ext[x] + 32 == filename[j]) {
							j++;
							continue;
						} else goto cmp_fail;
					} else {
						if (j < len && sdir->DIR_Ext[x] == filename[j]) {
							j++;
							continue;
						} else goto cmp_fail;
					}
				} else if (sdir->DIR_Ext[x] >= '0' && sdir->DIR_Ext[x] <= '9') {
					if (j < len && sdir->DIR_Ext[x] == filename[j]) {
						j++;
						continue;
					} else goto cmp_fail;
				} else if (sdir->DIR_Ext[x] == ' ') {
					if (sdir->DIR_Ext[x] == filename[j]) {
						if (sdir->DIR_Ext[x] == filename[j]) {
							j++;
							continue;
						} else goto cmp_fail;
					}
				} else {
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
	if (flag) {
		string_init(&inode->name);
		string_new(&inode->name, filename, len);
		file->dir_start	 = cc;
		file->dir_offset = i;
		file->start = file->cur_pos = (sdir->DIR_FstClusHI << 16 | sdir->DIR_FstClusLO) & 0x0fffffff;
		file->private_data			= kmalloc(sizeof(struct FAT32_dir));
		file->size					= sdir->DIR_FileSize;
		memcpy(file->private_data, sdir, sizeof(struct FAT32_dir));

		inode->create_date.year		  = (sdir->DIR_CrtDate >> 9) - 20;
		inode->create_date.month	  = (sdir->DIR_CrtDate >> 5) & 0x0f;
		inode->create_date.day		  = sdir->DIR_CrtDate & 0x1f;
		inode->write_date.year		  = (sdir->DIR_WrtDate >> 9) - 20;
		inode->write_date.month		  = (sdir->DIR_WrtDate >> 5) & 0x0f;
		inode->write_date.day		  = sdir->DIR_WrtDate & 0x1f;
		inode->last_access_date.year  = (sdir->DIR_LastAccDate >> 9) - 20;
		inode->last_access_date.month = (sdir->DIR_LastAccDate >> 5) & 0x0f;
		inode->last_access_date.day	  = sdir->DIR_LastAccDate & 0x1f;
		inode->create_time.hour		  = sdir->DIR_CrtTime >> 11;
		inode->create_time.minute	  = (sdir->DIR_CrtTime >> 5) & 0x3f;
		inode->create_time.second	  = sdir->DIR_CrtTime & 0x1f;
		inode->write_time.hour		  = sdir->DIR_WrtTime >> 11;
		inode->write_time.minute	  = (sdir->DIR_WrtTime >> 5) & 0x3f;
		inode->write_time.second	  = sdir->DIR_WrtTime & 0x1f;

		construct_idxtbl(inode);
		return inode;
	} else {
		vfs_close(inode);
		kfree(file);
		return NULL;
	}
}

struct index_node *FAT32_find_dir(struct _partition_s *part, struct index_node *parent, char *name) {
	unsigned int	   i, j, cc, x;
	bool			   flag, f;
	uint8_t			  *buf	 = kmalloc(((struct pt_fat32 *)part->private_data)->BPB_SecPerClus * SECTOR_SIZE);
	struct directory  *dir	 = kmalloc(sizeof(struct directory));
	struct pt_fat32	  *fat32 = part->private_data;
	struct index_node *dev	 = part->device->inode;
	struct FAT32_long_dir *ldir;
	struct FAT32_dir	  *sdir;
	int					   len = strlen(name);
	uint32_t			   offset;
	dir->inode			  = kmalloc(sizeof(struct index_node));
	dir->inode->attribute = ATTR_DIR;
	dir->inode->parent	  = parent;
	dir->inode->f_ops	  = parent->f_ops;
	dir->inode->device	  = part->device;
	dir->inode->part	  = part;
	list_init(&dir->inode->childs);
	list_add_tail(&dir->inode->list, &parent->childs);
	string_init(&dir->inode->name);
	string_new(&dir->inode->name, name, len);
	flag = 0;
	f	 = 1;
	cc	 = parent->dir->start;
	while (f) {
		if (find_member_in_fat(part, cc) >= 0x0fffffff) f = 0;
		offset = fat32->data_start + (cc - 2) * fat32->BPB_SecPerClus;
		dev->f_ops.seek(dev, offset, 0);
		dev->f_ops.read(dev, (uint8_t *)buf, fat32->BPB_SecPerClus * SECTOR_SIZE);
		for (i = 0x00; i < SECTOR_SIZE * fat32->BPB_SecPerClus; i += 0x20) {
			if (buf[i + 11] == FAT32_ATTR_LONG_NAME) continue;
			if (buf[i] == 0xe5 || buf[i] == 0x00 || buf[i] == 0x05) continue;
			ldir = (struct FAT32_long_dir *)(buf + i) - 1;
			j	 = 0;

			// 如果是长目录项
			while (ldir->LDIR_Attr == FAT32_ATTR_LONG_NAME && ldir->LDIR_Ord != 0xe5) {
				for (x = 0; x < 5; x++) {
					if (j > len && ldir->LDIR_Name1[x] == 0xffff) continue;
					else if (j > len || ldir->LDIR_Name1[x] != (unsigned short)(name[j++])) goto cmp_fail;
				}
				for (x = 0; x < 6; x++) {
					if (j > len && ldir->LDIR_Name2[x] == 0xffff) continue;
					else if (j > len || ldir->LDIR_Name2[x] != (unsigned short)(name[j++])) goto cmp_fail;
				}
				for (x = 0; x < 2; x++) {
					if (j > len && ldir->LDIR_Name3[x] == 0xffff) continue;
					else if (j > len || ldir->LDIR_Name3[x] != (unsigned short)(name[j++])) goto cmp_fail;
				}

				if (j >= len) {
					flag = true;
					goto cmp_success;
				}

				ldir--;
			}

			// 如果是短目录项
			j	 = 0;
			sdir = (struct FAT32_dir *)(buf + i);
			for (x = 0; x < 11; x++) {
				if (sdir->DIR_Name[x] == ' ') {
					if (sdir->DIR_Attr & FAT32_ATTR_DIRECTORY) {
						if (sdir->DIR_Name[x] == name[j]) {
							j++;
							continue;
						} else {
							goto cmp_fail;
						}
					}
				} else if ((sdir->DIR_Name[x] >= 'A' && sdir->DIR_Name[x] <= 'Z') ||
						   (sdir->DIR_Name[x] >= 'a' && sdir->DIR_Name[x] <= 'z')) {
					if (sdir->DIR_NTRes & FAT32_BASE_L) {
						if (j < len && sdir->DIR_Name[x] + 32 == name[j]) {
							j++;
							continue;
						} else {
							goto cmp_fail;
						}
					} else {
						if (j < len && sdir->DIR_Name[x] == name[j]) {
							j++;
							continue;
						} else {
							goto cmp_fail;
						}
					}

				} else if (j < len && sdir->DIR_Name[x] == name[j]) {
					j++;
					continue;
				} else if (sdir->DIR_Name[x] >= '0' && sdir->DIR_Name[x] <= '9') {
					goto cmp_fail;
				} else {
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
	if (flag) {
		sdir			= (struct FAT32_dir *)(buf + i);
		dir->dir_start	= cc;
		dir->dir_offset = i;
		dir->start		= (sdir->DIR_FstClusHI << 16 | sdir->DIR_FstClusLO);
		dir->inode->dir = dir;

		dir->inode->create_date.year	   = (sdir->DIR_CrtDate >> 9) - 20;
		dir->inode->create_date.month	   = (sdir->DIR_CrtDate >> 5) & 0x0f;
		dir->inode->create_date.day		   = sdir->DIR_CrtDate & 0x1f;
		dir->inode->write_date.year		   = (sdir->DIR_WrtDate >> 9) - 20;
		dir->inode->write_date.month	   = (sdir->DIR_WrtDate >> 5) & 0x0f;
		dir->inode->write_date.day		   = sdir->DIR_WrtDate & 0x1f;
		dir->inode->last_access_date.year  = (sdir->DIR_LastAccDate >> 9) - 20;
		dir->inode->last_access_date.month = (sdir->DIR_LastAccDate >> 5) & 0x0f;
		dir->inode->last_access_date.day   = sdir->DIR_LastAccDate & 0x1f;
		dir->inode->create_time.hour	   = sdir->DIR_CrtTime >> 11;
		dir->inode->create_time.minute	   = (sdir->DIR_CrtTime >> 5) & 0x3f;
		dir->inode->create_time.second	   = sdir->DIR_CrtTime & 0x1f;
		dir->inode->write_time.hour		   = sdir->DIR_WrtTime >> 11;
		dir->inode->write_time.minute	   = (sdir->DIR_WrtTime >> 5) & 0x3f;
		dir->inode->write_time.second	   = sdir->DIR_WrtTime & 0x1f;
		kfree(buf);
		return dir->inode;
	} else {
		vfs_close(dir->inode);
		kfree(dir);
		kfree(buf);
		return NULL;
	}
}

void construct_idxtbl(struct index_node *inode) {
	int i;

	struct pt_fat32 *fat32 = inode->part->private_data;
	int				 cnt   = inode->fp->size / SECTOR_SIZE / fat32->BPB_SecPerClus;
	inode->fp->index_table = kmalloc(cnt * sizeof(uint32_t));

	inode->fp->index_table[0] = inode->fp->start;
	for (i = 1; i < cnt; i++) {
		inode->fp->index_table[i] = find_member_in_fat(inode->part, inode->fp->index_table[i - 1]);
	}
}

int fat32_alloc_clus(partition_t *part, int last_clus, int first) {
	int				   i, j;
	struct pt_fat32	  *fat32  = part->private_data;
	uint32_t		   offset = fat32->fat_start;
	struct index_node *dev	  = part->device->inode;

	i = 3;
	if (DIV_ROUND_UP(i, 128) != fat32->buffer_pos) {
		dev->f_ops.seek(dev, offset, 0);
		dev->f_ops.read(dev, (uint8_t *)fat32->fat_buffer, SECTOR_SIZE);
	}
	while (fat32->fat_buffer[i % 128]) {
		if (i % 128 == 0) {
			offset = fat32->fat_start + (i / 128);
			dev->f_ops.seek(dev, offset, 0);
			dev->f_ops.read(dev, (uint8_t *)fat32->fat_buffer, SECTOR_SIZE);
		}
		i++;
	}
	for (j = 0; j < fat32->BPB_NumFATs; j++) {
		offset = fat32->fat_start + j * fat32->BPB_FATSz32 + (i / 128);
		dev->f_ops.seek(dev, offset, 0);
		dev->f_ops.read(dev, (uint8_t *)fat32->fat_buffer, SECTOR_SIZE);
		fat32->fat_buffer[i % 128] = 0x0fffffff;
		dev->f_ops.write(dev, (uint8_t *)fat32->fat_buffer, SECTOR_SIZE);
		if (!first) {
			offset = fat32->fat_start + j * fat32->BPB_FATSz32 + (last_clus / 128);
			dev->f_ops.seek(dev, offset, 0);
			dev->f_ops.read(dev, (uint8_t *)fat32->fat_buffer, SECTOR_SIZE);
			fat32->fat_buffer[last_clus % 128] = i;
			dev->f_ops.write(dev, (uint8_t *)fat32->fat_buffer, SECTOR_SIZE);
		}
	}
	return i;
}

int fat32_free_clus(partition_t *part, int last_clus, int clus) {
	int				 buf1[128], buf2[128];
	int				 j;
	struct pt_fat32 *fat32 = part->private_data;
	uint32_t		 offset;
	if (last_clus < 3 && clus < 3) return -1;
	for (j = 0; j < fat32->BPB_NumFATs; j++) {
		if (last_clus > 2 && clus > 2) {
			offset = fat32->fat_start + j * fat32->BPB_FATSz32 + last_clus / 128;
			dev->f_ops.seek(dev, offset, 0);
			dev->f_ops.read(dev, (uint8_t *)buf1, SECTOR_SIZE);
			if (clus / 128 != last_clus / 128) {
				offset = fat32->fat_start + j * fat32->BPB_FATSz32 + clus / 128;
				dev->f_ops.seek(dev, offset, 0);
				dev->f_ops.read(dev, (uint8_t *)buf2, SECTOR_SIZE);
				buf1[last_clus % 128] = buf2[clus % 128];
				buf2[clus % 128]	  = 0x00;
				dev->f_ops.write(dev, (uint8_t *)buf2, SECTOR_SIZE);
			} else {
				buf1[last_clus % 128] = buf1[clus % 128];
				buf1[clus % 128]	  = 0x00;
			}
			offset = fat32->fat_start + j * fat32->BPB_FATSz32 + last_clus / 128;
			dev->f_ops.seek(dev, offset, 0);
			dev->f_ops.write(dev, (uint8_t *)buf1, SECTOR_SIZE);
		} else if (clus > 2) {
			offset = fat32->fat_start + j * fat32->BPB_FATSz32 + clus / 128;
			dev->f_ops.seek(dev, offset, 0);
			dev->f_ops.read(dev, (uint8_t *)buf1, SECTOR_SIZE);
			buf1[clus % 128] = 0x00;
			dev->f_ops.write(dev, (uint8_t *)buf1, SECTOR_SIZE);
		}
	}
	return 0;
}

unsigned int find_member_in_fat(struct _partition_s *part, int i) {
	struct index_node *dev = part->device->inode;
	unsigned int	   next_clus;
	struct pt_fat32	  *fat32  = part->private_data;
	uint32_t		   offset = part->start_lba + fat32->BPB_RevdSecCnt + (i / 128);
	if (DIV_ROUND_UP(i, 128) != fat32->buffer_pos) {
		dev->f_ops.seek(dev, offset, 0);
		dev->f_ops.read(dev, (uint8_t *)fat32->fat_buffer, SECTOR_SIZE);
	}
	next_clus = fat32->fat_buffer[i % 128];
	return next_clus;
}

uint32_t fat_next(struct index_node *inode, uint32_t clus, int next, int alloc) {
	uint32_t	 i, tmp, c = clus;
	partition_t *part = inode->part;

	if (inode->fp->cur_pos - inode->fp->start < clus) { c = c - (inode->fp->cur_pos - inode->fp->start); }

	for (i = 0; i < next; i++) {
		tmp = find_member_in_fat(part, c);
		if (tmp < 0x0ffffff8) c = tmp;
		else {
			if (alloc) { c = fat32_alloc_clus(part, c, 0); }
			break;
		}
	}

	inode->fp->cur_pos = c;
	return c;
}

static __init void fat32_fs_entry(void) {
	fs_register("FAT32", &fat32_fs_ops);
}

fs_initcall(fat32_fs_entry);
