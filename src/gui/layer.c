/**
 * @file layer.c
 * @author Ryan Wang (ryan1202@foxmail.com)
 * @brief 图层管理
 * @version 0.2
 * @date 2022-11-13
 * 
 * @copyright Copyright (c) Ryan Wang 2022
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
struct layer_s *create_layer(int32_t x, int32_t y, int32_t width, int32_t height, uint8_t bpp)
{
	struct layer_s *layer = kmalloc(sizeof(struct layer_s));
	layer->rect.l = x;
	layer->rect.t = y;
	layer->rect.r = x + width;
	layer->rect.b = y + height;
	layer->bpp = bpp;
	layer->buffer = kmalloc(width*height*layer->bpp);
	layer->inc_tp = 0;
	layer->z = -1;
	layer->win = NULL;
	return layer;
}

/**
 * @brief 删除图层
 * 
 * @param layer 图层
 */
void delete_layer(struct gui_s *gui, struct layer_s *layer)
{
	hide_layer(gui, layer);
	kfree(layer->buffer);
	kfree(layer);
	return;
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
		gui_update_map(gui, &layer->rect);
	}
	else if (_z < layer->z) // 比之前低
	{
		if (z > 0)
		{
			for (h = layer->z; h > _z; h--)
			{
				gui->z[h] = gui->z[h - 1];
				gui->vsb_layer[gui->z[h]]->z = h;
			}
			gui->z[_z] = layer->did;
		}
		else if (z < 0)
		{
			if (gui->top > layer->z)
			{
				for (h = layer->z; h < gui->top; h++)
				{
					gui->z[h] = gui->z[h+1];
					gui->vsb_layer[gui->z[h]]->z = h;
				}
			}
			gui->top--;
		}
		gui_update_map(gui, &layer->rect);
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
	gui_refresh(gui, &layer->rect);
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
	int old_x = layer->rect.l, old_y = layer->rect.t;
	int width = layer->rect.r - layer->rect.l, height = layer->rect.b - layer->rect.t;
	layer->rect.r = new_x + layer->rect.r - layer->rect.l;
	layer->rect.b = new_y + layer->rect.b - layer->rect.t;
	layer->rect.l = new_x;
	layer->rect.t = new_y;
	if (layer->rect.r < 0)
	{
		layer->rect.r = 1;
		layer->rect.l = 1 - width;
	}
	else if (layer->rect.l > gui->width - 1)
	{
		layer->rect.r = gui->width + width - 1;
		layer->rect.l = gui->width - 1;
	}
	if (layer->rect.t < 0)
	{
		layer->rect.b = height;
		layer->rect.t = 0;
	}
	else if (layer->rect.t > gui->height - 1)
	{
		layer->rect.b = gui->height + height - 1;
		layer->rect.t = gui->height - 1;
	}
	if (gui->update_area.updated == 0)
	{
		gui->update_area.oldRect.l = MIN(old_x, gui->update_area.oldRect.l);
		gui->update_area.oldRect.t = MIN(old_y, gui->update_area.oldRect.t);
		gui->update_area.oldRect.r = MAX(old_x + width, gui->update_area.oldRect.r);
		gui->update_area.oldRect.b = MAX(old_y + height, gui->update_area.oldRect.b);
		gui->update_area.newRect.l = MIN(MAX(new_x, 0), gui->update_area.newRect.l);
		gui->update_area.newRect.t = MIN(MAX(new_y, 0), gui->update_area.newRect.t);
		gui->update_area.newRect.r = MAX(MIN(layer->rect.r, gui->width), gui->update_area.newRect.r);
		gui->update_area.newRect.b = MAX(MIN(layer->rect.b, gui->height), gui->update_area.newRect.b);
	}
	else
	{
		gui->update_area.oldRect.l = old_x;
		gui->update_area.oldRect.t = old_y;
		gui->update_area.oldRect.r = old_x + width;
		gui->update_area.oldRect.b = old_y + height;
		gui->update_area.newRect.l = MAX(new_x, 0);
		gui->update_area.newRect.t = MAX(new_y, 0);
		gui->update_area.newRect.r = MIN(layer->rect.r, gui->width);
		gui->update_area.newRect.b = MIN(layer->rect.b, gui->height);
		gui->update_area.updated = 0;
	}
	gui_update_map(gui, &gui->update_area.oldRect);
	gui_update_map(gui, &gui->update_area.newRect);
}