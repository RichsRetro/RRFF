XBE_TITLE = RichsRetroXDash
INCDIR = $(CURDIR)/Includes
RESOURCEDIR = $(CURDIR)/../branding
XBE_LOGO = $(RESOURCEDIR)/rrxd-xbe-logo.pgm
SRCDIR = $(CURDIR)/Sources

SRCS += \
	$(CURDIR)/main.cpp \
	$(SRCDIR)/audioMenu.cpp \
	$(SRCDIR)/config.cpp \
	$(SRCDIR)/dashboardPaths.cpp \
	$(SRCDIR)/xisoKernel.cpp \
	$(SRCDIR)/font.cpp \
	$(SRCDIR)/fileExplorer.cpp \
	$(SRCDIR)/dvdCopy.cpp \
	$(SRCDIR)/gamePatches.cpp \
	$(SRCDIR)/ftpConnection.cpp \
	$(SRCDIR)/ftpServer.cpp \
	$(SRCDIR)/infoLog.cpp \
	$(SRCDIR)/langMenu.cpp \
	$(SRCDIR)/logViewer.cpp \
	$(SRCDIR)/logViewerMenu.cpp \
	$(SRCDIR)/menu.cpp \
	$(SRCDIR)/networkManager.cpp \
	$(SRCDIR)/networking.cpp \
	$(SRCDIR)/renderer.cpp \
	$(SRCDIR)/settingsMenu.cpp \
	$(SRCDIR)/sntpClient.cpp \
	$(SRCDIR)/subAppRouter.cpp \
	$(SRCDIR)/subsystems.cpp \
	$(SRCDIR)/timeMenu.cpp \
	$(SRCDIR)/timing.cpp \
	$(SRCDIR)/videoMenu.cpp \
	$(SRCDIR)/wipeCache.cpp \
	$(SRCDIR)/xbeLauncher.cpp \
	$(SRCDIR)/xbeScanner.cpp \
	$(CURDIR)/3rdparty/SDL_FontCache/SDL_FontCache.c

NXDK_DIR ?= $(CURDIR)/../nxdk
NXDK_SDL = y
NXDK_CXX = y
NXDK_NET = y
NXDK_DISABLE_AUTOMOUNT_D = y

GEN_XISO = ${XBE_TITLE}.iso

CXXFLAGS += -I$(CURDIR) \
            -I$(INCDIR) \
            -Wall \
            -Wextra \
            -std=gnu++11 \
            -DSDL_DISABLE_JOYSTICK_INIT_DELAY

CFLAGS   += -std=gnu11 \
            -DSDL_DISABLE_JOYSTICK_INIT_DELAY

ifneq ($(DEBUG),y)
CFLAGS += -O2
CXXFLAGS += -O2
endif

new_all: copy_resources all

include $(NXDK_DIR)/Makefile

copy_resources: $(OUTPUT_DIR)/config/config.json
	@mkdir -p $(OUTPUT_DIR)/assets $(OUTPUT_DIR)/licences $(OUTPUT_DIR)/game-patches
	@cp $(CURDIR)/../game-patches/*.json $(OUTPUT_DIR)/game-patches
	@cp $(RESOURCEDIR)/480.png $(RESOURCEDIR)/720.png $(RESOURCEDIR)/Share-Bold.ttf \
		$(RESOURCEDIR)/rich-retro-xdash-logo.png $(RESOURCEDIR)/Lato-Bold.ttf $(OUTPUT_DIR)/assets
	@cp $(RESOURCEDIR)/Lato-OFL.txt $(OUTPUT_DIR)/licences

$(OUTPUT_DIR)/config/config.json: $(CURDIR)/../config-hardmod.json
	@mkdir -p $(OUTPUT_DIR)/config
	cp $(CURDIR)/../config-hardmod.json $(OUTPUT_DIR)/config/config.json
