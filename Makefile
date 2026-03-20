.PHONY: clean_baseline clean_entries run_baseline run_entries summary \
        simple_tables merge all lib clean

CC      = g++
OPT     = -O3
CXXSTD  = -DINSTRUMENT_HISTORY -std=c++17
DEBUG   = 0

#OPT    = -O0 -g
#CXXSTD = -DINSTRUMENT_HISTORY -DTR_EN=1 -std=c++17

ifeq ($(DEBUG), 1)
  CC += -ggdb3
endif

TOP     := .
LIBDIR  := $(TOP)/lib
LDLIBS  := -lz
COMMON_INC := -I$(TOP) -I$(LIBDIR)

ENTRIES ?= exp_koizumi behrendt cai cbp2016 fan jimenez koizumi man mose ros seznec tage192

ifdef ENTRY
  ENTRIES := $(ENTRY)
endif

BIN_DIR := $(TOP)/bin
BUILD   := $(TOP)/build

all: lib $(addprefix $(BIN_DIR)/cbp.,$(ENTRIES))

lib:
	$(MAKE) -C lib DEBUG=$(DEBUG)

# -------------------------------------------------------------------
# Build the competition models
# -------------------------------------------------------------------

define MAKE_ENTRY_RULES

# Reset per-entry temp vars before include.
ENTRY_SRCS :=
ENTRY_INCS :=

include $(TOP)/entries/$(1)/sources.mk

# Capture into per-entry vars so they don't "float" later.
ENTRY_SRCS_$(1) := $$(ENTRY_SRCS)
ENTRY_INCS_$(1) := $$(ENTRY_INCS)

ENTRY_OBJS_$(1) := $$(patsubst %.cc,$$(BUILD)/$(1)/%.o,$$(ENTRY_SRCS_$(1)))

$$(BIN_DIR)/cbp.$(1): $$(ENTRY_OBJS_$(1)) | lib
	@mkdir -p $$(BIN_DIR)
	$$(CC) $$(CXXSTD) $$(OPT) -L$$(LIBDIR) -o $$@ $$(ENTRY_OBJS_$(1)) \
	    -lcbp $$(LDLIBS)

# Compile rule for this entry: note ENTRY_INCS_$(1), not ENTRY_INCS.
$$(BUILD)/$(1)/%.o: %.cc
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CXXSTD) $$(OPT) $$(COMMON_INC) $$(ENTRY_INCS_$(1)) \
	    -c -o $$@ $$<

endef

$(foreach e,$(ENTRIES),$(eval $(call MAKE_ENTRY_RULES,$(e))))
# -------------------------------------------------------------------
# Build the development model
# -------------------------------------------------------------------

# -------------------------------------------------------------------
# Run the models
# -------------------------------------------------------------------
clean_baseline:
	chmod -R +w results/tage192
	rm -rf results/tage192

clean_entries:
	rm -rf results/*.csv
	rm -rf results/behrendt
	rm -rf results/cai
	rm -rf results/cbp2016
	rm -rf results/fan
	rm -rf results/jimenez
	rm -rf results/koizumi
	rm -rf results/man
	rm -rf results/mose
	rm -rf results/ros
	rm -rf results/seznec

run_baseline:
	bash scripts/run_selected.sh --pred tage192 --group all
	chmod -R -w results/tage192

R_ENTRIES ?= behrendt,cai,cbp2016,fan,jimenez,koizumi,man,mose,ros,seznec
R_GROUPS  ?= compress,fp,infra,int,media,web

run_entries:
	bash scripts/run_selected.sh --pred $(R_ENTRIES) --group $(R_GROUPS)
	chmod -R -w results/*

# -------------------------------------------------------------------
# Data formatting etc.
# -------------------------------------------------------------------
simple_tables:
	python3 scripts/emit_cbp_tables.py \
  --results_root results \
  --baseline tage192 \
  --groups int fp web media compress infra \
  --show_aliases

summary: merge
merge:
	python3 scripts/merge_by_group.py \
		--results_root results \
		--out_csv summary/group_results.csv \
		--summary_csv summary/group_summary.csv \
		--baseline tage192 \
		--groups compress fp infra int media web \
		--drop_fail

# -------------------------------------------------------------------
# Static info about traces
# -------------------------------------------------------------------
trace_info:
	python3 scripts/summarize_baseline_workload.py \
	  --results_root ./golden_results/tage192

#	  --results_root ./results/tage192

# -------------------------------------------------------------------
# -------------------------------------------------------------------

clean:
	rm -rf $(BUILD)
	rm -f $(BIN_DIR)/cbp.*
	$(MAKE) -C lib clean
	rm -rf summary

-include hist1.mk
