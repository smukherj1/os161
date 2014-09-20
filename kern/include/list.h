/*
 * list.h
 *
 *  Created on: 2013-02-22
 *      Author: suvanjan
 */

#ifndef LIST_H_
#define LIST_H_

struct list_item
{
	int key;
	void *value;
	struct list_item *next;
};

struct list
{
	struct list_item *head;
};

/*
 * Create an empty list. Set head to NULL.
 * returns NULL on no memory error
 */
void *list_create();

/*
 * Insets a new item into the list 'l' of with 'key' and 'value'.
 * returns 0 on success or ENOMEM on memory error
 *
 * The 'l' and 'value' should not be NULL. This is asserted.
 */
int list_insert(struct list *l, int key, void *value);

/*
 * Get the item with 'key' from the list.
 * -1 if not found. 0 on success.
 *
 * 'l' and 'value' can't be NULL. This is asserted.
 */
int list_get(struct list *l, int key, void **value);

/*
 * Remove the item with key and put its value in 'value'.
 *
 * return 0 if item found. -1 if not found and value set to NULL
 */

int list_remove(struct list *l, int key, void **value);

/*
 * Destroy this list and destroy all items in it.
 * 'item_destroy' is the function pointer to the function
 * that destroys each item's value.
 */
void list_destroy(struct list **l, void (*item_destroy)(void*));

#endif /* LIST_H_ */
