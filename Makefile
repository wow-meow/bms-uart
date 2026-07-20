CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Iinc
LDFLAGS ?=

SRC = $(wildcard src/*.c)
OBJ = $(SRC:src/%.c=bin/%.o)
HDR = $(wildcard inc/*.h)

TARGET = bms_query

# 主产物: 工程根的 bms_query
$(TARGET): $(OBJ) | bin
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

# .o 输出到 bin/
bin/%.o: src/%.c $(HDR) | bin
	$(CC) $(CFLAGS) -c -o $@ $<

bin:
	mkdir -p bin

clean:
	rm -f $(OBJ) $(TARGET)
	# 注意: logs/ 内的历史数据不会被清理, 手动删除请用 make distclean

distclean: clean
	rm -rf logs

run: $(TARGET)
	./$(TARGET) $(PORT)

# ============== 测试 ==============
TESTSRC = tests/test_proto.c
bin/test_proto: $(TESTSRC) bin/protocol.o | bin
	$(CC) $(CFLAGS) -o $@ $(TESTSRC) bin/protocol.o $(LDFLAGS)

TESTSRC_B = tests/test_balance.c
bin/test_balance: $(TESTSRC_B) bin/balance_fmt.o | bin
	$(CC) $(CFLAGS) -o $@ $(TESTSRC_B) bin/balance_fmt.o $(LDFLAGS)

TESTSRC_P = tests/test_prot.c
bin/test_prot: $(TESTSRC_P) bin/prot_fmt.o | bin
	$(CC) $(CFLAGS) -o $@ $(TESTSRC_P) bin/prot_fmt.o $(LDFLAGS)

test: bin/test_proto bin/test_balance bin/test_prot
	./bin/test_proto
	./bin/test_balance
	./bin/test_prot

.PHONY: clean distclean run test
