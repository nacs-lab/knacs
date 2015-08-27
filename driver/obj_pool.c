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

#define pr_fmt(fmt) "KNaCs: " fmt

#include "obj_pool.h"

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/err.h>

typedef struct {
    struct list_head node;
    void *obj;
} knacs_objpool_node;

struct knacs_objpool {
    int cache_max;
    void *(*alloc)(void*);
    void (*free)(void*, void*);
    void *data;

    struct list_head objs;
    struct list_head free_slots;
    spinlock_t lock;
};

knacs_objpool*
knacs_objpool_create(int cache_max, void *(*_alloc)(void*),
                     void (*_free)(void*, void*),
                     void *data)
{
    if (cache_max <= 0)
        cache_max = 5;

    knacs_objpool *pool = kzalloc(sizeof(knacs_objpool) +
                                  sizeof(knacs_objpool_node) * cache_max,
                                  GFP_KERNEL);
    if (!pool)
        return ERR_PTR(-ENOMEM);

    pool->cache_max = cache_max;
    pool->alloc = _alloc;
    pool->free = _free;
    pool->data = data;
    spin_lock_init(&pool->lock);
    INIT_LIST_HEAD(&pool->objs);
    INIT_LIST_HEAD(&pool->free_slots);

    knacs_objpool_node *nodes =
        (knacs_objpool_node*)(((char*)pool) + sizeof(knacs_objpool));

    for (int i = 0;i < cache_max;i++) {
        list_add_tail(&nodes[i].node, &pool->free_slots);
    }

    return pool;
}

void
knacs_objpool_destroy(knacs_objpool *pool)
{
    knacs_objpool_node *obj;
    knacs_objpool_node *next_obj;
    // No one should hold the lock we the pool is being destroyed
    list_for_each_entry_safe (obj, next_obj, &pool->objs, node) {
        list_del(&obj->node);
        pool->free(obj->obj, pool->data);
    }
    kfree(pool);
}

void*
knacs_objpool_alloc(knacs_objpool *pool)
{
    unsigned long irq_flags;
    spin_lock_irqsave(&pool->lock, irq_flags);
    void *obj;
    if (!list_empty(&pool->objs)) {
        knacs_objpool_node *_obj =
            list_first_entry(&pool->objs, knacs_objpool_node, node);
        list_del_init(&_obj->node);
        list_add(&_obj->node, &pool->free_slots);
        obj = _obj->obj;
        spin_unlock_irqrestore(&pool->lock, irq_flags);
    } else {
        spin_unlock_irqrestore(&pool->lock, irq_flags);
        obj = pool->alloc(pool->data);
        if (!obj) {
            return ERR_PTR(-ENOMEM);
        } else if (IS_ERR(obj)) {
            return obj;
        }
    }
    return obj;
}

void
knacs_objpool_free(knacs_objpool *pool, void *obj)
{
    unsigned long irq_flags;
    spin_lock_irqsave(&pool->lock, irq_flags);

    if (!list_empty(&pool->free_slots)) {
        knacs_objpool_node *_obj =
            list_first_entry(&pool->free_slots, knacs_objpool_node, node);
        _obj->obj = obj;
        list_del_init(&_obj->node);
        list_add(&_obj->node, &pool->objs);
        spin_unlock_irqrestore(&pool->lock, irq_flags);
    } else {
        spin_unlock_irqrestore(&pool->lock, irq_flags);
        pool->free(obj, pool->data);
    }
}
