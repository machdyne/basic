CFLAGS = -std=gnu99 -Wall -Wextra -O2

basic: basic.c basic.h fs/fs.h
	gcc $(CFLAGS) -DTARGET_LINUX -o basic basic.c

# Filesystem tests run with sanitizers and a large block limit, so that
# media up to 4MB can be tested.
fs_test: fs/fs.c fs/fs.h fs/test/fs_test.c
	gcc -std=gnu99 -Wall -Wextra -O1 -g -fsanitize=address,undefined \
		-DFS_MAX_BLOCKS=65534 -o fs_test fs/test/fs_test.c fs/fs.c

# The interpreter with the module filesystem on a simulated F-RAM.
basic_fs: basic.c basic.h fs/fs.c fs/fs.h fs/test/basic_fs.c
	gcc $(CFLAGS) -o basic_fs basic.c fs/fs.c fs/test/basic_fs.c

# The Sechs core with the interpreter and filesystem, on a simulated bus.
sechs_test: basic.c basic.h fs/fs.c fs/fs.h sechs/sechs.c sechs/sechs.h sechs/test/sechs_test.c
	gcc -std=gnu99 -Wall -Wextra -O1 -g -fsanitize=address,undefined \
		-o sechs_test basic.c fs/fs.c sechs/sechs.c sechs/test/sechs_test.c
	gcc -std=gnu99 -Wall -Wextra -O1 -g -fsanitize=address,undefined \
		-DSECHS_PROGRAM -o sechs_test_prog basic.c fs/fs.c sechs/sechs.c sechs/test/sechs_test.c

# The Sechs controller for Linux, and the same tool talking to a simulated
# module (used by the tests).
sechsctl: tools/sechs/sechs.c sechs/sechs.h
	gcc $(CFLAGS) -o sechsctl tools/sechs/sechs.c

sechs_sim: tools/sechs/sechs.c tools/sechs/sim.h basic.c basic.h fs/fs.c sechs/sechs.c sechs/sechs.h
	gcc -std=gnu99 -Wall -Wextra -O1 -DSECHS_SIM -DHW_FILES_FS \
		-o sechs_sim tools/sechs/sechs.c basic.c fs/fs.c sechs/sechs.c

# the bridge protocol on the host, with a simulated module (for the tests)
bridge_host: tools/sechs/test/bridge_host.c tools/sechs/bridge.c tools/sechs/bridge.h tools/sechs/sim.h basic.c fs/fs.c sechs/sechs.c
	gcc -std=gnu99 -Wall -Wextra -O1 -DSECHS_SIM -DHW_FILES_FS \
		-o bridge_host tools/sechs/test/bridge_host.c tools/sechs/bridge.c \
		tools/sechs/ch32prog.c basic.c fs/fs.c sechs/sechs.c

# the CH32V003 programmer against a simulated chip
ch32prog_test: tools/sechs/ch32prog.c tools/sechs/ch32prog.h tools/sechs/test/ch32prog_test.c tools/sechs/test/ch32sim.h
	gcc -std=gnu99 -Wall -Wextra -O1 -o ch32prog_test tools/sechs/test/ch32prog_test.c tools/sechs/ch32prog.c

# the filesystem in its NOR flash mode (Werkzeug), on a simulated NOR flash
fs_nor_test: fs/test/fs_test.c fs/fs.c fs/fs.h
	gcc -std=gnu99 -Wall -Wextra -O2 -DFS_NOR -DFS_MAX_BLOCKS=512 \
		-o fs_nor_test fs/test/fs_test.c fs/fs.c

test: basic basic_fs fs_test fs_nor_test sechs_test sechsctl sechs_sim bridge_host ch32prog_test
	./fs_test
	./fs_nor_test
	./ch32prog_test
	./sechs_test
	./sechs_test_prog
	./sechs_test_prog
	bash testsuite.sh

test-quick: basic basic_fs fs_test fs_nor_test sechs_test sechsctl sechs_sim bridge_host ch32prog_test
	./fs_test quick
	./fs_nor_test quick
	./ch32prog_test
	./sechs_test
	bash testsuite.sh

# reference sheet from docs/basic1.md, docs/targets.md and docs/sechs.md
# (the PDF needs WeasyPrint)
poster: tools/poster/poster.py docs/basic1.md docs/targets.md docs/sechs.md
	python3 tools/poster/poster.py --html poster.html --pdf poster.pdf

# the getting-started guide, one A4 page (docs/guide.md)
guide: tools/poster/guide.py docs/guide.md
	python3 tools/poster/guide.py --html guide.html --pdf guide.pdf

clean:
	rm -f poster.html poster.pdf guide.html guide.pdf ch32prog_test basic basic_fs fs_test fs_nor_test sechs_test sechsctl sechs_sim bridge_host

.PHONY: test test-quick clean
