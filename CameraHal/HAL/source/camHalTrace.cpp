#ifndef ANDROID_OS
#include "camHalTrace.h"
 int globalTraceLev = 0;
 CREATE_TRACER(CAMHAL_INFO,	   "CAMHALInfo:   ", INFO,    1);
 CREATE_TRACER(CAMHAL_WARNING,    "CAMHALWarn:   ", WARNING, 1);
 CREATE_TRACER(CAMHAL_ERROR,	   "CAMHALError:  ", ERROR,   1);
 
#endif

