bme280_store.o: bme280_store.cpp

	gcc  -Wall -pedantic -fpermissive -std=c++11  bme280_store.cpp  `mysql_config --cflags --libs` -o bme280_store

clean:

rm -r bme280_store
