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
	gcc -c -g -O -o $@ $<

housedepot: $(OBJS)
	gcc -g -O -o housedepot $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lrt

# Application installation. -------------------------------------

install-app:
	mkdir -p $(HROOT)/bin
	mkdir -p /etc/house
	rm -f $(HROOT)/bin/housedepot
	cp housedepot $(HROOT)/bin
	chown root:root $(HROOT)/bin/housedepot
	chmod 755 $(HROOT)/bin/housedepot
	mkdir -p $(SHARE)/public/depot
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/depot
	cp public/* $(SHARE)/public/depot
	chown root:root $(SHARE)/public/depot/*
	chmod 644 $(SHARE)/public/depot/*
	touch /etc/default/housedepot
	mkdir -p /var/lib/house/config /var/lib/house/state /var/lib/house/scripts
	chmod 755 /var/lib/house/config /var/lib/house/state /var/lib/house/scripts

uninstall-app:
	rm -rf $(SHARE)/public/depot
	rm -f $(HROOT)/bin/housedepot

purge-app:

purge-config:
	rm -rf /etc/default/housedepot

# System installation. ------------------------------------------

include $(SHARE)/install.mak

