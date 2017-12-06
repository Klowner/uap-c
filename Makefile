TARGET = user_agent
LIBS = -lasan -lyaml -lpcre
CC = clang
CFLAGS = -fsanitize=address -std=c99 -g -Wall

OBJECTS = $(patsubst %.c, %.o, $(wildcard src/*.c))
HEADERS = $(wildcard *.h)

.PHONY: default all clean

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -Wall $(LIBS) -o $@

clean:
	rm -f $(TARGET)
	find src -iname "*.o" -exec rm {} \;
