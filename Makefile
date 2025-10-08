
export TOPDIR	:=	$(CURDIR)

export DESTDIR	:=	$(DESTDIR)

default: cube-release wii-release

all: debug release

debug: cube-debug wii-debug

release: cube-release wii-release

cube-debug:
	-$(MAKE) -C libogc-rice PLATFORM=cube BUILD=cube_debug
	$(MAKE) -C libogc2 PLATFORM=gamecube BUILD=gamecube_debug

wii-debug:
	-$(MAKE) -C libogc-rice PLATFORM=wii BUILD=wii_debug
	$(MAKE) -C libogc2 PLATFORM=wii BUILD=wii_debug

cube-release:
	-$(MAKE) -C libogc-rice PLATFORM=cube BUILD=cube_release
	$(MAKE) -C libogc2 PLATFORM=gamecube BUILD=gamecube_release

wii-release:
	-$(MAKE) -C libogc-rice PLATFORM=wii BUILD=wii_release
	$(MAKE) -C libogc2 PLATFORM=wii BUILD=wii_release

clean: 
	-$(MAKE) -C libogc-rice clean
	$(MAKE) -C libogc2 clean

install: cube-release wii-release
	-$(MAKE) -C libogc-rice install
	$(MAKE) -C libogc2 install

run: install
	$(MAKE) -C example
	$(MAKE) -C example run

