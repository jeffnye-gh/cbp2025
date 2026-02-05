.PHONY: clean_hist1 hist1

hist1:
	$(MAKE) clean_hist1
	$(MAKE) ENTRY=hist1

run_hist1:
	bash scripts/run_selected.sh --pred hist1 --group $(R_GROUPS)

one_hist1:
	$(MAKE) hist1
#	bash scripts/run_selected.sh --pred hist1 --group sml
	bash scripts/run_selected.sh --pred hist1 --group sample_traces

one_tage192:
	bash scripts/run_selected.sh --pred tage192 --group sml

## Add hist1 to the build entries list (preserves existing ordering).
#ENTRIES += hist1
#
## Optional: include hist1 in the run list if you want it exercised by run_entries.
## (Keeps any user override like: make R_ENTRIES=... run_entries)
#R_ENTRIES ?= behrendt,cai,cbp2016,fan,jimenez,koizumi,man,mose,ros,seznec
#R_ENTRIES := $(R_ENTRIES),hist1
#
## Optional: give hist1 its own clean target and hook it into clean_entries.
#clean_hist1:
#	rm -rf results/hist1

clean_hist1:
	-rm -rf build/hist1
	-rm -f bin/cbp.hist1

#
#clean_entries: clean_hist1
#
