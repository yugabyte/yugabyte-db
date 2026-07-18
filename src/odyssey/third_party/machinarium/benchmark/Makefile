CC         = gcc
RM         = rm
CFLAGS     = -I. -Wall -g -O3 -I../sources
LFLAGS_LIB = ../sources/libmachinarium.a -pthread -lssl -lcrypto
LFLAGS     = $(LFLAGS_LIB)
EXAMPLES   = benchmark_csw benchmark_csw2 benchmark_channel benchmark_channel_shared benchmark_msg_alloc
all: clean $(EXAMPLES)
benchmark_csw:
	$(CC) $(CFLAGS) benchmark_csw.c $(LFLAGS) -o benchmark_csw
benchmark_csw2:
	$(CC) $(CFLAGS) benchmark_csw2.c $(LFLAGS) -o benchmark_csw2
benchmark_channel:
	$(CC) $(CFLAGS) benchmark_channel.c $(LFLAGS) -o benchmark_channel
benchmark_channel_shared:
	$(CC) $(CFLAGS) benchmark_channel_shared.c $(LFLAGS) -o benchmark_channel_shared
benchmark_msg_alloc:
	$(CC) $(CFLAGS) benchmark_msg_alloc.c $(LFLAGS) -o benchmark_msg_alloc
clean:
	$(RM) -f $(EXAMPLES)
