// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ctree.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <asm/timex.h>

#define __param(type, name, init, msg)		\
	static type name = init;		\
	module_param(name, type, 0444);		\
	MODULE_PARM_DESC(name, msg);

__param(int, nnodes, 100, "Number of nodes in the cartesian tree");
__param(int, perf_loops, 1000, "Number of iterations modifying the cartesian tree");
__param(int, check_loops, 100, "Number of iterations modifying and verifying the cartesian tree");

struct ctree_test_node {
	u32 val;
	struct ctree_node node;
};

static struct ctree_root tree;
static struct ctree_test_node *nodes = NULL;

static struct rnd_state rnd;

static bool cmp(const struct ctree_node *a, const struct ctree_node *b)
{
	const u32 val_a = ctree_entry(a, struct ctree_test_node, node)->val;
	const u32 val_b = ctree_entry(b, struct ctree_test_node, node)->val;

	return val_a < val_b;
}

static void test_node_insert(struct ctree_root *t, struct ctree_test_node *new)
{
	struct ctree_node *new_node = &new->node;
	struct ctree_node *pa_node = NULL;
	struct ctree_node *tmp;

	ctree_node_clean(new_node);

	if (unlikely(ctree_empty(t))) {
		t->node = new_node;
	} else if (cmp(new_node, t->node)) {
		new_node->left = t->node;
		t->node->parent = new_node;
		t->node = new_node;
	} else {
		pa_node = ctree_last(t);

		while (cmp(new_node, pa_node))
			pa_node = pa_node->parent;

		tmp = pa_node->right;
		pa_node->right = new_node;
		new_node->left = tmp;
		new_node->parent = pa_node;

		if (tmp)
			tmp->parent = new_node;
	}

	list_add_tail(&new_node->link, &t->link_head);
}

static void test_node_remove(struct ctree_root *tree, struct ctree_test_node *old)
{
	struct ctree_node *node = &old->node;
	struct ctree_node *pa = node->parent;
	struct ctree_node *l_node = node->left;
	struct ctree_node *r_node = node->right;
	struct ctree_node **link;

	if (likely(!pa)) {
		link = &tree->node;
	} else {
		if (pa->left == node)
			link = &pa->left;
		else
			link = &pa->right;
	}

	while (true) {
		if (!l_node) {
			*link = r_node;
			if (r_node)
				r_node->parent = pa;

			break;
		} else if (!r_node) {
			*link = l_node;
			l_node->parent = pa;

			break;
		} else if (cmp(r_node, l_node)) {
			*link = r_node;
			r_node->parent = pa;
			link = &r_node->left;

			pa = r_node;
			r_node = r_node->left;
		} else {
			*link = l_node;
			l_node->parent = pa;
			link = &l_node->right;

			pa = l_node;
			l_node = l_node->right;
		}
	}

	list_del(&node->link);
}

static struct ctree_node *ctree_leftmost(struct ctree_node *node)
{
	if (!node)
		return NULL;

	while (node->left)
		node = node->left;
	return node;
}

static struct ctree_node *ctree_next_node(struct ctree_node *node)
{
	if (node == NULL) {
		return NULL;
	} else if (node->right != NULL) {
		return ctree_leftmost(node->right);
	} else {
		struct ctree_node *pa = node->parent;
		for (; pa && pa->right == node; node = pa, pa = pa->parent);

		return pa;
	}
}

static bool check_input_order_holds(struct ctree_root *t)
{
	struct ctree_node *cur_node = ctree_leftmost(t->node);
	struct ctree_node *tmp = NULL;
	struct list_head *head = t->link_head.next;

	while (cur_node != NULL) {
		tmp = list_entry(head, struct ctree_node, link);
		if (tmp != cur_node)
			return false;

		cur_node = ctree_next_node(cur_node);
		head = head->next;
	}

	return true;
}

static inline struct ctree_test_node *ctree_top_test_node(struct ctree_root *t)
{
	return ctree_entry(t->node, struct ctree_test_node, node);
}

static void ctree_test_nodes_init(void)
{
	int i;
	for (i = 0; i < nnodes; i++)
		nodes[i].val = prandom_u32_state(&rnd);
}

static int __init ctree_test_init(void)
{
	int i, j;
	cycles_t time1, time2, time;
	struct ctree_test_node *tmp;

	INIT_CTREE_ROOT(&tree);

	nodes = kmalloc_array(nnodes, sizeof(struct ctree_test_node), GFP_KERNEL);
	if (nodes == NULL)
		return -ENOMEM;

	prandom_seed_state(&rnd, 3141592653589793238ULL);

	printk(KERN_ALERT "Cartesian tree property testing:\n");

	printk(KERN_ALERT "    1. inorder traversal == input order ");
	for (i = 0; i < check_loops; i++) {
		ctree_test_nodes_init();

		for (j = 0; j < nnodes; j++) {
			test_node_insert(&tree, nodes + j);
			if (!check_input_order_holds(&tree))
				goto test_fail_exit;
		}

		for (j = 0; j < nnodes; j++) {
			test_node_remove(&tree, nodes + j);

			if (!check_input_order_holds(&tree))
				goto test_fail_exit;
		}
	}

	printk(KERN_CONT "....... passed\n");

	printk(KERN_ALERT "    2. min/max heap property ");
	for (i = 0; i < check_loops; i++) {
		ctree_test_nodes_init();

		tmp = nodes;
		test_node_insert(&tree, nodes);
		for (j = 1; j < nnodes; j++) {
			test_node_insert(&tree, nodes + j);

			if (cmp(&tmp->node, tree.node))
				goto test_fail_exit;

			tmp = ctree_top_test_node(&tree);
		}

		tmp = ctree_top_test_node(&tree);
		test_node_remove(&tree, tmp);
		for (j = 1; j < nnodes; j++) {
			if (cmp(tree.node, &tmp->node))
				goto test_fail_exit;

			tmp = ctree_top_test_node(&tree);

			test_node_remove(&tree, tmp);
		}
	}

	printk(KERN_CONT "....... passed\n");

	printk(KERN_ALERT "Cartesian tree performance testing:\n");
	ctree_test_nodes_init();

	time1 = get_cycles();

	for (i = 0; i < perf_loops; i++) {
		for (j = 0; j < nnodes; j++)
			test_node_insert(&tree, nodes + j);

		for (j = 0; j < nnodes; j++)
			test_node_remove(&tree, nodes + j);
	}

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, perf_loops);
	printk(KERN_ALERT "    latency of nodes input/(remove in input order) :"
			" %lu cycles\n", (unsigned long)time);

	time1 = get_cycles();

	for (i = 0; i < perf_loops; i++) {
		for (j = 0; j < nnodes; j++)
			test_node_insert(&tree, nodes + j);

		for (j = 0; j < nnodes; j++) {
			tmp = ctree_top_test_node(&tree);
			test_node_remove(&tree, tmp);
		}
	}

	time2 = get_cycles();
	time = time2 - time1;

	time = div_u64(time, perf_loops);
	printk(KERN_ALERT "    latency of nodes input/(remove in input order) :"
			" %lu cycles\n", (unsigned long)time);

	kfree(nodes);
	return -EAGAIN;

test_fail_exit:
	printk(KERN_CONT "....... fail\n");
	kfree(nodes);

	return -EAGAIN; /* Fail will directly unload the module */
}

static void __exit ctree_test_destroy(void)
{
	printk(KERN_ALERT "Cartesian tree test exit\n");
}

module_init(ctree_test_init)
module_exit(ctree_test_destroy)

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cartesian Tree test");
