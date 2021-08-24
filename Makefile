FREESWITCH_MOD_PATH=/usr/local/freeswitch-1.10.2/lib/freeswitch/mod
FREESWITCH_INCLUDE=/oncon/jiekou/freeswitch-1.10.2/include/freeswitch
ROCKET_BRYNET_INCLUDE=./include

MODNAME = mod_http_server.so
MODOBJ = mod_http_server.o
MODCFLAGS = -Wall -Wno-unused-function -I$(FREESWITCH_INCLUDE) -I$(ROCKET_BRYNET_INCLUDE)
MODLDFLAGS = -lfreeswitch
CC = g++
CFLAGS = -fPIC -g -ggdb  $(MODCFLAGS)
CPPFLAGS = -fPIC -std=c++11 -g -ggdb  $(MODCFLAGS)
LDFLAGS = $(MODLDFLAGS)

.PHONY: all
all: $(MODNAME)

$(MODNAME): $(MODOBJ)
	@$(CC) -shared $(CPPFLAGS) -o $@ $(MODOBJ) $(LDFLAGS)

.c.o: $<
	@$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean
clean:
	rm -f $(MODNAME) $(MODOBJ)

install: $(MODNAME)
	install -d $(FREESWITCH_MOD_PATH)
	install $(MODNAME) $(FREESWITCH_MOD_PATH)