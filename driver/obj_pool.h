/*************************************************************************
 *   Copyright (c) 2015 - 2015 Yichao Yu <yyc1992@gmail.com>             *
 *                                                                       *
 *   This program is free software; you can redistribute it and/or       *
 *   modify it under the terms of the GNU General Public License         *
 *   as published by the Free Software Foundation; either version 2      *
 *   of the License, or (at your option) any later version.              *
 *                                                                       *
 *   This program is distributed in the hope that it will be useful,     *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of      *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the       *
 *   GNU General Public License for more details.                        *
 *                                                                       *
 *   You should have received a copy of the GNU General Public License   *
 *   along with this program; if not, write to the Free Software         *
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA       *
 *   02110-1301, USA.                                                    *
 *************************************************************************/

#ifndef __KNACS_OBJ_POOL_H__
#define __KNACS_OBJ_POOL_H__

typedef struct knacs_objpool knacs_objpool;

knacs_objpool *knacs_objpool_create(int cache_max, void *(*_alloc)(void*),
                                    void (*_free)(void*, void*), void *data);
void knacs_objpool_destroy(knacs_objpool *pool);

void *knacs_objpool_alloc(knacs_objpool *pool);
void knacs_objpool_free(knacs_objpool *pool, void *obj);

#endif
