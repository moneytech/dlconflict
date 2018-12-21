all: entry 
libs :=  W_A.so A_B_C.so imports.so
A_B_C.so:
	gcc -o $@ -shared exports_A_B_C.c 
W_A.so:
	gcc -o $@ -shared exports_W_A.c
imports.so:
	gcc -o $@ -shared imports_A_B.c imports_A_C_W.c
entry: $(libs)
	gcc -o $@ entry.c -L.  -l:imports.so -l:W_A.so -l:A_B_C.so -ldl
test: entry
	@(./entry 2>&1 | grep "name: 'A' bind") \
		&& echo 'test succeeded found conflict between A and A' || echo 'test failed'


clean:
	rm -f *.so entry
