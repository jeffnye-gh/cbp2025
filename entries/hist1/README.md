3-way mechanical split of original file.

Files:
  nhist1_sc.hpp     SC + GEHL + RBias + history structs + constants
  nhist1_tage.hpp   TAGE tables, folding, allocation, helpers
  nhist1_hist1.hpp  class nHIST1::HIST1 implementation
  nhist1.hpp        umbrella header
  hist1_adapter.cpp external adapter (outside namespace)

Usage:
  Include "nhist1.hpp" where the old file was included.
  Compile hist1_adapter.cpp.


brtype
  0x1 conditional
  0x2 ?
  0x4 call
  0x8 return


