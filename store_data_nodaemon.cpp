/*
*************************************************************************
   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS 
   IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED 
   TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A 
   PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT 
   HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
   ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
   OF OR INABILITY TO USE THIS SOFTWARE, EVEN IF THE COPYRIGHT HOLDERS OR 
   CONTRIBUTORS ARE AWARE OF THE POSSIBILITY OF SUCH DAMAGE.
************************************************************************
*/

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <limits.h>
#include <time.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <mysql.h>
#include "Kalman.h"
#include <wiringPi.h>


#define TEMP_DIFF 10
#define MAXTIMINGS	85
#define DHTPIN		15
int dht11_dat[5] = { 0, 0, 0, 0, 0 };
time_t t;
int error = 0;
struct tm *local;
const int SCAN_TIME = 2;

// Mysql variables

MYSQL *connection, mysql;

static const char LANG_DB_TEMP_DIFF[] = "\nWARNING: Temperature difference out of bonds (%f to %f). Data will NOT be saved!\n";
static const char LANG_DB_HUMID_DIFF[] = "\nWARNING: Humidity value out of bonds (%i %%). Data will NOT be saved!\n";

char device[] = "28-0417c3a4e1ff";      // 1-Wire Dev ID
char path[] = "/sys/bus/w1/devices";    // 1-Wire path


Kalman myFilterTemp(0.125,32,1023,0); //suggested initial values for high noise filtering
Kalman myFilterHum(0.125,32,1023,0);
Kalman myFilterPress(0.125,32,1023,0);


float ConvertFormat(float formData)
    {
	char tempdata[5] = "";
	sprintf(tempdata,"%.1f", formData);
	float finalTemp = atof(tempdata);
	return finalTemp;
     }



int saveTemperature(float temperature)
{
	static signed int t_old_min = -1;
	static float old_value = -FLT_MAX;

	t = time(NULL);
	local = localtime(&t);

	/* Store if value has changed or not from same minute */
	if (t_old_min != local->tm_hour || old_value != temperature) {

		/* Check for invalid values */
		float difference = old_value - temperature;
		if ((difference < -TEMP_DIFF || difference > TEMP_DIFF)
		    && old_value != -FLT_MAX) {
			printf(LANG_DB_TEMP_DIFF, old_value, temperature);
			return 1;
		}

		char query_1[255] = "";

        	sprintf( query_1, "INSERT INTO wr_temperature (sensor_id, value) " "VALUES(5, %.1f)", temperature );

        	int state = mysql_query(connection, query_1);

        	if (state != 0) {
                	printf("%s", mysql_error(connection));
                	return 1;
                }


	}

	t_old_min = local->tm_hour;
	old_value = temperature;

}


int saveHumidity(unsigned int humidity)
{
	static signed int h_old_min = -1;
	static int old_value = -INT_MAX;

	t = time(NULL);
	local = localtime(&t);

	/* Store if value has changed or not from same minute */
	if (h_old_min != local->tm_hour || old_value != humidity) {

		/* Check for invalid values */
		if (humidity <= 0 || humidity > 100) {
			fprintf(stderr, LANG_DB_HUMID_DIFF, humidity);
			return 1;
		}

		char query[255] = "";

        sprintf( query, "INSERT INTO wr_humidity (sensor_id, value) " "VALUES(5, %u)", humidity);

        int state = mysql_query(connection, query);

        if (state != 0) {
                printf("%s", mysql_error(connection));
                return 1;
                }
	}

	h_old_min = local->tm_hour;
	old_value = humidity;
}

int savePressure(int pressure)
{
	static signed int t_old_min = -1;
	static int old_value = -INT_MAX;

	t = time(NULL);
	local = localtime(&t);

	/* Store if value has changed or not from same minute */
	if (t_old_min != local->tm_hour || old_value != pressure) {

		/* Check for invalid values */
		int difference = old_value - pressure;
		if ((difference < -TEMP_DIFF || difference > TEMP_DIFF)
		    && old_value != -INT_MAX) {
			printf(LANG_DB_TEMP_DIFF, old_value, pressure);
			return 1;
		}

		char query_1[255] = "";

        	sprintf( query_1, "INSERT INTO wr_barometer (sensor_id, value) " "VALUES(5, %u)", pressure );

        	int state = mysql_query(connection, query_1);

        	if (state != 0) {
                	printf("%s", mysql_error(connection));
                	return 1;
                }


	}

	t_old_min = local->tm_hour;
	old_value = pressure;
}

int read_dht11_dat()
{
	uint8_t laststate	= HIGH;
	uint8_t counter		= 0;
	uint8_t j		= 0, i;
	float	f; /* fahrenheit */

	dht11_dat[0] = dht11_dat[1] = dht11_dat[2] = dht11_dat[3] = dht11_dat[4] = 0;

	/* pull pin down for 18 milliseconds */
	pinMode( DHTPIN, OUTPUT );
	digitalWrite( DHTPIN, LOW );
	delay( 18 );
	/* then pull it up for 40 microseconds */
	digitalWrite( DHTPIN, HIGH );
	delayMicroseconds( 40 );
	/* prepare to read the pin */
	pinMode( DHTPIN, INPUT );

	/* detect change and read data */
	for ( i = 0; i < MAXTIMINGS; i++ )
	{
		counter = 0;
		while ( digitalRead( DHTPIN ) == laststate )
		{
			counter++;
			delayMicroseconds( 1 );
			if ( counter == 255 )
			{
				break;
			}
		}
		laststate = digitalRead( DHTPIN );

		if ( counter == 255 )
			break;

		/* ignore first 3 transitions */
		if ( (i >= 4) && (i % 2 == 0) )
		{
			/* shove each bit into the storage bytes */
			dht11_dat[j / 8] <<= 1;
			if ( counter > 16 )
				dht11_dat[j / 8] |= 1;
			j++;
		}
	}

	/*
	 * check we read 40 bits (8bit x 5 ) + verify checksum in the last byte
	 * print it out if data is good
	 */
	if ( (j >= 40) &&
	     (dht11_dat[4] == ( (dht11_dat[0] + dht11_dat[1] + dht11_dat[2] + dht11_dat[3]) & 0xFF) ) )
	{


	return (dht11_dat[0]);

	}
}



int main (int argc, char *argv[]) 
{


	// Initialization Mysql
	mysql_init(&mysql);
        connection = mysql_real_connect(&mysql,"localhost", "pico", "maurizio", 
                                    "weather", 0, 0, 0);

        if (connection == NULL) {
                printf("%s", mysql_error(&mysql));
                return 1;
		}

  // Variable declaration
 char devPath[128]; // Path to device
 char buf[256];     // Data from device
 char tmpData[6];   // Temp C * 1000 reported by device 
 ssize_t numRead;
 
     //  code for DS18B20 sensor 1-wire acquisition 

  sprintf(devPath, "%s/%s/w1_slave", path, device);


while(-1)
{

int fd = open(devPath, O_RDONLY);
  if(fd == -1)
  {
   perror ("Couldn't open the w1 device.");
   return 1;
   }

while((numRead = read(fd, buf, 256)) > 0)
  {
   strncpy(tmpData, strstr(buf, "t=") + 2, 5);
   float tempC = strtof(tmpData, NULL);


	float kTemp = myFilterTemp.getFilteredValue(tempC / 1000);
	saveTemperature(ConvertFormat(kTemp));
	printf("\nValore Temp acquisito %.2f C\n  ", (ConvertFormat(kTemp)));

  }

  close(fd);

// DHT11 humidity acquisition

if ( wiringPiSetup() == -1 )
		exit( 1 );

	float kHum = myFilterHum.getFilteredValue(read_dht11_dat());
	saveHumidity((int)kHum);
	printf("\n Valore Hum acquisito  %.0f \n", kHum);
//	return(0);






	//float kPress = myFilterPress.getFilteredValue(pressure);
	//savePressure((int)kPress);

	sleep(SCAN_TIME);

  }

 	mysql_close(connection);



}


