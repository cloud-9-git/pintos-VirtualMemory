#ifndef __LIB_KERNEL_HASH_H
#define __LIB_KERNEL_HASH_H

/* 해시 테이블.
 *
 * 이 자료구조는 Project 3용 Pintos 안내서(Tour of Pintos)에
 * 자세히 문서화되어 있다.
 *
 * 이는 체이닝을 사용하는 표준 해시 테이블이다. 테이블에서
 * 원소를 찾으려면 원소 데이터에 대해 해시 함수를 계산하고,
 * 그 값을 이중 연결 리스트 배열의 인덱스로 사용한 뒤,
 * 해당 리스트를 선형 탐색한다.
 *
 * 체인 리스트는 동적 할당을 사용하지 않는다. 대신 해시에
 * 들어갈 가능성이 있는 각 구조체는 `struct hash_elem`
 * 멤버를 내장해야 한다. 모든 해시 함수는 이
 * `struct hash_elem`을 기준으로 동작한다. `hash_entry`
 * 매크로는 `struct hash_elem`에서 이를 포함하는 바깥
 * 구조체로 되돌아가는 변환을 제공한다. 이는 연결 리스트
 * 구현에서 사용하는 것과 같은 기법이다. 자세한 설명은
 * `lib/kernel/list.h`를 참고하라. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "list.h"

/* 해시 원소. */
struct hash_elem {
	struct list_elem list_elem;
};

/* 해시 원소 `HASH_ELEM` 포인터를, 그것을 포함하는 구조체의
 * 포인터로 변환한다. 바깥 구조체 이름 `STRUCT`와 해시 원소
 * 멤버 이름 `MEMBER`를 넘겨야 한다. 예시는 파일 상단의
 * 긴 주석을 참고하라. */
#define hash_entry(HASH_ELEM, STRUCT, MEMBER)                   \
	((STRUCT *) ((uint8_t *) &(HASH_ELEM)->list_elem        \
		- offsetof (STRUCT, MEMBER.list_elem)))

/* 보조 데이터 `AUX`를 바탕으로 해시 원소 `E`의 해시 값을
 * 계산해 반환한다. */
typedef uint64_t hash_hash_func (const struct hash_elem *e, void *aux);

/* 보조 데이터 `AUX`를 바탕으로 해시 원소 `A`와 `B`의 값을
 * 비교한다. `A < B`이면 true를, 그렇지 않으면 false를 반환한다. */
typedef bool hash_less_func (const struct hash_elem *a,
		const struct hash_elem *b,
		void *aux);

/* 보조 데이터 `AUX`를 바탕으로 해시 원소 `E`에 어떤 작업을 수행한다. */
typedef void hash_action_func (struct hash_elem *e, void *aux);

/* 해시 테이블. */
struct hash {
	size_t elem_cnt;            /* 테이블 안 원소 개수. */
	size_t bucket_cnt;          /* 버킷 개수(2의 거듭제곱). */
	struct list *buckets;       /* `bucket_cnt`개의 리스트 배열. */
	hash_hash_func *hash;       /* 해시 함수. */
	hash_less_func *less;       /* 비교 함수. */
	void *aux;                  /* `hash`와 `less`용 보조 데이터. */
};

/* 해시 테이블 반복자. */
struct hash_iterator {
	struct hash *hash;          /* 해시 테이블. */
	struct list *bucket;        /* 현재 버킷. */
	struct hash_elem *elem;     /* 현재 버킷의 현재 해시 원소. */
};

/* 기본 생명주기. */
bool hash_init (struct hash *, hash_hash_func *, hash_less_func *, void *aux);
void hash_clear (struct hash *, hash_action_func *);
void hash_destroy (struct hash *, hash_action_func *);

/* 탐색, 삽입, 삭제. */
struct hash_elem *hash_insert (struct hash *, struct hash_elem *);
struct hash_elem *hash_replace (struct hash *, struct hash_elem *);
struct hash_elem *hash_find (struct hash *, struct hash_elem *);
struct hash_elem *hash_delete (struct hash *, struct hash_elem *);

/* 순회. */
void hash_apply (struct hash *, hash_action_func *);
void hash_first (struct hash_iterator *, struct hash *);
struct hash_elem *hash_next (struct hash_iterator *);
struct hash_elem *hash_cur (struct hash_iterator *);

/* 정보 조회. */
size_t hash_size (struct hash *);
bool hash_empty (struct hash *);

/* 예시 해시 함수. */
uint64_t hash_bytes (const void *, size_t);
uint64_t hash_string (const char *);
uint64_t hash_int (int);

#endif /* lib/kernel/hash.h */
