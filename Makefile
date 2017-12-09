TARGET = user_agent
LIBS = -lyaml -lpcre
CC = clang
SANITIZER_FLAGS = -fsanitize=address -fno-omit-frame-pointer
CFLAGS = $(SANITIZER_FLAGS) -std=c99 -g -Wall
LDFLAGS = $(SANITIZER_FLAGS) -g
OBJECTS = $(patsubst %.c, %.o, $(wildcard src/*.c))
HEADERS = $(wildcard *.h)

.PHONY: default all clean

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -Wall $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f $(TARGET)
	find src -iname "*.o" -exec rm {} \;
