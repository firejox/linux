/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_CHILD_SIBLING_TREE_H
#define LINUX_CHILD_SIBLING_TREE_H

#include <linux/list.h>

struct child_sibling_node {
	struct list_head sibling;
	struct child_sibling_node *parent;
	struct child_sibling_node *child;
};

struct child_sibling_root {
	struct child_sibling_node *node;
};

#define CHILD_SIBLING_ROOT (struct child_sibling_root) { NULL }

#define CHILD_SIBLING_EMPTY_ROOT(root) (READ_ONCE((root)->node) == NULL)

#define CHILD_SILBLING_NODE_INIT(node) (struct child_sibling_node) \
		{ LIST_HEAD_INIT((node)->sibling), NULL, NULL }

#define CHILD_SIBLING_NODE_NO_SIB(node) list_empty(&(node)->sibling)

/**
 * child_sibling_node_add_child - add new child
 * @nd: the node to be added new child
 * @ch: new child node
 *
 * append new child to the child list
 */
static inline void child_sibling_node_add_child(struct child_sibling_node *nd,
						struct child_sibling_node *ch)
{
	struct child_sibling_node *nd_child = READ_ONCE(nd->child);

	WRITE_ONCE(ch->parent, nd);

	if (nd_child)
		list_add_tail(&ch->sibling, &nd_child->sibling);
	else {
		INIT_LIST_HEAD(&ch->sibling);
		WRITE_ONCE(nd->child, ch);
	}
}

#endif
