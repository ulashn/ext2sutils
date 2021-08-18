all:
	g++ main.cpp ext2fs.h -o ext2sutils

clean:
	rm ext2sutils