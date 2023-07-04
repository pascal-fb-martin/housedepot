
OBJS= housedepot.o housedepot_repository.o housedepot_revision.o
LIBOJS=

SHARE=/usr/local/share/house

# Local build ---------------------------------------------------

all: housedepot

clean:
	rm -f *.o *.a housedepot

rebuild: clean all

%.o: %.c
	gcc -c -g -O -o $@ $<

housedepot: $(OBJS)
	gcc -g -O -o housedepot $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lgpiod -lrt

# Distribution agnostic file installation -----------------------

install-files:
	mkdir -p /usr/local/bin
	mkdir -p /var/lib/house
	mkdir -p /etc/house
	rm -f /usr/local/bin/housedepot
	cp housedepot /usr/local/bin
	chown root:root /usr/local/bin/housedepot
	chmod 755 /usr/local/bin/housedepot
	mkdir -p $(SHARE)/public/depot
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/depot
	cp public/* $(SHARE)/public/depot
	chown root:root $(SHARE)/public/depot/*
	chmod 644 $(SHARE)/public/depot/*
	touch /etc/default/housedepot

uninstall:
	rm -f /usr/local/bin/housedepot
	rm -rf $(SHARE)/public/depot

purge-config:
	rm -rf /etc/default/housedepot

# Distribution agnostic systemd support -------------------------

install-systemd:
	cp systemd.service /lib/systemd/system/housedepot.service
	chown root:root /lib/systemd/system/housedepot.service
	systemctl daemon-reload
	systemctl enable housedepot
	systemctl start housedepot

uninstall-systemd:
	if [ -e /etc/init.d/housedepot ] ; then systemctl stop housedepot ; systemctl disable housedepot ; rm -f /etc/init.d/housedepot ; fi
	if [ -e /lib/systemd/system/housedepot.service ] ; then systemctl stop housedepot ; systemctl disable housedepot ; rm -f /lib/systemd/system/housedepot.service ; systemctl daemon-reload ; fi

stop-systemd: uninstall-systemd

# Debian GNU/Linux install --------------------------------------

install-debian: stop-systemd install-files install-systemd

uninstall-debian: uninstall-systemd uninstall-files

purge-debian: uninstall-debian purge-config

# Void Linux install --------------------------------------------

install-void: install-files

uninstall-void: uninstall-files

purge-void: uninstall-void purge-config

# Default install (Debian GNU/Linux) ----------------------------

install: install-debian

uninstall: uninstall-debian

purge: purge-debian

