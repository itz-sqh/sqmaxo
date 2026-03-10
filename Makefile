CC = clang
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic ${shell pkg-config --cflags x11}
LFLAGS = ${shell pkg-config --libs x11}
TARGET = a.out
SRC = main.c
OBJ = ${SRC:.c=.o}

all: ${TARGET}

run: ${TARGET}
	./${TARGET}

${TARGET}: ${OBJ}
	${CC} -o $@ $^ ${LFLAGS}

%.o: %.c
	${CC} ${CFLAGS} -c $< -o $@

clean:
	git clean -fdx

.PHONY: all run clean
