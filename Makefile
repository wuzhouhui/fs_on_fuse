CC = gcc
CFLAGS = -g -Wall
DEPS = *.h

all: format

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

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
