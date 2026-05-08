#ifndef THREADS_INIT_H
#define THREADS_INIT_H

#include <debug.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 물리 메모리 크기(4 kB 페이지 단위). */
extern size_t ram_pages;

/* 커널 매핑만 포함하는 4단계 페이지 맵. */
extern uint64_t *base_pml4;

/* `-q`: 커널 작업이 끝나면 전원을 끌지 여부. */
extern bool power_off_when_done;

void power_off (void) NO_RETURN;

#endif /* threads/init.h */
