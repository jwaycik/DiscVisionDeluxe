################################################################################
# GrabFrames Makefile
################################################################################
PROJECT_ROOT=.
OPT_INC = ${PROJECT_ROOT}/common/make/common_spin.mk
-include ${OPT_INC}

################################################################################
# Key paths and settings
################################################################################
CFLAGS += -std=c++11 -pthread
CPPFLAGS = $(shell pkg-config --cflags opencv4)
LIB += $(shell pkg-config --libs opencv4)
PLATFORM = $(shell uname)
ifeq ($(wildcard ${OPT_INC}),)
CXX = g++ ${CFLAGS} ${CPPFLAGS}
CXX_WIN = x86_64-w64-mingw32-g++ ${CFLAGS} ${CPPFLAGS}
ODIR  = .obj/build${D}
SDIR  = .
MKDIR = mkdir -p
endif

OUTPUTNAME = GrabFrames522${D}
OUTDIR = bin

################################################################################
# Dependencies
################################################################################
# Spinnaker deps
SPINNAKER_LIB = -Llib -lSpinnaker${D} ${SPIN_DEPS}

################################################################################
# Master inc/lib/obj/dep settings
################################################################################
_OBJ = GrabFrames522.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))
INC = -Iinc
ifneq ($(OS),mac)
INC += -I/usr/include/spinnaker
LIB += -Wl,-Bdynamic ${SPINNAKER_LIB}
LIB += -Wl,-rpath-link=../../lib 
endif

#opencv stuff
LIB += -lopencv_core -lopencv_highgui -lopencv_imgproc -lopencv_videoio 
LIB += -L/usr/local/lib -lopencv_core -lopencv_imgcodecs -lopencv_highgui

################################################################################
# Rules/recipes
################################################################################

# Final binary LINUX
${OUTPUTNAME}: ${OBJ}
	${CXX} -o ${OUTPUTNAME} ${OBJ} ${LIB}
	mv ${OUTPUTNAME} ${OUTDIR}

# Final binary WINDOWS
#${OUTPUTNAME}: ${OBJ}
#	${CXX_WIN} -o ${OUTPUTNAME} ${OBJ} ${LIB}
#	mv ${OUTPUTNAME} ${OUTDIR}

# Intermediate object files
${OBJ}: ${ODIR}/%.o : ${SDIR}/%.cpp
	@${MKDIR} ${ODIR}
	${CXX} ${CFLAGS} ${CPPFLAGS} ${INC} -Wall -D LINUX -c $< -o $@

# Clean up intermediate objects
clean_obj:
	rm -f ${OBJ}
	@echo "intermediate objects cleaned up!"

# Clean up everything.
clean: clean_obj
	rm -f ${OUTDIR}/${OUTPUTNAME}
	@echo "all cleaned up!"
