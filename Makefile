# Makefile
#
# Copyright (C) Stewart Brodie, 2019

PROG = zyzzyva-dawg
CPPFLAGS = -Wall -std=c++11

.PHONY: all clean
all: $(PROG)
clean:; rm -f $(PROG)

TESTPROG := ./$(PROG)
TESTDATA := testdata
TMP := $(TESTDATA)/tmp

# A test case will try to compile the text file and compare it with the pre-built DAWG
# and then try to decompile the pre-built DAWG and make sure it matches the text file.
define testcase
.PHONY: tests-$2
tests: tests-$2 | $(PROG) test-tmp-dir
tests-$2:
	@rm -f $(TMP)/$2.gen.dwg $(TMP)/$2.gen.txt
	@$(TESTPROG) create $1/$2.txt $(TMP)/$2.gen.dwg
	@$(TESTPROG) dump $(TESTDATA)/$2.dwg $(TMP)/$2.gen.txt
	@diff -q $(TMP)/$2.gen.txt $1/$2.txt
	@diff -q $(TMP)/$2.gen.dwg $1/$2.dwg
	@rm -f $(TMP)/$2.gen.dwg $(TMP)/$2.gen.txt
	@echo $2: PASS
endef
test-tmp-dir:; @mkdir -p $(TMP)
tests:

TESTS := $(basename $(wildcard $(TESTDATA)/*.dwg))
$(foreach t,$(TESTS),$(eval $(call testcase,$(TESTDATA),$(notdir $(t)))))
