all:
	g++ dada_beamform.c -fopenmp -O3 -fPIC -Wl,-x -I /home/kat/test/install/psrdada/src/ -L/home/kat/test/install/psrdada/src/.libs -lrdmacm -libverbs -lOpenCL -lm -lpsrdada -DIKATCP -o beamform

debug:
	g++ dada_beamform.c -fopenmp -O3 -fPIC -Wl,-x -I /home/kat/test/install/psrdada/src/ -L/home/kat/test/install/psrdada/src/.libs -g -lrdmacm -libverbs -lOpenCL -lm -lpsrdada -DIKATCP -o beamform