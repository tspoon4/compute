CFLAGS = -msse3 -ffast-math -g

all: compute.elf
	cp compute.elf ../

compute.elf: main.o cJSON.o aio_lnx.o clock_lnx.o
	g++ main.o cJSON.o aio_lnx.o clock_lnx.o -g -lvulkan -laio -ldl -o compute.elf

main.o: main.cpp cJSON.h aio.h clock.h
	g++ main.cpp -c $(CFLAGS)

cJSON.o: cJSON.c cJSON.h
	g++ cJSON.c -c $(CFLAGS)
	
aio_lnx.o: aio_lnx.cpp aio.h
	g++ aio_lnx.cpp -c $(CFLAGS)

clock_lnx.o: clock_lnx.cpp clock.h
	g++ clock_lnx.cpp -c $(CFLAGS)
	
clean:
	rm -f *.o *.elf
