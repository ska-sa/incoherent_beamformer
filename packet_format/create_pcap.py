"""Write our a text file which can be converted into a pcap file.
The format of these packets are made for spead packets used by the beamformer.
To convert from the output text file to a pcap file you must run [text2pcap -d out.txt out.pcap]
To fix checksum and set up src and dst machines you must run 
tcprewrite --fixcsum --infile=out.pcap --outfile=out_checksum.pcap --dstipmap=0.0.0.0/0:10.0.0.2/24 --enet-dmac=00:07:43:13:e4:50 --srcipmap=0.0.0.0/0:10.0.0.215/24 --enet-smac=00:60:dd:46:b1:ee
These lines are for my beamformer test set up and should be changed appropriately to send packets between other machines"""

import sys
from string import Template

def int_to_hexstr(i, line = -1):
    # print i
    hexstr = hex(i)[2:]
    # print hexstr
    hexstr = "0"*(10 - len(hexstr)) + hexstr

    if line == -1:  
        hexstr = hexstr[:2] + ' ' + hexstr[2:4] + ' ' + hexstr[4:6] + ' ' + hexstr[6:8] + ' ' + hexstr[8:10]
    else:  #If it is the payloadoffset
        linenum = hex(line)[2:]
        linenum = "0"*(4 - len(linenum)) + linenum
        hexstr = hexstr[:2] + ' ' + hexstr[2:4] + ' ' + hexstr[4:6] + '\n0060  ' + hexstr[6:8] + ' ' + hexstr[8:10]

    return hexstr

if len(sys.argv) < 1:
    print "no output file specified"

f = open(sys.argv[1],'w')

template = "0000  00 07 43 13 e4 50 00 60 dd 46 b1 ee 08 00 45 00\n"
# template+= "0010  20 54 00 00 40 00 ff 11 47 0e 0a 00 00 80 0a 00\n"
# template+= "0020  00 0b 22 b8 1b ef 20 40 2c fc 53 04 03 05 00 00\n"
template+= "0010  20 54 00 00 40 00 ff 11 ba 77 0a 00 00 02 0a 00\n"
template+= "0020  00 d7 1b ef 1b ef 20 40 2c fc 53 04 03 05 00 00\n" #SPEAD HEADER starts at 50 04...
template+= "0030  00 06 $heapcnt 80 00 02 00 00 04\n"
template+= "0040  00 00 $payloadoffset 80 00 04 00 00 00\n" #must include a new line in payload offset, ie xx xx\n0040 xx xx xx xx xx xx
template+= "0050  20 00 $adccnt 00 b0 01 00 00 00\n" #must include new line in ADCCOUNT
template+= "0060  00 00 de ad be ef de ad be ef de ad be ef de ad\n" #Beginning of payload

for i in range (112,8273,16):
    hexstr = hex(i)[2:]
    hexstr = "0"*(4 - len(hexstr)) + hexstr
    template+= hexstr + "  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n" #Payload data

template+= "2060  de ad\n" #End of payload
packetStr = ""
template = Template(template)
adcCount = 0
for i in range(500000):
    heapCnt = i / 32
    payloadOffset = (i % 32) * 8192
    adcCount+= 1

    heapStr = "80 00 01 " + int_to_hexstr(heapCnt*4)
    pOStr = "80 00 03 " + int_to_hexstr(payloadOffset)
    adcCountStr = "80 16 00 " + int_to_hexstr(adcCount/32*4)


   

    packetStr = template.substitute(heapcnt=heapStr, payloadoffset=pOStr, adccnt = adcCountStr)
    f.write(packetStr)

f.close()