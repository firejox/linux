// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/child-sibling-tree.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <asm/timex.h>

#define __param(type, name, init, msg)		\
	static type name = init;		\
	module_param(name, type, 0444);		\
	MODULE_PARM_DESC(name, msg);

__param(int, nnodes, 100, "Number of nodes in the pairing-heap");
__param(int, check_loops, 100, "Number of iterations modifying and verifying the pairing-heap");

struct test_node {
	u32 val;
	struct child_sibling_node node;
};

struct test_node *nodes = NULL;
static struct rnd_state rnd;

static inline int pairing_heap_comp(struct child_sibling_node *a,
				struct child_sibling_node *b)
{
	return container_of(a, struct test_node, node)->val <
		container_of(b, struct test_node, node)->val;
}

static void pairing_heap_sib_merge(struct child_sibling_node *node,
					struct child_sibling_node *pa,
					struct child_sibling_node **link)
{
	LIST_HEAD(dummy);
	struct list_head * const head = &dummy;
	struct list_head *i, *j, *tmp;
	struct child_sibling_node *a, *b;

	if (CHILD_SIBLING_NODE_NO_SIB(node)) {
		node->parent = pa;
		*link = node;
		return;
	}

	list_add_tail(head, &node->sibling);

	/* first step merge */
	for (i = head->next, j = i->next, tmp = j->next;
			(i != head) && (j != head);
			i = tmp, j = i->next, tmp = j->next) {
		a = list_entry(i, struct child_sibling_node, sibling);
		b = list_entry(j, struct child_sibling_node, sibling);

		if (pairing_heap_comp(a, b)) {
			list_del(j);
			child_sibling_node_add_child(a, b);
		} else {
			list_del(i);
			child_sibling_node_add_child(b, a);
		}
	}

	/* second step merge */
	a = list_entry(head->next, struct child_sibling_node, sibling);

	for (i = head->next, j = i->next, tmp = j->next;
			(i != head) && (j != head);
			j = tmp, tmp = j->next) {
		b = list_entry(j, struct child_sibling_node, sibling);

		if (pairing_heap_comp(a, b)) {
			list_del(j);
			child_sibling_node_add_child(a, b);
		} else {
			list_del(i);
			child_sibling_node_add_child(b, a);
			i = j;
			a = b;
		}
	}

	list_del(head);

	a->parent = pa;
	*link = a;
}

static inline void pairing_heap_ch_to_sib(struct child_sibling_node *node)
{
	struct child_sibling_node *ch = node->child;

	if (ch) {
		struct list_head *n_tail = node->sibling.prev;
		struct list_head *c_tail = ch->sibling.prev;
		node->child = NULL;

		n_tail->next = &ch->sibling;
		ch->sibling.prev = n_tail;

		c_tail->next = &node->sibling;
		node->sibling.prev = c_tail;
	}
}

static void insert(struct child_sibling_root *heap, struct test_node *nd)
{
	struct child_sibling_node *root = heap->node;
	struct child_sibling_node *new = &nd->node;

	*new = CHILD_SILBLING_NODE_INIT(new);

	if (unlikely(!root)) {
		heap->node = new;
	} else if (pairing_heap_comp(new, root)) {
		new->child = root;
		root->parent = new;
		heap->node = new;
	} else {
		struct child_sibling_node *ch = root->child;

		if (!ch) {
			new->parent = root;
			root->child = new;
		} else if (pairing_heap_comp(new, ch)) {
			ch->parent = new;
			new->child = ch;

			new->parent = root;
			root->child = new;
		} else {
			child_sibling_node_add_child(ch, new);
		}
	}
}

static void remove(struct child_sibling_root *heap, struct test_node *nd)
{
	struct child_sibling_node *old = &nd->node;
	struct child_sibling_node *pa = old->parent;
	struct list_head *sib = &old->sibling;
	struct child_sibling_node *t;
	struct child_sibling_node **link;

	if (likely(!pa))
		link = &heap->node;
	else
		link = &pa->child;

	pairing_heap_ch_to_sib(old);

	if (CHILD_SIBLING_NODE_NO_SIB(old)) {
		*link = NULL;
	} else {
		t = list_entry(sib->next, struct child_sibling_node, sibling);
		list_del(sib);
		pairing_heap_sib_merge(t, pa, link);
	}

	WRITE_ONCE(t, heap->node);

	if (t) {
		struct child_sibling_node *ch = t->child;

		if (ch) {
			pairing_heap_sib_merge(ch, t, &t->child);
		}
	}
}

static inline struct test_node *get_top(struct child_sibling_root *heap)
{
	struct child_sibling_node *top = heap->node;

	if (!top)
		return NULL;

	return container_of(top, struct test_node, node);
}

static void test_nodes_init(void)
{
	int i;
	for (i = 0; i < nnodes; i++)
		nodes[i].val = prandom_u32_state(&rnd);
}

static int __init pairing_heap_test_init(void)
{
	int i, j;
	struct test_node *tmp;
	struct child_sibling_root heap = CHILD_SIBLING_ROOT;

	nodes = kmalloc_array(nnodes, sizeof(struct test_node), GFP_KERNEL);
	if (NULL == nodes)
		return -ENOMEM;

	prandom_seed_state(&rnd, 3141592653589793238ULL);

	printk(KERN_ALERT "Pairing heap property testing:\n");

	printk(KERN_ALERT "    1. min/max property ");

	for (i = 0; i < check_loops; i++) {
		test_nodes_init();

		tmp = nodes;
		insert(&heap, nodes);
		for (j = 1; j < nnodes; j++) {
			insert(&heap, nodes + j);

			if (pairing_heap_comp(&tmp->node, heap.node))
				goto test_fail_exit;

			tmp = get_top(&heap);
		}

		tmp = get_top(&heap);
		remove(&heap, tmp);
		for (j = 1; j < nnodes; j++) {
			if (pairing_heap_comp(heap.node, &tmp->node))
				goto test_fail_exit;

			tmp = get_top(&heap);

			remove(&heap, tmp);
		}
	}

	printk(KERN_CONT "....... passed\n");

	kfree(nodes);

	return -EAGAIN;

test_fail_exit:
	printk(KERN_CONT "....... fail\n");

	kfree(nodes);

	return -EAGAIN;
}

static void __exit pairing_heap_test_destroy(void)
{
	printk(KERN_ALERT "Pairing heap test exited\n");
}

module_init(pairing_heap_test_init)
module_exit(pairing_heap_test_destroy)

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Pairing Heap Test");
