ungzip: ungzip.o decompress.o huffman_tree.o huffman_code.o
	gcc ungzip.o decompress.o huffman_tree.o huffman_code.o -o ungzip

ungzip.o: ungzip.c decompress.h
	gcc -O2 -c ungzip.c

decompress.o: decompress.c decompress.h huffman_tree.h
	gcc -O2 -c decompress.c

huffman_tree.o: huffman_tree.c huffman_tree.h huffman_code.h
	gcc -O2 -c huffman_tree.c

huffman_code.o: huffman_code.c huffman_code.h
	gcc -O2 -c huffman_code.c

clean:
	rm *.o ungzip
