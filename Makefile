TARGET = wwl
CC = gcc
CFLAGS = -g -O0 -Wall -Wextra
LIBS = -lwayland-client -lwayland-cursor
SRCS = $(wildcard src/*.c)
DEPS = $(wildcard src/*.h)
BUILDDIR = build

.PHONY: all clean run lib

all: $(TARGET) lib

$(TARGET): $(SRCS) $(DEPS) $(BUILDDIR)
	$(CC) $(CFLAGS) $(LIBS) $(SRCS) -o $(BUILDDIR)/$@

lib: $(SRCS) $(BUILDDIR)
	$(CC) $(SRCS) $(LIBS) -O3 -fPIC -shared -o $(BUILDDIR)/lib$(TARGET).so

run: $(TARGET)
	./$(BUILDDIR)/$<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

install:
ifeq ($(shell whoami), root)
	mkdir --parents --verbose /usr/local/lib
	mkdir --parents --verbose /usr/local/include
	cp --update --verbose $(BUILDDIR)/lib$(TARGET).so /usr/local/lib
	cp --update --verbose src/wwl.h /usr/local/include
	ldconfig /usr/local/lib

	@echo "successfully installed"
else
	@echo "root permissions required for installation" && exit 1
endif

uninstall:
ifeq ($(shell whoami), root)
	rm --interactive --verbose /usr/local/lib/lib$(TARGET).so
	rm --interactive --verbose /usr/local/include/wwl.h
	ldconfig

	@echo "successfully uninstalled"
else
	@echo "root permissions required for uninstallation" && exit 1
endif

clean:
	-rm $(BUILDDIR)/$(TARGET)
	-rm $(BUILDDIR)/lib$(TARGET).so
	-rm -r $(BUILDDIR)

