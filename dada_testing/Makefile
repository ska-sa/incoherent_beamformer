
all:
	gcc -DHAVE_CONFIG_H -I. -I..   -I/usr/local/include   -g -O2 -pthread -MT dada_buffer.o -MD -MP -MF $depbase.Tpo -c -o dada_buffer.o dada_buffer.c
	gcc -g -O2 -pthread -o dada_buffer dada_buffer.o  -L/usr/local/lib ./libs/libpsrdada.a -lm -pthread
    
clean:
	rm dada_buffer
