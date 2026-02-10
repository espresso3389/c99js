// c99js_test.c - Combined test driver for btree via c99js transpiler
// Includes btree implementation directly to produce a single compilation unit
// Tests: insert, search, iteration (ascend/descend), delete, min/max, load, pop
//
// Modifications for c99js:
//   - No goto statements
//   - No stdatomic.h (BTREE_NOATOMICS)
//   - No bitfields
//   - No flexible array members in btree_node (sizeof bug workaround)
//   - items_offset stored instead of char *items pointer (avoids overlap)
//   - children stored as a flat allocation after items
//   - Function pointer wrappers for malloc/free

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Wrapper functions for malloc/free so c99js can create function pointers
static void *my_malloc(size_t size) {
    return malloc(size);
}

static void my_free(void *ptr) {
    free(ptr);
}

// ============================================================================
// Btree implementation (fully refactored for c99js)
// ============================================================================

// No flexible array members. All dynamically-sized data (items, children)
// is accessed via offset calculations from a single flat allocation.

// Node header -- fixed size, no flexible array
struct btree_node_header {
    int rc;
    int leaf;        // use int instead of bool to avoid sizeof issues
    size_t nitems;
    size_t items_off;     // byte offset from node start to items data
    size_t children_off;  // byte offset from node start to children array (0 if leaf)
};

// btree struct -- no flexible array; spare data allocated separately
struct btree {
    void *(*bt_malloc)(size_t);
    void *(*bt_free_fn)(void *);  // returns void* to avoid c99js issues
    int (*compare)(const void *a, const void *b, void *udata);
    void *udata;
    char *root;          // pointer to root node (as char* to avoid flexible array)
    size_t count;
    size_t height;
    size_t max_items;
    size_t min_items;
    size_t elsize;
    int oom;
    char *spare;         // spare data buffer
    size_t spare_elsize;
};

// --- Helpers to access node fields ---
static struct btree_node_header *node_hdr(char *node) {
    return (struct btree_node_header *)node;
}

static char *node_items(char *node) {
    return node + node_hdr(node)->items_off;
}

static char **node_children(char *node) {
    return (char **)(node + node_hdr(node)->children_off);
}

static char *node_child(char *node, size_t i) {
    return node_children(node)[i];
}

static void node_set_child(char *node, size_t i, char *child) {
    node_children(node)[i] = child;
}

// --- Spare data helpers ---
#define NSPARES 4
static char *spare_at(struct btree *bt, size_t index) {
    return bt->spare + bt->spare_elsize * index;
}
#define SPARE_RETURN spare_at(bt, 0)
#define SPARE_POPMAX spare_at(bt, 2)

// --- Item access ---
static char *get_item_at(struct btree *bt, char *node, size_t index) {
    return node_items(node) + bt->elsize * index;
}

static void set_item_at(struct btree *bt, char *node, size_t index, const void *item) {
    memcpy(get_item_at(bt, node, index), item, bt->elsize);
}

static void swap_item_at(struct btree *bt, char *node, size_t index,
    const void *item, void *into)
{
    char *ptr = get_item_at(bt, node, index);
    memcpy(into, ptr, bt->elsize);
    memcpy(ptr, item, bt->elsize);
}

static void copy_item_into(struct btree *bt, char *node, size_t index, void *into) {
    memcpy(into, get_item_at(bt, node, index), bt->elsize);
}

// --- Shift operations ---
static void node_shift_right(struct btree *bt, char *node, size_t index) {
    struct btree_node_header *h = node_hdr(node);
    size_t num = h->nitems - index;
    char *items = node_items(node);
    memmove(items + bt->elsize * (index + 1),
            items + bt->elsize * index,
            num * bt->elsize);
    if (!h->leaf) {
        char **ch = node_children(node);
        memmove(&ch[index + 1], &ch[index], (num + 1) * sizeof(char*));
    }
    h->nitems++;
}

static void node_shift_left(struct btree *bt, char *node, size_t index, int for_merge) {
    struct btree_node_header *h = node_hdr(node);
    size_t num = h->nitems - index - 1;
    char *items = node_items(node);
    memmove(items + bt->elsize * index,
            items + bt->elsize * (index + 1),
            num * bt->elsize);
    if (!h->leaf) {
        size_t ci = index;
        size_t cn = num;
        if (for_merge) {
            ci++;
            cn--;
        }
        char **ch = node_children(node);
        memmove(&ch[ci], &ch[ci + 1], (cn + 1) * sizeof(char*));
    }
    h->nitems--;
}

static void copy_item(struct btree *bt, char *node_a, size_t ia,
    char *node_b, size_t ib)
{
    memcpy(get_item_at(bt, node_a, ia), get_item_at(bt, node_b, ib), bt->elsize);
}

static void node_join(struct btree *bt, char *left, char *right) {
    struct btree_node_header *lh = node_hdr(left);
    struct btree_node_header *rh = node_hdr(right);
    memcpy(node_items(left) + bt->elsize * lh->nitems, node_items(right),
           rh->nitems * bt->elsize);
    if (!lh->leaf) {
        char **lch = node_children(left);
        char **rch = node_children(right);
        size_t i;
        for (i = 0; i <= rh->nitems; i++) {
            lch[lh->nitems + i] = rch[i];
        }
    }
    lh->nitems += rh->nitems;
}

// --- Compare ---
static int bt_compare(struct btree *bt, const void *a, const void *b) {
    return bt->compare(a, b, bt->udata);
}

// --- Binary search ---
static size_t node_bsearch(struct btree *bt, char *node, const void *key, int *found) {
    size_t lo = 0;
    size_t hi = node_hdr(node)->nitems;
    while (lo < hi) {
        size_t mid = (lo + hi) >> 1;
        int cmp = bt_compare(bt, key, get_item_at(bt, node, mid));
        if (cmp == 0) {
            *found = 1;
            return mid;
        } else if (cmp < 0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    *found = 0;
    return lo;
}

// --- Align ---
static size_t align_size(size_t size) {
    size_t boundary = sizeof(uintptr_t);
    if (size < boundary) return boundary;
    if (size & (boundary - 1)) return size + boundary - (size & (boundary - 1));
    return size;
}

// --- Node allocation ---
// Layout for leaf: [header | items...]
// Layout for branch: [header | children... | items...]
static size_t calc_node_size(struct btree *bt, int leaf) {
    size_t size = sizeof(struct btree_node_header);
    if (!leaf) {
        size += sizeof(char*) * (bt->max_items + 2); // children pointers
    }
    size += bt->elsize * bt->max_items;  // items
    return align_size(size);
}

static char *node_new(struct btree *bt, int leaf) {
    size_t size = calc_node_size(bt, leaf);
    char *node = (char *)bt->bt_malloc(size);
    if (!node) return NULL;
    memset(node, 0, size);
    struct btree_node_header *h = node_hdr(node);
    h->leaf = leaf;
    h->nitems = 0;
    h->rc = 0;
    if (leaf) {
        h->items_off = sizeof(struct btree_node_header);
        h->children_off = 0;
    } else {
        h->children_off = sizeof(struct btree_node_header);
        h->items_off = sizeof(struct btree_node_header) + sizeof(char*) * (bt->max_items + 2);
    }
    return node;
}

static void node_free_fn(struct btree *bt, char *node);

static void bt_free_ptr(struct btree *bt, void *ptr) {
    bt->bt_free_fn(ptr);
}

static void node_free_fn(struct btree *bt, char *node) {
    struct btree_node_header *h = node_hdr(node);
    if (!h->leaf) {
        size_t i;
        for (i = 0; i <= h->nitems; i++) {
            node_free_fn(bt, node_child(node, i));
        }
    }
    bt_free_ptr(bt, node);
}

// --- btree lifecycle ---
static struct btree *btree_new(size_t elsize, size_t max_items,
    int (*compare)(const void *a, const void *b, void *udata), void *udata)
{
    struct btree *bt = (struct btree *)my_malloc(sizeof(struct btree));
    if (!bt) return NULL;
    memset(bt, 0, sizeof(struct btree));

    size_t deg = max_items / 2;
    if (deg == 0) deg = 128;
    else if (deg == 1) deg = 2;
    bt->max_items = deg * 2 - 1;
    if (bt->max_items > 2045) bt->max_items = 2045;
    bt->min_items = bt->max_items / 2;
    bt->compare = compare;
    bt->elsize = elsize;
    bt->udata = udata;
    bt->bt_malloc = my_malloc;
    bt->bt_free_fn = (void *(*)(void *))my_free;
    bt->spare_elsize = align_size(elsize);
    bt->spare = (char *)my_malloc(bt->spare_elsize * NSPARES);
    if (!bt->spare) {
        my_free(bt);
        return NULL;
    }
    memset(bt->spare, 0, bt->spare_elsize * NSPARES);
    return bt;
}

static void btree_clear(struct btree *bt) {
    if (bt->root) {
        node_free_fn(bt, bt->root);
    }
    bt->oom = 0;
    bt->root = NULL;
    bt->count = 0;
    bt->height = 0;
}

static void btree_free(struct btree *bt) {
    btree_clear(bt);
    my_free(bt->spare);
    my_free(bt);
}

// --- Mutation result ---
enum mut_result {
    MUT_NOCHANGE,
    MUT_NOMEM,
    MUT_MUST_SPLIT,
    MUT_INSERTED,
    MUT_REPLACED,
    MUT_DELETED
};

// --- Split ---
static void node_split(struct btree *bt, char *node, char **right_out, char **median_out) {
    struct btree_node_header *h = node_hdr(node);
    *right_out = node_new(bt, h->leaf);
    if (!*right_out) return;
    size_t mid = bt->max_items / 2;
    *median_out = get_item_at(bt, node, mid);
    struct btree_node_header *rh = node_hdr(*right_out);
    rh->nitems = h->nitems - (mid + 1);
    memmove(node_items(*right_out),
            node_items(node) + bt->elsize * (mid + 1),
            rh->nitems * bt->elsize);
    if (!h->leaf) {
        size_t i;
        for (i = 0; i <= rh->nitems; i++) {
            node_set_child(*right_out, i, node_child(node, mid + 1 + i));
        }
    }
    h->nitems = mid;
}

// --- Set (insert/replace) ---
static enum mut_result node_set(struct btree *bt, char *node, const void *item) {
    int found = 0;
    size_t i = node_bsearch(bt, node, item, &found);
    if (found) {
        swap_item_at(bt, node, i, item, SPARE_RETURN);
        return MUT_REPLACED;
    }
    if (node_hdr(node)->leaf) {
        if (node_hdr(node)->nitems == bt->max_items) {
            return MUT_MUST_SPLIT;
        }
        node_shift_right(bt, node, i);
        set_item_at(bt, node, i, item);
        return MUT_INSERTED;
    }
    enum mut_result result = node_set(bt, node_child(node, i), item);
    if (result == MUT_INSERTED || result == MUT_REPLACED) {
        return result;
    }
    if (result == MUT_NOMEM) {
        return MUT_NOMEM;
    }
    // Must split child
    if (node_hdr(node)->nitems == bt->max_items) {
        return MUT_MUST_SPLIT;
    }
    char *right = NULL;
    char *median = NULL;
    node_split(bt, node_child(node, i), &right, &median);
    if (!right) return MUT_NOMEM;
    node_shift_right(bt, node, i);
    set_item_at(bt, node, i, median);
    node_set_child(node, i + 1, right);
    return node_set(bt, node, item);
}

static void *btree_set0(struct btree *bt, const void *item) {
    bt->oom = 0;
    if (!bt->root) {
        bt->root = node_new(bt, 1);
        if (!bt->root) { bt->oom = 1; return NULL; }
        set_item_at(bt, bt->root, 0, item);
        node_hdr(bt->root)->nitems = 1;
        bt->count++;
        bt->height++;
        return NULL;
    }
    while (1) {
        enum mut_result result = node_set(bt, bt->root, item);
        if (result == MUT_REPLACED) {
            return SPARE_RETURN;
        }
        if (result == MUT_INSERTED) {
            bt->count++;
            return NULL;
        }
        if (result == MUT_NOMEM) {
            bt->oom = 1;
            return NULL;
        }
        // MUT_MUST_SPLIT: split root
        {
            char *old_root = bt->root;
            char *new_root = node_new(bt, 0);
            if (!new_root) { bt->oom = 1; return NULL; }
            char *right = NULL;
            char *median = NULL;
            node_split(bt, old_root, &right, &median);
            if (!right) {
                bt_free_ptr(bt, new_root);
                bt->oom = 1;
                return NULL;
            }
            bt->root = new_root;
            node_set_child(bt->root, 0, old_root);
            set_item_at(bt, bt->root, 0, median);
            node_set_child(bt->root, 1, right);
            node_hdr(bt->root)->nitems = 1;
            bt->height++;
        }
    }
}

// --- Get ---
static const void *btree_get0(struct btree *bt, const void *key) {
    char *node = bt->root;
    if (!node) return NULL;
    while (1) {
        int found;
        size_t i = node_bsearch(bt, node, key, &found);
        if (found) {
            return get_item_at(bt, node, i);
        }
        if (node_hdr(node)->leaf) return NULL;
        node = node_child(node, i);
    }
}

// --- Rebalance ---
static void node_rebalance(struct btree *bt, char *node, size_t i) {
    if (i == node_hdr(node)->nitems) i--;
    char *left = node_child(node, i);
    char *right = node_child(node, i + 1);
    struct btree_node_header *lh = node_hdr(left);
    struct btree_node_header *rh = node_hdr(right);

    if (lh->nitems + rh->nitems < bt->max_items) {
        copy_item(bt, left, lh->nitems, node, i);
        lh->nitems++;
        node_join(bt, left, right);
        bt_free_ptr(bt, right);
        node_shift_left(bt, node, i, 1);
    } else if (lh->nitems > rh->nitems) {
        node_shift_right(bt, right, 0);
        copy_item(bt, right, 0, node, i);
        if (!lh->leaf) {
            node_set_child(right, 0, node_child(left, lh->nitems));
        }
        copy_item(bt, node, i, left, lh->nitems - 1);
        if (!lh->leaf) {
            node_set_child(left, lh->nitems, NULL);
        }
        lh->nitems--;
    } else {
        copy_item(bt, left, lh->nitems, node, i);
        if (!lh->leaf) {
            node_set_child(left, lh->nitems + 1, node_child(right, 0));
        }
        lh->nitems++;
        copy_item(bt, node, i, right, 0);
        node_shift_left(bt, right, 0, 0);
    }
}

// --- Delete action types ---
enum del_act {
    DEL_KEY, DEL_POPFRONT, DEL_POPBACK, DEL_POPMAX
};

static enum mut_result node_delete(struct btree *bt, char *node,
    enum del_act act, const void *key, char *prev)
{
    size_t i = 0;
    int found = 0;
    struct btree_node_header *h = node_hdr(node);

    if (act == DEL_KEY) {
        i = node_bsearch(bt, node, key, &found);
    } else if (act == DEL_POPMAX) {
        i = h->nitems - 1;
        found = 1;
    } else if (act == DEL_POPFRONT) {
        i = 0;
        found = h->leaf;
    } else if (act == DEL_POPBACK) {
        if (!h->leaf) {
            i = h->nitems;
            found = 0;
        } else {
            i = h->nitems - 1;
            found = 1;
        }
    }

    if (h->leaf) {
        if (found) {
            copy_item_into(bt, node, i, prev);
            node_shift_left(bt, node, i, 0);
            return MUT_DELETED;
        }
        return MUT_NOCHANGE;
    }

    enum mut_result result;
    if (found) {
        if (act == DEL_POPMAX) {
            i++;
            result = node_delete(bt, node_child(node, i), DEL_POPMAX, NULL, prev);
            if (result == MUT_NOMEM) return MUT_NOMEM;
            result = MUT_DELETED;
        } else {
            copy_item_into(bt, node, i, prev);
            result = node_delete(bt, node_child(node, i), DEL_POPMAX, NULL, SPARE_POPMAX);
            if (result == MUT_NOMEM) return MUT_NOMEM;
            set_item_at(bt, node, i, SPARE_POPMAX);
            result = MUT_DELETED;
        }
    } else {
        result = node_delete(bt, node_child(node, i), act, key, prev);
    }
    if (result != MUT_DELETED) return result;
    if (node_hdr(node_child(node, i))->nitems < bt->min_items) {
        node_rebalance(bt, node, i);
    }
    return MUT_DELETED;
}

static void *btree_delete0(struct btree *bt, enum del_act act, const void *key) {
    bt->oom = 0;
    if (!bt->root) return NULL;
    enum mut_result result = node_delete(bt, bt->root, act, key, SPARE_RETURN);
    if (result == MUT_NOCHANGE) return NULL;
    if (result == MUT_NOMEM) { bt->oom = 1; return NULL; }
    if (node_hdr(bt->root)->nitems == 0) {
        char *old_root = bt->root;
        if (!node_hdr(bt->root)->leaf) {
            bt->root = node_child(bt->root, 0);
        } else {
            bt->root = NULL;
        }
        bt_free_ptr(bt, old_root);
        bt->height--;
    }
    bt->count--;
    return SPARE_RETURN;
}

// --- Public API wrappers ---
static const void *btree_set(struct btree *bt, const void *item) {
    return btree_set0(bt, item);
}

static const void *btree_get(struct btree *bt, const void *key) {
    return btree_get0(bt, key);
}

static const void *btree_delete(struct btree *bt, const void *key) {
    return btree_delete0(bt, DEL_KEY, key);
}

static const void *btree_pop_min(struct btree *bt) {
    return btree_delete0(bt, DEL_POPFRONT, NULL);
}

static const void *btree_pop_max(struct btree *bt) {
    return btree_delete0(bt, DEL_POPBACK, NULL);
}

static size_t btree_count(struct btree *bt) { return bt->count; }
static size_t btree_height(struct btree *bt) { return bt->height; }

static const void *btree_min(struct btree *bt) {
    char *node = bt->root;
    if (!node) return NULL;
    while (1) {
        if (node_hdr(node)->leaf) return get_item_at(bt, node, 0);
        node = node_child(node, 0);
    }
}

static const void *btree_max(struct btree *bt) {
    char *node = bt->root;
    if (!node) return NULL;
    while (1) {
        struct btree_node_header *h = node_hdr(node);
        if (h->leaf) return get_item_at(bt, node, h->nitems - 1);
        node = node_child(node, h->nitems);
    }
}

// --- Iteration ---
static int node_scan(struct btree *bt, char *node,
    int (*iter)(const void *item, void *udata), void *udata)
{
    struct btree_node_header *h = node_hdr(node);
    if (h->leaf) {
        size_t i;
        for (i = 0; i < h->nitems; i++) {
            if (!iter(get_item_at(bt, node, i), udata)) return 0;
        }
        return 1;
    }
    size_t i;
    for (i = 0; i < h->nitems; i++) {
        if (!node_scan(bt, node_child(node, i), iter, udata)) return 0;
        if (!iter(get_item_at(bt, node, i), udata)) return 0;
    }
    return node_scan(bt, node_child(node, h->nitems), iter, udata);
}

static int node_ascend(struct btree *bt, char *node, const void *pivot,
    int (*iter)(const void *item, void *udata), void *udata)
{
    int found;
    size_t i = node_bsearch(bt, node, pivot, &found);
    if (!found) {
        if (!node_hdr(node)->leaf) {
            if (!node_ascend(bt, node_child(node, i), pivot, iter, udata))
                return 0;
        }
    }
    for (; i < node_hdr(node)->nitems; i++) {
        if (!iter(get_item_at(bt, node, i), udata)) return 0;
        if (!node_hdr(node)->leaf) {
            if (!node_scan(bt, node_child(node, i + 1), iter, udata)) return 0;
        }
    }
    return 1;
}

static int btree_ascend(struct btree *bt, const void *pivot,
    int (*iter)(const void *item, void *udata), void *udata)
{
    if (bt->root) {
        if (!pivot) return node_scan(bt, bt->root, iter, udata);
        return node_ascend(bt, bt->root, pivot, iter, udata);
    }
    return 1;
}

static int node_reverse(struct btree *bt, char *node,
    int (*iter)(const void *item, void *udata), void *udata)
{
    struct btree_node_header *h = node_hdr(node);
    if (h->leaf) {
        size_t i = h->nitems;
        while (i > 0) {
            i--;
            if (!iter(get_item_at(bt, node, i), udata)) return 0;
        }
        return 1;
    }
    if (!node_reverse(bt, node_child(node, h->nitems), iter, udata)) return 0;
    size_t i = h->nitems;
    while (i > 0) {
        i--;
        if (!iter(get_item_at(bt, node, i), udata)) return 0;
        if (!node_reverse(bt, node_child(node, i), iter, udata)) return 0;
    }
    return 1;
}

static int node_descend(struct btree *bt, char *node, const void *pivot,
    int (*iter)(const void *item, void *udata), void *udata)
{
    int found;
    size_t i = node_bsearch(bt, node, pivot, &found);
    if (!found) {
        if (!node_hdr(node)->leaf) {
            if (!node_descend(bt, node_child(node, i), pivot, iter, udata))
                return 0;
        }
        if (i == 0) return 1;
        i--;
    }
    while (1) {
        if (!iter(get_item_at(bt, node, i), udata)) return 0;
        if (!node_hdr(node)->leaf) {
            if (!node_reverse(bt, node_child(node, i), iter, udata)) return 0;
        }
        if (i == 0) break;
        i--;
    }
    return 1;
}

static int btree_descend(struct btree *bt, const void *pivot,
    int (*iter)(const void *item, void *udata), void *udata)
{
    if (bt->root) {
        if (!pivot) return node_reverse(bt, bt->root, iter, udata);
        return node_descend(bt, bt->root, pivot, iter, udata);
    }
    return 1;
}

// --- Load (optimized sequential insert) ---
static const void *btree_load(struct btree *bt, const void *item) {
    bt->oom = 0;
    if (!bt->root) {
        return btree_set0(bt, item);
    }
    char *node = bt->root;
    while (1) {
        struct btree_node_header *h = node_hdr(node);
        if (h->leaf) {
            if (h->nitems == bt->max_items) break;
            char *litem = get_item_at(bt, node, h->nitems - 1);
            if (bt_compare(bt, item, litem) <= 0) break;
            set_item_at(bt, node, h->nitems, item);
            h->nitems++;
            bt->count++;
            return NULL;
        }
        node = node_child(node, h->nitems);
    }
    return btree_set0(bt, item);
}

// ============================================================================
// Test driver
// ============================================================================

static int tests_passed = 0;
static int tests_failed = 0;

static void check(int condition, const char *name) {
    if (condition) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("FAIL: %s\n", name);
    }
}

static int compare_ints(const void *a, const void *b, void *udata) {
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    if (ia < ib) return -1;
    if (ia > ib) return 1;
    return 0;
}

struct iter_ctx {
    int *collected;
    int count;
};

static int collect_iter(const void *item, void *udata) {
    struct iter_ctx *ctx = (struct iter_ctx *)udata;
    ctx->collected[ctx->count] = *(const int *)item;
    ctx->count++;
    return 1;
}

static int collect_first_3(const void *item, void *udata) {
    struct iter_ctx *ctx = (struct iter_ctx *)udata;
    if (ctx->count >= 3) return 0;
    ctx->collected[ctx->count] = *(const int *)item;
    ctx->count++;
    return 1;
}

static void test_new_and_empty(void) {
    printf("-- test_new_and_empty --\n");
    struct btree *bt = btree_new(sizeof(int), 0, compare_ints, NULL);
    check(bt != NULL, "btree_new returns non-null");
    check(btree_count(bt) == 0, "new btree has count 0");
    check(btree_height(bt) == 0, "new btree has height 0");
    check(btree_min(bt) == NULL, "min of empty btree is NULL");
    check(btree_max(bt) == NULL, "max of empty btree is NULL");
    int key = 42;
    check(btree_get(bt, &key) == NULL, "get from empty btree is NULL");
    btree_free(bt);
}

static void test_insert_and_get(void) {
    printf("-- test_insert_and_get --\n");
    struct btree *bt = btree_new(sizeof(int), 0, compare_ints, NULL);
    int i;
    for (i = 100; i >= 1; i--) {
        const void *prev = btree_set(bt, &i);
        check(prev == NULL, "insert new item returns NULL");
    }
    check(btree_count(bt) == 100, "count is 100 after 100 inserts");
    check(btree_height(bt) > 0, "height > 0 after inserts");

    int all_found = 1;
    for (i = 1; i <= 100; i++) {
        const int *val = (const int *)btree_get(bt, &i);
        if (!val || *val != i) {
            all_found = 0;
            break;
        }
    }
    check(all_found, "all 100 items found via get");

    int key = 0;
    check(btree_get(bt, &key) == NULL, "get(0) returns NULL");
    key = 101;
    check(btree_get(bt, &key) == NULL, "get(101) returns NULL");
    key = -5;
    check(btree_get(bt, &key) == NULL, "get(-5) returns NULL");

    btree_free(bt);
}

static void test_insert_replace(void) {
    printf("-- test_insert_replace --\n");
    struct btree *bt = btree_new(sizeof(int), 0, compare_ints, NULL);
    int val = 42;
    btree_set(bt, &val);
    check(btree_count(bt) == 1, "count is 1 after first insert");

    const int *prev = (const int *)btree_set(bt, &val);
    check(prev != NULL, "replacing returns non-null");
    if (prev) {
        check(*prev == 42, "replaced value is 42");
    }
    check(btree_count(bt) == 1, "count still 1 after replace");

    btree_free(bt);
}

static void test_min_max(void) {
    printf("-- test_min_max --\n");
    struct btree *bt = btree_new(sizeof(int), 0, compare_ints, NULL);
    int vals[] = {50, 20, 80, 10, 60, 30, 90, 40, 70, 5, 95};
    int nvals = 11;
    int i;
    for (i = 0; i < nvals; i++) {
        btree_set(bt, &vals[i]);
    }
    const int *mn = (const int *)btree_min(bt);
    const int *mx = (const int *)btree_max(bt);
    check(mn != NULL && *mn == 5, "min is 5");
    check(mx != NULL && *mx == 95, "max is 95");
    btree_free(bt);
}

static void test_delete(void) {
    printf("-- test_delete --\n");
    struct btree *bt = btree_new(sizeof(int), 0, compare_ints, NULL);
    int i;
    for (i = 1; i <= 50; i++) {
        btree_set(bt, &i);
    }
    check(btree_count(bt) == 50, "count is 50 before delete");

    for (i = 1; i <= 25; i++) {
        const int *prev = (const int *)btree_delete(bt, &i);
        if (!prev || *prev != i) {
            check(0, "delete returns correct value");
            btree_free(bt);
            return;
        }
    }
    check(1, "delete returns correct value for items 1-25");
    check(btree_count(bt) == 25, "count is 25 after deleting 25");

    int key = 1;
    const void *result = btree_delete(bt, &key);
    check(result == NULL, "delete non-existent returns NULL");

    int remaining_ok = 1;
    for (i = 26; i <= 50; i++) {
        const int *val = (const int *)btree_get(bt, &i);
        if (!val || *val != i) {
            remaining_ok = 0;
            break;
        }
    }
    check(remaining_ok, "remaining items 26-50 still present");

    btree_free(bt);
}

static void test_ascend(void) {
    printf("-- test_ascend --\n");
    struct btree *bt = btree_new(sizeof(int), 0, compare_ints, NULL);
    int i;
    for (i = 1; i <= 10; i++) {
        btree_set(bt, &i);
    }

    int buf[20];
    struct iter_ctx ctx;
    ctx.collected = buf;
    ctx.count = 0;
    btree_ascend(bt, NULL, collect_iter, &ctx);
    check(ctx.count == 10, "ascend(NULL) visits all 10 items");
    int asc_ok = 1;
    for (i = 0; i < 10; i++) {
        if (buf[i] != i + 1) {
            asc_ok = 0;
            break;
        }
    }
    check(asc_ok, "ascend(NULL) items in order 1..10");

    ctx.collected = buf;
    ctx.count = 0;
    int pivot = 5;
    btree_ascend(bt, &pivot, collect_iter, &ctx);
    check(ctx.count == 6, "ascend(5) visits 6 items (5..10)");
    int asc5_ok = 1;
    for (i = 0; i < 6; i++) {
        if (buf[i] != i + 5) {
            asc5_ok = 0;
            break;
        }
    }
    check(asc5_ok, "ascend(5) items are 5,6,7,8,9,10");

    ctx.collected = buf;
    ctx.count = 0;
    btree_ascend(bt, NULL, collect_first_3, &ctx);
    check(ctx.count == 3, "ascend with early stop collects 3 items");
    check(buf[0] == 1 && buf[1] == 2 && buf[2] == 3,
        "early stop items are 1,2,3");

    btree_free(bt);
}

static void test_descend(void) {
    printf("-- test_descend --\n");
    struct btree *bt = btree_new(sizeof(int), 0, compare_ints, NULL);
    int i;
    for (i = 1; i <= 10; i++) {
        btree_set(bt, &i);
    }

    int buf[20];
    struct iter_ctx ctx;
    ctx.collected = buf;
    ctx.count = 0;
    btree_descend(bt, NULL, collect_iter, &ctx);
    check(ctx.count == 10, "descend(NULL) visits all 10 items");
    int desc_ok = 1;
    for (i = 0; i < 10; i++) {
        if (buf[i] != 10 - i) {
            desc_ok = 0;
            break;
        }
    }
    check(desc_ok, "descend(NULL) items in order 10..1");

    ctx.collected = buf;
    ctx.count = 0;
    int pivot = 5;
    btree_descend(bt, &pivot, collect_iter, &ctx);
    check(ctx.count == 5, "descend(5) visits 5 items (5..1)");
    int desc5_ok = 1;
    for (i = 0; i < 5; i++) {
        if (buf[i] != 5 - i) {
            desc5_ok = 0;
            break;
        }
    }
    check(desc5_ok, "descend(5) items are 5,4,3,2,1");

    btree_free(bt);
}

static void test_pop_min_max(void) {
    printf("-- test_pop_min_max --\n");
    struct btree *bt = btree_new(sizeof(int), 0, compare_ints, NULL);
    int i;
    for (i = 1; i <= 20; i++) {
        btree_set(bt, &i);
    }

    const int *val = (const int *)btree_pop_min(bt);
    check(val != NULL && *val == 1, "pop_min returns 1");
    check(btree_count(bt) == 19, "count is 19 after pop_min");

    val = (const int *)btree_pop_max(bt);
    check(val != NULL && *val == 20, "pop_max returns 20");
    check(btree_count(bt) == 18, "count is 18 after pop_max");

    const int *mn = (const int *)btree_min(bt);
    const int *mx = (const int *)btree_max(bt);
    check(mn != NULL && *mn == 2, "new min is 2");
    check(mx != NULL && *mx == 19, "new max is 19");

    btree_free(bt);
}

static void test_load(void) {
    printf("-- test_load --\n");
    struct btree *bt = btree_new(sizeof(int), 0, compare_ints, NULL);
    int i;
    for (i = 1; i <= 200; i++) {
        btree_load(bt, &i);
    }
    check(btree_count(bt) == 200, "load 200 sequential items: count=200");

    int all_ok = 1;
    for (i = 1; i <= 200; i++) {
        const int *val = (const int *)btree_get(bt, &i);
        if (!val || *val != i) {
            all_ok = 0;
            break;
        }
    }
    check(all_ok, "all 200 loaded items found");

    btree_free(bt);
}

static void test_delete_all(void) {
    printf("-- test_delete_all --\n");
    struct btree *bt = btree_new(sizeof(int), 0, compare_ints, NULL);
    int i;
    for (i = 1; i <= 100; i++) {
        btree_set(bt, &i);
    }

    for (i = 1; i <= 100; i++) {
        btree_delete(bt, &i);
    }
    check(btree_count(bt) == 0, "count is 0 after deleting all");
    check(btree_min(bt) == NULL, "min is NULL after deleting all");
    check(btree_max(bt) == NULL, "max is NULL after deleting all");
    check(btree_height(bt) == 0, "height is 0 after deleting all");

    btree_free(bt);
}

static void test_large_insert_delete(void) {
    printf("-- test_large_insert_delete --\n");
    struct btree *bt = btree_new(sizeof(int), 0, compare_ints, NULL);
    int i;
    int n = 1000;

    for (i = 0; i < n; i++) {
        btree_set(bt, &i);
    }
    check((int)btree_count(bt) == n, "1000 items inserted");

    int deleted = 0;
    for (i = 1; i < n; i += 2) {
        const void *prev = btree_delete(bt, &i);
        if (prev) deleted++;
    }
    check(deleted == 500, "500 odd numbers deleted");
    check(btree_count(bt) == 500, "500 items remain");

    int even_ok = 1;
    for (i = 0; i < n; i += 2) {
        const int *val = (const int *)btree_get(bt, &i);
        if (!val || *val != i) {
            even_ok = 0;
            break;
        }
    }
    check(even_ok, "all 500 even numbers still present");

    int odd_gone = 1;
    for (i = 1; i < n; i += 2) {
        if (btree_get(bt, &i) != NULL) {
            odd_gone = 0;
            break;
        }
    }
    check(odd_gone, "all 500 odd numbers gone");

    btree_free(bt);
}

static void test_clear(void) {
    printf("-- test_clear --\n");
    struct btree *bt = btree_new(sizeof(int), 0, compare_ints, NULL);
    int i;
    for (i = 1; i <= 50; i++) {
        btree_set(bt, &i);
    }
    btree_clear(bt);
    check(btree_count(bt) == 0, "count 0 after clear");
    check(btree_height(bt) == 0, "height 0 after clear");

    for (i = 1; i <= 10; i++) {
        btree_set(bt, &i);
    }
    check(btree_count(bt) == 10, "count 10 after re-insert");
    btree_free(bt);
}

int main(void) {
    printf("=== btree c99js test suite ===\n");

    test_new_and_empty();
    test_insert_and_get();
    test_insert_replace();
    test_min_max();
    test_delete();
    test_ascend();
    test_descend();
    test_pop_min_max();
    test_load();
    test_delete_all();
    test_large_insert_delete();
    test_clear();

    printf("\n=== Results: %d passed, %d failed ===\n",
        tests_passed, tests_failed);

    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    } else {
        printf("SOME TESTS FAILED\n");
    }

    return tests_failed > 0 ? 1 : 0;
}
