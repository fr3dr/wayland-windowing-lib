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

clean:
	-rm $(BUILDDIR)/$(TARGET)
	-rm $(BUILDDIR)/lib$(TARGET).so
	-rm -r $(BUILDDIR)

