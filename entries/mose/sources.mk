ENTRY=mose
ENTRY_SRCS := \
  entries/$(ENTRY)/cond_branch_predictor_interface.cc \
  entries/$(ENTRY)/my_cond_branch_predictor.cc \
  entries/$(ENTRY)/idioms/hash.cc \
  entries/$(ENTRY)/idioms/idiom_tracker_asciz.cc \
  entries/$(ENTRY)/idioms/idiom_tracker.cc \
  entries/$(ENTRY)/idioms/idiom_tracker_for.cc \
  entries/$(ENTRY)/idioms/uop_tracker.cc

ENTRY_INCS := -Ientries/$(ENTRY) -Ientries/$(ENTRY)/idioms

