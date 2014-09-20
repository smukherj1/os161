/*
 * list.c
 *
 *  Created on: 2013-02-22
 *      Author: suvanjan
 */

#include <types.h>
#include <lib.h>
#include <kern/errno.h>
#include <list.h>

void *list_create()
{
	struct list *l = kmalloc(sizeof(struct list));

	if(l == NULL)
		return NULL;

	l->head = NULL;
	return l;
}

int list_insert(struct list *l, int key, void *value)
{
	assert(l != NULL);

	// Empty list
	if(l->head == NULL)
	{
		l->head = kmalloc(sizeof(struct list_item));
		if(l->head == NULL)
			return ENOMEM;
		l->head->key = key;
		l->head->value = value;
		l->head->next = NULL;
	}

	// Non empty list. Just push at head. Don't bother checking
	// for duplicates
	else
	{
		struct list_item *new_item = kmalloc(sizeof(struct list_item));
		if(new_item == NULL)
			return ENOMEM;
		new_item->key = key;
		new_item->value = value;
		new_item->next = l->head;
		l->head = new_item;
	}
	return 0;
}

int list_get(struct list *l, int key, void **value)
{
	assert(l != NULL);
	assert(value != NULL);

	struct list_item *li = l->head;
	while(li != NULL)
	{
		if(li->key == key)
		{
			*value = li->value;
			return 0;
		}
		li = li->next;
	}

	// Not found
	return -1;
}

void list_remove_helper(struct list *l, struct list_item *li,
		struct list_item *prev)
{
	if(li == l->head)
	{
		prev = l->head;
		l->head = l->head->next;
		kfree(prev);
	}
	else
	{
		prev->next = li->next;
		kfree(li);
	}
}

int list_remove(struct list *l, int key, void **value)
{
	assert(l != NULL);
	assert(value != NULL);

	struct list_item *li = l->head, *prev = NULL;

	while(li != NULL)
	{
		if(li->key == key)
		{
			*value = li->value;
			list_remove_helper(l, li, prev);
			return 0;
		}
		prev = li;
		li = li->next;
	}

	*value = NULL;
	return -1;
}

void list_destroy(struct list **l, void (*item_destroy)(void*))
{
	struct list_item *li, *temp;

	assert(l != NULL);
	assert(*l != NULL);
	assert(item_destroy != NULL);

	li = (*l)->head;
	while(li != NULL)
	{
		item_destroy(li->value);
		temp = li;
		li = li->next;
		kfree(temp);
	}

	kfree(*l);
	*l = NULL;
	return;
}
