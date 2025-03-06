CC = gcc

CFLAGS = -Wall -I/usr/include
LDFLAGS = -liniparser -lrt

SRCS = config.c main.c
OBJS = $(SRCS:.c=.o)

TARGET = ntprof_cli

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -I$(CURDIR) -I. -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)