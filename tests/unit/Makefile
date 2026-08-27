# tests/unit/Makefile - C unit tests for nxe_phase_sort_entries()

CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -Wno-unused-parameter
# -Wno-format: ngx_log_error()/ngx_conf_log_error() use nginx's own
# printf-like directives (%ui, %V), which the stub's plain fprintf()
# cannot satisfy and -Wformat would otherwise flag.
# -Wno-unused-function: test_runner.c pulls in the whole nxe_phase.c
# translation unit to reach the static nxe_phase_sort_entries(); the
# rest of that file's static functions (registry add/reset) are
# necessarily unused here since only sort_entries() is under test.
CFLAGS += -Wno-format -Wno-unused-function
CFLAGS += -I ngx_compat -I ../../src
CFLAGS += -DNXE_PHASE_TAG=unit_test

SRCS = test_runner.c \
       ngx_compat/ngx_stub.c

BIN      = test_runner
BIN_ASAN = test_runner_asan
BIN_COV  = test_runner_cov

.PHONY: test test-asan test-cov clean

test: $(BIN)
	./$(BIN)

test-asan: $(BIN_ASAN)
	./$(BIN_ASAN)

test-cov: $(BIN_COV)
	@rm -f *.gcda
	./$(BIN_COV)
	@echo ""
	@echo "=== Coverage Summary (nxe_phase_sort_entries) ==="
	@# nxe_phase.c is pulled into test_runner.c's translation unit (see
	@# test_runner.c), so most of its file-level line coverage reflects
	@# registry add/reset code this suite intentionally never calls
	@# (see -Wno-unused-function above). Report function-level coverage
	@# for the one function actually under test instead of the whole
	@# file.
	@LC_ALL=C gcov -f -n $(BIN_COV)-test_runner.gcda 2>/dev/null \
		| awk ' \
			/^Function .nxe_phase_sort_entries./ { active = 1; next } \
			active && /^Lines executed:/ { print; active = 0 }'

$(BIN): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

$(BIN_ASAN): $(SRCS)
	$(CC) $(CFLAGS) -fsanitize=address -fno-omit-frame-pointer -g \
		-o $@ $(SRCS)

$(BIN_COV): $(SRCS)
	$(CC) $(CFLAGS) --coverage -g -O0 -o $@ $(SRCS)

clean:
	rm -f $(BIN) $(BIN_ASAN) $(BIN_COV) *.gcno *.gcda *.gcov
