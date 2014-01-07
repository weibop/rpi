/*
 * RPi-Bosca communication programme
 */
#include <time.h>
#include <string.h>
#include <dirent.h>
#include <bcm2835.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

typedef int bool;
#define TRUE 1
#define FALSE 0

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a,b) (a<b?a:b)

#define CRC_INIT 		0xB0CA

typedef uint8_t error_t;
#define SUCCESS 1
#define FAIL 0

#define PIN_PI_RDY RPI_GPIO_P1_15//p1.7
#define PIN_BO_FLG RPI_GPIO_P1_18//p1.6
#define PIN_PI_PKT RPI_GPIO_P1_11//p1.4, p1.5(RPI_GPIO_P1_11) seems to have some problem.
#define PIN_PI_CRC RPI_GPIO_P1_16//p2.0
	
static char *months[] = {
   "January",
   "February", 
   "March",
   "April",
   "May",
   "June",
   "July",
   "August",
   "September",
   "October",
   "November",
   "December"
};
uint32_t file_seq=0;
char *destFolder=NULL;
char *destFile=NULL;
char *logFile=NULL;
FILE *fp, *lp;

struct timeval tv;
struct tm *tm;
int seqNo, packetType, rawCO2, avgCO2, rawTemperature, rawHumidity, nh3, 
	light, afvOut, afvTmp, battery, co2Error, printinfo;
char timestr[9];

void save(uint8_t *buf, uint8_t len)
{
	if(len!=24){
		printf(" *** ERROR:  *** wrong packet length *** \n");
		return;
	}
	float temperature, humidity;
	gettimeofday(&tv, NULL);
	tm = localtime(&tv.tv_sec);
	sprintf(timestr, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);

	seqNo = buf[0] + (buf[1] << 8);
	packetType = buf[2];
	rawCO2 = buf[3] + (buf[4] << 8);
	avgCO2 = buf[5] + (buf[6] << 8);
	rawTemperature = (buf[7] << 8) + buf[8];
	rawHumidity = (buf[9] << 8) + buf[10];
	light = buf[11] + (buf[12] << 8);
	nh3 = buf[13] + (buf[14] << 8);
	afvOut = buf[15] + (buf[16] << 8);
	afvTmp = buf[17] + (buf[18] << 8);
	battery = buf[19] + (buf[20] << 8);
	co2Error = buf[21];
	temperature = -46.85 + (175.72/65536 * (float)(rawTemperature & 0xFFFC));
	humidity = -6.0 + (125.0/65536 * (float)(rawHumidity & 0xFFFC));

	printf(" %5d ||   %5d |    %5d ||   %02.6f |     %02.6f ||    %4d |    %4d |     %4d |     %4d |  %4d | %s\n", seqNo, rawCO2, avgCO2, temperature, humidity, light, nh3, afvOut, afvTmp, battery, timestr);
	fprintf(fp, "%d,%d,%d,%.6f,%.6f,%d,%d,%d,%d,%d,%d,%s,%02d.%06d\n", seqNo, rawCO2, avgCO2, temperature, humidity,  light, nh3, afvOut, afvTmp, battery, co2Error, timestr, (int)tv.tv_sec, (int)tv.tv_usec);
	fflush(fp);
}

uint16_t crcByte(uint16_t crc, uint8_t b) {
	crc = (uint8_t)(crc >> 8) | (crc << 8);
	crc ^= b;
	crc ^= (uint8_t)(crc & 0xff) >> 4;
	crc ^= crc << 12;
	crc ^= (crc & 0xff) << 5;
	return crc;
}

uint16_t calcCrc(uint8_t *msg, uint8_t len) {
	uint16_t crcCalc;
	uint8_t i;

	crcCalc = crcByte(CRC_INIT, *msg);
	for(i=1; i<len; i++) {
		crcCalc = crcByte(crcCalc, *(msg+i));
	}
	if(len%2) {
		crcCalc = crcByte(crcCalc, 0x00);
	}
	return crcCalc;
}

bool crcCheck(uint8_t *msg, uint8_t len) {
	uint16_t crc;

	crc = calcCrc(msg, len-2);

	if(((crc&0xFF) == msg[len-2]) && (((crc&0xFF00)>>8) == msg[len-1]))
		return TRUE;
	else
		return FALSE;
}

static void get_bosca_rdy(void){
	fprintf(lp, "Initializing bcm2835...\n");
	printf("Initializing bcm2835...\n");
	if (!bcm2835_init()){
		fprintf(lp, " *** Error: *** bcm2835 initialization failed ***");
		printf(" *** Error: *** bcm2835 initialization failed ***");
	}

	if(printinfo) printf("Preparing PIN1.15 (P1.7 on Bosca)...\n");
	fprintf(lp, "Preparing PIN1.15 (P1.7 on Bosca)...\n");
	bcm2835_gpio_fsel(PIN_PI_RDY, BCM2835_GPIO_FSEL_OUTP);
	bcm2835_gpio_write(PIN_PI_RDY, LOW);
	if(printinfo) printf("Preparing PIN1.18 (P1.6 on Bosca)...\n");
	fprintf(lp, "Preparing PIN1.18 (P1.6 on Bosca)...\n");
	bcm2835_gpio_fsel(PIN_BO_FLG, BCM2835_GPIO_FSEL_INPT);
	if(printinfo) printf("Preparing PIN1.11 (P1.4 on Bosca)...\n");
	fprintf(lp, "Preparing PIN1.11 (P1.4 on Bosca)...\n");
	bcm2835_gpio_fsel(PIN_PI_PKT, BCM2835_GPIO_FSEL_OUTP);
	bcm2835_gpio_write(PIN_PI_PKT, LOW);
	if(printinfo) printf("Preparing PIN1.16 (P2.0 on Bosca)...\n");
	fprintf(lp, "Preparing PIN1.16 (P2.0 on Bosca)...\n");
	bcm2835_gpio_fsel(PIN_PI_CRC, BCM2835_GPIO_FSEL_OUTP);
	bcm2835_gpio_write(PIN_PI_CRC, LOW);

	if(printinfo) printf("Setting P1.7 high and saying 'I'm rdy, its your turn now'(GPIO 22)...\n");
	fprintf(lp, "Setting P1.7 high and saying 'I'm rdy, its your turn now'(GPIO 22)...\n");
	bcm2835_gpio_write(PIN_PI_RDY, HIGH);
	
	bcm2835_delay(1000);
	
	//wait till SR33 rdy
	if(printinfo) printf("Waiting SR33 to get ready...\n");
	fprintf(lp, "Waiting SR33 to get ready...\n");
	while(!bcm2835_gpio_lev(PIN_BO_FLG))// while low, detect again;
		bcm2835_delayMicroseconds(100);
	// reaching here = SR33 high = SR33 rdy

	//tell SR33 to start sensing
	if(printinfo) printf("Getting SR33 to start sensing...\n");
	fprintf(lp, "Getting SR33 to start sensing...\n");
	bcm2835_gpio_write(PIN_PI_RDY, LOW);
}

static void pabort(const char *s)
{
	perror(s);
	abort();
}
void next_data_packet(bool* dtrans, uint8_t* dtrans_cnt)
{
	*dtrans_cnt=0;
	*dtrans=0;
	//printf("Set P1.4 & P2.0 low so that SR33 knows RPi is rdy to get len ...\n");
	bcm2835_gpio_write(PIN_PI_PKT, LOW);
	bcm2835_gpio_write(PIN_PI_CRC, LOW);
}
 
static const char *device = "/dev/spidev0.0";
static uint8_t mode;
static uint8_t bits = 8;
static uint32_t speed = 500000;
static uint16_t delay;
 
static void transfer(int fd)
{
	int ret;
	bool dtrans=0;
	uint8_t dtrans_cnt=0, detect_flag =0;
	uint8_t length_dummy = 0x00, length_val = 0x00;
	uint8_t next_data = 0, new_file;
	uint8_t packet_num = 0xff;
	
	float temperature, humidity;
	const unsigned char *packet;
	char timestr[9];
	//char filepath[] = "/home/bosca/Desktop/Bosca";
	char filepath[] = "Bosca";
	//struct stat st;
	char foldername[15];

	uint8_t tx[256]; 
	uint8_t rx[ARRAY_SIZE(tx)];
	
	uint8_t filename[]="data/exp_000/data_000.csv";

	struct spi_ioc_transfer tr_len;
		tr_len.tx_buf = (unsigned long)&length_dummy;
		tr_len.rx_buf = (unsigned long)&length_val;
		tr_len.len = 1;
		tr_len.delay_usecs = delay;
		tr_len.speed_hz = speed;
		tr_len.bits_per_word = bits;

	struct spi_ioc_transfer tr_data;
		tr_data.tx_buf = (unsigned long)tx;
		tr_data.rx_buf = (unsigned long)rx;
		tr_data.len = length_val;
		tr_data.delay_usecs = delay;
		tr_data.speed_hz = speed;
		tr_data.bits_per_word = bits;


	gettimeofday(&tv, NULL);
	tm = localtime(&tv.tv_sec);
	
	// =========== set_dirname(); ==============
	sprintf(foldername, "%02d%02d%02d-%02d%02d%02d/", tm->tm_mon+1, tm->tm_mday, tm->tm_year%100, tm->tm_hour, tm->tm_min, tm->tm_sec);
	//if(stat(filepath, &st)) {
	// directory is not present
		if(mkdir(filepath, S_IRWXU|S_IRWXG|S_IRWXO)) {
			printf(" *** ERROR:  *** Unable to create directory \"%s\" *** \n", filepath);
		}
	//}	
	destFolder = (char *)malloc((strlen(filepath) + strlen(foldername) + 2) * sizeof(char));
	sprintf(destFolder, "%s/%s", filepath, foldername);
	//if(stat(destFolder, &st)) {
		// directory is not present
		int dir_errcode;
		if(dir_errcode = mkdir(destFolder, S_IRWXU|S_IRWXG|S_IRWXO)) {
			if(dir_errcode != -1)
				printf(" *** warning:  *** Unable to create directory \"%s\" *** \n", destFolder);
			//return 2;
		}
	//}
	destFile = (char *)malloc((strlen(destFolder) + 50) * sizeof(char));	
	//return 0;

	// =========== set log.txt file ==============
	logFile = (char *)malloc((strlen(destFolder) + 9) * sizeof(char));
	if(!logFile) {
		printf(" *** ERROR:  *** Unable to allocate sufficient memory *** \nExiting\n");
		//return 2;
	}
	sprintf(logFile, "%sLog.txt", destFolder);
	if(!(lp = fopen(logFile, "w+"))) {
		printf(" *** ERROR:  *** Unable to open %s *** \n", logFile);
	}
	fprintf(lp, "%02d %s %d %d:%02d:%02d %s\n", tm->tm_mday, months[tm->tm_mon], tm->tm_year+1900, tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_zone);

	get_bosca_rdy();

	//wait till SR33 says packets are ready to send
	if(printinfo) printf("Waiting SR33 for the data packets...\n");
	while(1){
		fflush(lp);
		if(!dtrans){
			if(!detect_flag){// 1st time
				detect_flag =1;
				if(printinfo) printf("Waiting for Data_Ready flag to go low");
				fprintf(lp, "Waiting for Data_Ready flag to go low");
			}
			bcm2835_delayMicroseconds(100);
			if(!bcm2835_gpio_lev(PIN_BO_FLG)){// while HIGH, detect again;
				dtrans = 1;// reaching here = SR33 gets a package done
				if(printinfo) printf("...done\n");
				fprintf(lp, "...done\n");
				detect_flag = 0;
			}
		}
		else{
			if(dtrans_cnt<5){
				dtrans_cnt++;

				if(printinfo) printf("getting the length of the data packet...\n");
				fprintf(lp, "getting the length of the data packet...\n");
				ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr_len);
				if (ret < 1){
					fprintf(lp, "can't send spi message");
					pabort("can't send spi message");
				}
				if(length_val==0xff)
					return;
				if(printinfo) printf("length of the data packet = %d\n", length_val);
				fprintf(lp, "length of the data packet = %d\n", length_val);

				tr_data.len = length_val;
				if(printinfo) printf("getting data packet...\n");
				fprintf(lp, "getting data packet...\n");
				ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr_data);

				// say: all bytes of the packet are received.
				// and sr33 will turn to RPI_DCRC state to wait for 2.0 to go high 
				bcm2835_gpio_write(PIN_PI_PKT, HIGH);

				if (ret < 1){
					fprintf(lp, "can't send spi message");
					pabort("can't send spi message");
				}

				fprintf(lp, "rx : ");
				for (ret = 0; ret < MIN(length_val,50); ret++) {
					fprintf(lp, "%.2X ", rx[ret]);
				}
				fprintf(lp, "\n");


				if(crcCheck(rx, length_val)){//true, CRC successful
				
					gettimeofday(&tv, NULL);
					tm = localtime(&tv.tv_sec);

					bcm2835_gpio_write(PIN_PI_PKT, HIGH);
					if(printinfo) printf("CRC check succeeds...\n");// now wait for the next data packet
					fprintf(lp, "CRC check succeeds...\n");// now wait for the next data packet
					next_data = 1;
					if(!(++packet_num%60)){// do this every 60 mins
						packet_num=0;
						//file_seq++;
						if(file_seq)
							fclose(fp);
						//set_filename();//change file name
						printf("new file: ");
						fprintf(lp, "new file: ");
						gettimeofday(&tv, NULL);
						tm = localtime(&tv.tv_sec);
						
						sprintf(destFile, "%s%d-bosca-%02d%s%04d-%02d%02d%02d.csv", destFolder, file_seq++,  tm->tm_mday, months[tm->tm_mon], tm->tm_year+1900, tm->tm_hour, tm->tm_min, tm->tm_sec);
						printf("trying to open %s\n",destFile);
						fprintf(lp, "trying to open %s\n",destFile);
						if(!(fp = fopen(destFile, "w"))) {
							printf(" *** ERROR:  *** Unable to open %s\nExiting *** \n", destFile);
						}else{
							printf(" seqNo ||  rawCO2 |   avgCO2 || temperature |     humidity  ||   light |   NH3   |   afvOut |   afvTmp |  batt | time\n");
							fprintf(fp, "%02d %s %d %d:%02d:%02d %s\n", tm->tm_mday, months[tm->tm_mon], tm->tm_year+1900, tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_zone);
							fprintf(fp, "Seq No.,CO2_out,CO2_avg,TMP,HUM,LIGHT,NH3,AFO,AFT,BATT,CO2_err,Time,Unix Time\n");   
							fflush(fp);
						}
						//printf("file name set to %s\n",destFile);//
						//save1(filename);
					}
					save(rx, length_val);
				}
				else{//false
					bcm2835_gpio_write(PIN_PI_PKT, LOW);
					printf("CRC check fails...\n");//need to do trans again
					fprintf(lp, "CRC check fails...\n");//need to do trans again
				}
				bcm2835_gpio_write(PIN_PI_CRC, HIGH);
				bcm2835_delayMicroseconds(100);// wait for sr33 to receive the flag.
			}
			else{//dtrans_cnt>=5
				next_data = 1;
			}
			if(next_data){
				next_data = 0;
				next_data_packet(&dtrans, &dtrans_cnt);
			}
		}
	}
}
static void print_usage(const char *prog)
{
	printf("Usage: %s [-DsbdlHOLC3]\n", prog);
	puts("  -D --device   device to use (default /dev/spidev1.1)\n"
	     "  -s --speed    max speed (Hz)\n"
	     "  -d --delay    delay (usec)\n"
	     "  -b --bpw      bits per word \n"
	     "  -l --loop     loopback\n"
	     "  -H --cpha     clock phase\n"
	     "  -O --cpol     clock polarity\n"
	     "  -L --lsb      least significant bit first\n"
	     "  -C --cs-high  chip select active high\n"
	     "  -3 --3wire    SI/SO signals shared\n");
	exit(1);
}
 
static void parse_opts(int argc, char *argv[])
{
	while (1) {
		static const struct option lopts[] = {
			{ "device",  1, 0, 'D' },
			{ "speed",   1, 0, 's' },
			{ "delay",   1, 0, 'd' },
			{ "bpw",     1, 0, 'b' },
			{ "loop",    0, 0, 'l' },
			{ "cpha",    0, 0, 'H' },
			{ "cpol",    0, 0, 'O' },
			{ "lsb",     0, 0, 'L' },
			{ "cs-high", 0, 0, 'C' },
			{ "3wire",   0, 0, '3' },
			{ "no-cs",   0, 0, 'N' },
			{ "ready",   0, 0, 'R' },
			{ NULL, 0, 0, 0 },
		};
		int c;
 
		c = getopt_long(argc, argv, "D:s:d:b:lHOLC3NR", lopts, NULL);
 
		if (c == -1)
			break;
 
		switch (c) {
		case 'D':
			device = optarg;
			break;
		case 's':
			speed = atoi(optarg);
			break;
		case 'd':
			delay = atoi(optarg);
			break;
		case 'b':
			bits = atoi(optarg);
			break;
		case 'l':
			mode |= SPI_LOOP;
			break;
		case 'H':
			mode |= SPI_CPHA;
			break;
		case 'O':
			mode |= SPI_CPOL;
			break;
		case 'L':
			mode |= SPI_LSB_FIRST;
			break;
		case 'C':
			mode |= SPI_CS_HIGH;
			break;
		case '3':
			mode |= SPI_3WIRE;
			break;
		case 'N':
			mode |= SPI_NO_CS;
			break;
		case 'R':
			mode |= SPI_READY;
			break;
		default:
			print_usage(argv[0]);
			break;
		}
	}
}
 
int main(int argc, char *argv[])
{
	file_seq = 0;
	int ret = 0;
	int fd;
	printinfo=0;
	parse_opts(argc, argv);
 
	fd = open(device, O_RDWR);
	if (fd < 0)
		pabort("can't open device");
 
	/*
	 * spi mode
	 */
	ret = ioctl(fd, SPI_IOC_WR_MODE, &mode);
	if (ret == -1)
		pabort("can't set spi mode");
 
	ret = ioctl(fd, SPI_IOC_RD_MODE, &mode);
	if (ret == -1)
		pabort("can't get spi mode");

	/*
	 * bits per word
	 */
	ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
	if (ret == -1)
		pabort("can't set bits per word");
 
	ret = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits);
	if (ret == -1)
		pabort("can't get bits per word");
 
	/*
	 * max speed hz
	 */
	ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
	if (ret == -1)
		pabort("can't set max speed hz");
 
	ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
	if (ret == -1)
		pabort("can't get max speed hz");
 
	printf("spi mode: %d\n", mode);
	printf("bits per word: %d\n", bits);
	printf("max speed: %d Hz (%d KHz)\n", speed, speed/1000);
	
	while(1){	
		//get_bosca_rdy();
 		transfer(fd);
	} 
	close(fd);
 
	return ret;
}