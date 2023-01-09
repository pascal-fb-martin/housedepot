
OBJS= housedepot.o housedepot_repository.o housedepot_revision.o
LIBOJS=

SHARE=/usr/local/share/house

all: housedepot

clean:
	rm -f *.o *.a housedepot

rebuild: clean all

%.o: %.c
	gcc -c -g -O -o $@ $<

housedepot: $(OBJS)
	gcc -g -O -o housedepot $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lgpiod -lrt

install:
	if [ -e /etc/init.d/housedepot ] ; then systemctl stop housedepot ; systemctl disable housedepot ; rm -f /etc/init.d/housedepot ; fi
	if [ -e /lib/systemd/system/housedepot.service ] ; then systemctl stop housedepot ; systemctl disable housedepot ; rm -f /lib/systemd/system/housedepot.service ; fi
	mkdir -p /usr/local/bin
	mkdir -p /var/lib/house
	mkdir -p /etc/house
	rm -f /usr/local/bin/housedepot
	cp housedepot /usr/local/bin
	chown root:root /usr/local/bin/housedepot
	chmod 755 /usr/local/bin/housedepot
	cp systemd.service /lib/systemd/system/housedepot.service
	chown root:root /lib/systemd/system/housedepot.service
	mkdir -p $(SHARE)/public/depot
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/depot
	cp public/* $(SHARE)/public/depot
	chown root:root $(SHARE)/public/depot/*
	chmod 644 $(SHARE)/public/depot/*
	touch /etc/default/housedepot
	systemctl daemon-reload
	systemctl enable housedepot
	systemctl start housedepot

uninstall:
	systemctl stop housedepot
	systemctl disable housedepot
	rm -f /usr/local/bin/housedepot
	rm -f /lib/systemd/system/housedepot.service /etc/init.d/housedepot
	systemctl daemon-reload

purge: uninstall
	rm -rf /etc/default/housedepot

