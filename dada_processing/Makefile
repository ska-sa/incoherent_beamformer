all:
	g++ dada_beamform.c child_beam.c spead2/common_thread_pool.cpp spead2/common_flavour.cpp spead2/send_heap.cpp spead2/send_udp.cpp spead2/send_stream.cpp -std=c++11 -fopenmp -O3 -fPIC -Wl,-x -I/home/kat/test/install/psrdada/src/ -I/home/kat/git/incoherent_beamformer/dada_processing/spead2 -L/home/kat/git/incoherent_beamformer/dada_processing/libs -fpermissive -lboost_system -lspead2 -libverbs -lOpenCL -lm -lpsrdada -DIKATCP -o beamform

timing:
	g++ dada_beamform.c timing.c spead2/common_thread_pool.cpp spead2/common_flavour.cpp spead2/send_heap.cpp spead2/send_udp.cpp spead2/send_stream.cpp -std=c++11 -fopenmp -O3 -fPIC -Wl,-x -I/home/kat/test/install/psrdada/src/ -I/home/kat/git/incoherent_beamformer/dada_processing/spead2 -L/home/kat/git/incoherent_beamformer/dada_processing/libs -fpermissive -lrdmacm -lboost_system -lspead2 -libverbs -lOpenCL -lm -lpsrdada -DIKATCP -o timing

debug:
	g++ dada_beamform.c -fopenmp -O3 -fPIC -Wl,-x -I /home/kat/test/install/psrdada/src/ -L/home/kat/test/install/psrdada/src/.libs -g -lrdmacm -libverbs -lspead2 -lboost_system -pthread -lOpenCL -lm -lpsrdada -DIKATCP -o beamform

test_spead:
	g++ test_spead.cpp spead2/common_thread_pool.cpp spead2/common_flavour.cpp spead2/send_heap.cpp spead2/send_udp.cpp spead2/send_stream.cpp -std=c++11 -fopenmp -O3 -fPIC -Wl,-x -I/home/kat/test/install/psrdada/src/ -I/home/kat/git/incoherent_beamformer/dada_processing/spead2 -L/home/kat/git/incoherent_beamformer/dada_processing/libs -fpermissive -lrdmacm -lboost_system -lspead2 -libverbs -lOpenCL -lm -lpsrdada -DIKATCP -o test_spead

beamform_head:
	g++ ipcutil.c beamform_head_node.cpp dada_beamform.c spead2/common_thread_pool.cpp spead2/common_flavour.cpp spead2/recv_udp.cpp spead2/recv_heap.cpp spead2/recv_live_heap.cpp -std=c++11 -g -w -fopenmp -O3 -fPIC -Wl,-x -I/home/kat/psrdada/src/ -I/home/kat/git/incoherent_beamformer/dada_processing/spead2 -L/home/kat/git/incoherent_beamformer/dada_processing/libs -fpermissive -lrdmacm -lboost_system -libverbs -lspead2 -lOpenCL -lm -lpsrdada -DIKATCP -o beamform_head
