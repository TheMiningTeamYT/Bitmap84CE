NAME = PICVIEW
DESCRIPTION = "High quality image viewer for the TI 84 Plus CE"
COMPRESSED = YES
COMPRESSED_MODE = zx7
ARCHIVED = YES
CFLAGS = -O3 -ffast-math -fapprox-func -funroll-loops -fslp-vectorize -fjump-tables -ftree-vectorize -mstackrealign -finline-functions
CXXFLAGS = -O3 -ffast-math -fapprox-func -funroll-loops -fslp-vectorize -fjump-tables -ftree-vectorize -mstackrealign -finline-functions
ICON = logo.png
PREFER_OS_LIBC = YES
include $(shell cedev-config --makefile)