userprog_SRC  = userprog/process.c	# 프로세스 적재.
userprog_SRC += userprog/exception.c	# 사용자 예외 핸들러.
userprog_SRC += userprog/syscall-entry.S # 시스템 호출 진입점.
userprog_SRC += userprog/syscall.c	# 시스템 호출 핸들러.
userprog_SRC += userprog/gdt.c		# GDT 초기화.
userprog_SRC += userprog/tss.c		# TSS 관리.
