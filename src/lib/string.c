/*
 * file:		lib/string.c
 * auther:	    Jason Hu
 * time:		2019/6/2
 * copyright:	(C) 2018-2020 by Book OS developers. All rights reserved.
 */

#include <kernel/memory.h>
#include <string.h>

/**
 * @brief 初始化字符串
 *
 * @param string 要初始化的字符串
 */
void string_init(string_t *string) {
	string->length	   = 0;
	string->max_length = STRING_MAX_LEN;
	string->text	   = NULL;
}

/**
 * @brief 新建字符串
 *
 * @param string 要新建的字符串
 * @param text 字符串
 * @param max_len 最大长度
 * @return int 0:成功 1:失败
 */
int string_new(string_t *string, char *text, unsigned int max_len) {
	if (string == NULL || text == NULL || max_len < 1) { return -1; }
	string->text = kmalloc(max_len);

	if (string->text == NULL) { return -1; }
	if (max_len >= STRING_MAX_LEN) { max_len = STRING_MAX_LEN - 1; }
	string->length = strlen(text);
	if (string->length > max_len) { string->length = max_len; }
	string->max_length = max_len;
	memset(string->text, 0, max_len);
	memcpy(string->text, text, string->length);
	string->text[string->length] = '\0';
	return 0;
}

/**
 * @brief 删除字符串
 *
 * @param string 要删除的字符串
 */
void string_del(string_t *string) {
	if (string->text) {
		kfree(string->text);
		string->text = NULL;
	}
	string->length	   = 0;
	string->max_length = STRING_MAX_LEN;
}

/**
 * @brief 复制字符串
 *
 * @param dest 目的字符串
 * @param src 源字符串
 * @return int 0:成功 1:失败
 */
int string_cpy(string_t *dest, string_t *src) {
	string_del(dest);
	return string_new(dest, src->text, src->max_length);
}

int strncmp(const char *s1, const char *s2, int n) {
	if (!n) return (0);

	while (--n && *s1 && *s1 == *s2) {
		s1++;
		s2++;
	}
	return (*s1 - *s2);
}

char *itoa(char **ps, int val, int base) {
	int m = val % base;
	int q = val / base;
	if (q) { itoa(ps, q, base); }
	*(*ps)++ = (m < 10) ? (m + '0') : (m - 10 + 'A');

	return *ps;
}

int atoi(const char *src) {
	int	 s		  = 0;
	char is_minus = 0;

	while (*src == ' ') {
		src++;
	}

	if (*src == '+' || *src == '-') {
		if (*src == '-') { is_minus = 1; }
		src++;
	} else if (*src < '0' || *src > '9') {
		s = 2147483647;
		return s;
	}

	while (*src != '\0' && *src >= '0' && *src <= '9') {
		s = s * 10 + *src - '0';
		src++;
	}
	return s * (is_minus ? -1 : 1);
}

void *memset(void *src, uint8_t value, uint32_t size) {
	uint8_t *s = (uint8_t *)src;
	while (size > 0) {
		*s = value;
		s++;
		--size;
	}
	return src;
}

void *memset16(void *src, uint16_t value, uint32_t size) {
	uint16_t *s = (uint16_t *)src;
	while (size-- > 0) {
		*s = value;
		s++;
	}
	return src;
}

void *memset32(void *src, uint32_t value, uint32_t size) {
	uint32_t *s = (uint32_t *)src;
	while (size-- > 0) {
		*s = value;
		s++;
	}
	return src;
}

void memcpy(void *dst_, const void *src_, uint32_t size) {

	uint8_t		*dst = dst_;
	const uint8_t *src = src_;
	while (size-- > 0) {
		*dst = *src;
		dst++;
		src++;
	}
}

char *strcpy(char *dst_, const char *src_) {

	char *r = dst_;
	while ((*dst_ = *src_)) {
		dst_++;
		src_++;
	}
	return r;
}

char *strncpy(char *dst_, char *src_, int n) {

	char *r = dst_;
	while ((*dst_ = *src_) && n > 0) {
		dst_++;
		src_++;
		n--;
	}
	return r;
}

uint32_t strlen(const char *str) {

	const char *p = str;
	while (*p)
		p++;
	return (p - str);
}

int8_t strcmp(const char *a, const char *b) {

	while (*a != 0 && *a == *b) {
		a++;
		b++;
	}
	return *a < *b ? -1 : *a > *b;
}

/**
 * strcoll - 需要根据本地语言做处理，为了简便，直接调用strcmp
 *
 */
int strcoll(const char *str1, const char *str2) {
	return strcmp(str1, str2);
}

int memcmp(const void *s1, const void *s2, int n) {
	if ((s1 == 0) || (s2 == 0)) { /* for robustness */
		return (s1 - s2);
	}

	const char *p1 = (const char *)s1;
	const char *p2 = (const char *)s2;
	int			i;
	for (i = 0; i < n; i++, p1++, p2++) {
		if (*p1 != *p2) { return (*p1 - *p2); }
	}
	return 0;
}
char *strrchr(char *str, int c) {

	char *ret = NULL;
	while (*str) {
		if (*str == (char)c) ret = (char *)str;
		str++;
	}
	if ((char)c == *str) ret = (char *)str;

	return ret;
}

char *strcat(char *strDest, const char *strSrc) {
	char *address = strDest;
	while (*strDest) {
		strDest++;
	}
	while ((*strDest = *strSrc)) {
		strDest++;
		strSrc++;
	}
	return (char *)address;
}

int strpos(char *str, char ch) {
	int i	  = 0;
	int flags = 0;
	while (*str) {
		if (*str == ch) {
			flags = 1; // find ch
			break;
		}
		i++;
		str++;
	}
	if (flags) {
		return i;
	} else {
		return -1; // str over but not found
	}
}

char *strncat(char *dst, const char *src, int n) {
	char *ret = dst;
	while (*dst != '\0') {
		dst++;
	}
	while (n && (*dst++ = *src++) != '\0') {
		n--;
	}
	*dst = '\0';
	return ret;
}

char *strchr(const char *s, int c) {
	if (s == NULL) { return NULL; }

	while (*s != '\0') {
		if (*s == (char)c) { return (char *)s; }
		s++;
	}
	return NULL;
}

void *memmove(void *dst, const void *src, uint32_t count) {
	char *tmpdst = (char *)dst;
	char *tmpsrc = (char *)src;

	if (tmpdst <= tmpsrc || tmpdst >= tmpsrc + count) {
		while (count--) {
			*tmpdst++ = *tmpsrc++;
		}
	} else {
		tmpdst = tmpdst + count - 1;
		tmpsrc = tmpsrc + count - 1;
		while (count--) {
			*tmpdst-- = *tmpsrc--;
		}
	}

	return dst;
}

char *itoa16_align(char *str, int num) {
	char *p = str;
	char  ch;
	int	  i;
	//为0
	if (num == 0) {
		*p++ = '0';
	} else {						   // 4位4位的分解出来
		for (i = 28; i >= 0; i -= 4) { //从最高得4位开始
			ch = (num >> i) & 0xF;	   //取得4位
			ch += '0';				   //大于0就+'0'变成ASICA的数字
			if (ch > '9') {			   //大于9就加上7变成ASICA的字母
				ch += 7;
			}
			*p++ = ch; //指针地址上记录下来。
		}
	}
	*p = 0; //最后在指针地址后加个0用于字符串结束
	return str;
}

/**
 * strmet - 复制直到遇到某个字符串
 * @src: 要操作的字符串
 * @buf: 要保存的地方
 * @ch: 要遇到的字符串
 *
 * 返回缓冲区中字符的长度
 */
int strmet(const char *src, char *buf, char ch) {
	char *p = (char *)src;

	/* 没有遇到就一直复制直到字符串结束或者遇到 */
	while (*p && *p != ch) {
		*buf = *p;
		p++;
		buf++;
	}
	/* 最后添加结束字符 */
	*buf = '\0';
	return p - (char *)src;
}

/* 朴素的模式匹配算法 ，只用一个外层循环 */
char *strstr(const char *haystack, const char *needle) {
	char *thaystack = (char *)haystack;
	char *tneedle	= (char *)needle;
	int	  i			= 0; // thaystack 主串的元素下标位置，从下标0开始找，可以通过变量进行设置，从其他下标开始找！
	int	  j			= 0; // tneedle 子串的元素下标位置
	while (i <= strlen(thaystack) - 1 && j <= strlen(tneedle) - 1) {
		//字符相等，则继续匹配下一个字符
		if (thaystack[i] == tneedle[j]) {
			i++;
			j++;
		} else { //在匹配过程中发现有一个字符和子串中的不等，马上回退到 下一个要匹配的位置
			i = i - j + 1;
			j = 0;
		}
	}
	//循环完了后j的值等于strlen(tneedle) 子串中的字符已经在主串中都连续匹配到了
	if (j == strlen(tneedle)) { return thaystack + i - strlen(tneedle); }

	return NULL;
}

size_t strspn(const char *s, const char *accept) {
	const char *p = s;
	const char *a;
	size_t		count = 0;

	for (; *p != '\0'; ++p) {
		for (a = accept; *a != '\0'; ++a) {
			if (*p == *a) break;
		}
		if (*a == '\0') return count;
		++count;
	}
	return count;
}

const char *strpbrk(const char *str1, const char *str2) {
	if (str1 == NULL || str2 == NULL) {
		// perror("str1 or str2");
		return NULL;
	}
	const char *temp1 = str1;
	const char *temp2 = str2;

	while (*temp1 != '\0') {
		temp2 = str2; //将str2 指针从新指向在字符串的首地址
		while (*temp2 != '\0') {
			if (*temp2 == *temp1) return temp1;
			else temp2++;
		}
		temp1++;
	}
	return NULL;
}

/*
 *本文件大部分都是从网上搜索到的代码，如有侵权，请联系我。
 */