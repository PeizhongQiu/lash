all: dynamic_hash dynamic_hash_valid

dynamic_hash: test.o memory_management.o hash.o
	gcc -g -o dynamic_hash test.o memory_management.o hash.o -lm -lpthread -lpmem

dynamic_hash_valid: test_valid.o memory_management.o hash.o
	gcc -g -o dynamic_hash_valid test_valid.o memory_management.o hash.o -lm -lpthread -lpmem

test.o : test.c hash.h
	gcc -g -c test.c
test_valid.o : test_valid.c hash.h
	gcc -g -c test_valid.c
memory_management.o: memory_management.c memory_management.h hash.h
	gcc -g -c memory_management.c 
hash.o :hash.c hash.h memory_management.h
	gcc -g -c hash.c
clean:
	rm -rf *.o