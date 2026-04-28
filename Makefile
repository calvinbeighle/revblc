CC ?= cc
CFLAGS ?= -std=c89 -Wall -Wextra -pedantic -O2

.PHONY: all clean native traces test test-reference tromp

all: revblc native_beta tromp

revblc: revblc.c
	$(CC) $(CFLAGS) -o revblc revblc.c

native_beta: native_beta.c
	$(CC) $(CFLAGS) -o native_beta native_beta.c

native: native_beta
	./native_beta examples/identity_app.blc
	./native_beta examples/native_erasure.blc
	./native_beta examples/native_duplication.blc

tromp:
	$(MAKE) -C instrumented_krivine

traces: revblc
	./revblc examples/identity_app.blc > traces/identity_app.txt
	./revblc examples/k_i_i.blc > traces/k_i_i.txt
	./revblc examples/closure_env.blc > traces/closure_env.txt
	$(MAKE) -C instrumented_krivine traces

test: revblc native_beta
	./revblc examples/identity.blc
	./revblc examples/identity_app.blc
	./revblc examples/k_i_i.blc
	./revblc examples/closure_env.blc
	./native_beta examples/identity_app.blc
	./native_beta examples/native_erasure.blc
	./native_beta examples/native_duplication.blc
	@if ./revblc --bits 10 >/tmp/revblc-stuck.txt 2>&1; then \
		echo "expected free variable to fail"; exit 1; \
	else \
		grep -q "stuck free variable: yes" /tmp/revblc-stuck.txt; \
	fi
	$(MAKE) -C instrumented_krivine test

test-reference:
	$(MAKE) -C instrumented_krivine test-reference

clean:
	rm -f revblc native_beta
	$(MAKE) -C instrumented_krivine clean
