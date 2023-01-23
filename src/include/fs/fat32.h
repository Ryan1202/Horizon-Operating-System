#ifndef FAT32_H
#define FAT32_H

#include <fs/fs.h>
#include <fs/vfs.h>
#include <string.h>

#define isFAT32(data) strncmp(((struct pt_fat32 *)data)->BS_FilSysType, "FAT32", 5) == 0
#define fs_FAT32(fs)  ((struct pt_fat32 *)(fs))
#define FAT32_checksum(string, checksum)                                          \
    {                                                                             \
        int i;                                                                    \
        for (i = 0; i < 11; i++) {                                                \
            checksum = ((checksum & 1) ? 0x80 : 0) * (checksum >> 1) * string[i]; \
        }                                                                         \
    }

#define FAT32_ATTR_READ_ONLY 0x01
#define FAT32_ATTR_HIDDEN    0x02
#define FAT32_ATTR_SYSTEM    0x04
#define FAT32_ATTR_VOLUME_ID 0x08
#define FAT32_ATTR_DIRECTORY 0x10
#define FAT32_ATTR_ARCHIVE   0x20
#define FAT32_ATTR_LONG_NAME 0x0f

#define FAT32_BASE_L 0x08
#define FAT32_EXT_L  0x10

struct FS_Info {
    unsigned int  FSI_LeadSig;
    unsigned char FSI_Reserved1[480];
    unsigned int  FSI_StrucSig;
    unsigned int  FSI_Free_Count;
    unsigned int  FSI_Nxt_Free;
    unsigned char FSI_Reserved2[12];
    unsigned int  FSI_TrailSig;
} __attribute__((packed));

struct pt_fat32 {
    unsigned char  BS_jmpBoot[3];
    unsigned char  BS_OEMName[8];
    unsigned short BPB_BytesPerSec;
    unsigned char  BPB_SecPerClus;
    unsigned short BPB_RevdSecCnt;
    unsigned char  BPB_NumFATs;
    unsigned short BPB_RootEntCnt;
    unsigned short BPB_TotSec16;
    unsigned char  BPB_Media;
    unsigned short BPB_FATSz16;
    unsigned short BPB_SecPerTrk;
    unsigned short BPB_NumHeads;
    unsigned int   BPB_HiddSec;
    unsigned int   BPB_TotSec32;
    unsigned int   BPB_FATSz32;
    unsigned short BPB_ExtFlags;
    unsigned short BPB_FSVer;
    unsigned int   BPB_RootClus;
    unsigned short BPB_FSInfo;
    unsigned short BPB_BkBootSec;
    unsigned char  BPB_Reserved[12];
    unsigned char  BS_DrvNum;
    unsigned char  BS_Reserved1;
    unsigned char  BS_BootSig;
    unsigned int   BS_VolID;
    unsigned char  BS_VolLab[11];
    unsigned char  BS_FilSysType[8];
    unsigned char  BootCode[420];
    unsigned short Signature;

    struct FS_Info FSInfo;

    unsigned int  fat_start;
    unsigned int *fat_buffer, buffer_pos;
    unsigned int  data_start;

} __attribute__((packed));

struct FAT_clus_list {
    struct FAT_clus_list *prev;
    unsigned int          next_clus;
    struct FAT_clus_list *next;
};

struct FAT32_dir {
    unsigned char  DIR_Name[8];
    unsigned char  DIR_Ext[3];
    unsigned char  DIR_Attr;
    unsigned char  DIR_NTRes;
    unsigned char  DIR_CrtTimeTenth;
    unsigned short DIR_CrtTime;
    unsigned short DIR_CrtDate;
    unsigned short DIR_LastAccDate;
    unsigned short DIR_FstClusHI;
    unsigned short DIR_WrtTime;
    unsigned short DIR_WrtDate;
    unsigned short DIR_FstClusLO;
    unsigned int   DIR_FileSize;
} __attribute__((packed));

struct FAT32_long_dir {
    unsigned char  LDIR_Ord;
    unsigned short LDIR_Name1[5];
    unsigned char  LDIR_Attr;
    unsigned char  LDIR_Type;
    unsigned char  LDIR_Chksum;
    unsigned short LDIR_Name2[6];
    unsigned short LDIR_FstClusLO;
    unsigned short LDIR_Name3[2];
} __attribute__((packed));

status_t           fat32_check(struct partition_table *pt);
status_t           fat32_readsuperblock(partition_t *partition, char *data);
struct index_node *FAT32_open(struct _partition_s *part, struct index_node *parent, char *filename);
void               FAT32_close(struct index_node *inode);
int                FAT32_read(struct index_node *inode, uint8_t *buffer, uint32_t length);
int                FAT32_write(struct index_node *inode, uint8_t *buffer, uint32_t length);
struct index_node *FAT32_create_file(partition_t *part, struct index_node *parent, char *name, int len);
void               FAT32_delete_file(partition_t *part, struct index_node *inode);
struct index_node *FAT32_find_dir(struct _partition_s *part, struct index_node *parent, char *name);
int                fat32_alloc_clus(partition_t *part, int last_clus, int first);
int                fat32_free_clus(partition_t *part, int last_clus, int clus);
unsigned int       find_member_in_fat(struct _partition_s *part, int i);
uint32_t           fat_next(struct index_node *inode, uint32_t clus, int next, int alloc);

#endif