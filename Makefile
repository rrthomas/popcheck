#
# Makefile for popcheck.c
#
# (c) 1998 Staffan Hämälä
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
#
# Should you need to contact me, the author, you can do so by
# e-mail - mail your message to <staham@algonet.se>.
#
#
# Edit the options below to suit your needs
#

CC		= gcc

CFLAGS		= -O2 -Wall -g -ggdb

# Uncomment for Sparc Solaris
# LDFLAGS		= -lcurses -lresolv -lnsl -lsocket
LDLIBS		= -lcurses

PROG      = popcheck

INSTPATH	= /usr/local

$(PROG): $(PROG).o

$(PROG).o: $(PROG).c
	$(CC) $(CFLAGS) -c $(PROG).c -o $(PROG).o

clean:
	-$(RM) $(PROG).o $(PROG) *\~ \#*\#

dist: clean
	-$(RM) ../$(PROG).tar.gz
	@tar -C.. -cf ../$(PROG).tar $(PROG)
	@gzip ../$(PROG).tar

install: $(PROG)
	install -d $(INSTPATH)/bin
	install -m 755 popcheck $(INSTPATH)/bin
	install -d $(INSTPATH)/man/man1
	install -m 644 popcheck.1 $(INSTPATH)/man/man1

