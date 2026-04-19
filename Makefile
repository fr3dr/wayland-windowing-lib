TARGET = wwl
CC = gcc
CFLAGS = -g -O0 -Wall
LIBS = -lwayland-client -lwayland-cursor -lxkbcommon
SRCS = $(wildcard src/*.c)
DEPS = $(wildcard src/*.h)
BUILDDIR = build

.PHONY: all clean run lib

all: $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET): $(SRCS) $(DEPS) $(BUILDDIR)
	$(CC) $(CFLAGS) $(LIBS) $(SRCS) -o $(BUILDDIR)/$@

run: $(TARGET)
	./$(BUILDDIR)/$<

lib: $(SRCS) $(BUILDDIR)
	$(CC) $(SRCS) $(LIBS) -fPIC -shared -o $(BUILDDIR)/lib$(TARGET).so

clean:
	-rm $(TARGET)

