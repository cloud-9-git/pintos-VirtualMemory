#include "list.h"
#include "../debug.h"

/* 이 이중 연결 리스트는 두 개의 헤더 원소를 가진다.
   첫 번째 원소 바로 앞의 "head"와 마지막 원소 바로 뒤의
   "tail"이다. 앞쪽 헤더의 `prev` 링크는 null이고,
   뒤쪽 헤더의 `next` 링크도 null이다. 나머지 두 링크는
   리스트의 내부 원소를 통해 서로를 가리킨다.

   빈 리스트는 다음과 같다.

   +------+     +------+
   <---| head |<--->| tail |--->
   +------+     +------+

   원소 두 개를 가진 리스트는 다음과 같다.

   +------+     +-------+     +-------+     +------+
   <---| head |<--->|   1   |<--->|   2   |<--->| tail |<--->
   +------+     +-------+     +-------+     +------+

   이런 대칭 구조는 리스트 처리에서 많은 특수 경우를 없애 준다.
   예를 들어 `list_remove()`는 포인터 대입 두 번만으로 끝나고
   조건문이 필요 없다. 헤더 원소가 없을 때보다 훨씬 단순하다.

   (사실 각 헤더 원소에서는 포인터 하나만 쓰이므로, 단순함을
   잃지 않고 하나의 헤더 원소로 합칠 수도 있다. 하지만
   두 개의 별도 원소를 두면 몇몇 연산에서 약간의 검사까지
   할 수 있어서 유용하다.) */

static bool is_sorted (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) UNUSED;

/* `ELEM`이 head이면 true, 아니면 false를 반환한다. */
static inline bool
is_head (struct list_elem *elem) {
	return elem != NULL && elem->prev == NULL && elem->next != NULL;
}

/* `ELEM`이 내부 원소이면 true, 아니면 false를 반환한다. */
static inline bool
is_interior (struct list_elem *elem) {
	return elem != NULL && elem->prev != NULL && elem->next != NULL;
}

/* `ELEM`이 tail이면 true, 아니면 false를 반환한다. */
static inline bool
is_tail (struct list_elem *elem) {
	return elem != NULL && elem->prev != NULL && elem->next == NULL;
}

/* `LIST`를 빈 리스트로 초기화한다. */
void
list_init (struct list *list) {
	ASSERT (list != NULL);
	list->head.prev = NULL;
	list->head.next = &list->tail;
	list->tail.prev = &list->head;
	list->tail.next = NULL;
}

/* `LIST`의 시작 원소를 반환한다. */
struct list_elem *
list_begin (struct list *list) {
	ASSERT (list != NULL);
	return list->head.next;
}

/* 리스트에서 `ELEM` 다음 원소를 반환한다.
   `ELEM`이 마지막 원소면 리스트 tail을 반환한다.
   `ELEM` 자체가 tail이면 동작은 정의되지 않는다. */
struct list_elem *
list_next (struct list_elem *elem) {
	ASSERT (is_head (elem) || is_interior (elem));
	return elem->next;
}

/* `LIST`의 tail을 반환한다.

   `list_end()`는 리스트를 앞에서 뒤로 순회할 때 자주 쓴다.
   예시는 `list.h` 상단의 긴 주석을 참고하라. */
struct list_elem *
list_end (struct list *list) {
	ASSERT (list != NULL);
	return &list->tail;
}

/* `LIST`를 뒤에서 앞으로 역순 순회할 때의 시작 원소를 반환한다. */
struct list_elem *
list_rbegin (struct list *list) {
	ASSERT (list != NULL);
	return list->tail.prev;
}

/* 리스트에서 `ELEM` 이전 원소를 반환한다.
   `ELEM`이 첫 번째 원소면 리스트 head를 반환한다.
   `ELEM` 자체가 head이면 동작은 정의되지 않는다. */
struct list_elem *
list_prev (struct list_elem *elem) {
	ASSERT (is_interior (elem) || is_tail (elem));
	return elem->prev;
}

/* `LIST`의 head를 반환한다.

   `list_rend()`는 리스트를 뒤에서 앞으로 역순 순회할 때 자주 쓴다.
   전형적인 사용 예시는 `list.h` 상단 예제를 기준으로 다음과 같다.

   for (e = list_rbegin (&foo_list); e != list_rend (&foo_list);
   e = list_prev (e))
   {
   struct foo *f = list_entry (e, struct foo, elem);
   ...do something with f...
   }
   */
struct list_elem *
list_rend (struct list *list) {
	ASSERT (list != NULL);
	return &list->head;
}

/* `LIST`의 head를 반환한다.

   `list_head()`는 리스트를 순회하는 다른 스타일에 쓸 수 있다. 예:

   e = list_head (&list);
   while ((e = list_next (e)) != list_end (&list))
   {
   ...
   }
   */
struct list_elem *
list_head (struct list *list) {
	ASSERT (list != NULL);
	return &list->head;
}

/* `LIST`의 tail을 반환한다. */
struct list_elem *
list_tail (struct list *list) {
	ASSERT (list != NULL);
	return &list->tail;
}

/* `BEFORE` 바로 앞에 `ELEM`을 삽입한다.
   `BEFORE`는 내부 원소여도 되고 tail이어도 된다.
   후자의 경우는 `list_push_back()`과 같다. */
void
list_insert (struct list_elem *before, struct list_elem *elem) {
	ASSERT (is_interior (before) || is_tail (before));
	ASSERT (elem != NULL);

	elem->prev = before->prev;
	elem->next = before;
	before->prev->next = elem;
	before->prev = elem;
}

/* 현재 리스트에서 `FIRST`부터 `LAST` 직전까지의 원소를 떼어낸 뒤,
   `BEFORE` 바로 앞에 삽입한다. `BEFORE`는 내부 원소이거나
   tail일 수 있다. */
void
list_splice (struct list_elem *before,
		struct list_elem *first, struct list_elem *last) {
	ASSERT (is_interior (before) || is_tail (before));
	if (first == last)
		return;
	last = list_prev (last);

	ASSERT (is_interior (first));
	ASSERT (is_interior (last));

	/* 현재 리스트에서 `FIRST...LAST`를 깔끔하게 떼어낸다. */
	first->prev->next = last->next;
	last->next->prev = first->prev;

	/* `FIRST...LAST`를 새 리스트에 이어 붙인다. */
	first->prev = before->prev;
	last->next = before;
	before->prev->next = first;
	before->prev = last;
}

/* `ELEM`을 `LIST`의 맨 앞에 넣어 front가 되게 한다. */
void
list_push_front (struct list *list, struct list_elem *elem) {
	list_insert (list_begin (list), elem);
}

/* `ELEM`을 `LIST`의 맨 뒤에 넣어 back이 되게 한다. */
void
list_push_back (struct list *list, struct list_elem *elem) {
	list_insert (list_end (list), elem);
}

/* `ELEM`을 리스트에서 제거하고 그 뒤에 있던 원소를 반환한다.
   `ELEM`이 리스트에 없으면 동작은 정의되지 않는다.

   제거한 뒤의 `ELEM`을 여전히 리스트 원소처럼 다루면 안전하지 않다.
   특히 제거 후 `ELEM`에 `list_next()`나 `list_prev()`를 쓰면
   동작은 정의되지 않는다. 즉, 리스트 원소를 제거하려는
   순진한 루프는 실패한다.

 ** DON'T DO THIS **
 for (e = list_begin (&list); e != list_end (&list); e = list_next (e))
 {
 ...do something with e...
 list_remove (e);
 }
 ** DON'T DO THIS **

 다음은 올바르게 순회하며 원소를 제거하는 한 가지 방법이다.

for (e = list_begin (&list); e != list_end (&list); e = list_remove (e))
{
...do something with e...
}

 리스트 원소를 `free()`까지 해야 한다면 더 조심해야 한다.
 그 경우에도 동작하는 다른 전략은 다음과 같다.

while (!list_empty (&list))
{
struct list_elem *e = list_pop_front (&list);
...do something with e...
}
*/
struct list_elem *
list_remove (struct list_elem *elem) {
	ASSERT (is_interior (elem));
	elem->prev->next = elem->next;
	elem->next->prev = elem->prev;
	return elem->next;
}

/* `LIST`의 맨 앞 원소를 제거하고 반환한다.
   제거 전 `LIST`가 비어 있으면 동작은 정의되지 않는다. */
struct list_elem *
list_pop_front (struct list *list) {
	struct list_elem *front = list_front (list);
	list_remove (front);
	return front;
}

/* `LIST`의 맨 뒤 원소를 제거하고 반환한다.
   제거 전 `LIST`가 비어 있으면 동작은 정의되지 않는다. */
struct list_elem *
list_pop_back (struct list *list) {
	struct list_elem *back = list_back (list);
	list_remove (back);
	return back;
}

/* `LIST`의 front 원소를 반환한다.
   `LIST`가 비어 있으면 동작은 정의되지 않는다. */
struct list_elem *
list_front (struct list *list) {
	ASSERT (!list_empty (list));
	return list->head.next;
}

/* `LIST`의 back 원소를 반환한다.
   `LIST`가 비어 있으면 동작은 정의되지 않는다. */
struct list_elem *
list_back (struct list *list) {
	ASSERT (!list_empty (list));
	return list->tail.prev;
}

/* `LIST`의 원소 개수를 반환한다.
   시간 복잡도는 원소 수에 대해 O(n)이다. */
size_t
list_size (struct list *list) {
	struct list_elem *e;
	size_t cnt = 0;

	for (e = list_begin (list); e != list_end (list); e = list_next (e))
		cnt++;
	return cnt;
}

/* `LIST`가 비어 있으면 true, 아니면 false를 반환한다. */
bool
list_empty (struct list *list) {
	return list_begin (list) == list_end (list);
}

/* `A`와 `B`가 가리키는 `struct list_elem *`를 맞바꾼다. */
static void
swap (struct list_elem **a, struct list_elem **b) {
	struct list_elem *t = *a;
	*a = *b;
	*b = t;
}

/* `LIST`의 순서를 뒤집는다. */
void
list_reverse (struct list *list) {
	if (!list_empty (list)) {
		struct list_elem *e;

		for (e = list_begin (list); e != list_end (list); e = e->prev)
			swap (&e->prev, &e->next);
		swap (&list->head.next, &list->tail.prev);
		swap (&list->head.next->prev, &list->tail.prev->next);
	}
}

/* 보조 데이터 `AUX`를 바탕으로 했을 때
   `A`부터 `B` 직전까지의 리스트 원소가 `LESS` 기준으로
   정렬되어 있을 때만 true를 반환한다. */
static bool
is_sorted (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) {
	if (a != b)
		while ((a = list_next (a)) != b)
			if (less (a, list_prev (a), aux))
				return false;
	return true;
}

/* 보조 데이터 `AUX` 기준 `LESS`에 따라 비내림차순으로 정렬된
   리스트 원소 구간(run)을 `A`에서 시작해 `B`를 넘지 않는 범위에서
   찾는다. 그 구간의 끝(배타적)을 반환한다.
   `A`부터 `B` 직전까지는 비어 있지 않은 구간이어야 한다. */
static struct list_elem *
find_end_of_run (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) {
	ASSERT (a != NULL);
	ASSERT (b != NULL);
	ASSERT (less != NULL);
	ASSERT (a != b);

	do {
		a = list_next (a);
	} while (a != b && !less (a, list_prev (a), aux));
	return a;
}

/* `A0`부터 `A1B0` 직전까지와 `A1B0`부터 `B1` 직전까지를
   병합해 `B1` 직전에서 끝나는 하나의 구간으로 만든다.
   두 입력 구간은 모두 비어 있지 않아야 하고, 보조 데이터 `AUX`
   기준 `LESS`에 따라 비내림차순으로 정렬되어 있어야 한다.
   출력 구간도 같은 기준으로 정렬된다. */
static void
inplace_merge (struct list_elem *a0, struct list_elem *a1b0,
		struct list_elem *b1,
		list_less_func *less, void *aux) {
	ASSERT (a0 != NULL);
	ASSERT (a1b0 != NULL);
	ASSERT (b1 != NULL);
	ASSERT (less != NULL);
	ASSERT (is_sorted (a0, a1b0, less, aux));
	ASSERT (is_sorted (a1b0, b1, less, aux));

	while (a0 != a1b0 && a1b0 != b1)
		if (!less (a1b0, a0, aux))
			a0 = list_next (a0);
		else {
			a1b0 = list_next (a1b0);
			list_splice (a0, list_prev (a1b0), a1b0);
		}
}

/* 보조 데이터 `AUX` 기준 `LESS`에 따라 `LIST`를 정렬한다.
   시간은 O(n lg n), 추가 공간은 O(1)인 자연 반복 병합 정렬을 쓴다. */
void
list_sort (struct list *list, list_less_func *less, void *aux) {
	size_t output_run_cnt;        /* 현재 패스에서 나온 run 개수. */

	ASSERT (list != NULL);
	ASSERT (less != NULL);

	/* 비내림차순 run을 인접한 것끼리 계속 병합해,
	   run이 하나만 남을 때까지 리스트를 반복해서 훑는다. */
	do {
		struct list_elem *a0;     /* 첫 번째 run의 시작. */
		struct list_elem *a1b0;   /* 첫 번째 run의 끝이자 두 번째 run의 시작. */
		struct list_elem *b1;     /* 두 번째 run의 끝. */

		output_run_cnt = 0;
		for (a0 = list_begin (list); a0 != list_end (list); a0 = b1) {
			/* 각 반복은 출력 run 하나를 만든다. */
			output_run_cnt++;

			/* 비내림차순 원소 구간 `A0...A1B0`와 `A1B0...B1`
			   두 인접 run을 찾는다. */
			a1b0 = find_end_of_run (a0, list_end (list), less, aux);
			if (a1b0 == list_end (list))
				break;
			b1 = find_end_of_run (a1b0, list_end (list), less, aux);

			/* run들을 병합한다. */
			inplace_merge (a0, a1b0, b1, less, aux);
		}
	}
	while (output_run_cnt > 1);

	ASSERT (is_sorted (list_begin (list), list_end (list), less, aux));
}

/* 보조 데이터 `AUX` 기준 `LESS`에 따라 정렬된 `LIST`의 알맞은
   위치에 `ELEM`을 삽입한다.
   평균 시간 복잡도는 `LIST` 원소 수에 대해 O(n)이다. */
void
list_insert_ordered (struct list *list, struct list_elem *elem,
		list_less_func *less, void *aux) {
	struct list_elem *e;

	ASSERT (list != NULL);
	ASSERT (elem != NULL);
	ASSERT (less != NULL);

	for (e = list_begin (list); e != list_end (list); e = list_next (e))
		if (less (elem, e, aux))
			break;
	return list_insert (e, elem);
}

/* 보조 데이터 `AUX` 기준 `LESS`에 따라 같은 값으로 판정되는
   인접 원소 묶음마다 첫 번째만 남기고 나머지를 제거한다.
   `DUPLICATES`가 null이 아니면 제거한 원소를 그 리스트 뒤에 붙인다. */
void
list_unique (struct list *list, struct list *duplicates,
		list_less_func *less, void *aux) {
	struct list_elem *elem, *next;

	ASSERT (list != NULL);
	ASSERT (less != NULL);
	if (list_empty (list))
		return;

	elem = list_begin (list);
	while ((next = list_next (elem)) != list_end (list))
		if (!less (elem, next, aux) && !less (next, elem, aux)) {
			list_remove (next);
			if (duplicates != NULL)
				list_push_back (duplicates, next);
		} else
			elem = next;
}

/* 보조 데이터 `AUX` 기준 `LESS`에 따라 `LIST`에서 가장 큰 값을
   가진 원소를 반환한다. 최댓값이 여러 개면 더 앞에 나온 원소를
   반환한다. 리스트가 비어 있으면 tail을 반환한다. */
struct list_elem *
list_max (struct list *list, list_less_func *less, void *aux) {
	struct list_elem *max = list_begin (list);
	if (max != list_end (list)) {
		struct list_elem *e;

		for (e = list_next (max); e != list_end (list); e = list_next (e))
			if (less (max, e, aux))
				max = e;
	}
	return max;
}

/* 보조 데이터 `AUX` 기준 `LESS`에 따라 `LIST`에서 가장 작은 값을
   가진 원소를 반환한다. 최솟값이 여러 개면 더 앞에 나온 원소를
   반환한다. 리스트가 비어 있으면 tail을 반환한다. */
struct list_elem *
list_min (struct list *list, list_less_func *less, void *aux) {
	struct list_elem *min = list_begin (list);
	if (min != list_end (list)) {
		struct list_elem *e;

		for (e = list_next (min); e != list_end (list); e = list_next (e))
			if (less (e, min, aux))
				min = e;
	}
	return min;
}
