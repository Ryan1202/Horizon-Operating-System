/**
 * @file layer.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 图层管理
 * @version 0.1
 * @date 2022-08-19
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include <gui/gui.h>
#include <gui/layer.h>
#include <kernel/memory.h>
#include <math.h>

/**
 * @brief 创建一个图层
 * 
 * @param x 图层的x坐标
 * @param y 图层的y坐标
 * @param width 宽度
 * @param height 高度
 * @param bpp 每像素字节数
 * @return struct layer_s* 
 */
struct layer_s *create_layer(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint8_t bpp)
{
	struct layer_s *layer = kmalloc(sizeof(struct layer_s));
	layer->x = x;
	layer->y = y;
	layer->width = width;
	layer->height = height;
	layer->bpp = bpp;
	layer->buffer = kmalloc(layer->width*layer->height*layer->bpp);
	layer->opacity = 1;
	layer->z = -1;
	return layer;
}

/**
 * @brief 设置图层在z轴上的高度
 * 
 * @param gui gui描述结构
 * @param layer 图层结构
 * @param z 高度
 */
void layer_set_z(struct gui_s *gui, struct layer_s *layer, int32_t z)
{
	int h;
	int _z;
	if (z > gui->top + 1)
	{
		_z = gui->top + 1;
	}
	else
	{
		_z = z;
	}
	
	if (_z > layer->z) // 比之前高
	{
		if (layer->z > 0)
		{
			for (h = layer->z; h < _z; h++)
			{
				gui->z[h] = gui->z[h + 1];
				gui->vsb_layer[gui->z[h]]->z = h;
			}
			gui->z[_z] = layer->did;
		}
		else if (layer->z < 0)
		{
			if (gui->z[gui->top] > 0)
			{
				for (h = gui->top; h >= _z; h--)
				{
					gui->z[h + 1] = gui->z[h];
					gui->vsb_layer[gui->z[h + 1]]->z = h + 1;
				}
			}
			gui->z[_z] = layer->did;
			gui->top++;
		}
		gui_update_map(gui, layer->x, layer->y, layer->x + layer->width, layer->y + layer->height);
	}
	else if (_z < layer->z) // 比之前低
	{
		if (layer->z > 0)
		{
			for (h = layer->z; h > _z; h--)
			{
				gui->z[h] = gui->z[h - 1];
				gui->vsb_layer[gui->z[h]]->z = h;
			}
			gui->z[_z] = layer->did;
		}
		else if (layer->z < 0)
		{
			for (h = layer->z; h < gui->top; h++)
			{
				gui->z[h] = gui->z[h + 1];
				gui->vsb_layer[gui->z[h]]->z = h;
			}
			gui->top--;
		}
		gui_update_map(gui, layer->x, layer->y, layer->x + layer->width, layer->y + layer->height);
	}
	layer->z = z;
}

/**
 * @brief 显示图层
 * 
 * @param gui gui描述结构
 * @param layer 图层
 * @return int 0表示成功,-1表示失败
 */
int show_layer(struct gui_s *gui, struct layer_s *layer)
{
	layer->did = alloc_id(&gui->idmm);
	if (layer->did == -1)
	{
		return -1;
	}
	gui->vsb_layer[layer->did] = layer;
	layer_set_z(gui, layer, gui->top);
	return 0;
}

/**
 * @brief 隐藏图层
 * 
 * @param gui gui描述结构
 * @param layer 图层
 */
void hide_layer(struct gui_s * gui, struct layer_s *layer)
{
	layer_set_z(gui, layer, -1);
	gui->vsb_layer[layer->did] = NULL;
	free_id(&gui->idmm, layer->did);
	layer->did = -1;
	return;
}

/**
 * @brief 移动图层
 * 
 * @param gui gui描述结构
 * @param layer 图层
 * @param new_x 新的x坐标
 * @param new_y 新的y坐标
 */
void move_layer(struct gui_s *gui, struct layer_s *layer, int new_x, int new_y)
{
	int old_x = layer->x, old_y = layer->y;
	layer->x = MIN(MAX(new_x, 0), gui->width - 1);
	layer->y = MIN(MAX(new_y, 0), gui->height - 1);
	gui_update_map(gui, MIN(old_x, new_x), MIN(old_y, new_y), MAX(old_x, new_x)+layer->width, MAX(old_y, new_y) + layer->height);
}