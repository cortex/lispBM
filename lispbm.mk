first_rule: all

LISPBM_SRC = $(LISPBM)/src/env.c \
             $(LISPBM)/src/fundamental.c \
             $(LISPBM)/src/heap.c \
             $(LISPBM)/src/lispbm_memory.c \
             $(LISPBM)/src/print.c \
             $(LISPBM)/src/qq_expand.c \
             $(LISPBM)/src/stack.c \
             $(LISPBM)/src/symrepr.c \
             $(LISPBM)/src/tokpar.c \
             $(LISPBM)/src/compression.c \
             $(LISPBM)/src/prelude.c \
             $(LISPBM)/src/extensions.c \
             $(LISPBM)/src/lispbm.c \
             $(LISPBM)/src/ec_eval.c \
             $(LISPBM)/src/eval_cps.c

LISPBM_INC = -I$(LISPBM)/include \
             -I$(LISPBM)/src

LISPBM_FLAGS =
LISPBM_DEPS  =

LISPBM_FLAGS += -D_PRELUDE

LISPBM_DEPS += $(LISPBM)/src/prelude.xxd

$(LISPBM)/src/prelude.xxd: $(LISPBM)/src/prelude.lisp
	xxd -i < $(LISPBM)/src/prelude.lisp > $(LISPBM)/src/prelude.xxd
