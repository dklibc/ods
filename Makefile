TARGET:=ods

OBJ:= \
	ebuf.o  \
	main.o  \
	ods.o   \
	sbuf.o  \
	stack.o \
	xml.o   \
	zip.o   \

.PHONY: clean all

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ -lz

%.o: %.c
	$(CC) $(CFLAGS) -c -g2 -o $@ $^

clean:
	rm -f $(TARGET) $(OBJ)

