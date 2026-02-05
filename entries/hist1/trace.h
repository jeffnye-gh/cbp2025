
#pragma once

#include <iostream>

#if TR_EN==1
#define TR(s) std::cerr<<"TR XXX"<<s<<std::endl;
#else
#define TR(s)
#endif
