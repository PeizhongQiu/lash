all: dynamic_hash dynamic_hash_valid

dynamic_hash: test.o memory_management.o hash.o
	gcc -g -o dynamic_hash test.o memory_management.o hash.o -lm -lpthread -lpmem

dynamic_hash_valid: test_valid.o memory_management.o hash.o
	gcc -g -o dynamic_hash_valid test_valid.o memory_management.o hash.o -lm -lpthread -lpmem

test.o : test.cpp hash.h
	gcc -g -c test.cpp
test_valid.o : test_valid.cpp hash.h
	gcc -g -c test_valid.cpp
memory_management.o: memory_management.cpp memory_management.h hash.h
	gcc -g -c memory_management.cpp
hash.o :hash.cpp hash.h memory_management.h
	gcc -g -msse3 -c hash.cpp
clean:
	rm -rf *.o