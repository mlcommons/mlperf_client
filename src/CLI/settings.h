#pragma once


// System dependent
#if defined(_WIN32) || defined(_WIN64)
#define SETTINGS_PATH "Software\\MLCommons\\MLPerf"
#else
#define SETTINGS_PATH "com.mlcommons.mlperf"
#endif
