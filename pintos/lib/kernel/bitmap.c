#include "bitmap.h"
#include <debug.h>
#include <limits.h>
#include <round.h>
#include <stdio.h>
#include "threads/malloc.h"
#ifdef FILESYS
#include "filesys/file.h"
#endif

/* 요소 유형.

   이는 적어도 int만큼 넓은 부호 없는 정수 유형이어야 합니다.

   각 비트는 비트맵의 한 비트를 나타냅니다.
   요소의 비트 0이 비트맵의 비트 K를 나타내는 경우
   요소의 비트 1은 비트맵의 비트 K+1을 나타냅니다.
   이런 식으로 이어집니다. */
typedef unsigned long elem_type;

/* 요소의 비트 수입니다. */
#define ELEM_BITS (sizeof (elem_type) * CHAR_BIT)

/* 외부에서 보면 비트맵은 비트 배열입니다.  
   내부에는 elem_type(위에 정의됨)의 배열입니다.
   비트 배열을 시뮬레이션합니다. */
struct bitmap {
	size_t bit_cnt;     /* 비트 수. */
	elem_type *bits;    /* 비트를 나타내는 요소입니다. */
};

/* 비트를 포함하는 요소의 인덱스를 반환합니다.
   번호는 BIT_IDX입니다. */
static inline size_t
elem_idx (size_t bit_idx) {
	return bit_idx / ELEM_BITS;
}

/* 다음에 해당하는 비트만 있는 elem_type을 반환합니다.
   BIT_IDX가 켜져 있습니다. */
static inline elem_type
bit_mask (size_t bit_idx) {
	return (elem_type) 1 << (bit_idx % ELEM_BITS);
}

/* BIT_CNT 비트에 필요한 요소 수를 반환합니다. */
static inline size_t
elem_cnt (size_t bit_cnt) {
	return DIV_ROUND_UP (bit_cnt, ELEM_BITS);
}

/* BIT_CNT 비트에 필요한 바이트 수를 반환합니다. */
static inline size_t
byte_cnt (size_t bit_cnt) {
	return sizeof (elem_type) * elem_cnt (bit_cnt);
}

/* 마지막에 실제로 사용된 비트가 포함된 비트 마스크를 반환합니다.
   B 비트의 요소는 1로 설정되고 나머지는 0으로 설정됩니다. */
static inline elem_type
last_mask (const struct bitmap *b) {
	int last_bits = b->bit_cnt % ELEM_BITS;
	return last_bits ? ((elem_type) 1 << last_bits) - 1 : (elem_type) -1;
}

/* 창조와 파괴. */

/* B를 BIT_CNT 비트의 비트맵으로 초기화합니다.
   모든 비트를 false로 설정합니다.
   성공하면 true를 반환하고, 메모리 할당이면 false를 반환합니다.
   실패한. */
struct bitmap *
bitmap_create (size_t bit_cnt) {
	struct bitmap *b = malloc (sizeof *b);
	if (b != NULL) {
		b->bit_cnt = bit_cnt;
		b->bits = malloc (byte_cnt (bit_cnt));
		if (b->bits != NULL || bit_cnt == 0) {
			bitmap_set_all (b, false);
			return b;
		}
		free (b);
	}
	return NULL;
}

/* BIT_CNT 비트가 포함된 비트맵을 생성하고 반환합니다.
   BLOCK에 사전 할당된 BLOCK_SIZE바이트의 저장소입니다.
   BLOCK_SIZE는 최소한 bitmap_needed_bytes(BIT_CNT) 이상이어야 합니다. */
struct bitmap *
bitmap_create_in_buf (size_t bit_cnt, void *block, size_t block_size UNUSED) {
	struct bitmap *b = block;

	ASSERT (block_size >= bitmap_buf_size (bit_cnt));

	b->bit_cnt = bit_cnt;
	b->bits = (elem_type *) (b + 1);
	bitmap_set_all (b, false);
	return b;
}

/* 비트맵을 수용하는 데 필요한 바이트 수를 반환합니다.
   BIT_CNT 비트 사용(bitmap_create_in_buf()와 함께 사용). */
size_t
bitmap_buf_size (size_t bit_cnt) {
	return sizeof (struct bitmap) + byte_cnt (bit_cnt);
}

/* 비트맵 B를 삭제하여 저장 공간을 확보합니다.
   다음에 의해 생성된 비트맵에는 사용되지 않습니다.
   비트맵_생성_사전할당(). */
void
bitmap_destroy (struct bitmap *b) {
	if (b != NULL) {
		free (b->bits);
		free (b);
	}
}

/* 비트맵 크기. */

/* B의 비트 수를 반환합니다. */
size_t
bitmap_size (const struct bitmap *b) {
	return b->bit_cnt;
}

/* 단일 비트 설정 및 테스트. */

/* B의 비트 번호 IDX를 원자적으로 VALUE로 설정합니다. */
void
bitmap_set (struct bitmap *b, size_t idx, bool value) {
	ASSERT (b != NULL);
	ASSERT (idx < b->bit_cnt);
	if (value)
		bitmap_mark (b, idx);
	else
		bitmap_reset (b, idx);
}

/* B의 비트 번호 BIT_IDX를 원자적으로 true로 설정합니다. */
void
bitmap_mark (struct bitmap *b, size_t bit_idx) {
	size_t idx = elem_idx (bit_idx);
	elem_type mask = bit_mask (bit_idx);

	/* 이는 `b->bits[idx] |= 마스크'와 같습니다.
	   단일 프로세서 시스템에서는 원자성이 보장됩니다.  
	   [IA32-v2b]의 OR 명령어에 대한 설명입니다. */
	asm ("lock orq %1, %0" : "=m" (b->bits[idx]) : "r" (mask) : "cc");
}

/* B의 비트 번호 BIT_IDX를 원자적으로 false로 설정합니다. */
void
bitmap_reset (struct bitmap *b, size_t bit_idx) {
	size_t idx = elem_idx (bit_idx);
	elem_type mask = bit_mask (bit_idx);

	/* 이는 `b->bits[idx] &= ~mask'와 같습니다.
	   단일 프로세서 시스템에서는 원자성이 보장됩니다.  
	   [IA32-v2a]의 AND 명령어에 대한 설명입니다. */
	asm ("lock andq %1, %0" : "=m" (b->bits[idx]) : "r" (~mask) : "cc");
}

/* B에서 비트 번호가 지정된 IDX를 원자적으로 토글합니다.
   즉, 그것이 참이라면 그것을 거짓으로 만들고,
   거짓이면 참으로 만듭니다. */
void
bitmap_flip (struct bitmap *b, size_t bit_idx) {
	size_t idx = elem_idx (bit_idx);
	elem_type mask = bit_mask (bit_idx);

	/* 이는 `b->bits[idx] ^= 마스크'와 같습니다.
	   단일 프로세서 시스템에서는 원자성이 보장됩니다.  
	   [IA32-v2b]의 XOR 명령어에 대한 설명입니다. */
	asm ("lock xorq %1, %0" : "=m" (b->bits[idx]) : "r" (mask) : "cc");
}

/* B의 IDX 비트 번호 값을 반환합니다. */
bool
bitmap_test (const struct bitmap *b, size_t idx) {
	ASSERT (b != NULL);
	ASSERT (idx < b->bit_cnt);
	return (b->bits[elem_idx (idx)] & bit_mask (idx)) != 0;
}

/* 여러 비트를 설정하고 테스트합니다. */

/* B의 모든 비트를 VALUE로 설정합니다. */
void
bitmap_set_all (struct bitmap *b, bool value) {
	ASSERT (b != NULL);

	bitmap_set_multiple (b, 0, bitmap_size (b), value);
}

/* B의 START에서 시작하는 CNT 비트를 VALUE로 설정합니다. */
void
bitmap_set_multiple (struct bitmap *b, size_t start, size_t cnt, bool value) {
	size_t i;

	ASSERT (b != NULL);
	ASSERT (start <= b->bit_cnt);
	ASSERT (start + cnt <= b->bit_cnt);

	for (i = 0; i < cnt; i++)
		bitmap_set (b, start + i, value);
}

/* START와 START + CNT 사이의 B 비트 수를 반환합니다.
   VALUE로 설정된 독점입니다. */
size_t
bitmap_count (const struct bitmap *b, size_t start, size_t cnt, bool value) {
	size_t i, value_cnt;

	ASSERT (b != NULL);
	ASSERT (start <= b->bit_cnt);
	ASSERT (start + cnt <= b->bit_cnt);

	value_cnt = 0;
	for (i = 0; i < cnt; i++)
		if (bitmap_test (b, start + i) == value)
			value_cnt++;
	return value_cnt;
}

/* START와 START + CNT 사이에 B의 비트가 있으면 true를 반환합니다.
   배타적이면 VALUE로 설정되고 그렇지 않으면 false로 설정됩니다. */
bool
bitmap_contains (const struct bitmap *b, size_t start, size_t cnt, bool value) {
	size_t i;

	ASSERT (b != NULL);
	ASSERT (start <= b->bit_cnt);
	ASSERT (start + cnt <= b->bit_cnt);

	for (i = 0; i < cnt; i++)
		if (bitmap_test (b, start + i) == value)
			return true;
	return false;
}

/* START와 START + CNT 사이에 B의 비트가 있으면 true를 반환합니다.
   배타적이면 true로 설정되고, 그렇지 않으면 false로 설정됩니다.*/
bool
bitmap_any (const struct bitmap *b, size_t start, size_t cnt) {
	return bitmap_contains (b, start, cnt, true);
}

/* START와 START + CNT 사이에 B에 비트가 없으면 true를 반환합니다.
   배타적이면 true로 설정되고, 그렇지 않으면 false로 설정됩니다.*/
bool
bitmap_none (const struct bitmap *b, size_t start, size_t cnt) {
	return !bitmap_contains (b, start, cnt, true);
}

/* START와 START + CNT 사이 B의 모든 비트가 true인 경우 true를 반환합니다.
   배타적이면 true로 설정되고, 그렇지 않으면 false로 설정됩니다. */
bool
bitmap_all (const struct bitmap *b, size_t start, size_t cnt) {
	return !bitmap_contains (b, start, cnt, false);
}

/* 설정된 또는 설정되지 않은 비트 찾기. */

/* CNT의 첫 번째 그룹의 시작 인덱스를 찾아 반환합니다.
   모두 다음으로 설정된 START 또는 그 이후의 B 연속 비트
   값.
   해당 그룹이 없으면 BITMAP_ERROR를 반환합니다. */
size_t
bitmap_scan (const struct bitmap *b, size_t start, size_t cnt, bool value) {
	ASSERT (b != NULL);
	ASSERT (start <= b->bit_cnt);

	if (cnt <= b->bit_cnt) {
		size_t last = b->bit_cnt - cnt;
		size_t i;
		for (i = start; i <= last; i++)
			if (!bitmap_contains (b, i, cnt, !value))
				return i;
	}
	return BITMAP_ERROR;
}

/* B에서 또는 그 이후의 CNT 연속 비트의 첫 번째 그룹을 찾습니다.
   모두 VALUE로 설정된 START를 모두 !VALUE로 뒤집습니다.
   그룹의 첫 번째 비트 인덱스를 반환합니다.
   해당 그룹이 없으면 BITMAP_ERROR를 반환합니다.
   CNT가 0이면 0을 반환합니다.
   비트는 원자적으로 설정되지만 테스트 비트는 원자적이지 않습니다.
   설정합니다. */
size_t
bitmap_scan_and_flip (struct bitmap *b, size_t start, size_t cnt, bool value) {
	size_t idx = bitmap_scan (b, start, cnt, value);
	if (idx != BITMAP_ERROR)
		bitmap_set_multiple (b, idx, cnt, !value);
	return idx;
}

/* 파일 입력 및 출력. */

#ifdef FILESYS
/* B를 파일에 저장하는 데 필요한 바이트 수를 반환합니다. */
size_t
bitmap_file_size (const struct bitmap *b) {
	return byte_cnt (b->bit_cnt);
}

/* FILE에서 B를 읽습니다.  
   그렇지 않으면. */
bool
bitmap_read (struct bitmap *b, struct file *file) {
	bool success = true;
	if (b->bit_cnt > 0) {
		off_t size = byte_cnt (b->bit_cnt);
		success = file_read_at (file, b->bits, size, 0) == size;
		b->bits[elem_cnt (b->bit_cnt) - 1] &= last_mask (b);
	}
	return success;
}

/* B를 FILE에 씁니다.  
   그렇지 않으면. */
bool
bitmap_write (const struct bitmap *b, struct file *file) {
	off_t size = byte_cnt (b->bit_cnt);
	return file_write_at (file, b->bits, size, 0) == size;
}
#endif /* 파일시스 */

/* 디버깅. */

/* B의 내용을 16진수로 콘솔에 덤프합니다. */
void
bitmap_dump (const struct bitmap *b) {
	hex_dump (0, b->bits, byte_cnt (b->bit_cnt), false);
}

