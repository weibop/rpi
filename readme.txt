To start from a NOOB Rasbian:

1. Keep your Raspberry pi up to date:
	$sudo apt-get update 
	$sudo apt-get upgrade 
	$sudo reboot

2. free the spi from the blacklist:
	$sudo nano /etc/modprobe.d/raspi-blacklsit.conf spi-bcm2708 
	Put a # before this line to comment it out. 
	Save (Ctrl+O) and exit (Ctrl+X). After a reboot the device should be found
	# --- after reboot --- 
	$lsmod
	In the list lsmod outputs you should find the entry “spi_bcm2708” now. 

3. follow http://www.airspayce.com/mikem/bcm2835/ to install bcm2835 library
	this is the library using which your c programme will be able to access the GPIO and SPI

4. create your own bosca.c file:
	$mkdir bosca
	$cd bosca
	$nano bosca.c
	then copy the existed bosca.c file into your own one.
	Save and exit

5. compile your c file:
	$gcc -o bosca bosca.c -l bcm2835

6. run your application file:
	$sudo ./bosca
	The application file need sudo permission to manipulate the GPIO and SPI

7. data files will be stored in folder:
	/Bosca/date-time/
	
LED indicators on Bosca board:
	LED_RED = ON:	getting CO2 data, 		
	LED_RED = OFF:	not getting CO2 data, 	
	LED_YELLOW = TOGGLE: a data packet is successfully sent