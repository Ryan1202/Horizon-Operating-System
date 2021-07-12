#include <drivers/disk.h>
#include <kernel/memory.h>
#include <string.h>
#include <stddef.h>

struct disk_manager dm = {0};

struct disk *add_disk(char *type)
{
	struct disks *disks, *last_disk;
	if (dm.disks == NULL)
	{
		disks = kmalloc(sizeof(struct disks));
		memset(disks, 0, sizeof(struct disks));
		disks->type = type;
		disks->disk.disks = disks;
		disks->disk.part_cnt = 0;
		disks->prev = disks;
		disks->next = disks;
		dm.disk_num++;
		dm.disks = disks;
	}
	else
	{
		last_disk = find_member_in_disks(dm.disk_num - 1);
		disks = kmalloc(sizeof(struct disks));
		memset(disks, 0, sizeof(struct disks));
		disks->type = type;
		disks->disk.disks = disks;
		disks->disk.part_cnt = 0;
		disks->prev = last_disk;
		disks->next = disks;
		last_disk->next = disks;
		dm.disk_num++;
	}
	return &disks->disk;
}

void delete_disk(struct disk *disk)
{
	disk->disks->prev->next = disk->disks->next;
	if (disk->disks->next != NULL) disk->disks->next->prev = disk->disks->prev;
	kfree(disk->disks);
	disk->disk_delete(disk);
}

struct disks *find_member_in_disks(int i)
{
	int j;
	struct disks *disks = dm.disks;
	if (i < dm.disk_num)
	{
		for(j = 0; j < i; j++)
		{
			if (disks == NULL)
			{
				return NULL;
			}
			disks = disks->next;
		}
		return disks;
	}
	return NULL;
}