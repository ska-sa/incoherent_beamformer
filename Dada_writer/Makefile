
all:
	gcc -DHAVE_CONFIG_H -I. -I..   -I/usr/local/include   -g -O2 -pthread -MT dada_simple_writer.o -MD -MP -MF $depbase.Tpo -c -o dada_simple_writer.o dada_simple_writer.c
	gcc -g -O2 -pthread -o dada_simple_writer dada_simple_writer.o  -L/usr/local/lib -I/home/frb/incoherent_beamformer/Dada_writer/libs -lm -lpsrdada -pthread
    
clean:
	rm dada_buffer

debug:
	gcc -DHAVE_CONFIG_H -I. -I..   -I/usr/local/include   -g -O2 -pthread -DDEBUG -MT dada_buffer.o -MD -MP -MF $depbase.Tpo -c -o dada_buffer.o dada_buffer.c
	gcc -g -O2 -pthread -DDEBUG  -o dada_buffer dada_buffer.o  -L/usr/local/lib ./libs/libpsrdada.a -lm -pthread

library:
	gcc dada_simple_writer.c -fPIC -shared -Wl,-x -I/home/frb/install/psrdada/src -I/usr/local/include -L/home/frb/incoherent_beamformer/Dada_writer/libs -lpsrdada -lOpenCL -lm -DIKATCP -o libdada_simple_writer.so
