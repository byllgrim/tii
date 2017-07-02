TARG = tii
SRC = ${TARG}.c
OBJ = ${SRC:.c=.o}
CFLAGS = -Os -pedantic -std=c89 -Wall -Wextra
LDFLAGS  = -s

${TARG}: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

.c.o:
	${CC} -c ${CFLAGS} $<

clean:
	rm -f ${TARG} ${OBJ}
