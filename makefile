store_data.o: store_data.cpp

	gcc  -Wall -pedantic -fpermissive -std=c++11  store_data.cpp  `mysql_config --cflags --libs` -o store_data

clean:

rm -r store_data
