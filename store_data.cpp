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
#include <syslog.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <float.h>
#include <math.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
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





int main (int argc, char *argv[]) 
{

	pid_t pid, sid;

    // 1 - Fork
    pid = fork(); 

    if (pid < 0) 
    {
        exit(EXIT_FAILURE);
    }
    if (pid > 0) 
    {
        exit(EXIT_SUCCESS);
    }

    //2 - Umask
    umask(0);

    //3 - Logs
    openlog(argv[0],LOG_NOWAIT|LOG_PID,LOG_USER); 
    syslog(LOG_NOTICE, "Successfully started daemon\n"); 

    //4 - Session Id
    sid = setsid();
    if (sid < 0) {
        syslog(LOG_ERR, "Could not create process group\n");
        exit(EXIT_FAILURE);
    }

    //5 - WD
    if ((chdir("/")) < 0) {
        syslog(LOG_ERR, "Could not change working directory to /\n");
        exit(EXIT_FAILURE);
    }

    //6 - FD
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

	// Initialization Mysql
	mysql_init(&mysql);
        connection = mysql_real_connect(&mysql,"localhost", "user", "password", 
                                    "weather", 0, 0, 0);

        if (connection == NULL) {
                printf("%s", mysql_error(&mysql));
                return 1;
		}
	
	
  // Variable declaration  
 char device[] = "28-0417c3a4e1ff";      // Dev ID
 char devPath[128]; // Path to device
 char buf[256];     // Data from device
 char tmpData[6];   // Temp C * 1000 reported by device 
 char path[] = "/sys/bus/w1/devices"; 
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
	float kTemp = myFilterTemp.getFilteredValue(cTemp / 1000);
	saveTemperature(ConvertFormat(kTemp));
	printf("\nTemp: %.1f C\n", (ConvertFormat(KTemp)));
  }
  
  close(fd);
	
	
	//float kHum = myFilterHum.getFilteredValue(humidity);
	//saveHumidity((int)kHum);
	
	//float kPress = myFilterPress.getFilteredValue(pressure);
	//savePressure((int)kPress);
	
	sleep(1);
	
  }
	
 	mysql_close(connection);
	
	closelog();
        
}
