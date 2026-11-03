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
   OF OR INABILITY TO USE THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
   OF SUCH DAMAGE.
************************************************************************
*/

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <mysql.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <wiringPi.h>

#include "Kalman.h"

#define TEMP_DIFF 10
#define MAXTIMINGS 85
#define DHTPIN 15

static const int SCAN_TIME = 2;
static const char DEVICE[] = "28-0417c3a4e1ff";
static const char W1_PATH[] = "/sys/bus/w1/devices";

static MYSQL mysql;
static MYSQL *connection;

static Kalman myFilterTemp(0.125, 32, 1023, 0);
static Kalman myFilterHum(0.125, 32, 1023, 0);

static float roundToTenth(float value)
{
	return roundf(value * 10.0f) / 10.0f;
}

static int currentHour(int *hour)
{
	time_t now = time(NULL);
	struct tm localTime;

	if (now == (time_t)-1 || localtime_r(&now, &localTime) == NULL) {
		perror("Could not read local time");
		return 1;
	}

	*hour = localTime.tm_hour;
	return 0;
}

static int saveTemperature(float temperature)
{
	static int previousHour = -1;
	static float previousValue = -FLT_MAX;
	int hour;

	if (!isfinite(temperature) || currentHour(&hour) != 0)
		return 1;

	if (previousHour != hour || previousValue != temperature) {
		if (previousValue != -FLT_MAX &&
		    fabsf(previousValue - temperature) > TEMP_DIFF) {
			fprintf(stderr,
				"WARNING: Temperature difference out of bounds "
				"(%.1f to %.1f). Data will NOT be saved!\n",
				previousValue, temperature);
			return 1;
		}

		char query[128];
		int length = snprintf(query, sizeof(query),
				      "INSERT INTO wr_temperature "
				      "(sensor_id, value) VALUES(5, %.1f)",
				      temperature);
		if (length < 0 || (size_t)length >= sizeof(query)) {
			fprintf(stderr, "Temperature query is too long\n");
			return 1;
		}

		if (mysql_query(connection, query) != 0) {
			fprintf(stderr, "%s\n", mysql_error(connection));
			return 1;
		}
	}

	previousHour = hour;
	previousValue = temperature;
	return 0;
}

static int saveHumidity(unsigned int humidity)
{
	static int previousHour = -1;
	static int previousValue = -1;
	int hour;

	if (humidity == 0 || humidity > 100) {
		fprintf(stderr,
			"WARNING: Humidity value out of bounds (%u%%). "
			"Data will NOT be saved!\n",
			humidity);
		return 1;
	}
	if (currentHour(&hour) != 0)
		return 1;

	if (previousHour != hour || previousValue != (int)humidity) {
		char query[128];
		int length = snprintf(query, sizeof(query),
				      "INSERT INTO wr_humidity "
				      "(sensor_id, value) VALUES(5, %u)",
				      humidity);
		if (length < 0 || (size_t)length >= sizeof(query)) {
			fprintf(stderr, "Humidity query is too long\n");
			return 1;
		}

		if (mysql_query(connection, query) != 0) {
			fprintf(stderr, "%s\n", mysql_error(connection));
			return 1;
		}
	}

	previousHour = hour;
	previousValue = (int)humidity;
	return 0;
}

int savePressure(int pressure)
{
	static int previousHour = -1;
	static int previousValue = INT_MIN;
	int hour;

	if (currentHour(&hour) != 0)
		return 1;

	if (previousHour != hour || previousValue != pressure) {
		if (previousValue != INT_MIN &&
		    llabs((long long)previousValue - pressure) > TEMP_DIFF) {
			fprintf(stderr,
				"WARNING: Pressure difference out of bounds "
				"(%d to %d). Data will NOT be saved!\n",
				previousValue, pressure);
			return 1;
		}

		char query[128];
		int length = snprintf(query, sizeof(query),
				      "INSERT INTO wr_barometer "
				      "(sensor_id, value) VALUES(5, %d)",
				      pressure);
		if (length < 0 || (size_t)length >= sizeof(query)) {
			fprintf(stderr, "Pressure query is too long\n");
			return 1;
		}

		if (mysql_query(connection, query) != 0) {
			fprintf(stderr, "%s\n", mysql_error(connection));
			return 1;
		}
	}

	previousHour = hour;
	previousValue = pressure;
	return 0;
}

static int readDht11Humidity(unsigned int *humidity)
{
	uint8_t data[5] = {0};
	uint8_t lastState = HIGH;
	unsigned int bitCount = 0;

	pinMode(DHTPIN, OUTPUT);
	digitalWrite(DHTPIN, LOW);
	delay(18);
	digitalWrite(DHTPIN, HIGH);
	delayMicroseconds(40);
	pinMode(DHTPIN, INPUT);

	for (unsigned int i = 0; i < MAXTIMINGS; ++i) {
		uint8_t counter = 0;
		while (digitalRead(DHTPIN) == lastState) {
			if (++counter == 255)
				break;
			delayMicroseconds(1);
		}

		lastState = digitalRead(DHTPIN);
		if (counter == 255)
			break;

		if (i >= 4 && i % 2 == 0) {
			if (bitCount >= 40)
				break;
			data[bitCount / 8] <<= 1;
			if (counter > 16)
				data[bitCount / 8] |= 1;
			++bitCount;
		}
	}

	if (bitCount != 40 ||
	    data[4] != (uint8_t)(data[0] + data[1] + data[2] + data[3]))
		return 1;

	*humidity = data[0];
	return 0;
}

static int readTemperature(float *temperature)
{
	char devicePath[128];
	char buffer[256];
	int pathLength = snprintf(devicePath, sizeof(devicePath),
				  "%s/%s/w1_slave", W1_PATH, DEVICE);
	if (pathLength < 0 || (size_t)pathLength >= sizeof(devicePath)) {
		fprintf(stderr, "1-Wire device path is too long\n");
		return 1;
	}

	int fd = open(devicePath, O_RDONLY);
	if (fd == -1) {
		perror("Couldn't open the 1-Wire device");
		return 1;
	}

	size_t total = 0;
	while (total < sizeof(buffer) - 1) {
		ssize_t count = read(fd, buffer + total, sizeof(buffer) - 1 - total);
		if (count < 0 && errno == EINTR)
			continue;
		if (count < 0) {
			perror("Couldn't read the 1-Wire device");
			close(fd);
			return 1;
		}
		if (count == 0)
			break;
		total += (size_t)count;
	}
	close(fd);
	buffer[total] = '\0';

	char *value = strstr(buffer, "t=");
	if (strstr(buffer, "YES") == NULL || value == NULL) {
		fprintf(stderr, "Invalid 1-Wire sensor response\n");
		return 1;
	}

	errno = 0;
	char *end;
	long milliDegrees = strtol(value + 2, &end, 10);
	if (errno == ERANGE || end == value + 2) {
		fprintf(stderr, "Invalid 1-Wire temperature value\n");
		return 1;
	}

	*temperature = (float)milliDegrees / 1000.0f;
	return 0;
}

int main(void)
{
	if (wiringPiSetup() == -1) {
		fprintf(stderr, "Could not initialize wiringPi\n");
		return 1;
	}

	if (mysql_init(&mysql) == NULL) {
		fprintf(stderr, "Could not initialize MySQL client\n");
		return 1;
	}

	connection = mysql_real_connect(&mysql, "localhost", "pico", "maurizio",
					"weather", 0, NULL, 0);
	if (connection == NULL) {
		fprintf(stderr, "%s\n", mysql_error(&mysql));
		mysql_close(&mysql);
		return 1;
	}

	while (1) {
		float rawTemperature;
		if (readTemperature(&rawTemperature) == 0) {
			float filteredTemperature =
				roundToTenth((float)myFilterTemp.getFilteredValue(rawTemperature));
			saveTemperature(filteredTemperature);
			printf("Valore Temp acquisito %.1f C\n", filteredTemperature);
		}

		unsigned int rawHumidity;
		if (readDht11Humidity(&rawHumidity) == 0) {
			float filteredHumidity =
				(float)myFilterHum.getFilteredValue(rawHumidity);
			saveHumidity((unsigned int)filteredHumidity);
			printf("Valore Hum acquisito %.0f\n", filteredHumidity);
		} else {
			fprintf(stderr, "Invalid DHT11 response\n");
		}

		sleep(SCAN_TIME);
	}

	mysql_close(connection);
	return 0;
}
