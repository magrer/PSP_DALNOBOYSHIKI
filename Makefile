TARGET = DalniyReys2
OBJS = src/main_psp.o src/render_psp.o src/sim.o
CFLAGS = -O2 -G0 -Wall -Wextra -ffast-math -fno-strict-aliasing -std=gnu99
CXXFLAGS = $(CFLAGS)
LIBS = -lpspgum -lpspgu -lpspge -lpspdisplay -lpspctrl -lpsppower -lm
BUILD_PRX = 1
PSP_EBOOT_TITLE = ДАЛЬНИЙ РЕЙС 2.0
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_ICON = ICON0.PNG
SFOFLAGS = -s DISC_ID=DLRY01100 -s DISC_VERSION=01.40
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

PARAM.SFO: Makefile

# Header changes affect the shared Game layout across every PSP object.
$(OBJS): src/sim.h
src/main_psp.o: src/render_psp.h
src/render_psp.o: src/render_psp.h src/font.h
