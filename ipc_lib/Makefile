all:
	gcc -fPIC -DHAVE_CONFIG_H -I. -I..   -I/usr/local/include   -g -O2 -pthread -MT dada_buffer.o -MD -MP -MF $depbase.Tpo -c -o ipcio.o ipcio.c
	gcc -fPIC -DHAVE_CONFIG_H -I. -I..   -I/usr/local/include   -g -O2 -pthread -MT dada_buffer.o -MD -MP -MF $depbase.Tpo -c -o ipcutil.o ipcutil.c
	gcc -fPIC -DHAVE_CONFIG_H -I. -I..   -I/usr/local/include   -g -O2 -pthread -MT dada_buffer.o -MD -MP -MF $depbase.Tpo -c -o ipcbuf.o ipcbuf.c
	gcc -fPIC -shared -Wl,-x -I/home/frb/spead-master/src -I/usr/local/include -o libipc.so ipcio.o ipcbuf.o ipcutil.o
