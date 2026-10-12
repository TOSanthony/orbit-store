# Included after the upstream Format7zF makefile. Build only the three readers
# Orbit accepts. No executable or dynamic loading. The C bridge exposes only
# IInArchive and writes through Orbit's validated callbacks.
LOCAL_FLAGS = -DZ7_ST
ifdef ORBIT_PS5
LOCAL_FLAGS += -DORBIT7ZIP_PS5
endif
AR_OBJS =
CAB_OBJS =
CHM_OBJS =
COM_OBJS =
ISO_OBJS =
NSIS_OBJS =
TAR_OBJS =
UDF_OBJS =
WIM_OBJS =
AR_OBJS_2 = $O/ArchiveExports.o
COMPRESS_OBJS_2 =
OBJS = $(ARC_OBJS) $(AR_OBJS_2) $(SYS_OBJS)
.PHONY: orbit-static
orbit-static: $(OBJS)
	$(AR) rcs $(O)/liborbit7zip.a $(OBJS)
