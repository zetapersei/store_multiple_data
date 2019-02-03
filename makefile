store_data_nodaemon.o: store_data_nodaemon.cpp

	gcc  -Wall -pedantic  store_data_nodaemon.cpp  -lwiringPi `mysql_config --cflags --libs` -o store_data_nodaemon

store_data.o: store_data.cpp       
	
	gcc  -Wall -pedantic  store_data.cpp  -lwiringPi `mysql_config --cflags --libs` -o store_data


clean:

	rm -r store_data
	rm -r store_data_nodaemon
