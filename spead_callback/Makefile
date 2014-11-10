
all:
	gcc to_buffer.c -fPIC -shared -Wl,-x -I/home/frb/incoherent_beamformer/dada_testing -I/home/frb/spead-master/src -I/home/frb/install/psrdada/src -I/usr/local/include -L/home/frb/incoherent_beamformer/spead_callback/libs -lOpenCL -lm -lipc -ldada_buffer -lspead -DIKATCP -o to_buffer.so

toFile:
	gcc toFile.c -fPIC -shared -Wl,-x -I/home/frb/incoherent_beamformer/dada_testing -I/home/frb/spead-master/src -I/usr/local/include -L./libs -lOpenCL -lm -ldada_buffer -lspead -DIKATCP -o toFile.so

dummy:
	gcc dummy.c -fPIC -shared -Wl,-x -I/home/frb/incoherent_beamformer/dada_testing -I/home/frb/spead-master/src -I/usr/local/include -L./libs -lOpenCL -lm -ldada_buffer -lspead -DIKATCP -o dummy.so