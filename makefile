CXX ?= g++

store_data_nodaemon:

	$(CXX) -Wall -pedantic store_data_nodaemon.cpp -lwiringPi `mysql_config --cflags --libs` -lm -o store_data_nodaemon

store_data:       
	
	$(CXX) -Wall -pedantic store_data.cpp -lwiringPi `mysql_config --cflags --libs` -lm -o store_data


clean:

	rm -r store_data
	rm -r store_data_nodaemon
