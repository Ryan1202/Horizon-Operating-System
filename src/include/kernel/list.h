#ifndef _LIST_H
#define _LIST_H

#include <types.h>

#define offsetof(type, member) (int)(&((type *)0)->member)
#define container_of(ptr, type, member)                    \
	({                                                     \
		const typeof(((type *)0)->member) *__mptr = (ptr); \
		(type *)((char *)__mptr - offsetof(type, member)); \
	})

typedef struct list {
	struct list *prev;
	struct list *next;
} list_t;

#define LIST_HEAD_INIT(name) \
	{ &(name), &(name) }

#define LIST_HEAD(name) struct list name = LIST_HEAD_INIT(name)

static inline void list_init(struct list *list) {
	list->next = list;
	list->prev = list;
}

static inline void __list_add(struct list *_new, struct list *prev, struct list *next) {
	next->prev = _new;
	_new->next = next;
	_new->prev = prev;
	prev->next = _new;
}

static inline void list_add(struct list *_new, struct list *head) {
	// :) 插入到链表头和链表头的下一个节点之间
	__list_add(_new, head, head->next);
}

static inline void list_add_before(struct list *_new, struct list *node) {
	node->prev->next = _new;

	_new->prev = node->prev;
	_new->next = node;

	node->prev = _new;
}

static inline void list_add_after(struct list *_new, struct list *node) {
	node->next->prev = _new;

	_new->prev = node;
	_new->next = node->next;

	node->next = _new;
}

static inline void list_add_tail(struct list *_new, struct list *head) {
	// :) 插入到链表头前一个和链表头之间
	__list_add(_new, head->prev, head);
}

/* 把一个节点从链表中删除 */
static inline void __list_del(struct list *prev, struct list *next) {
	// ^_^ 把前一个和下一个进行关联，那中间一个就被删去了
	next->prev = prev;
	prev->next = next;
}

/* 把一个节点从链表中删除 */
static inline void __list_del_node(struct list *node) {
	// 传入节点的前一个和后一个，执行后只是脱离链表，而自身还保留了节点信息
	__list_del(node->prev, node->next);
}

static inline void list_del(struct list *node) {
	__list_del_node(node);
	// @.@ 把前驱指针和后驱指针都指向空，完全脱离
	node->prev = (struct list *)NULL;
	node->next = (struct list *)NULL;
}

static inline void list_del_init(struct list *node) {
	__list_del_node(node);
	// 初始化节点，使得可以成为链表头，我猜的。:-)
	list_init(node);
}

static inline void list_replace(struct list *old, struct list *_new) {
	/*
	@.@ 把old的前后指针都指向_new，那么_new就替代了old，真可恶！
	不过，旧的节点中还保留了链表的信息
	*/
	_new->next		 = old->next;
	_new->next->prev = _new;
	_new->prev		 = old->prev;
	_new->prev->next = _new;
}

static inline void list_replace_init(struct list *old, struct list *_new) {
	/*
	先把old取代，然后把old节点初始化，使它完全脱离链表。
	*/
	list_replace(old, _new);
	list_init(old);
}

static inline void list_move(struct list *node, struct list *head) {
	// ^.^ 先把自己脱离关系，然后添加到新的链表
	__list_del_node(node);
	list_add(node, head);
}

static inline void list_move_tail(struct list *node, struct list *head) {
	// ^.^ 先把自己脱离关系，然后添加到新的链表
	__list_del_node(node);
	list_add_tail(node, head);
}

static inline int list_is_first(const struct list *node, const struct list *head) {
	return (node->prev == head); // 节点的前一个是否为链表头
}

static inline int list_is_last(const struct list *node, const struct list *head) {
	return (node->next == head); // 节点的后一个是否为链表头
}

static inline int list_empty(const struct list *head) {
	return (head->next == head); // 链表头的下一个是否为自己
}

#define list_owner(ptr, type, member) container_of(ptr, type, member)

#define list_first_owner(head, type, member) list_owner((head)->next, type, member)

#define list_last_owner(head, type, member) list_owner((head)->prev, type, member)

#define list_first_owner_or_null(head, type, member)               \
	({                                                             \
		struct list *__head = (head);                              \
		struct list *__pos	= (__head->next);                      \
		 __pos != __head ? list_owner(__pos, type, member) : NULL; \
	})

#define list_last_owner_or_null(head, type, member)                \
	({                                                             \
		struct list *__head = (head);                              \
		struct list *__pos	= (__head->prev);                      \
		 __pos != __head ? list_owner(__pos, type, member) : NULL; \
	})

#define list_next_owner(pos, member) list_owner((pos)->member.next, typeof(*(pos)), member)

#define list_prev_onwer(pos, member) list_owner((pos)->member.prev, typeof(*(pos)), member)

#define list_for_each(pos, head) for (pos = (head)->next; pos != (head); pos = pos->next)

static inline int list_find(struct list *list, struct list *head) {
	struct list *node;
	list_for_each(node, head) {
		// 找到一样的
		if (node == list) { return 1; }
	}
	return 0;
}

static inline int list_length(struct list *head) {
	struct list *list;
	int			 n = 0;
	list_for_each(list, head) {
		// 找到一样的
		if (list == head) break;
		n++;
	}
	return n;
}

#define list_for_each_prev(pos, head) for (pos = (head)->prev; pos != (head); pos = pos->prev)

#define list_for_each_safe(pos, _next, head) \
	for (pos = (head)->next, _next = pos->next; pos != (head); pos = _next, _next = pos->next)

#define list_for_each_prev_safe(pos, _prev, head) \
	for (pos = (head)->prev, _prev = pos->prev; pos != (head); pos = _prev, _prev = pos->prev)

#define list_for_each_owner(pos, head, member)                                       \
	for (pos = list_first_owner(head, typeof(*pos), member); &pos->member != (head); \
		 pos = list_next_owner(pos, member))

#define list_for_each_owner_reverse(pos, head, member)                              \
	for (pos = list_last_owner(head, typeof(*pos), member); &pos->member != (head); \
		 pos = list_prev_onwer(pos, member))

#define list_for_each_owner_safe(pos, next, head, member)                                         \
	for (pos = list_first_owner(head, typeof(*pos), member), next = list_next_owner(pos, member); \
		 &pos->member != (head); pos = next, next = list_next_owner(next, member))

#define list_for_each_owner_reverse_safe(pos, prev, head, member)                                \
	for (pos = list_last_owner(head, typeof(*pos), member), prev = list_prev_onwer(pos, member); \
		 &pos->member != (head); pos = prev, prev = list_prev_onwer(prev, member))

#endif