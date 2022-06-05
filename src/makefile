CFLAGS = -msse3 -ffast-math -g

all: compute.elf
	cp compute.elf ../

compute.elf: main.o compute.o desc.o aio_lnx.o clock_lnx.o cJSON.o
	g++ main.o compute.o desc.o aio_lnx.o clock_lnx.o cJSON.o -g -lvulkan -laio -ldl -o compute.elf

main.o: main.cpp desc.h aio.h clock.h
	g++ main.cpp -c $(CFLAGS)

compute.o: compute.cpp compute.h desc.h aio.h
	g++ compute.cpp -c $(CFLAGS)

desc.o: desc.cpp desc.h cJSON.h
	g++ desc.cpp -c $(CFLAGS)
	
aio_lnx.o: aio_lnx.cpp aio.h
	g++ aio_lnx.cpp -c $(CFLAGS)

clock_lnx.o: clock_lnx.cpp clock.h
	g++ clock_lnx.cpp -c $(CFLAGS)
	
cJSON.o: cJSON.c cJSON.h
	g++ cJSON.c -c $(CFLAGS)

clean:
	rm -f *.o *.elf

