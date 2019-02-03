store_data_nodaemon:

	gcc  -Wall -pedantic  store_data_nodaemon.cpp  -lwiringPi `mysql_config --cflags --libs` -o store_data_nodaemon

store_data:       
	
	gcc  -Wall -pedantic  store_data.cpp  -lwiringPi `mysql_config --cflags --libs` -o store_data


clean:

	rm -r store_data
	rm -r store_data_nodaemon
