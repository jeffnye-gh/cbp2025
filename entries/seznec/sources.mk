ENTRY=seznec
ENTRY_SRCS := \
  entries/$(ENTRY)/cond_branch_predictor_interface.cc \
  entries/$(ENTRY)/my_cond_branch_predictor.cc

ENTRY_INCS := -Ientries/$(ENTRY)

