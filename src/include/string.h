/*
 * file:		include/lib/string.h
 * auther:		Jason Hu
 * time:		2019/6/2
 * copyright:	(C) 2018-2020 by Book OS developers. All rights reserved.
 */

#ifndef _LIB_STRING_H_
#define _LIB_STRING_H_

#include "types.h"
#include "stdint.h"
#include "stddef.h"

#define STRING_MAX_LEN 256

typedef struct _string {
    unsigned int length;
    unsigned int max_length;
    char *text;
} string_t;

void string_init(string_t *string);
int string_new(string_t *string, char *text, unsigned int max_len);
void string_del(string_t *string);
int string_cpy(string_t *dest, string_t *src);

char* itoa(char ** ps, int val, int base);
int atoi(const char *src);
char *itoa16_align(char * str, int num);

void *memset(void* src, uint8_t value, uint32_t size);
void memcpy(void* dst_, const void* src_, uint32_t size);
int memcmp(const void * s1, const void *s2, int n);
void *memset16(void* src, uint16_t value, uint32_t size);
void *memset32(void* src, uint32_t value, uint32_t size);
void* memmove(void* dst,const void* src,uint32_t count);

#define bzero(str, n) memset(str, 0, n) 

char* strcpy(char* dst_, const char* src_);
uint32_t strlen(const char* str);
int8_t strcmp (const char *a, const char *b); 
char *strchr(const char *s,int c);
char* strrchr(char* str, int c);
char* strcat(char* strDest , const char* strSrc);
int strncmp (const char * s1, const char * s2, int n);
int strpos(char *str, char ch);
char* strncpy(char* dst_, char* src_, int n) ;
char *strncat(char *dst, const char *src, int n);
int strmet(const char *src, char *buf, char ch);
char *strstr(const char *haystack, const char *needle);
size_t strspn(const char *s, const char *accept);
const char *strpbrk(const char *str1, const char *str2);
int strcoll(const char *str1, const char *str2);

#endif  /*_LIB_STDINT_H*/

