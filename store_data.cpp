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
#include <mysql.h>
#include "Kalman.h"

#define TEMP_DIFF 10
time_t t;
int error = 0;
struct tm *local;

// Mysql variables
	
MYSQL *connection, mysql;

static const char LANG_DB_TEMP_DIFF[] = "\nWARNING: Temperature difference out of bonds (%f to %f). Data will NOT be saved!\n";
static const char LANG_DB_HUMID_DIFF[] = "\nWARNING: Humidity value out of bonds (%i %%). Data will NOT be saved!\n";

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



void saveTemperature(float temperature)
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
			return;
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


void saveHumidity(unsigned int humidity)
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
			return;
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

void savePressure(int pressure)
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
			return;
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





int main (void) {

  // Variable declaration  
 char device[] = "28-xxxx";      // Dev ID
 char devPath[128]; // Path to device
 char buf[256];     // Data from device
 char tmpData[6];   // Temp C * 1000 reported by device 
 char path[] = "/sys/bus/w1/devices"; 
 ssize_t numRead;
 
     //  code for DS18B20 sensor 1-wire acquisition 
     
  sprintf(devPath, "%s/%s/w1_slave", path, device);
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
   printf("Device: %s  - ", dev); 
   printf("Temp: %.3f C  ", tempC / 1000);
   printf("%.3f F\n\n", (tempC / 1000) * 9 / 5 + 32);
  }
  
  close(fd);
 
        
}
