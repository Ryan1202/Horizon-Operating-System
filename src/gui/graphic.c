/**
 * @file graphic.c
 * @author Ryan1202 (ryan1202@foxmail.com)
 * @brief 
 * @version 0.1
 * @date 2022-08-19
 * 
 */
#include <gui/graphic.h>
#include <drivers/video.h>

/**
 * @brief 在图层内填充矩形
 * 
 * @param layer 图层
 * @param l 左
 * @param t 上
 * @param r 右
 * @param b 下
 * @param color 颜色
 */
void g_fill_rect(struct layer_s *layer, uint32_t l, uint32_t t, uint32_t r, uint32_t b, uint32_t color)
{
	uint32_t *buffer = (uint32_t *)layer->buffer;
	int width = layer->rect.r - layer->rect.l, height = layer->rect.b - layer->rect.t;
	if (l > width || r > width || t > height || b > height)
	{
		return;
	}
	if (l > r || t > b)
	{
		return;
	}
	uint32_t c;
	c = color;
	uint32_t x, y;
	for (y = t; y < b; y++)
	{
		for (x = l; x < r; x++)
		{
			buffer[(y*width+x)*layer->bpp/4] = c;
		}
	}
}

void g_print_char8(struct layer_s *layer, int x, int y, unsigned char *ascii, unsigned int color)
{
	unsigned int *vram;
	int i, width = layer->rect.r - layer->rect.l;
	char d;
	for (i = 0; i < 16; i++) {
		vram = (unsigned int *)(layer->buffer + (y + i)*width*layer->bpp);
		d = ascii[i];
		if (d & 0x80) { vram[x + 1] = color; }
		if (d & 0x40) { vram[x + 2] = color; }
		if (d & 0x20) { vram[x + 3] = color; }
		if (d & 0x10) { vram[x + 4] = color; }
		if (d & 0x08) { vram[x + 5] = color; }
		if (d & 0x04) { vram[x + 6] = color; }
		if (d & 0x02) { vram[x + 7] = color; }
		if (d & 0x01) { vram[x + 8] = color; }
	}
}

void g_print_string(struct layer_s *layer, int x, int y, unsigned char *font, char *string, unsigned int color)
{
	while(*string != 0)
	{
		g_print_char8(layer, x, y, font+(*string)*16, color);
		string++;
		x+=9;
	}
}