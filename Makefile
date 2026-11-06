CC ?= cc
CFLAGS ?= -O2
WARN = -std=c11 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS += -Isrc
ASAN_OPTIONS ?= detect_leaks=1
GUI_CFLAGS = $(shell pkg-config --cflags xft x11 xt 2>/dev/null)
GUI_LIBS = -lXm $(shell pkg-config --libs xft x11 xt 2>/dev/null)
.PHONY: all check-deps test sanitize clean source
all: check-deps build/mdv
check-deps:
	@pkg-config --exists xft x11 xt || { echo 'Faltam dependências de desenvolvimento: libmotif-dev libxft-dev libx11-dev libxt-dev e pkg-config.' >&2; exit 1; }
	@printf '#include <Xm/Xm.h>\n' | $(CC) $(CPPFLAGS) $(GUI_CFLAGS) -x c -fsyntax-only - || { echo 'Faltam os cabeçalhos Motif (libmotif-dev).' >&2; exit 1; }
build:
	mkdir -p build
build/mdv: src/mdv_motif.c src/md_document.c src/md_document.h | build
	$(CC) $(CPPFLAGS) $(GUI_CFLAGS) $(CFLAGS) $(WARN) src/mdv_motif.c src/md_document.c $(LDFLAGS) $(GUI_LIBS) -o $@
build/mdv-core: src/md_core_cli.c src/md_document.c src/md_document.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) src/md_core_cli.c src/md_document.c $(LDFLAGS) -o $@
build/test-core: tests/test_core.c src/md_document.c src/md_document.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) tests/test_core.c src/md_document.c $(LDFLAGS) -o $@
test: build/test-core build/mdv-core
	./build/test-core
	python3 tests/test_files.py
sanitize: | build
	$(CC) $(CPPFLAGS) $(WARN) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_core.c src/md_document.c -o build/test-sanitize
	ASAN_OPTIONS=$(ASAN_OPTIONS) ./build/test-sanitize
source:
	mkdir -p dist
	tar --exclude=./build --exclude=./dist -cJf dist/mdv-motif-0.1.0-dev-fontes.tar.xz --transform='s,^\.,mdv-motif-0.1.0-dev,' .
clean:
	rm -rf build
