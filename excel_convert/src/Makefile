
#CC=arm-v7a8v4r3-linux-gnueabi-gcc
#CC=arm-v7a15v4r3-linux-gnueabi-gcc
#CC=gcc


xlmake:xlmake.o txtToExcel.o
	${CC} xlmake.o txtToExcel.o -o xlmake
	rm -rf *.csv *.xls

clean:
	rm -rf *.o *.csv *.xls xlmake
