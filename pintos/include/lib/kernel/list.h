#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H

/* 이중 연결 리스트.
 *
 * 이 구현은 동적으로 할당한 메모리를 요구하지 않는다. 대신
 * 리스트 원소가 될 가능성이 있는 각 구조체는 `struct list_elem`
 * 멤버를 내장해야 한다. 모든 리스트 함수는 이
 * `struct list_elem`을 기준으로 동작한다. `list_entry`
 * 매크로는 `struct list_elem`에서 이를 포함하는 구조체로
 * 되돌아가는 변환을 제공한다.
 *
 * 예를 들어 `struct foo`의 리스트가 필요하다고 하자.
 * `struct foo`는 다음처럼 `struct list_elem` 멤버를
 * 포함해야 한다.
 *
 * struct foo {
 *   struct list_elem elem;
 *   int bar;
 *   ...다른 멤버들...
 * };
 *
 * 그러면 `struct foo`의 리스트는 다음과 같이 선언하고
 * 초기화할 수 있다.
 *
 * struct list foo_list;
 *
 * list_init (&foo_list);
 *
 * 순회는 `struct list_elem`에서 바깥 구조체로 되돌아가야
 * 하는 대표적인 상황이다. `foo_list`를 사용하는 예시는
 * 다음과 같다.
 *
 * struct list_elem *e;
 *
 * for (e = list_begin (&foo_list); e != list_end (&foo_list);
 * e = list_next (e)) {
 *   struct foo *f = list_entry (e, struct foo, elem);
 *   ...f로 어떤 작업을 수행한다...
 * }
 *
 * 실제 사용 예시는 소스 곳곳에서 찾을 수 있다. 예를 들어
 * threads 디렉터리의 `malloc.c`, `palloc.c`, `thread.c`
 * 모두 리스트를 사용한다.
 *
 * 이 리스트 인터페이스는 C++ STL의 `list<>` 템플릿에서
 * 영감을 받았다. `list<>`에 익숙하다면 사용이 어렵지 않을
 * 것이다. 다만 이 리스트는 타입 검사도 거의 없고, 그 밖의
 * 정합성 검사도 많지 않다는 점을 강조해 둔다. 잘못 쓰면
 * 그대로 문제가 터진다.
 *
 * 리스트 용어 정리:
 *
 * - "front": 리스트의 첫 번째 원소. 빈 리스트에서는 정의되지
 *   않는다. `list_front()`가 반환한다.
 *
 * - "back": 리스트의 마지막 원소. 빈 리스트에서는 정의되지
 *   않는다. `list_back()`이 반환한다.
 *
 * - "tail": 마지막 원소 바로 뒤에 있는 가상의 원소. 빈
 *   리스트에서도 항상 정의된다. `list_end()`가 반환하며,
 *   앞에서 뒤로 순회할 때 끝을 나타내는 센티넬로 사용한다.
 *
 * - "beginning": 비어 있지 않은 리스트에서는 `front`,
 *   빈 리스트에서는 `tail`이다. `list_begin()`이 반환하며,
 *   앞에서 뒤로 순회할 때 시작점으로 사용한다.
 *
 * - "head": 첫 번째 원소 바로 앞에 있는 가상의 원소. 빈
 *   리스트에서도 항상 정의된다. `list_rend()`가 반환하며,
 *   뒤에서 앞으로 순회할 때 끝을 나타내는 센티넬로 사용한다.
 *
 * - "reverse beginning": 비어 있지 않은 리스트에서는 `back`,
 *   빈 리스트에서는 `head`이다. `list_rbegin()`이 반환하며,
 *   뒤에서 앞으로 순회할 때 시작점으로 사용한다.
 *
 * - "interior element": `head`나 `tail`이 아닌 실제 리스트
 *   원소를 뜻한다. 빈 리스트에는 내부 원소가 없다. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 리스트 원소. */
struct list_elem {
	struct list_elem *prev;     /* 이전 리스트 원소. */
	struct list_elem *next;     /* 다음 리스트 원소. */
};

/* 리스트. */
struct list {
	struct list_elem head;      /* 리스트 머리. */
	struct list_elem tail;      /* 리스트 꼬리. */
};

/* 리스트 원소 `LIST_ELEM` 포인터를, 그것을 포함하는 구조체의
   포인터로 변환한다. 바깥 구조체 이름 `STRUCT`와 리스트 원소
   멤버 이름 `MEMBER`를 넘겨야 한다. 예시는 파일 상단의
   긴 주석을 참고하라. */
#define list_entry(LIST_ELEM, STRUCT, MEMBER)           \
	((STRUCT *) ((uint8_t *) &(LIST_ELEM)->next     \
		- offsetof (STRUCT, MEMBER.next)))

void list_init (struct list *);

/* 리스트 순회. */
struct list_elem *list_begin (struct list *);
struct list_elem *list_next (struct list_elem *);
struct list_elem *list_end (struct list *);

struct list_elem *list_rbegin (struct list *);
struct list_elem *list_prev (struct list_elem *);
struct list_elem *list_rend (struct list *);

struct list_elem *list_head (struct list *);
struct list_elem *list_tail (struct list *);

/* 리스트 삽입. */
void list_insert (struct list_elem *, struct list_elem *);
void list_splice (struct list_elem *before,
		struct list_elem *first, struct list_elem *last);
void list_push_front (struct list *, struct list_elem *);
void list_push_back (struct list *, struct list_elem *);

/* 리스트 제거. */
struct list_elem *list_remove (struct list_elem *);
struct list_elem *list_pop_front (struct list *);
struct list_elem *list_pop_back (struct list *);

/* 리스트 원소 접근. */
struct list_elem *list_front (struct list *);
struct list_elem *list_back (struct list *);

/* 리스트 속성. */
size_t list_size (struct list *);
bool list_empty (struct list *);

/* 기타. */
void list_reverse (struct list *);

/* 보조 데이터 `AUX`를 바탕으로 리스트 원소 `A`와 `B`의 값을
   비교한다. `A < B`이면 true를, 그렇지 않으면 false를 반환한다. */
typedef bool list_less_func (const struct list_elem *a,
                             const struct list_elem *b,
                             void *aux);

/* 정렬된 리스트에 대한 연산. */
void list_sort (struct list *,
                list_less_func *, void *aux);
void list_insert_ordered (struct list *, struct list_elem *,
                          list_less_func *, void *aux);
void list_unique (struct list *, struct list *duplicates,
                  list_less_func *, void *aux);

/* 최댓값과 최솟값. */
struct list_elem *list_max (struct list *, list_less_func *, void *aux);
struct list_elem *list_min (struct list *, list_less_func *, void *aux);

#endif /* lib/kernel/list.h */
