TARG = tii
SRC = ${TARG}.c
OBJ = ${SRC:.c=.o}
CFLAGS = -Os -pedantic -std=c89 -Wall -Wextra
LDFLAGS  = -s
PREFIX = /usr/local

${TARG}: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

.c.o:
	${CC} -c ${CFLAGS} $<

clean:
	rm -f ${TARG} ${OBJ}

install: ${TARG}
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f ${TARG} ${DESTDIR}${PREFIX}/bin/
	chmod 755 ${DESTDIR}${PREFIX}/bin/${TARG}
