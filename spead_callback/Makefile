
all:
	gcc ipcutil.c to_buffer.c -fPIC -shared -Wl,-x -I/home/kat/git/spead/src/ -I/home/kat/test/install/psrdada/src -L/home/kat/git/incoherent_beamformer/spead_callback/libs -lOpenCL -lm -lpsrdada -lspead -DIKATCP -o to_buffer.so

toFile:
	gcc toFile.c -fPIC -shared -Wl,-x -I/home/frb/incoherent_beamformer/dada_testing -I/home/frb/spead-master/src -I/usr/local/include -L./libs -lOpenCL -lm -ldada_buffer -lspead -DIKATCP -o toFile.so

dummy:
	gcc dummy.c -pg -fPIC -shared -Wl,-x -I/home/frb/incoherent_beamformer/dada_testing -I/home/kat/git/spead/src/ -I/usr/local/include -L./libs -lm -lspead -DIKATCP -o dummy.so

test:
	gcc test.c -fPIC -shared -Wl,-x -I/home/frb/spead-master/src -I/home/frb/install/psrdada/src -I/usr/local/include -L/home/frb/incoherent_beamformer/spead_callback/libs -lOpenCL -lm -lipc -lpsrdada -lspead -DIKATCP -o to_buffer.so

pps:
	gcc pps_test.c -fPIC -shared -Wl,-x -I/home/frb/incoherent_beamformer/dada_testing -I/home/frb/spead-master/src -I/usr/local/include -L./libs -lOpenCL -lm -ldada_buffer -lspead -DIKATCP -o pps_test.so

ave:
	gcc ave_beamformer.c -fPIC -shared -Wl,-x -I/home/kat/git/spead/src/ -L/home/kat/git/incoherent_beamformer/spead_callback/libs -lOpenCL -lm -lspead -DIKATCP -fopenmp -o ave_beamformer.so