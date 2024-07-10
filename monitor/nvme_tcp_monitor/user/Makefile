CC = gcc

CFLAGS = -I$(PWD)/../../include -I$(PWD)/../common -I$(PWD)

SRCS = u_blk_layer.c u_nvme_tcp_layer.c u_tcp_layer.c u_ntm.c
OBJS = $(SRCS:.c=.o)

TARGET = u_ntm

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) -lrt

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
