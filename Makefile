# HouseDepot - a log and ressource file storage service.
#
# Copyright 2025, Pascal Martin
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.
#
# WARNING
#
# This Makefile depends on echttp and houseportal (dev) being installed.

prefix=/usr/local
SHARE=$(prefix)/share/house

INSTALL=/usr/bin/install

HAPP=housedepot
HMAN=/var/lib/house/note/content/manuals/infrastructure
HMANCACHE=/var/lib/house/note/cache

# Application build. --------------------------------------------

OBJS= housedepot.o housedepot_repository.o housedepot_revision.o
LIBOJS=

all: housedepot

clean:
	rm -f *.o *.a housedepot

rebuild: clean all

%.o: %.c
	gcc -c -Os -o $@ $<

housedepot: $(OBJS)
	gcc -Os -o housedepot $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lmagic -lrt

# Application installation. -------------------------------------

install-ui: install-preamble
	$(INSTALL) -m 0755 -d $(DESTDIR)$(SHARE)/public/depot
	$(INSTALL) -m 0644 public/* $(DESTDIR)$(SHARE)/public/depot
	$(INSTALL) -m 0755 -d $(DESTDIR)$(HMAN)
	$(INSTALL) -m 0644 README.md $(DESTDIR)$(HMAN)/$(HAPP).md
	rm -rf $(DESTDIR)$(HMANCACHE)/*

install-runtime: install-preamble
	$(INSTALL) -m 0755 -s housedepot $(DESTDIR)$(prefix)/bin
	touch $(DESTDIR)/etc/default/housedepot
	$(INSTALL) -m 0755 -d $(DESTDIR)/var/lib/house/depot/config
	$(INSTALL) -m 0755 -d $(DESTDIR)/var/lib/house/depot/state
	$(INSTALL) -m 0755 -d $(DESTDIR)/var/lib/house/depot/scripts
	if [ "x$(DESTDIR)" = "x" ] ; then grep -q '^house:' /etc/passwd && chown -R house:house /var/lib/house/depot ; fi

install-app: install-ui install-runtime

uninstall-app:
	rm -rf $(DESTDIR)$(SHARE)/public/depot
	rm -f $(DESTDIR)$(prefix)/bin/housedepot
	rm -f $(DESTDIR)$(HMAN)/$(HAPP).md
	rm -rf $(DESTDIR)$(HMANCACHE)/*

purge-app:

purge-config:
	rm -rf $(DESTDIR)/etc/default/housedepot

# Build a private Debian package. -------------------------------

install-package: install-ui install-runtime install-systemd

debian-package:
	rm -rf build
	install -m 0755 -d build/$(HAPP)/DEBIAN
	cat debian/control | sed "s/{{arch}}/`dpkg --print-architecture`/" > build/$(HAPP)/DEBIAN/control
	install -m 0644 debian/copyright build/$(HAPP)/DEBIAN
	install -m 0644 debian/changelog build/$(HAPP)/DEBIAN
	install -m 0755 debian/postinst build/$(HAPP)/DEBIAN
	install -m 0755 debian/prerm build/$(HAPP)/DEBIAN
	install -m 0755 debian/postrm build/$(HAPP)/DEBIAN
	make DESTDIR=build/$(HAPP) install-package
	cd build ; fakeroot dpkg-deb -b $(HAPP) .

# System installation. ------------------------------------------

include $(SHARE)/install.mak

