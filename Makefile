CC ?= gcc
CFLAGS ?= -m32 -O2 -Wall -Wextra
LDFLAGS ?= -m32

TARGET = libquake-tls.so
FIXTURE = libtls-fixture.so
TEST_BIN = test-tls

all: $(TARGET) $(FIXTURE) $(TEST_BIN)

$(TARGET): quake-tls.c quake-tls.S
	$(CC) $(CFLAGS) -fPIC -shared -Wl,-z,now -Wl,-z,defs -o $@ $^ -ldl

$(FIXTURE): tls-fixture.c
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $^

$(TEST_BIN): test-tls.c
	$(CC) $(CFLAGS) -o $@ $^ -ldl -lpthread

test: all
	LD_PRELOAD=./$(TARGET) ./$(TEST_BIN)

clean:
	rm -f $(TARGET) $(FIXTURE) $(TEST_BIN)

.PHONY: all test clean
