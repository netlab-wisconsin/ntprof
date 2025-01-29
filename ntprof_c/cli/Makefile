CC = gcc

SRCS = main.c
OBJS = $(SRCS:.c=.o)

TARGET = ntprof_cli

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) -lrt

%.o: %.c
	$(CC) $(EXTRA_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

