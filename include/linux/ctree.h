/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cartesian Tree
 *
 * */

#ifndef _LINUX_CTREE_H
#define _LINUX_CTREE_H

#include <linux/list.h>

struct ctree_node {
	struct ctree_node *parent;
	struct ctree_node *left;
	struct ctree_node *right;
	struct list_head link;
};

struct ctree_root {
	struct ctree_node *node;
	struct list_head link_head;
};

static inline void INIT_CTREE_ROOT(struct ctree_root *tree)
{
	tree->node = NULL;
	INIT_LIST_HEAD(&tree->link_head);
}

#define ctree_entry(ptr, type, member) container_of(ptr, type, member)

#define ctree_empty(root_ptr) (list_empty(&(root_ptr)->link_head))

#define ctree_last(root_ptr) \
	list_last_entry(&((root_ptr)->link_head), struct ctree_node, link)

#define ctree_last_or_null(root_ptr) ({\
	struct list_head *head__ = &((root_ptr)->link_head); \
	struct list_head *pos__ = READ_ONCE(head__->prev); \
	pos__ != head__ ? list_entry(pos__, struct ctree_node, link) : NULL; \
})

static inline void ctree_node_clean(struct ctree_node *node)
{
	node->parent = node->left = node->right = NULL;
}

#endif
