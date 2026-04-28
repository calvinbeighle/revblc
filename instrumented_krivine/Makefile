CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -O2
REFERENCE_UNI ?= /tmp/AIT/uni

.PHONY: all clean test test-io test-reference traces

all: krivine_rev

krivine_rev: tromp.c rlog.h
	$(CC) $(CFLAGS) -o krivine_rev tromp.c

test: krivine_rev test-io
	./krivine_rev ../examples/identity.blc
	./krivine_rev ../examples/identity_app.blc
	./krivine_rev ../examples/k_i_i.blc
	./krivine_rev ../examples/omega.blc --max-steps 12

test-io: krivine_rev
	./krivine_rev ../examples/cat.blc --input 0101 > /tmp/tromp-cat-bit.out 2> /tmp/tromp-cat-bit.err
	@test "$$(cat /tmp/tromp-cat-bit.out)" = "0101"
	@grep -q "round trip: yes" /tmp/tromp-cat-bit.err
	@grep -q "kLazy restored: yes" /tmp/tromp-cat-bit.err
	./krivine_rev ../examples/cat.blc --byte --input A > /tmp/tromp-cat-byte.out 2> /tmp/tromp-cat-byte.err
	@test "$$(cat /tmp/tromp-cat-byte.out)" = "A"
	@grep -q "round trip: yes" /tmp/tromp-cat-byte.err
	@grep -q "kLazy restored: yes" /tmp/tromp-cat-byte.err
	@printf '\040' > /tmp/tromp-cat.Blc
	./krivine_rev /tmp/tromp-cat.Blc --input 01 > /tmp/tromp-cat-packed-ext.out 2> /tmp/tromp-cat-packed-ext.err
	@test "$$(cat /tmp/tromp-cat-packed-ext.out)" = "01"
	@grep -q "round trip: yes" /tmp/tromp-cat-packed-ext.err
	@printf '\040' | ./krivine_rev --packed - --input 10 > /tmp/tromp-cat-packed-stdin.out 2> /tmp/tromp-cat-packed-stdin.err
	@test "$$(cat /tmp/tromp-cat-packed-stdin.out)" = "10"
	@grep -q "round trip: yes" /tmp/tromp-cat-packed-stdin.err
	./krivine_rev ../examples/reverse.Blc --byte --input abc > /tmp/tromp-reverse.out 2> /tmp/tromp-reverse.err
	@test "$$(cat /tmp/tromp-reverse.out)" = "cba"
	@grep -q "round trip: yes" /tmp/tromp-reverse.err
	@printf '\000\001\177\377' > /tmp/tromp-binary.in
	./krivine_rev ../examples/cat.blc --byte --input-file /tmp/tromp-binary.in > /tmp/tromp-binary.out 2> /tmp/tromp-binary.err
	@cmp -s /tmp/tromp-binary.in /tmp/tromp-binary.out
	@grep -q "round trip: yes" /tmp/tromp-binary.err

test-reference: krivine_rev
	@test -x "$(REFERENCE_UNI)" || { echo "missing reference uni at $(REFERENCE_UNI)"; echo "build it with: git clone https://github.com/tromp/AIT /tmp/AIT && make -C /tmp/AIT uni"; exit 1; }
	@printf 'Hello' > /tmp/tromp-ref-input.txt
	@printf '\040Hello' | "$(REFERENCE_UNI)" > /tmp/tromp-ref-cat.uni 2> /tmp/tromp-ref-cat.uni.err
	@printf '\040' > /tmp/tromp-ref-cat.Blc
	./krivine_rev /tmp/tromp-ref-cat.Blc --byte --input-file /tmp/tromp-ref-input.txt > /tmp/tromp-ref-cat.rev 2> /tmp/tromp-ref-cat.rev.err
	@cmp -s /tmp/tromp-ref-cat.uni /tmp/tromp-ref-cat.rev
	@grep -q "round trip: yes" /tmp/tromp-ref-cat.rev.err
	@cat ../examples/reverse.Blc /tmp/tromp-ref-input.txt | "$(REFERENCE_UNI)" > /tmp/tromp-ref-reverse.uni 2> /tmp/tromp-ref-reverse.uni.err
	./krivine_rev ../examples/reverse.Blc --byte --input-file /tmp/tromp-ref-input.txt > /tmp/tromp-ref-reverse.rev 2> /tmp/tromp-ref-reverse.rev.err
	@cmp -s /tmp/tromp-ref-reverse.uni /tmp/tromp-ref-reverse.rev
	@grep -q "round trip: yes" /tmp/tromp-ref-reverse.rev.err
	@printf 'reference parity: yes (cat, reverse.Blc)\n'

traces: krivine_rev
	./krivine_rev ../examples/identity.blc > traces/identity.txt 2>&1
	./krivine_rev ../examples/identity_app.blc > traces/identity_app.txt 2>&1
	./krivine_rev ../examples/k_i_i.blc > traces/k_i_i.txt 2>&1
	./krivine_rev ../examples/omega.blc --max-steps 12 > traces/omega_12.txt 2>&1
	./krivine_rev ../examples/cat.blc --input 0101 > traces/cat_bit.txt 2>&1
	./krivine_rev ../examples/cat.blc --byte --input A > traces/cat_byte.txt 2>&1
	@printf '\040' > /tmp/tromp-cat.Blc
	./krivine_rev /tmp/tromp-cat.Blc --input 01 > traces/cat_packed.txt 2>&1
	./krivine_rev ../examples/reverse.Blc --byte --input abc > traces/reverse_byte.txt 2>&1
	@printf '\000\001\177\377' > /tmp/tromp-binary.in
	./krivine_rev ../examples/cat.blc --byte --input-file /tmp/tromp-binary.in > /tmp/tromp-binary.out 2> traces/cat_binary.txt

clean:
	rm -f krivine_rev
