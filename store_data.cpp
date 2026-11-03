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
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <mysql.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
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
		syslog(LOG_ERR, "Could not read local time");
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
			syslog(LOG_WARNING,
			       "Temperature difference out of bounds "
			       "(%.1f to %.1f); data not saved",
			       previousValue, temperature);
			return 1;
		}

		char query[128];
		int length = snprintf(query, sizeof(query),
				      "INSERT INTO wr_temperature "
				      "(sensor_id, value) VALUES(5, %.1f)",
				      temperature);
		if (length < 0 || (size_t)length >= sizeof(query)) {
			syslog(LOG_ERR, "Temperature query is too long");
			return 1;
		}

		if (mysql_query(connection, query) != 0) {
			syslog(LOG_ERR, "MySQL: %s", mysql_error(connection));
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
		syslog(LOG_WARNING, "Humidity out of bounds (%u%%); data not saved",
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
			syslog(LOG_ERR, "Humidity query is too long");
			return 1;
		}

		if (mysql_query(connection, query) != 0) {
			syslog(LOG_ERR, "MySQL: %s", mysql_error(connection));
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
			syslog(LOG_WARNING,
			       "Pressure difference out of bounds (%d to %d); "
			       "data not saved",
			       previousValue, pressure);
			return 1;
		}

		char query[128];
		int length = snprintf(query, sizeof(query),
				      "INSERT INTO wr_barometer "
				      "(sensor_id, value) VALUES(5, %d)",
				      pressure);
		if (length < 0 || (size_t)length >= sizeof(query)) {
			syslog(LOG_ERR, "Pressure query is too long");
			return 1;
		}

		if (mysql_query(connection, query) != 0) {
			syslog(LOG_ERR, "MySQL: %s", mysql_error(connection));
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
		syslog(LOG_ERR, "1-Wire device path is too long");
		return 1;
	}

	int fd = open(devicePath, O_RDONLY);
	if (fd == -1) {
		syslog(LOG_ERR, "Could not open 1-Wire device: %s",
		       strerror(errno));
		return 1;
	}

	size_t total = 0;
	while (total < sizeof(buffer) - 1) {
		ssize_t count = read(fd, buffer + total, sizeof(buffer) - 1 - total);
		if (count < 0 && errno == EINTR)
			continue;
		if (count < 0) {
			syslog(LOG_ERR, "Could not read 1-Wire device: %s",
			       strerror(errno));
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
		syslog(LOG_WARNING, "Invalid 1-Wire sensor response");
		return 1;
	}

	errno = 0;
	char *end;
	long milliDegrees = strtol(value + 2, &end, 10);
	if (errno == ERANGE || end == value + 2) {
		syslog(LOG_WARNING, "Invalid 1-Wire temperature value");
		return 1;
	}

	*temperature = (float)milliDegrees / 1000.0f;
	return 0;
}

static int daemonize(const char *processName)
{
	openlog(processName, LOG_NOWAIT | LOG_PID, LOG_USER);

	pid_t pid = fork();
	if (pid < 0) {
		syslog(LOG_ERR, "Could not fork: %s", strerror(errno));
		return 1;
	}
	if (pid > 0)
		return 2;

	umask(0);
	if (setsid() < 0) {
		syslog(LOG_ERR, "Could not create session: %s", strerror(errno));
		return 1;
	}
	if (chdir("/") < 0) {
		syslog(LOG_ERR, "Could not change working directory: %s",
		       strerror(errno));
		return 1;
	}

	int nullFd = open("/dev/null", O_RDWR);
	if (nullFd < 0) {
		syslog(LOG_ERR, "Could not open /dev/null: %s", strerror(errno));
		return 1;
	}
	for (int fd = STDIN_FILENO; fd <= STDERR_FILENO; ++fd) {
		if (dup2(nullFd, fd) < 0) {
			syslog(LOG_ERR, "Could not redirect standard descriptors: %s",
			       strerror(errno));
			if (nullFd > STDERR_FILENO)
				close(nullFd);
			return 1;
		}
	}
	if (nullFd > STDERR_FILENO)
		close(nullFd);

	return 0;
}

int main(int argc, char *argv[])
{
	(void)argc;
	int daemonStatus = daemonize(argv[0]);
	if (daemonStatus != 0) {
		if (daemonStatus == 2)
			return EXIT_SUCCESS;
		return EXIT_FAILURE;
	}

	syslog(LOG_NOTICE, "Sensor daemon started");

	if (wiringPiSetup() == -1) {
		syslog(LOG_ERR, "Could not initialize wiringPi");
		closelog();
		return EXIT_FAILURE;
	}

	if (mysql_init(&mysql) == NULL) {
		syslog(LOG_ERR, "Could not initialize MySQL client");
		closelog();
		return EXIT_FAILURE;
	}

	connection = mysql_real_connect(&mysql, "localhost", "user", "password",
					"weather", 0, NULL, 0);
	if (connection == NULL) {
		syslog(LOG_ERR, "MySQL connection failed: %s", mysql_error(&mysql));
		mysql_close(&mysql);
		closelog();
		return EXIT_FAILURE;
	}

	for (;;) {
		float rawTemperature;
		if (readTemperature(&rawTemperature) == 0) {
			float filteredTemperature =
				roundToTenth((float)myFilterTemp.getFilteredValue(rawTemperature));
			saveTemperature(filteredTemperature);
			syslog(LOG_INFO, "Temperature: %.1f C", filteredTemperature);
		}

		unsigned int rawHumidity;
		if (readDht11Humidity(&rawHumidity) == 0) {
			float filteredHumidity =
				(float)myFilterHum.getFilteredValue(rawHumidity);
			saveHumidity((unsigned int)filteredHumidity);
			syslog(LOG_INFO, "Humidity: %.0f%%", filteredHumidity);
		} else {
			syslog(LOG_WARNING, "Invalid DHT11 response");
		}

		sleep(SCAN_TIME);
	}

	mysql_close(connection);
	closelog();
	return EXIT_SUCCESS;
}
