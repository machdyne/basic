basic:
	gcc -DTARGET_LINUX -o basic basic.c

clean:
	rm -f basic

.PHONY: clean
