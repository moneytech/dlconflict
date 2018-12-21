all: dlconflict.so test
dlconflict.so:
	$(CC) $(CFLAGS) -Wall -Wpedantic -std=c99 -fPIC dlconflict.c -shared -o $@
test: dlconflict.so
	$(MAKE) -B -C testcase test;
clean:
	$(MAKE) -B -C testcase clean;
	rm *.so

