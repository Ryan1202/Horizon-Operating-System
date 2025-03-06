#include "include/name.h"
#include "include/dir.h"
#include "include/fat.h"
#include "math.h"
#include "multiple_return.h"
#include <fs/fs.h>
#include <stdint.h>
#include <string.h>

/**
 * @brief 确认文件名与扩展名的大小写状况
 *
 * @param name 文件名
 * @param len 文件名长度
 */
int check_name_caps(uint8_t *name, int len) {
	int ret	 = 0;
	int flag = 0;
	for (int i = 0; i < len; i++) {
		if (name[i] == '.') {
			flag = 2;
			continue;
		}
		if ('a' <= name[i] && name[i] <= 'z') {
			ret |= 1 << flag;
		} else if ('A' <= name[i] && name[i] <= 'Z') {
			ret |= 2 << flag;
		}
	}
	return ret;
}

bool is_available_short_name_char(uint8_t c) {
	// 非ASCII字符
	if (c >= 0x80) { return false; }
	// '^', '_', '`', a-z, '{'
	if ('^' <= c && c <= '{') { return true; }
	// '@', A-Z
	if ('@' <= c && c <= 'Z') { return true; }
	// 0-9
	if ('0' <= c && c <= '9') { return true; }
	// ' ', '!', '#', $', '%', '&', '\'' ,'(', ')'
	if (' ' <= c && c <= ')' && c != '\"') { return true; }
	// '-',  '}', '~'
	if (c == '-' || c == '}' || c == '~') { return true; }
	return false;
}

bool is_available_long_name_char(uint8_t c) {
	// '@', A-Z, '[', ']',
	if ('@' <= c && c <= ']') { return true; }
	// '^', '_', '`', a-z, '{'
	if ('^' <= c && c <= '{') { return true; }
	// 0-9
	if ('0' <= c && c <= '9') { return true; }
	// '+', ',', '-', '.'
	if ('+' <= c && c <= '.') { return true; }
	// ' ', '!', '#', '$', '%', '&', '\'', '(', ')'
	if (' ' <= c && c <= ')' && c != '\"') { return true; }
	// ';', '}', '~'
	if (c == ';' || c == '}' || c == '~') { return true; }
	return false;
}

/**
 * @brief 确认文件名是否能直接作为短文件名
 *
 * @param name 文件名
 */
bool check_short_name(uint8_t *text, int len, int dot) {
	// 如果文件名和扩展名的长度超过了8.3
	if (dot > 8 || dot < len - 3) return false;

	// 如果存在大小写混合
	int caps = check_name_caps(text, len);
	if ((caps & 0x03) || (caps & 0x0c) == 0x0c) return false;

	for (int i = 0; i < len; i++) {
		if (i == dot) continue;
		if (!is_available_short_name_char(text[i])) return false;
	}
	return true;
}

bool check_long_name(uint8_t *text, int len) {
	for (int i = 0; i < len; i++) {
		if (text[i] == 0) break;
		if (text[i] >= 0x80) {
			// 跳过UTF-8字符
			if ((text[i] & 0xe0) == 0xc0) i++;
			if ((text[i] & 0xf0) == 0xe0) i += 2;
			if ((text[i] & 0xf8) == 0xf0) i += 3;
		} else if (!is_available_long_name_char(text[i])) return false;
	}
	return true;
}

NameType check_name(FatType type, uint8_t *text, int len) {
	int dot = 0;
	while (text[dot] != '.' && dot < len) {
		dot++;
	}
	if (!check_short_name(text, len, dot)) {
		if (type == FAT_TYPE_FAT32 && check_long_name(text, len)) {
			return LONG_NAME;
		}
		return INVALID_NAME_TYPE;
	}
	return SHORT_NAME;
}

FsResult short_name_new(string_t name, ShortName *short_name) {
	int		 len  = name.length;
	uint8_t *text = (uint8_t *)name.text;

	int dot = len - 1;
	while (text[dot] != '.' && dot >= 0) {
		dot--;
	}

	if (!check_short_name(text, len, dot)) {
		return FS_ERROR_INVALID_PATH_OR_NAME;
	}

	for (int i = 0; i < 8; i++) {
		if (i < dot) {
			if ('a' <= text[i] && text[i] <= 'z') {
				short_name->base[i] = text[i] - 32;
			} else {
				short_name->base[i] = text[i];
			}
		} else {
			short_name->base[i] = ' ';
		}
	}
	for (int i = 0; i < 3; i++) {
		if (i < len - dot) {
			char c = text[dot + 1 + i];
			if ('a' <= c && c <= 'z') {
				short_name->ext[i] = c - 32;
			} else {
				short_name->ext[i] = c;
			}
		} else {
			short_name->ext[i] = ' ';
		}
	}

	return FS_OK;
}

uint8_t fat_checksum(ShortName *short_name) {
	uint8_t *name = (uint8_t *)short_name;

	uint8_t sum = 0;
	for (int i = 11; i != 0; i--) {
		sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + *name++;
	}
	return sum;
}

int read_long_name(LongDir *long_dir, uint16_t *utf16_name) {
	int x;
	int pos = 0;
	for (x = 0; x < 5; x++) {
		if (long_dir->name1[x] == 0xffff || long_dir->name1[x] == 0x00) break;
		else utf16_name[pos++] = long_dir->name1[x];
	}
	for (x = 0; x < 6; x++) {
		if (long_dir->name2[x] == 0xffff || long_dir->name2[x] == 0x00) break;
		else utf16_name[pos++] = long_dir->name2[x];
	}
	for (x = 0; x < 2; x++) {
		if (long_dir->name3[x] == 0xffff || long_dir->name3[x] == 0x00) break;
		else utf16_name[pos++] = long_dir->name3[x];
	}
	return pos++;
}

string_t read_short_name(ShortDir *short_dir) {
	int		 j = 0;
	int		 x;
	string_t name;
	name.text		= kmalloc(13);
	name.max_length = 13;

	for (x = 0; x < 8; x++) {
		char c = short_dir->name.base[x];
		if (c != ' ') {
			if ('A' <= c && c <= 'Z') {
				name.text[j++] =
					(short_dir->nt_res & FAT32_BASE_L) ? c + 32 : c;
			} else {
				name.text[j++] = c;
			}
		}
	}
	if (short_dir->name.ext[0] == ' ' && short_dir->name.ext[1] == ' ' &&
		short_dir->name.ext[2] == ' ') {
		name.length = j;
		return name;
	}
	name.text[j++] = '.';
	for (x = 0; x < 3; x++) {
		char c = short_dir->name.ext[x];
		if (c == ' ') {
			if ('A' <= c && c <= 'Z') {
				name.text[j++] = (short_dir->nt_res & FAT32_EXT_L) ? c + 32 : c;
			} else {
				name.text[j++] = c;
			}
		}
	}
	return name;
}

FsResult long_name2short_name(
	FatInfo *fat_info, FatDirEntry *parent, string_t long_name,
	bool is_directory, ShortName *short_name) {
	if (short_name == NULL) { return FS_ERROR_OUT_OF_MEMORY; }

	int		 len  = long_name.length;
	uint8_t *text = (uint8_t *)long_name.text;
	bool	 flag = true;

	int dot = len - 1;
	while (text[dot] != '.' && dot >= 0) {
		dot--;
	}
	int	 base_name_len = 0;
	int	 ext_name_len  = 0;
	char tmp_name[12]  = "            ";

	int i, j;
	for (i = 0, j = 0; i < 8; i++) {
		if (i < dot) {
			if (is_available_short_name_char(text[i])) {
				if ('a' <= text[i] && text[i] <= 'z') {
					short_name->base[j] = text[i] - 32;
				} else {
					short_name->base[j] = text[i];
				}
			} else if (text[i] >= 0x80) { // 跳过UTF-8字符
				flag				= false;
				short_name->base[j] = '_';
			} else {
				continue;
			}
			tmp_name[j] = short_name->base[j];
			base_name_len++;
		} else {
			short_name->base[j] = ' ';
		}
		j++;
	}

	tmp_name[j] = '.';
	int k		= j + 1;

	for (i = 0, j = 0; i < 3; i++) {
		if (i < dot) {
			uint8_t c = text[dot + 1 + i];
			if (is_available_short_name_char(c)) {
				if ('a' <= c && c <= 'z') {
					short_name->ext[j] = c - 32;
				} else {
					short_name->ext[j] = c;
				}
			} else if (c >= 0x80) {
				flag			   = false;
				short_name->ext[j] = '_';
			} else {
				continue;
			}
		} else {
			short_name->ext[j] = ' ';
		}
		tmp_name[k + j] = short_name->ext[j];
		j++;
		ext_name_len++;
	}

	string_t name;
	name.text		= tmp_name;
	name.length		= base_name_len + ext_name_len + 1;
	name.max_length = name.length;

	if (!flag && check_short_name((uint8_t *)short_name, 11, dot)) {
		// 完全满足短文件名条件，检查是否重名
		return search_dir(fat_info, parent, name, is_directory, NULL, 0);
	} else {
		for (int n = 1; n < 999999; n++) {
			// 统计位数
			int x = n;
			int i = MIN(base_name_len, 7);
			while (x != 0 && i > 1) {
				char c				= (x % 10) + '0';
				short_name->base[i] = c;
				x /= 10;
				i--;
			}
			short_name->base[i] = '~';

			// 检查是否重名
			FsResult result =
				search_dir(fat_info, parent, name, is_directory, NULL, 0);
			if (result == FS_ERROR_CANNOT_FIND) {
				// 找不到说明可用
				return FS_OK;
			}
			// 找得到说明重名，继续循环
		}
	}

	return FS_ERROR_INVALID_PATH_OR_NAME;
}

uint32_t fat_utf16_to_unicode(uint16_t **utf16) {
	uint16_t *buf	  = *utf16;
	uint32_t  unicode = buf[0];
	if (((buf[0] & 0xfc00) == 0xdc00) && ((buf[1] & 0xfc00) == 0xd800)) {
		unicode = (buf[1] & 0x03ff) << 16;
		unicode |= buf[0] & 0x03ff;
		(*utf16)++;
	}
	(*utf16)++;
	return unicode;
}

uint32_t fat_utf8_to_unicode(uint8_t **utf8) {
	uint32_t unicode = 0;
	uint8_t *buf	 = *utf8;
	if (buf[0] < 0x80) {
		unicode = buf[0];
		(*utf8)++;
	} else if (buf[0] < 0xe0) {
		unicode = (buf[0] & 0x1f) << 6;
		unicode |= buf[1] & 0x3f;
		(*utf8) += 2;
	} else if (buf[0] < 0xf0) {
		unicode = (buf[0] & 0x0f) << 12;
		unicode |= (buf[1] & 0x3f) << 6;
		unicode |= buf[2] & 0x3f;
		(*utf8) += 3;
	} else {
		unicode = (buf[0] & 0x07) << 18;
		unicode |= (buf[1] & 0x3f) << 12;
		unicode |= (buf[2] & 0x3f) << 6;
		unicode |= buf[3] & 0x3f;
		(*utf8) += 4;
	}
	return unicode;
}

void fat_unicode_to_utf16(uint32_t unicode, uint16_t **utf16) {
	uint16_t *buf = *utf16;
	if (unicode < 0x10000) {
		buf[0] = unicode;
	} else {
		buf[0] = 0xd800 | ((unicode >> 10) & 0x03ff);
		buf[1] = 0xdc00 | (unicode & 0x03ff);
		(*utf16)++;
	}
	(*utf16)++;
}

int fat_utf16_count_utf8_length(uint16_t *utf16, int utf16_length) {
	int		  utf8_length = 0;
	uint16_t *p			  = utf16;
	for (int i = 0; i < utf16_length; i++) {
		uint32_t unicode = fat_utf16_to_unicode(&p);

		if (unicode < 0x80) {
			utf8_length += 1;
		} else if (unicode < 0x800) {
			utf8_length += 2;
		} else if (unicode < 0x10000) {
			utf8_length += 3;
		} else {
			utf8_length += 4;
		}
	}
	return utf8_length;
}

void fat_utf16_to_utf8(uint16_t *utf16, int utf16_length, string_t *utf8) {
	int utf8_length	 = fat_utf16_count_utf8_length(utf16, utf16_length);
	utf8->text		 = kmalloc(utf8_length + 1);
	utf8->length	 = utf8_length;
	utf8->max_length = utf8_length + 1;

	uint16_t *p = utf16;
	uint8_t	 *q = (uint8_t *)utf8->text;
	for (int i = 0; i < utf16_length; i++) {
		uint32_t unicode = fat_utf16_to_unicode(&p);

		if (unicode < 0x80) {
			q[0] = unicode;
			q++;
		} else if (unicode < 0x800) {
			q[0] = 0xc0 | (unicode >> 6);
			q[1] = 0x80 | (unicode & 0x3f);
			q += 2;
		} else if (unicode < 0x10000) {
			q[0] = 0xe0 | (unicode >> 12);
			q[1] = 0x80 | ((unicode >> 6) & 0x3f);
			q[2] = 0x80 | (unicode & 0x3f);
			q += 3;
		} else {
			q[0] = 0xf0 | (unicode >> 18);
			q[1] = 0x80 | ((unicode >> 12) & 0x3f);
			q[2] = 0x80 | ((unicode >> 6) & 0x3f);
			q[3] = 0x80 | (unicode & 0x3f);
			q += 4;
		}
	}
	return;
}

void fat_utf8_to_utf16(
	uint8_t *utf8, uint16_t *utf16, DEF_MRET(int, utf8_length),
	DEF_MRET(int, utf16_length)) {
	uint32_t  unicode;
	uint8_t	 *p = utf8;
	uint16_t *q = utf16;
	while (*p != '\0' || *(p + 1) != '\0') {
		unicode = fat_utf8_to_unicode(&p);
		fat_unicode_to_utf16(unicode, &q);
	}
	MRET(utf8_length)  = p - utf8;
	MRET(utf16_length) = q - utf16;
	return;
}
