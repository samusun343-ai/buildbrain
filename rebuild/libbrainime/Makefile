CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -std=c11
INCLUDE  = -Iinclude
AR      ?= ar

SRC = src/dict.c src/conn.c src/engine.c
OBJ = $(SRC:.c=.o)

all: libbrainime.a tools/bi tools/train_rerank tools/train_rerank_ck

libbrainime.a: $(OBJ)
	$(AR) rcs $@ $(OBJ)

%.o: %.c include/brainime.h src/bi_internal.h
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

tools/bi: tools/bi.c libbrainime.a
	$(CC) $(CFLAGS) $(INCLUDE) $< -L. -lbrainime -o $@

tools/train_rerank: tools/train_rerank.c libbrainime.a
	$(CC) $(CFLAGS) $(INCLUDE) $< -L. -lbrainime -o $@

# M9: checkpointing trainer is canonical here (imported from the
# milestone work trees; the copies under m*work/ are stale builds
# against old libraries and must not be used — see REPORT-m9.md).
tools/train_rerank_ck: tools/train_rerank_ck.c libbrainime.a
	$(CC) $(CFLAGS) $(INCLUDE) $< -L. -lbrainime -o $@

test: tools/bi tests/session_test
	-python3 tests/check.py tools/bi $(DICT)
	python3 tests/check_lookup.py tools/bi $(DICT)
	./tests/session_test $(DICT)

tests/session_test: tests/session_test.c libbrainime.a
	$(CC) $(CFLAGS) $(INCLUDE) $< -L. -lbrainime -o $@

DICT ?= ../../ime-dict/brainux2.bidf

# M2 sidecar: when the generated .bisc is present, tests run with
# the lexicalised (mozc matrix) connection model; without it the
# engine falls back to the M1 rule model.
ifneq ($(wildcard data/brainux2.bisc),)
export BI_SIDECAR := $(abspath data/brainux2.bisc)
endif

MOZC_DIR ?= data/mozc/src/data/dictionary_oss
sidecar:
	python3 tools/gen_sidecar.py --bidf $(DICT) \
		--mozc-dir $(MOZC_DIR) --out data/brainux2.bisc

# M3 acceptance on the v3 file: same corpora as `test`, plus the
# session suite against the v3 dictionary.  BI_SIDECAR is cleared:
# a v3 file carries its connection model inside.
V3 = data/brainux3.bid3
test3: all tests/session_test
	-BI_SIDECAR= python3 tests/check.py tools/bi $(V3) tests/sentences.tsv
	-BI_SIDECAR= python3 tests/check.py tools/bi $(V3) tests/heldout.tsv
	BI_SIDECAR= ./tests/session_test $(V3)
	sh tests/crc_test.sh $(V3) tools/bi

clean:
	rm -f $(OBJ) libbrainime.a tools/bi tests/session_test \
		tools/train_rerank tools/train_rerank_ck

.PHONY: all test test3 clean
