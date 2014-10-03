
all:
	gcc -DHAVE_CONFIG_H -I. -I..   -I/usr/local/include   -g -O2 -pthread -MT dada_buffer.o -MD -MP -MF $depbase.Tpo -c -o dada_buffer.o dada_buffer.c
	gcc -g -O2 -pthread -o dada_buffer dada_buffer.o  -L/usr/local/lib ./libs/libpsrdada.a -lm -pthread
    
clean:
	rm dada_buffer

debug:
	gcc -DHAVE_CONFIG_H -I. -I..   -I/usr/local/include   -g -O2 -pthread -DDEBUG -MT dada_buffer.o -MD -MP -MF $depbase.Tpo -c -o dada_buffer.o dada_buffer.c
	gcc -g -O2 -pthread -DDEBUG  -o dada_buffer dada_buffer.o  -L/usr/local/lib ./libs/libpsrdada.a -lm -pthread

library:
	gcc -DHAVE_CONFIG_H -I. -I..   -I/usr/local/include -lpsrdada  -g -O2 -pthread -DDEBUG -MT dada_buffer.o -MD -MP -MF $depbase.Tpo -c -o dada_buffer.o dada_buffer.c
	ar -cvq libdada_buffer.a *.o