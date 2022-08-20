/**
 * @file graphic.c
 * @author Ryan1202 (ryan1202@foxmail.com)
 * @brief 
 * @version 0.1
 * @date 2022-08-19
 * 
 */
#include <gui/graphic.h>

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
void g_layer_fill_rect(struct layer_s *layer, uint32_t l, uint32_t t, uint32_t r, uint32_t b, uint32_t color)
{
	uint32_t *buffer = (uint32_t *)layer->buffer;
	if (l > layer->width || r > layer->width || t >layer->height || b >layer->height)
	{
		return;
	}
	if (l > r || t > b)
	{
		return;
	}
	uint32_t c;
	if (layer->bpp == 4)
	{
		c = color;
	}
	uint32_t x, y;
	for (y = t; y < b; y++)
	{
		for (x = l; x < r; x++)
		{
			buffer[((y*layer->width*layer->bpp)+(x*layer->bpp))/4] = c;
		}
	}
}