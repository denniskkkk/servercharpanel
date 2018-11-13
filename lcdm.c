/*
 ============================================================================
 Name        : lcdm.c
 Author      : Dennis
 Version     :
 Copyright   : 2016
 Description : Hello World in C, Ansi-style
 ============================================================================
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <fcntl.h>
#include <usb.h>
#include <sys/statvfs.h>

#include "dispstr.h"

#define _GNU_SOURCE

#define LCD2USB_VID  0x0403
#define LCD2USB_PID  0xc630

#define LCD_CTRL_0         (1<<3)
#define LCD_CTRL_1         (1<<4)
#define LCD_BOTH           (LCD_CTRL_0 | LCD_CTRL_1)

#define LCD_ECHO           (0<<5)
#define LCD_CMD            (1<<5)
#define LCD_DATA           (2<<5)
#define LCD_SET            (3<<5)
#define LCD_GET            (4<<5)

/* target is value to set */
#define LCD_SET_CONTRAST   (LCD_SET | (0<<3))
#define LCD_SET_BRIGHTNESS (LCD_SET | (1<<3))
#define LCD_SET_RESERVED0  (LCD_SET | (2<<3))
#define LCD_SET_RESERVED1  (LCD_SET | (3<<3))

/* target is value to get */
#define LCD_GET_FWVER      (LCD_GET | (0<<3))
#define LCD_GET_KEYS       (LCD_GET | (1<<3))
#define LCD_GET_CTRL       (LCD_GET | (2<<3))
#define LCD_GET_RESERVED1  (LCD_GET | (3<<3))

#define BUFFER_MAX_CMD 4

char *datapath = "/data";

int startstop = 1;

#include <stdio.h>
#include <stdlib.h>

int cpuload(void)
{
    long double a[4], b[4], loadavg;
    FILE *fp;

        fp = fopen("/proc/stat","r");
        fscanf(fp,"%*s %Lf %Lf %Lf %Lf",&a[0],&a[1],&a[2],&a[3]);
        fclose(fp);
        sleep (1);

        fp = fopen("/proc/stat","r");
        fscanf(fp,"%*s %Lf %Lf %Lf %Lf",&b[0],&b[1],&b[2],&b[3]);
        fclose(fp);

        loadavg = ((b[0]+b[1]+b[2]) - (a[0]+a[1]+a[2])) / ((b[0]+b[1]+b[2]+b[3]) - (a[0]+a[1]+a[2]+a[3]));
        return (int)(loadavg * 100);
}

long getavailable(const char* path)
{
    struct statvfs stat;
    if (statvfs(path, &stat) != 0) {
	     return -1;
    }
    return stat.f_bsize * stat.f_bavail / 1024 / 1024 / 1024;
}

long getfree(const char* path)
{
    struct statvfs stat;
    if (statvfs(path, &stat) != 0) {
            return -1;
    }
    return stat.f_bsize * stat.f_blocks / 1024 / 1024 / 1024;
}


int getRamTotal(void)
{
    FILE *meminfo = fopen("/proc/meminfo", "r");
    if(meminfo == NULL) {
    	fprintf (stderr, "error reading system\n");
    	exit (-1);
    }
    char line[256];
    while(fgets(line, sizeof(line), meminfo))
    {
        int ram;
        if(sscanf(line, "MemTotal: %d kB", &ram) == 1)
        {
            fclose(meminfo);
            return ram;
        }
    }
    fclose(meminfo);
    return -1;
}

int getRamFree(void)
{
    FILE *meminfo = fopen("/proc/meminfo", "r");
    if(meminfo == NULL) {
    	fprintf (stderr, "error reading system\n");
    	exit (-1);
    }
    char line[256];
    while(fgets(line, sizeof(line), meminfo))
    {
        int ram;
        if(sscanf(line, "MemFree: %d kB", &ram) == 1)
        {
            fclose(meminfo);
            return ram;
        }
    }
    fclose(meminfo);
    return -1;
}

usb_dev_handle *handle = NULL;
int lcd_send(int request, int value, int index) {
	if (usb_control_msg(handle, USB_TYPE_VENDOR, request, value, index, NULL, 0,
			1000) < 0) {
		fprintf(stderr, "USB failed\n");
		exit(EXIT_FAILURE);
	}
	return 0;
}

int buffer_current_type = -1; /* nothing in buffer yet */
int buffer_current_fill = 0; /* -"- */
unsigned char buffer[BUFFER_MAX_CMD];
void lcd_flush(void) {
	int request, value, index;
	if (buffer_current_type == -1)
		return;
	request = buffer_current_type | (buffer_current_fill - 1);
	value = buffer[0] | (buffer[1] << 8);
	index = buffer[2] | (buffer[3] << 8);
	lcd_send(request, value, index);
	buffer_current_type = -1;
	buffer_current_fill = 0;
}

void lcd_enqueue(int command_type, int value) {
	if ((buffer_current_type >= 0) && (buffer_current_type != command_type))
		lcd_flush();
	buffer_current_type = command_type;
	buffer[buffer_current_fill++] = value;
	if (buffer_current_fill == BUFFER_MAX_CMD)
		lcd_flush();
}

void lcd_command(const unsigned char ctrl, const unsigned char cmd) {
	lcd_enqueue(LCD_CMD | ctrl, cmd);
}

void lcd_clear(void) {
	lcd_command(LCD_BOTH, 0x01); /* clear display */
	lcd_command(LCD_BOTH, 0x03); /* return home */
}

void lcd_home(void) {
	lcd_command(LCD_BOTH, 0x03); /* return home */
}

void lcd_write(const char *data, int len) {
	int ctrl = LCD_CTRL_0;
	int q;
	for (q = 0; q < len; q++) {
		lcd_enqueue(LCD_DATA | ctrl, *(data + q));
	}
	lcd_flush();
}


void lcd_writeEx(const char *data) {
	int i;
	char linedata[128];
	char tmp[128];
	memset(tmp, 32, 80);
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	int lcpu = cpuload ();
	//fprintf (stdout, "%d , %d \n", getfree (datapath), getavailable (datapath));
	snprintf  (tmp + 0  , 21, "%s", data); // line 1
	snprintf  (tmp + 20 , 21, "MEM:%d/%dMB" ,  getRamFree()/1024, getRamTotal() /1024); // line 3
	snprintf  (tmp + 40 , 12, "DSK:%.2f%%", (double)getavailable(datapath) / (double)getfree (datapath) * 100 );
	snprintf  (tmp + 52 , 10, "CPU:%d%%", lcpu); // line 2
	snprintf  (tmp + 60 , 21, "%02d-%s-%04d %02d:%02d:%02d", tm.tm_mday , mth[tm.tm_mon],
			tm.tm_year + 1900 , tm.tm_hour, tm.tm_min, tm.tm_sec);   // line 4
	for (i = 0 ; i < 80 ;i ++) {
		if (tmp [i] > 31) {
		linedata[i] = tmp[i] & 0x7f;
		} else {
			linedata[i] = 32;
		}
	}
	lcd_write(linedata, 80);
}

#define ECHO_NUM 100
void lcd_echo(void) {
	int i, nBytes, errors = 0;
	unsigned short val, ret;
	for (i = 0; i < ECHO_NUM; i++) {
		val = rand() & 0xffff;
		nBytes = usb_control_msg(handle,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN,
		LCD_ECHO, val, 0, (char*) &ret, sizeof(ret), 1000);
		if (nBytes < 0) {
			fprintf(stderr, "USB failed\n");
			return;
		}
		if (val != ret)
			errors++;
	}
	if (errors > 0) {
		fprintf(stderr, "USB failed\n");
		exit(EXIT_FAILURE);
	}
}
/* set a value in the LCD interface */
void lcd_set(unsigned char cmd, int value) {
	if (usb_control_msg(handle, USB_TYPE_VENDOR, cmd, value, 0, NULL, 0, 30000)
			< 0) {
		fprintf(stderr, "USB failed\n");
		exit(EXIT_FAILURE);
	}
}

void disp() {
	struct usb_bus *bus;
	struct usb_device *dev;
	size_t len;
	ssize_t read;
	FILE *f;
	int i;
	usb_init();
	usb_find_busses();
	usb_find_devices();
	for (bus = usb_get_busses(); bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			if ((dev->descriptor.idVendor == LCD2USB_VID)
					&& (dev->descriptor.idProduct == LCD2USB_PID)) {
				if (!(handle = usb_open(dev)))
					fprintf(stderr, "Error:Cannot open device: %s\n",
							usb_strerror());
				break;
			}
		}
	}
	if (!handle) {
		fprintf(stderr, "Error:No LCD\n");
		exit(-1);
	}
	lcd_echo();
	lcd_home();
	char *tmpline = malloc(sizeof(char) * 80);
	while (startstop) {
		f = fopen("/tmp/disp.txt", "r");
		memset(tmpline, 0, sizeof(tmpline) / sizeof(char));
		if (f <= 0) {
			//fprintf(stderr, "wait 3sec\n");
			lcd_writeEx(disp0000);
			sleep(3);
		} else {
			lcd_clear();
			if ((read = getline(&tmpline, &len, f)) != -1) {
				if (read > 80) {
					read = 80;
				}
				if (read > 0) {
					int code = atoi(tmpline);
					//fprintf (stdout, "code= %d\n", code);
					if (code >= 0) {
						switch (code) {
						case 1001:
							lcd_writeEx(disp1001);
							break;
						case 1002:
							lcd_writeEx(disp1002);
							break;
						case 1003:
							lcd_writeEx(disp1003);
							break;
						case 1004:
							lcd_writeEx(disp1004);
							break;
						case 1005:
							lcd_writeEx(disp1005);
							break;
						case 1006:
							lcd_writeEx(disp1006);
							break;
						case 1007:
							lcd_writeEx(disp1007);
							break;
						case 1008:
							lcd_writeEx(disp1008);
							break;
						case 1009:
							lcd_writeEx(disp1009);
							break;
						case 1010:
							lcd_writeEx(disp1010);
							break;
						case 1011:
							lcd_writeEx(disp1011);
							break;
						case 1012:
							lcd_writeEx(disp1012);
							break;
						case 1013:
							lcd_writeEx(disp1013);
							break;
						case 1014:
							lcd_writeEx(disp1014);
							break;
						case 1015:
							lcd_writeEx(disp1015);
							break;

						default:
							lcd_writeEx(disp0000);
						}
					}
				}
				fseek(f, 0, SEEK_SET);
			}
			fclose(f);
			//fprintf(stdout, "wait 2sec\n");
			//sleep(1);
		}
	}
	free(tmpline);
	usb_close(handle);
}

/*
 *
 * test fifo read
 */
void disp2() {
	struct usb_bus *bus;
	struct usb_device *dev;
	int len = 128;
	fd_set set;
	char line[128];
	char tmpline[128];
	struct timeval timeout;
	int rv;
	int f;
	int readed;
	memset(tmpline, 0, 128);
	memset(line, 0, 128);
	usb_init();
	usb_find_busses();
	usb_find_devices();
	for (bus = usb_get_busses(); bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			if ((dev->descriptor.idVendor == LCD2USB_VID)
					&& (dev->descriptor.idProduct == LCD2USB_PID)) {
				if (!(handle = usb_open(dev)))
					fprintf(stderr, "Error:Cannot open device: %s\n",
							usb_strerror());
				break;
			}
		}
	}
	if (!handle) {
		fprintf(stderr, "Error:No LCD\n");
		exit(EXIT_FAILURE);
	}
	lcd_echo();
	lcd_home();
	f = open("/tmp/disp", O_RDONLY); // fifo test
	FD_ZERO(&set); /* clear the set */
	FD_SET(f, &set); /* add our file descriptor to the set */
	timeout.tv_sec = 3;
	timeout.tv_usec = 0;
	while (1) {
		rv = select(f + 1, &set, NULL, NULL, &timeout);
		if (rv == -1) {
			perror("select"); /* an error accured */
		} else if (rv == 0) {
			fprintf( stdout, "timeout\n"); /* a timeout occured */
			//lcd_writeEx(
			//		"................                        ................");
		} else {
			readed = read(f, line, len); /* there was data to read */
			strncpy(tmpline, line, len);
		}
		fprintf(stdout, "%s\n", tmpline);
		lcd_clear();
		lcd_write(tmpline, readed);
		//close (f);
	}
	usb_close(handle);
}

void pdaemon() {
	pid_t pid;
	/* Fork off the parent process */
	pid = fork();
	/* An error occurred */
	if (pid < 0) {
		fprintf(stderr, "daemon fail\n");
		exit(EXIT_FAILURE);
	}
	if (pid > 0) {
		fprintf(stdout, "done\n");
		exit(EXIT_SUCCESS);
	}
	/* On success: The child process becomes session leader */
	if (setsid() < 0) {
		fprintf(stderr, "daemon fail\n");
		exit(EXIT_FAILURE);
	}
	/* Catch, ignore and handle signals */
	//TODO: Implement a working signal handler */
	signal(SIGCHLD, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	pid = fork();

	if (pid < 0) {
		fprintf(stderr, "daemon fail\n");
		exit(EXIT_FAILURE);
	}

	/* Success: Let the parent terminate */
	if (pid > 0) {
		fprintf(stderr, "done\n");
		exit(EXIT_SUCCESS);
	}

	/* Set new file permissions */
	umask(0);

	/* Change the working directory to the root directory */
	/* or another appropriated directory */
	chdir("/usr/sbin");

	/* Close all open file descriptors */
	int x;
	for (x = sysconf(_SC_OPEN_MAX); x > 0; x--) {
		close(x);
	}

	/* Open the log file */
	//openlog ("disp", LOG_PID, LOG_DAEMON);
}

void signal_callback_handler(int signum) {
	startstop = 0;
	lcd_clear();
	lcd_writeEx(disp1099);
	sleep(5);
	exit(signum);
}

int main(int argc, char *argv[]) {
	//signal(SIGINT, signal_callback_handler);
	signal(SIGHUP, signal_callback_handler);
	signal(SIGTERM, signal_callback_handler);
	//pdaemon ();
	disp();
	sleep(1);
	fprintf(stdout, "exit\n");
	return EXIT_SUCCESS;
}

