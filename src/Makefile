CC = gcc
# XXX: does -D_FILE_OFFSET_BITS affects sizeof(off_t) ?
# -D_FILE_OFFSET_BITS affects sizeof(off_t, blkcnt_t and ino_t)
CFLAGS = -g -Wall `pkg-config fuse --cflags`
FUSE_LIB = -g -Wall `pkg-config fuse --libs` -L/usr/lib

DEPS = *.h

all: format ufs

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

ufs: ufs.o errorlog.o error.o ufslib.o
	$(CC) $(CFLAGS) ufs.o errorlog.o error.o ufslib.o \
		$(FUSE_LIB) -o ufs

format: format.o error.o
	$(CC) $(CFLAGS) format.o error.o -o format

pdf: *.tex
	latexmk -pdf -pdflatex="xelatex -interactive=nostopmode" \
		-use-make fs_based_on_fuse.tex

ignore:
	sed -i '/### Makefile ###/,$$$ d' .gitignore
	echo '### Makefile ###' >> .gitignore
	(for file in `ls`; do \
		if [ -f $$file ] && [ -x $$file ]; \
		then echo $$file >> .gitignore;	\
		fi \
	done)

clean:
	rm -f *.o format ufs fs_based_on_fuse.pdf *.aux *.toc \
		*.log *.out *.fls *latex*
