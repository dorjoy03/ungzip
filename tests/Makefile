test: test.o huffman_code.o
	gcc test.o huffman_code.o -o test

test.o: test.c ../huffman_code.h
	gcc -c test.c

huffman_code.o: ../huffman_code.c ../huffman_code.h
	gcc -c ../huffman_code.c

clean:
	rm *.o test
