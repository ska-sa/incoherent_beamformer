import sys
import datetime
import numpy as np

packet_number = np.uint32(1)
f_id = np.uint32(0)
timestamp = np.uint64(np.random.random()*10e15)

headerStr = "\x53\x04\x03\x05\x00\x00\x00\x07\x80\x10\x00\x00\x00\x00\x00\x00\x80\x10\x01\x00\x00\x00\x00\x00\x00\x10\x02\x00\x00\x00\x00\x00\x80\x00\x01\x00\x00\x00\x00\x00\x80\x00\x02\x00\x00\x00\x08\x00\x80\x00\x03\x00\x00\x00\x00\x00\x80\x00\x04\x00\x00\x00\x08\x00"

packetData = np.empty(shape = (64 + 2048,), dtype = np.uint8) #64byte header plus 2048 8 bit samples (1024 channels by 2 pol)

arr = np.fromstring(headerStr, dtype=np.uint8)

print arr.shape
print arr
print np.array_str(arr)

packetData[:64] = np.fromstring(headerStr, dtype=np.uint8)
print packetData[:64]

packetData[64:2048 + 64] = np.arange(2048).astype(np.uint8)

packetData.tofile("packet.dat")
