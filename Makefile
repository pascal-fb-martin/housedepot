# HouseDepot - a log and ressource file storage service.
#
# Copyright 2023, Pascal Martin
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

HAPP=housedepot
HROOT=/usr/local
SHARE=$(HROOT)/share/house

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

install-ui:
	mkdir -p $(SHARE)/public/depot
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/depot
	cp public/* $(SHARE)/public/depot
	chown root:root $(SHARE)/public/depot/*
	chmod 644 $(SHARE)/public/depot/*

install-app: install-ui
	mkdir -p $(HROOT)/bin
	mkdir -p /etc/house
	rm -f $(HROOT)/bin/housedepot
	cp housedepot $(HROOT)/bin
	chown root:root $(HROOT)/bin/housedepot
	chmod 755 $(HROOT)/bin/housedepot
	touch /etc/default/housedepot
	mkdir -p /var/lib/house/depot
	grep -q '^house:' /etc/passwd && chown -R house:house /var/lib/house/depot
	if [ -d /var/lib/house/config ] ; then tar cf backupconfig.tar /var/lib/house/config ; mv /var/lib/house/config /var/lib/house/depot ; fi
	if [ -d /var/lib/house/state ] ; then tar cf backupstate.tar /var/lib/house/state ; mv /var/lib/house/state /var/lib/house/depot ; fi
	if [ -d /var/lib/house/scripts ] ; then tar cf backupscripts.tar /var/lib/house/scripts ; mv /var/lib/house/scripts /var/lib/house/depot ; fi
	mkdir -p /var/lib/house/depot/config /var/lib/house/depot/state /var/lib/house/depot/scripts
	chmod 755 /var/lib/house/depot/config /var/lib/house/depot/state /var/lib/house/depot/scripts

uninstall-app:
	rm -rf $(SHARE)/public/depot
	rm -f $(HROOT)/bin/housedepot

purge-app:

purge-config:
	rm -rf /etc/default/housedepot

# System installation. ------------------------------------------

include $(SHARE)/install.mak

