#ifndef _FAT_NAME_H
#define _FAT_NAME_H

#include "fs/fs.h"
#include "multiple_return.h"

#define MAX_LONGNAME 256

typedef struct ShortName {
	char base[8];
	char ext[3];
} __attribute__((packed)) ShortName;

typedef enum NameType {
	SHORT_NAME,
	LONG_NAME,
	INVALID_NAME_TYPE,
} NameType;

enum FatType;
struct FatInfo;
struct FatDirEntry;
struct ShortDir;
struct LongDir;
int		 check_name_caps(uint8_t *name, int len);
bool	 is_available_short_name_char(uint8_t c);
bool	 check_short_name(uint8_t *text, int len, int dot);
NameType check_name(enum FatType type, uint8_t *text, int len);
FsResult short_name_new(string_t name, ShortName *short_name);
uint8_t	 fat_checksum(ShortName *short_name);
int		 read_long_name(struct LongDir *long_dir, uint16_t *utf16_name);
string_t read_short_name(struct ShortDir *short_dir);
FsResult long_name2short_name(
	struct FatInfo *fat_info, struct FatDirEntry *parent, string_t long_name,
	bool is_directory, ShortName *short_name);

void fat_utf16_to_utf8(uint16_t *utf16, int utf16_length, string_t *utf8);
void fat_utf8_to_utf16(
	uint8_t *utf8, uint16_t *utf16, DEF_MRET(int, utf8_length),
	DEF_MRET(int, utf16_length));
int fat_utf16_count_utf8_length(uint16_t *utf16, int utf16_length);

#endif