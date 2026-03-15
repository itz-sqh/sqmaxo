CC = clang
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic ${shell pkg-config --cflags x11 xft}
LFLAGS = ${shell pkg-config --libs x11 xft}
TARGET = a.out
SRC = main.c
OBJ = ${SRC:.c=.o}

all: ${TARGET}

run: ${TARGET}
	./${TARGET} testfile

${TARGET}: ${OBJ}
	${CC} -o $@ $^ ${LFLAGS}

%.o: %.c
	${CC} ${CFLAGS} -c $< -o $@

clean:
	git clean -fdx

.PHONY: all run clean
