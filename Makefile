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
	gcc -Os -o housedepot $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lrt

# Application installation. -------------------------------------

install-ui: install-preamble
	$(INSTALL) -m 0755 -d $(DESTDIR)$(SHARE)/public/depot
	$(INSTALL) -m 0644 public/* $(DESTDIR)$(SHARE)/public/depot

install-app: install-ui
	$(INSTALL) -m 0755 -s housedepot $(DESTDIR)$(prefix)/bin
	touch $(DESTDIR)/etc/default/housedepot
	$(INSTALL) -m 0755 -d $(DESTDIR)/var/lib/house/depot/config
	$(INSTALL) -m 0755 -d $(DESTDIR)/var/lib/house/depot/state
	$(INSTALL) -m 0755 -d $(DESTDIR)/var/lib/house/depot/scripts
	if [ "x$(DESTDIR)" = "x" ] ; then grep -q '^house:' /etc/passwd && chown -R house:house /var/lib/house/depot ; fi

uninstall-app:
	rm -rf $(DESTDIR)$(SHARE)/public/depot
	rm -f $(DESTDIR)$(prefix)/bin/housedepot

purge-app:

purge-config:
	rm -rf $(DESTDIR)/etc/default/housedepot

# System installation. ------------------------------------------

include $(SHARE)/install.mak

