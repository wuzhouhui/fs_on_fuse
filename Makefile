CC = gcc
# XXX: does -D_FILE_OFFSET_BITS affects sizeof(off_t) ?
CFLAGS = -g -Wall -D_FILE_OFFSET_BITS=64
DEPS = *.h

all: format ufs

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

ufs: ufs.o error.o
	$(CC) $(CFLAGS)  ufs.o error.o -o ufs

format: format.o error.o
	$(CC) $(CFLAGS) format.o error.o -o format

ignore:
	sed -i '/### Makefile ###/,$$$ d' .gitignore
	echo '### Makefile ###' >> .gitignore
	(for file in `ls`; do \
		if [ -f $$file ] && [ -x $$file ]; \
		then echo $$file >> .gitignore;	\
		fi \
	done)

clean:
	rm -f *.o format a.out
