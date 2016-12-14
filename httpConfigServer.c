/*
 * Minimal http server to allow configuration of WiFi and other network parameters.
 * 
 * GPIO controlled.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h> //Remember to add -lpthread to linker options
#include "iptools2.3.h"
#include <signal.h>             //For the signal() line)
#include "minimal_gpio.h"

#define _POSIX_C_SOURCE 200809L  //This line required for OSX otherwise popen() fails)
//#define _POSIX_SOURCE
#define  FIELD          1024     //used for user entry field buffers
#define  SECTION        4096    //Used for buffers containing sections of the html page

//#define WPA_CONFIG_FILENAME "/etc/wpa_supplicant/wpa_supplicant.conf"

//Ascii colour escape codes
#define KNRM "\x1B[0m"      //normal
#define KRED "\x1B[31m"     //red
#define KGRN "\x1B[32m"     //green
#define KYEL "\x1B[33m"     //yellow
#define KBLU "\x1B[34m"     //blue
#define KMAG "\x1B[35m"     //magenta
#define KCYN "\x1B[36m"     //cyan
#define KWHT "\x1B[37m"     //white

/*
typedef struct GPIOPin{
    int input;         //Used for 'enter/exit setup mode' button
    int output;        //Used for status LED
    
} gpioPin;
 */
//Global variables
volatile int twoSecSq, oneSecSq, halfSecSq, quartSecSq; //'Square wave' signal rails used to flash LEDs
volatile int setupMode = 0; //0= normal mode, 1=adhoc AP, 2= hostAP
volatile int wifiConnectedStatus;
int sockfd = -1; //file descriptor for 'passive' or 'master'  http listening socket
int web_sockfd = -1; //file descriptor for 'passive' or 'master'  http listening socket for web socket thread
int httpListeningPort = 0; //This is the 'actual' port no that was successfully bound to, in simpleHTTPServerThread;
char ap_ssid[FIELD] = {0}; //Stores the name of the SSID
char wpa_supplicantConfigPath[FIELD] = {0}; //Holds the path/name of the target wpa_supplicant file (supplied at runtime)
char hostapdPath[FIELD] = {0}; //Holds the path/filename of the external hostapd (wpa access point) executable
int unsavedChangesFlag=0;       //Signifies whether there are any unsaved/non backed up config changes made via the website

enum DHCPClient { //Used to signal which dhcp client to use
    nodhcpclient, dhclient, udhcpc
};
enum DHCPClient installedDHCPClient;

size_t strlcpy(char* dst, const char* src, size_t bufsize) {
    /*
     * Safer version of strncpy() which always termainates the copied string with
     * the null character (strncpy() doesn't terminate a string)
     */
    size_t srclen = strlen(src);
    size_t result = srclen; /* Result is always the length of the src string */
    if (bufsize > 0) {
        if (srclen >= bufsize)
            srclen = bufsize - 1;
        if (srclen > 0)
            memcpy(dst, src, srclen);
        dst[srclen] = '\0';
    }
    return result;
}

int stringBuilder(char output[], unsigned int outputLength, char input[]) {
    /*
     * Concatenates string input[] to output[] (appends the null character) safely.
     * 
     * Returns the no of characters of input[] remaining or -1 of no more room to append
     */

    int inputLength = strlen(input) + 1; //Take into account null character
    int outputUsed = strlen(output) + 1; //Calculate how much of input buffer already used

    if (outputUsed > outputLength) return -1; //If no null detected, danger of an overflow

    int lengthRequired = outputUsed + inputLength - 1; //Calculate total requirement (null included)
    int lengthRemaining = outputLength - outputUsed - 1; //Take into account space needed for null character
    if (lengthRequired < outputLength) {
        /*
        printf("inputLength: %d, outputUsed: %d, lengthRequired: %d, lengthRemaining: %d\n",
                inputLength, outputUsed, lengthRequired, lengthRemaining);
         */
        strncat(output, input, lengthRemaining);
        //printf("new output length is : %d\n", (int) strlen(output) + 1);

        //Now recaculate amount of output[] used
        outputUsed = strlen(output) + 1; //Calculate how much of input buffer already used
        int lengthRemaining = outputLength - outputUsed - 1; //Take into account space needed for null character
        return lengthRemaining;
    } else return -1;
}

void updateTime(char timeAsString[], unsigned int length) {
    /*
     * Populates the supplied string with the current time and date
     */
    time_t currentTime;
    //char timeAsString[FIELD] = {0}; //Buffer to hold human readable time
    time(&currentTime); //Capture current time, assign it to currentTime
    memset(timeAsString, 0, length); //Clear char array before use
    strftime(timeAsString, length, "%H:%M:%S, %d/%m/%y", localtime(&currentTime)); //Format human readable time
}

int scanForNetworks(char htmlNetworksFound[], unsigned int outputBufferLength) {
    /*
     * Initiates a network scan and populates the supplied string formattedd, complete with html tags
     */
    //Create WiFiNetwork struct to contain results of network scan
    printf("scanForNetworks() called\n");
    int k;
    wifiNetwork networkList[50]; //Allow up to 50 networks to be displayed
    for (k = 0; k < 50; k++)
        initWiFiNetworkStruct(&networkList[k]);
    int noOfNetworksFound = iwscanWrapper(networkList, 50, "wlan0");

    if (k > 0) {
        memset(htmlNetworksFound, 0, outputBufferLength); //Clear htmlNetworksFound array
        char buffer[FIELD] = {0}; //Temporary output buffer
        stringBuilder(htmlNetworksFound, outputBufferLength, "<br><br><form><fieldset><legend>The following wireless networks found</legend>");
        for (k = 0; k < noOfNetworksFound; k++) {
            //printf("%d: %s\n", k, networkList[k].essid);
            memset(buffer, 0, FIELD);
            snprintf(buffer, FIELD, "%d: %s,           encryption: %s<br>", k,
                    networkList[k].essid, networkList[k].encryption); //Create formatted string
            int ret = stringBuilder(htmlNetworksFound, outputBufferLength, buffer);
            if (ret == -1) printf("htmlNetworksFound char array not large enough\n");
        }
        int ret = stringBuilder(htmlNetworksFound, outputBufferLength, "</fieldset></form>");
        if (ret == -1) printf("htmlNetworksFound char array not large enough\n");
    }
    return 0;
}

int updateStatus(char htmlStatus[], unsigned int outputBufferLength) {
    /*
     * Creates a formatted html string containing status information
     */
    memset(htmlStatus, 0, outputBufferLength); //Clear char array before we start
    char buffer[FIELD] = {0}; //Temporary buffer
    stringBuilder(htmlStatus, outputBufferLength, "<form><fieldset><legend>Status</legend>");
    //Display hostname
    stringBuilder(htmlStatus, outputBufferLength, "Hostname: ");
    int hostNameLength = getHostName(buffer, FIELD);
    if (hostNameLength > 0) stringBuilder(htmlStatus, outputBufferLength, buffer); //Append hostname (contained within buffer[]) to htmlStatus[]

    //Display serial number
    memset(buffer, 0, FIELD); //Clear temp buffer
    int serialNo = getSerialNumber(); //Get serial number
    snprintf(buffer, FIELD, ", Serial Number: %X<br>", serialNo); //Format serial number as Hexadecimal
    stringBuilder(htmlStatus, outputBufferLength, buffer); //Append serial no (contained within buffer[]) to htmlStatus[]

    //1) What network interfaces do we have?
    //Use getLocalIPAddr() to give us a list of interface names
    int k;
    char interfaceList[20][2][20]; //Can store 20 addresses
    //int noOfInterfaces = getLocalIPaddr(interfaceList, 20); //Display active local ethernet interfaces
    int noOfInterfaces = listAllInterfaces(interfaceList, 20); //Display all local ethernet interfaces
    /*
    printf("No of interfaces found:%d\n", noOfInterfaces);
    for (k = 0; k < noOfInterfaces; k++)
        printf("%d: %s %s\n", k, interfaceList[k][0], interfaceList[k][1]);
     */
    if (noOfInterfaces > 0) {
        //Create an array of Nic structs to hold the data returned above
        nic nicList[20]; //Allow for 20 possible interfaces (unlikely!)
        for (k = 0; k < noOfInterfaces; k++) //Initialise struct members       
            initNicStruct(&nicList[k]);

        //Now copy info returned by getLocalIPadd() into nicList
        for (k = 0; k < noOfInterfaces; k++) {
            strlcpy(nicList[k].name, interfaceList[k][0], ARG_LENGTH); //copy name
            //strlcpy(nicList[k].address,interfaceList[k][1],ARG_LENGTH); //copy ip address. Don't need this yet, get this info at the next step
            //printf("nicList: %d: Name: %s, Addr: %s\n", k, nicList[k].name, nicList[k].address);
        }

        //Now use the list of names to query the actual card ip settings (equiv of ifconfig)
        for (k = 0; k < noOfInterfaces; k++) {
            ifconfigGetNicStatus(&nicList[k]); //Populate nicList with complete info
            /*
            printf("nicList: %d: Name: %s, \tAddr: %s \tB.Addr: %s, \tMask: %s, \tStatus:%d\n"
                    , k, nicList[k].name, nicList[k].address, nicList[k].broadcastAddress, nicList[k].netmask, nicList[k].status);
             */
        }

        stringBuilder(htmlStatus, outputBufferLength, "Interface        Address        Netmask<br>");
        for (k = 1; k < noOfInterfaces; k++) { //Start at interface '1' because '0' is the loopback device. Not interested in that
            //Create list of interface parameters in html
            memset(buffer, 0, FIELD);
            snprintf(buffer, FIELD, "%s,  %s,       %s<br>", nicList[k].name, nicList[k].address, nicList[k].netmask);
            stringBuilder(htmlStatus, outputBufferLength, buffer);
        }

        //Add a <br> to the html
        stringBuilder(htmlStatus, outputBufferLength, "<br>");

        //Get the default gateway
        memset(buffer, 0, FIELD); //Clear buffer

        if (getGateway(buffer, FIELD) > 0) {
            stringBuilder(htmlStatus, outputBufferLength, "Current default gateway: ");
            stringBuilder(htmlStatus, outputBufferLength, buffer);
            stringBuilder(htmlStatus, outputBufferLength, "<br>");
        }


        //Get Wifi connection status for wlan0
        wifiNetwork wifiStatus;
        initWiFiNetworkStruct(&wifiStatus);
        if (getWiFiConnStatus(&wifiStatus, "wlan0") > 0) { //If currently associated
            memset(buffer, 0, FIELD);
            snprintf(buffer, FIELD, "Interface wlan0 Connected to network: %s, signal strength: %ddBm<br>", wifiStatus.essid, wifiStatus.sigLevel);
            stringBuilder(htmlStatus, outputBufferLength, buffer);
        }
        //And also WiFi Connection status for wlan1 (if it is installed))
        if (isWlan1Present() == 1) {
            initWiFiNetworkStruct(&wifiStatus);
            if (getWiFiConnStatus(&wifiStatus, "wlan1") > 0) { //If currently associated
                memset(buffer, 0, FIELD);
                snprintf(buffer, FIELD, "Interface wlan1 Connected to network: %s, signal strength: %ddBm<br>", wifiStatus.essid, wifiStatus.sigLevel);
                stringBuilder(htmlStatus, outputBufferLength, buffer);
            }
        }
        if (getSetupMode() == 1) {
            stringBuilder(htmlStatus, outputBufferLength, "**Adhoc Access Point mode enabled:**<br>");
            stringBuilder(htmlStatus, outputBufferLength, ap_ssid);
        }
        if (getSetupMode() == 2) {
            stringBuilder(htmlStatus, outputBufferLength, "**HostAP Access Point mode enabled:**<br>");
            stringBuilder(htmlStatus, outputBufferLength, ap_ssid);
        }

    }
    /*
    if(getUnsavedChangesFlag()==1){
        stringBuilder(htmlStatus, outputBufferLength, "<font>color=\"red\"**Warning: Unsaved changes. Backup config to make permanent **</font><br>");
    }
    */
    //And finally...
    stringBuilder(htmlStatus, outputBufferLength, "</fieldset></form>");
    return 0;
}

int updateKnownNetworks(char htmlKnownNetworks[], unsigned int outputBufferLength) {
    /*
     *Parses the wpa configuration file specified in the global array wpa_supplicantConfigPath[]
     * line  and creates a formatted html string based on the contents
     */
    memset(htmlKnownNetworks, 0, outputBufferLength); //Clear output buffer 
    wifiNetwork knownNetworksList[50]; //Create a list of network structs
    int n;
    for (n = 0; n < 50; n++) //Initialise the array of structs
        initWiFiNetworkStruct(&knownNetworksList[n]);

    int ret = parseWPASupplicantConfig2(knownNetworksList, 50, wpa_supplicantConfigPath); //Parse the WPA config file
    if (ret == -1)
        printf("Error parsing file %s, or file doesn't exist\n", wpa_supplicantConfigPath);
    if (ret > 0) {
        char buffer[FIELD]; //Temporary output buffer
        stringBuilder(htmlKnownNetworks, outputBufferLength,
                "<br><form name=\"AddSSID\"  method=\"post\" action=\"AddSSID\">"
                "<fieldset>"
                "<legend>Known WiFi networks:</legend>");

        for (n = 0; n < ret; n++) { //Iterate through network list, formatting as html
            memset(buffer, 0, FIELD);
            snprintf(buffer, FIELD, "%s<br>\n", knownNetworksList[n].essid);
            stringBuilder(htmlKnownNetworks, outputBufferLength, buffer);
        }
        stringBuilder(htmlKnownNetworks, outputBufferLength, "</fieldset></form>");

    }

    return 0;
}

int reformatHTMLString(char input[], unsigned int length) {
    /*
     * A web browser will format strings entered in web page forms:-
     * spaces will become '+'
     * 
     * Other non alphanumeric characters become %xx where xx is a two digit
     * hex value representing the ascii code.
     * 
     * This function modifies the supplied string such that the original 
     * format is returned:
     * 
     * 1) Convert all '+' charcters back to spaces
     * 2)scan through string looking for '%'character
     * 3)Copy the proceeding two numeric values (which are hex) into a string
     * 4)Convert the string into a numeric value using int number = (int)strtol(hexstring, NULL, 16);
     * 5)Modify the supplied string
     *   i)Replace the location of % in the string with the ascii equivalent
     *  ii)Shift along the remainder of the string by 2 characters until end of string reached
     */

    int m;

    //Step 1) Find and replace all instances of '+' with ' '
    length = strlen(input);
    for (m = 0; m < length; m++)
        if (input[m] == '+')input[m] = ' ';

    //Step 2) Convert '%' prefaced hex numbers to their ascii equivalent characters

    for (m = 0; m < length; m++)
        if (input[m] == '%') {
            char asciiValue[3] = {0};
            asciiValue[0] = input[m + 1]; //Retrieve MSB
            asciiValue[1] = input[m + 2]; //Retrieve LSB
            int asciiHex = (int) strtol(asciiValue, NULL, 16); //Convert string to numeric val, specifying base 16
            //printf("ascii value: %d, %c\n", asciiHex, (char) asciiHex);
            input[m] = asciiHex; //Replace '%' in string with equivalent ascii value

            int n = m + 1; //Capture the start point (which is one char after the old '%')

            //            while ((input[n] != '0') && (n <= (length - 2))) { //Now delete proceeding two chars by shifting string 2 chars
            while (n <= (length - 2)) { //Now delete proceeding two chars by shifting string 2 chars
                input[n] = input[n + 2]; //to the left from this point to the end of the string
                n++;
            }
        }
}

void *twoSecSqThread(void *arg) {
    /*
     *  PThread: GEnerates 2 second toggle on global variable int twoSecSq
     */
    while (1) {
        twoSecSq = 1;
        //printf("halfSecSq: %d\n",halfSecSq);
        usleep(1000 * 1000);
        twoSecSq = 0;
        //printf("halfSecSq: %d\n",halfSecSq);
        usleep(1000 * 1000); //Gives a 2 sec period
    }
}

void *oneSecSqThread(void *arg) {
    /*
     *  PThread: GEnerates 1 second toggle on global variable int oneSecSq
     */
    while (1) {
        oneSecSq = 1;
        //printf("halfSecSq: %d\n",halfSecSq);
        usleep(500 * 1000);
        oneSecSq = 0;
        //printf("halfSecSq: %d\n",halfSecSq);
        usleep(500 * 1000); //Gives a 1 sec period
    }
}

void *halfSecSqThread(void *arg) {
    /*
     *  PThread: GEnerates half second toggle on global variable int halfSecSq
     */
    while (1) {
        halfSecSq = 1;
        usleep(250 * 1000);
        halfSecSq = 0;
        usleep(250 * 1000); //Gives a 0.5 sec period
    }
}

void *quartSecSqThread(void *arg) {
    /*
     *  PThread: GEnerates quarter second toggle on global variable int quartSecSq
     */
    while (1) {
        quartSecSq = 1;
        usleep(125 * 1000);
        quartSecSq = 0;
        usleep(125 * 1000); //Gives a 0.25 sec period
    }
}

void *monitorButtonThread(void *arg) {
    /*
     *  Pthread: Monitors the AP Enable (setup) button (GPI gpioPin).
     *  Updates global setupMode variable accordingly
     */
    //Set pin directions
    //    gpioSetMode(23, PI_OUTPUT); //GPIO 23 as output
    //    gpioSetMode(24, PI_OUTPUT); //GPIO 24 as output
    int gpioPin = *((int*) arg); //Tale local copy of arg
    free(arg); //Free up memory requested by malloc 
    printf("monitorButtonThread(): supplied pin no.: %d\n", gpioPin);

    gpioSetMode(gpioPin, PI_INPUT); //GPIO gpioPin as input
    //gpioSetMode(22, PI_INPUT); //GPIO 22 as input

    //Set internal pull-ups on GPIO gpioPin and 22
    gpioSetPullUpDown(gpioPin, PI_PUD_UP);
    //gpioSetPullUpDown(22, PI_PUD_UP);

    int buttonTimer = 0;
    int n;
    while (1) {
        if (!gpioRead(gpioPin)) {
            //If pin low, start sampling
            //pin has to be held low for 3 seconds to trigger
            buttonTimer += (20 * 1000);
            if (buttonTimer == (3 * 1000 * 1000)) { //3 seconds elapsed
                if (getSetupMode() == 0) {//Put prog into setup mode
                    printf("Entering setup mode\n");
                    printf("Configuring wlan0 in Host-AP mode\n");
                    if (setSetupMode(2) < 1) { //Try to start (preferred) AP Host mode first
                        printf("Couldn't start Host-AP setupMode\n"); //Couldn't start Host-AP mode
                        if (setSetupMode(1) < 1) { //so try the adhoc AP mode instead
                            printf("Couldn't start Adhoc-AP setupMode\n");
                        }
                    }

                } else {//Or else, take it out of setup mode
                    printf("Leaving setup mode\n");
                    //printf("Reverting wlan0 in managed mode\n");
                    if (setSetupMode(0) < 1) {
                        printf("Couldn't stop setupMode\n");
                    }
                }
                buttonTimer = 0; //Reset Timer 

            }
        } else buttonTimer = 0; //Reset Timer 
        usleep(20 * 1000); //Gives a 20 mS button sampling period
    }
}

void *statusLEDThread(void *arg) {
    /*
     * Controls the flashing of status LED based on the status of 
     * the global variable setupMode and wifiConnectedStatus
     *      Slow (1 sec period): wlan0 not associated with any network
     *      Medium (half sec): wlan0 asssociated with a network
     *      fast (quart sec): program in setup mode
     */
    int gpioPin = *((int*) arg); //Tale local copy of arg
    free(arg); //Free up memory requested by malloc 
    printf("statusLEDThread(): supplied pin no.: %d\n", gpioPin);

    //Set pin directions
    gpioSetMode(gpioPin, PI_OUTPUT); //GPIO gpioPin as output
    //gpioSetMode(24, PI_OUTPUT); //GPIO 24 as output
    while (1) {
        if (getSetupMode() > 0)
            gpioWrite(gpioPin, (HIGH & quartSecSq)); //Cause LED on GPIOgpioPin to blink fast
        else if (wifiConnectedStatus == 1)
            gpioWrite(gpioPin, (HIGH & halfSecSq));
        else
            gpioWrite(gpioPin, (HIGH & twoSecSq)); //Cause LED on GPIOgpioPin to blink slowly (like a watchdog)
        usleep(20 * 1000); //Gives a 20 mS button sampling period
    }
}

void *wiFiConnectedThread(void *arg) {
    /*
     *      sets global variable wifiConnectedStatus if valid WiFi connection on wlan0
     *      //Note doesn't test other wlan interfaces, just wlan0
     */
    //gpioSetMode(24, PI_OUTPUT); //GPIO 24 as output
    wifiNetwork nic;
    initWiFiNetworkStruct(&nic); //Init the struct

    while (1) {
        if (getWiFiConnStatus(&nic, "wlan0") == 1)
            //gpioWrite(24, HIGH); //Cause LED on GPIO24 to go high
            wifiConnectedStatus = 1;
        else
            //gpioWrite(24, LOW); //Cause LED on GPIO24 to be off
            wifiConnectedStatus = 0;
        sleep(2); //2 second delay
    }

}

int setAdhocWlanMode(int mode) {
    /*
     *      Puts the wlan0 interface into adhoc mode if mode=1, else disable access point mode.
     *      Returns 1 on success, or -1 on fail
     * 
     *      Known issues: 
     *      OSX doesn't always correctly identify the WiFi network type as (WEP). It then prompts for
     *      a WPA password, which obviously won't work. Repeated clicking on the SSID on the WiFi
     *      menu sometimes works and eventually OSX 'get's it'
     * 
     *      It parses the return string from iwconfig, to verify that the correct mode has been set
     *      (i.e it looks for the string "managed" or "Ad-Hoc")
     * 
     */

    char commandString[FIELD] = {0}; //Char array to hold command string
    char commandResponse[FIELD] = {0}; //Char array to hold command response

    //WiFi Network parameters
    //char ipAddress[] = "192.168.42.1";
    char interface[] = "wlan0";
    char ipAddress[] = "192.168.0.11";
    char subnetMask[] = "255.255.255.0";
    //char essid[] = "RPi";
    char essid[FIELD] = {0};
    char wepKey[] = "0123456789";

    if (mode == 1) {
        //construct SSID based on host and serial no. of Pi and a random element
        //To aid connection for wireless clients, the ssid will change every time

        char buffer[FIELD] = {0}; //Temp buffer to hold hostname
        getHostName(buffer, FIELD); //Get hostname
        int serialNo = getSerialNumber(); //Get serial number
        srand(time(NULL));
        int r = rand() % 100; //Get random number between 0 and 100
        snprintf(essid, FIELD, "%d%s%X", r, buffer, serialNo); //Construct SSID from random no+hostname+serialNo
        printf("setAdhocWlanMode():essid: %s\n", essid);

        char commandResponse[FIELD] = {0};
        char commandString[FIELD] = {0};

        //Kill existing wpa_supplicant process relating to wlan0
        //Get pid of wpa_supplicant related to wlan0
        snprintf(commandString, FIELD, "ps x | grep wpa_supp | grep %s | grep -v grep | awk '{print $1}' ", interface);
        //sysCmd2("ps x | grep wpa_supp | grep wlan0 | awk '{print $1}' ", commandResponse, FIELD);
        sysCmd2(commandString, commandResponse, FIELD);
        char *ptr;
        int wlan0Pid = strtol(commandResponse, &ptr, 10);

        if (wlan0Pid > 0) { //If process running
            printf("setAdhocWlanMode(): wlan0 wpa_supplicant pid. Killing process: %d\n", wlan0Pid);
            //Kill wpa_supplicant for wlan0
            memset(commandResponse, 0, FIELD);
            snprintf(commandString, FIELD, "kill -9 %d", wlan0Pid); //Create command string to kill wpa_supplicant by process id
            sysCmd2(commandString, commandResponse, FIELD); //Kill the process
        }

        //Take interface down
        memset(commandString, 0, FIELD);
        snprintf(commandString, FIELD, "ifconfig %s down", interface);
        printf("setAdhocWlanMode(): commandString: %s\n", commandString);
        sysCmd2(commandString, commandResponse, FIELD);

        sleep(2);
        //Put interface into adhoc mode
        memset(commandString, 0, FIELD);
        snprintf(commandString, FIELD, "iwconfig %s mode ad-hoc", interface);
        printf("setAdhocWlanMode(): commandString: %s\n", commandString);
        sysCmd2(commandString, commandResponse, FIELD);

        //Set WEP key
        memset(commandString, 0, FIELD);
        snprintf(commandString, FIELD, "iwconfig %s key %s", interface, wepKey);
        printf("setAdhocWlanMode(): commandString: %s\n", commandString);
        sysCmd2(commandString, commandResponse, FIELD);

        sleep(1);
        memset(commandString, 0, FIELD);
        snprintf(commandString, FIELD, "iwconfig %s channel 1 essid %s", interface, essid);
        printf("setAdhocWlanMode(): commandString: %s\n", commandString);
        sysCmd2(commandString, commandResponse, FIELD);

        sleep(1);
        //Set static ip address/mask
        memset(commandString, 0, FIELD);
        snprintf(commandString, FIELD, "ifconfig %s %s netmask %s up", interface, ipAddress, subnetMask);
        printf("setAdhocWlanMode(): commandString: %s\n", commandString);
        sysCmd2(commandString, commandResponse, FIELD);

        //Now need to check whether interface is actually in adHoc mode
        memset(commandString, 0, FIELD);
        memset(commandResponse, 0, FIELD);
        snprintf(commandString, FIELD, "iwconfig %s", interface);
        sysCmd2(commandString, commandResponse, FIELD);
        //Parse iwconfig response for the term 'Ad-Hoc'. If not present, Ad-Hoc mode has not been set
        //printf("setAdhocWlanMode: Gets here:\n");
        if (strstr(commandResponse, "Ad-Hoc") == NULL) {
            printf("Couldn't put %s into Ad-Hoc mode\n", interface);
            return -1;
        }
        strlcpy(ap_ssid, essid, FIELD); //Copy to global 
        return 1;
    } else {
        //Turn off Adhoc AP mode
        //First check to see if adhoc mode is actually set. If so, revert to Managed mode, else do nothing.
        //Now need to check whether interface is actually in adHoc mode
        memset(commandString, 0, FIELD);
        memset(commandResponse, 0, FIELD);

        //Interrogate interface
        snprintf(commandString, FIELD, "iwconfig %s", interface);
        sysCmd2(commandString, commandResponse, FIELD);

        //Parse iwconfig response for the term 'Ad-Hoc'. If not present, Ad-Hoc mode has not been set
        if (strstr(commandResponse, "Ad-Hoc") != NULL) { //'Ad-Hoc' is present in the response        
            /*
                        //Release the existing dhcp lease for that interface
                        memset(commandString, 0, FIELD);
                        snprintf(commandString, FIELD, "dhclient %s -r -v", interface);
                        printf("setAdhocWlanMode(): commandString: %s\n", commandString);
                        sysCmd2(commandString, commandResponse, FIELD); //Release old lease
             */
            //Take interface down
            memset(commandString, 0, FIELD);
            snprintf(commandString, FIELD, "ifconfig %s down", interface);
            printf("setAdhocWlanMode(): commandString: %s\n", commandString);
            sysCmd2(commandString, commandResponse, FIELD);

            sleep(2);

            //Take interface up
            snprintf(commandString, FIELD, "ifconfig %s up", interface);
            printf("setAdhocWlanMode(): commandString: %s\n", commandString);
            sysCmd2(commandString, commandResponse, FIELD);

            sleep(1);
            /*
            //Set interface to 'managed' mode
            memset(commandString, 0, FIELD);
            memset(commandResponse, 0, FIELD);
            snprintf(commandString, FIELD, "iwconfig %s mode managed", interface);
            printf("setAdhocWlanMode(): commandString: %s\n", commandString);
            sysCmd2(commandString, commandResponse, FIELD);
             */
            int attempts = 1; //Counts how many attempts it takes to successfully set wlan0 back to Managed mode
            int maxNoOfAttemptsAllowed = 10;
            do {
                //Set interface back to 'managed' mode  -This seemingly takes several goes before it takes
                memset(commandString, 0, FIELD);
                memset(commandResponse, 0, FIELD);
                snprintf(commandString, FIELD, "iwconfig %s mode managed", interface);
                printf("setAdhocWlanMode(): commandString: %s. Attempt %d of %d\n", commandString, attempts, maxNoOfAttemptsAllowed);
                sysCmd2(commandString, commandResponse, FIELD);

                //Now need to check whether interface is actually in managed mode
                memset(commandString, 0, FIELD);
                memset(commandResponse, 0, FIELD);
                snprintf(commandString, FIELD, "iwconfig %s", interface);
                sysCmd2(commandString, commandResponse, FIELD);
                //Parse iwconfig response for the term 'Mode:Managed'. If not present, Mode:Managed mode has not been set
                //printf("setAdhocWlanMode: Gets here:\n");
                if (strstr(commandResponse, "Mode:Managed") == NULL) {
                    printf("setAdhocWlanMode(): Couldn't put %s into Managed mode\n", interface);
                } else break; //Else interface mode successfully set. Break out of do-while loop
                attempts++;
                sleep(1);
            } while (attempts < (maxNoOfAttemptsAllowed + 1));

            sleep(2);
            //Now restart wpa-supplicant for wlan0
            memset(commandString, 0, FIELD);
            snprintf(commandString, FIELD, "wpa_supplicant -B -P /run/wpa_supplicant.%s.pid -i %s -D nl80211,wext -c %s", interface, interface, wpa_supplicantConfigPath);
            printf("setAdhocWlanMode(): %s\n", commandString);
            sysCmd2(commandString, commandResponse, FIELD);

            memset(ap_ssid, 0, FIELD); //Clear global ssid field
            sleep(2);
            return 1;

        } else { //Ad-hoc mode not set, so don't need to make any changes to the interface
            return 1;
        }
    }
}

int setHostAPWlanMode(int mode) {
    /*
     * Attempts to start the external hostapd package, to put the wlan0 card into 
     * access point mode using WPA
     * 
     * 1: Start AP mode
     * 0: Stop AP mode
     * 
     * This function relies upon the fact that hostapd is installed on the system and that
     * the installed wlan0 card is capable of AP mode. 
     * 
     * The hostapd binary path is specified in the global array hostapdPath[]
     * 
     * hostapd requires it's own config file.
     * Therefore this function will create a config file for hostapd on the fly:- /etc/httpConfigServer_hostapd.conf
     * 
     * It works with V2.3
     * hostapd v2.3
     * User space daemon for IEEE 802.11 AP management,
     * IEEE 802.1X/WPA/WPA2/EAP/RADIUS Authenticator
     * Copyright (c) 2002-2014, Jouni Malinen <j@w1.fi> and contributors
     * 
     */
    char interface[] = "wlan0";
    char ipAddress[] = "192.168.0.11";
    char subnetMask[] = "255.255.255.0";
    char essid[FIELD] = {0};
    char wpaKey[] = "raspberry";
    //char hostapdPath[] = "/usr/sbin/hostapd";
    //char *fileNameToWrite = "/etc/httpConfigServer_hostapd.conf";
    char *fileNameToWrite = "/tmp/httpConfigServer_hostapd.conf";
    
    //construct SSID based on host and serial no. of Pi and a random element
    char buffer[FIELD] = {0}; //Temp buffer to hold hostname
    getHostName(buffer, FIELD); //Get hostname
    int serialNo = getSerialNumber(); //Get serial number
    snprintf(essid, FIELD, "%s%X", buffer, serialNo); //Construct SSID from random no+hostname+serialNo
    printf("APHostWlanMode():essid: %s, wpaKey: %s\n", essid,wpaKey);

    char commandResponse[FIELD] = {0};
    char commandString[FIELD] = {0};

    char hostapdConfigFile[SECTION] = {0};
    snprintf(hostapdConfigFile, FIELD,
            "#hostapd config file auto generated by httpConfigServer. To suit hostapd V2.3"
            "\n"
            "\n"
            "# This is the name of the WiFi interface we configured above\n"
            "interface=wlan0\n"
            "\n"
            "# Use the nl80211 driver with the brcmfmac driver\n"
            "driver=nl80211\n"
            "\n"
            "# This is the name of the network\n"
            "ssid=%s\n"
            "\n"
            "# Use the 2.4GHz band\n"
            "hw_mode=g\n"
            "\n"
            "# Use channel 6\n"
            "channel=6\n"
            "\n"
            "# Enable 802.11n\n"
            "ieee80211n=1\n"
            "\n"
            "# Enable WMM\n"
            "wmm_enabled=1\n"
            "\n"
            "# Enable 40MHz channels with 20ns guard interval\n"
            "ht_capab=[HT40][SHORT-GI-20][DSSS_CCK-40]\n"
            "\n"
            "# Accept all MAC addresses\n"
            "macaddr_acl=0\n"
            "\n"
            "# Use WPA authentication\n"
            "auth_algs=1\n"
            "\n"
            "# Require clients to know the network name\n"
            "ignore_broadcast_ssid=0\n"
            "\n"
            "# Use WPA2\n"
            "wpa=2\n"
            "\n"
            "# Use a pre-shared key\n"
            "wpa_key_mgmt=WPA-PSK\n"
            "\n"
            "# The network passphrase\n"
            "wpa_passphrase=%s\n"
            "\n"
            "# Use AES, instead of TKIP\n"
            "rsn_pairwise=CCMP\n", essid, wpaKey);

    if (mode == 1) { //Requested mode = 1
        //Create a config file for hostapd
        FILE *fp; //Create pointer to a file 
        fp = fopen(fileNameToWrite, "w+"); //Open file for reading and writing. Truncate to zero first
        if (fp == NULL) {
            perror("setHostAPWlanMode().fopen(): (file can't be re-written). Error creating file");
            printf("setHostAPWlanMode().fopen(): Couldn't write %s\n",fileNameToWrite);
            return -1;
        }
        printf("Writing %s\n",fileNameToWrite);
        fprintf(fp, hostapdConfigFile); //Write string to disk
        fclose(fp); //Close file

        //Kill existing wpa_supplicant process relating to wlan0
        //Get pid of wpa_supplicant related to wlan0
        snprintf(commandString, FIELD, "ps x | grep wpa_supp | grep %s | grep -v grep | awk '{print $1}' ", interface);
        //sysCmd2("ps x | grep wpa_supp | grep wlan0 | awk '{print $1}' ", commandResponse, FIELD);
        sysCmd2(commandString, commandResponse, FIELD);
        char *ptr;
        int wlan0Pid = strtol(commandResponse, &ptr, 10);

        if (wlan0Pid > 0) { //Is process actually running?
            //Kill wpa_supplicant for wlan0
            printf("setHostAPWlanMode(): wlan0 wpa_supplicant pid. Killing process: %d\n", wlan0Pid);
            memset(commandResponse, 0, FIELD);
            snprintf(commandString, FIELD, "kill -9 %d", wlan0Pid); //Create command string to kill wpa_supplicant by process id
            sysCmd2(commandString, commandResponse, FIELD); //Kill the process
        }

        //Take interface down
        memset(commandString, 0, FIELD);
        snprintf(commandString, FIELD, "ifconfig %s down", interface);
        printf("setHostAPWlanMode(): commandString: %s\n", commandString);
        sysCmd2(commandString, commandResponse, FIELD);

        sleep(1);
        //Set static ip address/mask
        memset(commandString, 0, FIELD);
        snprintf(commandString, FIELD, "ifconfig %s %s netmask %s up", interface, ipAddress, subnetMask);
        printf("setHostAPWlanMode(): commandString: %s\n", commandString);
        sysCmd2(commandString, commandResponse, FIELD);

        //start hostapd
        memset(commandString, 0, FIELD);
        sysCmd2("killall -9 hostapd", commandResponse, FIELD); //Kill all existing instances that might be running
        snprintf(commandString, FIELD, "%s %s &", hostapdPath, fileNameToWrite);
        printf("setHostAPWlanMode(): commandString: %s\n", commandString);
        system(commandString);
        sleep(1);
        //check hostapd is actually running 
        //Get pid of hostapd
        memset(commandString, 0, FIELD);
        memset(commandResponse, 0, FIELD);
        snprintf(commandString, FIELD, "ps x | grep hostapd | grep -v grep | awk '{print $1}'");
        sysCmd2(commandString, commandResponse, FIELD);

        int hostapdPid = 0;
        hostapdPid = strtol(commandResponse, &ptr, 10); //Convert returned string to an int

        if (hostapdPid > 0) {
            printf("setHostAPWlanMode(): hostapd pid.: %d\n", hostapdPid);
            strlcpy(ap_ssid, essid, FIELD); //Copy to global
            return 1;
        } else {
            printf("setHostAPWlanMode(): Couldn't start hostapd\n");
            return -1;
        }

    } else { //Requested mode 0 disable hostAP mode

        //Check to see if hostapd is running (by grepping ps x)
        memset(commandString, 0, FIELD);
        memset(commandResponse, 0, FIELD);

        snprintf(commandString, FIELD, "ps x | grep hostapd | grep -v grep | awk '{print $1}' &");
        sysCmd2(commandString, commandResponse, FIELD);
        char *ptr;
        int hostapdPid = 0;
        hostapdPid = strtol(commandResponse, &ptr, 10); //Convert returned string to an int
        if (hostapdPid > 0) {
            //hostapd running, so kill it
            printf("setHostAPWlanMode(): hostapd pid. process to be killed: %d\n", hostapdPid);
            sysCmd2("killall -9 hostapd", commandResponse, FIELD);
            sleep(1);

            int attempts = 1; //Counts how many attempts it takes to successfully set wlan0 back to Managed mode
            int maxNoOfAttemptsAllowed = 10;
            do {
                //Set interface back to 'managed' mode  -This seemingly takes several goes before it takes
                memset(commandString, 0, FIELD);
                memset(commandResponse, 0, FIELD);
                snprintf(commandString, FIELD, "iwconfig %s mode managed", interface);
                printf("setHostAPWlanMode(): commandString: %s. Attempt %d of %d\n", commandString, attempts, maxNoOfAttemptsAllowed);
                sysCmd2(commandString, commandResponse, FIELD);

                //Now need to check whether interface is actually in managed mode
                memset(commandString, 0, FIELD);
                memset(commandResponse, 0, FIELD);
                snprintf(commandString, FIELD, "iwconfig %s", interface);
                sysCmd2(commandString, commandResponse, FIELD);
                //Parse iwconfig response for the term 'Mode:Managed'. If not present, Mode:Managed mode has not been set
                //printf("setAdhocWlanMode: Gets here:\n");
                if (strstr(commandResponse, "Mode:Managed") == NULL) {
                    printf("setHostAPWlanMode(): Couldn't put %s into Managed mode\n", interface);
                } else break; //Else interface mode successfully set. Break out of do-while loop
                attempts++;
                sleep(1);
            } while (attempts < (maxNoOfAttemptsAllowed + 1));

            sleep(2);
            //Now restart wpa-supplicant for wlan0
            memset(commandString, 0, FIELD);
            snprintf(commandString, FIELD, "wpa_supplicant -B -P /run/wpa_supplicant.%s.pid -i %s -D nl80211,wext -c %s", interface, interface, wpa_supplicantConfigPath);
            printf("setHostAPWlanMode(): %s\n", commandString);
            sysCmd2(commandString, commandResponse, FIELD);
            sleep(2);
            memset(ap_ssid, 0, FIELD); //Clear global ssid field
            return 1;
        } else { //If it's not running, do nothing
            printf("setHostAPWlanMode(): hostapd not currently running. Ignoring request\n");
            return 1;
        }

    }

}

int getSetupMode() {

    /*
     * Retrieves the status of the global variable setupMode
     */
    return setupMode;
}

int getUnsavedChangesFlag(){
    /*
     *      Returns the status of the global var unsavedChangesFlag.
     */
    return unsavedChangesFlag;
}

void setUnsavedChangesFlag(int var){
    /**
     * Sets the global var unsavedChanges.
     * @param var
     */
    if (var>0) unsavedChangesFlag=1;
}

int getHTTPListeningPort() {
    /*
     *  Retrieves the current http listening port
     */
    return httpListeningPort;
}

void getAPssid(char output[], unsigned int outputLength) {
    /*
     * Retrieves the current ap_ssid (relevent if running in AP mode)
     * and populates the supplied array.
     */

    strlcpy(output, ap_ssid, outputLength);
}

int restartWPASupplicant() {
    /**
     * 
     * Kills all existing instances of the wpa supplicant and restarts wpa_supplicant for wlan0 
     * (and also wlan1 if it exists).
     * 
     * 
     * 
     */
    if (getSetupMode() == 0) { //Not  in setup mode so restart wpa_supplicant for wlan0 (and wlan1 if installed)
        char commandResponse[FIELD] = {0};
        char commandString[FIELD] = {0};
        printf("restartWPASupplicant(): Killing all running wpa_supplicant processes\n");
        sysCmd2("killall -9 wpa_supplicant", commandResponse, FIELD); //kill all running wpa_supplicant processes
        sleep(1);
        //Now force wlan0 into Managed mode (it might be stuck in 'adhoc' or 'master' mode from an
        //aborted setup mode session)
        sysCmd2("iwconfig wlan0 mode managed", commandResponse, FIELD);

        //Restart wlan0 wpa_supplicant
        printf("restartWPASupplicant(): Restarting wpa_supplicant for wlan0\n");
        snprintf(commandString, FIELD, "wpa_supplicant -B -P /run/wpa_supplicant.wlan0.pid -i wlan0 -D nl80211,wext -c %s", wpa_supplicantConfigPath);
        printf("restartWPASupplicant():commandString: %s\n", commandString);
        sysCmd2(commandString, commandResponse, FIELD);

        //Restart wlan1 wpa_supplicant (if wlan1 installed)
        if (isWlan1Present() == 1) {
            printf("restartWPASupplicant(): wlan1 present. Restarting wpa_supplicant for wlan1\n");
            memset(commandString, 0, FIELD);
            snprintf(commandString, FIELD, "wpa_supplicant -B -P /run/wpa_supplicant.wlan1.pid -i wlan1 -D nl80211,wext -c %s", wpa_supplicantConfigPath);
            printf("restartWPASupplicant():commandString: %s\n", commandString);
            sysCmd2(commandString, commandResponse, FIELD);
        }

    } else { //Must be in setup mode. Therefore wlan0 is busy so only want to restart wlan1
        if (isWlan1Present() == 1) {
            char commandResponse[FIELD] = {0};
            char commandString[FIELD] = {0};
            //Get pid of wpa_supplicant related to wlan1
            sysCmd2("ps x | grep wpa_supp | grep wlan1 | grep -v grep | awk '{print $1}' ", commandResponse, FIELD);
            char *ptr;
            int wlan1Pid = strtol(commandResponse, &ptr, 10);
            if (wlan1Pid > 0) { //Check process is actually running (before we try to kill it)
                printf("restartWPASupplicant(): Current wlan1 wpa_supplicant pid: %d\n", wlan1Pid);
                snprintf(commandString, FIELD, "kill -9 %d", wlan1Pid); //Create command string to kill wpa_supplicant by process id
                printf("restartWPASupplicant(): commandString: %s\n", commandString);
                sysCmd2(commandString, commandResponse, FIELD); //Kill wpa_supplicant   
            }

            //Now restart wpa_supplicant
            memset(commandString, 0, FIELD);
            snprintf(commandString, FIELD, "wpa_supplicant -B -P /run/wpa_supplicant.wlan1.pid -i wlan1 -D nl80211,wext -c %s", wpa_supplicantConfigPath);
            printf("restartWPASupplicant(): commandString: %s\n", commandString);
            sysCmd2(commandString, commandResponse, FIELD);

            //Get new pid of wpa_supplicant
            memset(commandResponse, 0, FIELD);
            sysCmd2("ps x | grep wpa_supp | grep wlan1 | awk '{print $1}' ", commandResponse, FIELD);
            wlan1Pid = strtol(commandResponse, &ptr, 10);
            printf("restartWPASupplicant(): New wlan1 wpa_supplicant pid: %d\n", wlan1Pid);
        }

    }
}

int getSerialNumber() {
    /*
     * Retrieves the serial number string from /cat/proc
     * Note: serial number on the Pi is expressed as a Hex number
     * Sample code:-
     *          int serialNo=getSerialNumber();
     *          printf("Serial number: %X\n",serialNo);
     */
    char output[FIELD];
    memset(output, 0, FIELD);
    //sysCmd2("grep -Po '^Serial\\s*:\\s*\\K[[:xdigit:]]{16}' /proc/cpuinfo", output, FIELD);
    sysCmd2("cat /proc/cpuinfo | grep Serial | cut -d ':' -f 2", output, FIELD);
    char *ptr;
    unsigned int serialNumber = (int) strtol(output, &ptr, 16); //Serial number is written in 
    //printf("getSerialNumber(): raw: %s, integer:%d, as Hex: %x\n", output, serialNumber, serialNumber);
    return serialNumber;
}

int getHostName(char output[], int outputLength) {
    memset(output, 0, outputLength);
    int ret = sysCmd2("hostname", output, FIELD); //Run 'hostname' in terminal, Copy response to output[]]
    output[ret - 1] = 0; //Removes a wierd carriage return character appended by 'hostname'

    return ret - 1; //Return length of returned name (not including null char)                      
}

/*
void *dhcpServerThread(void *arg) {

    startDHCPServer(); //Start dhcp server. 
}
 */
int renewDHCPLeases() {
    /*
     * Checks the status of setupmode.
     * If setupMode=0, executes dhcclient for all interfaces
     * If setupMode=1, only renews for eth0 and wlan1 (if wlan1 is installed)
     * 
     * Checks to see which dhclient is installed on the system
     * 
     * Returns -1 if it can't find any known client.
     */

    //First establish which dhcp client is installed (Raspian uses dhclient, PiCore uses udhcpc)
    char commandResponse[FIELD] = {0};
    char commandString[FIELD] = {0};
    //Define enum to hold the two (known) options of possible dhcp client, plus an 'unknown'

    /*

    snprintf(commandString, FIELD, "dhclient -?");
    sysCmd2(commandString, commandResponse, FIELD); //Execute command
    printf("renewDHCPLeases(): %s\n",commandResponse);
    exit(1);
    if (strstr(commandResponse, "not found") == NULL) { //i.e error not found so we can infer dhclient exists
        printf("renewDHCPLeases(): Using dhclient\n");
        installedDHCPClient = dhclient; //set flag
    } else {
        printf("renewDHCPLeases(): dhclient not found. Trying udhcpc instead.\n");
        memset(commandResponse, 0, FIELD);
        memset(commandString, 0, FIELD);
        snprintf(commandString, FIELD, "udhcpc -?");
        sleep(1);
        sysCmd2(commandString, commandResponse, FIELD); //Execute command
        printf("renewDHCPLeases(): %s\n",commandResponse);
        if (strstr(commandResponse, "not found") == NULL) { //i.e error not found so we can infer dhclient exists
            printf("renewDHCPLeases(): Using udhcpc\n");
            installedDHCPClient = udhcpc;
        } else {
            printf("renewDHCPLeases(): udhcpc not found either. No known valid dhcp client.\n");
            installedDHCPClient = nodhcpclient; //set flag
            return -1;
        }

    }
     */
    //Now we know which version of dhcp client is installed, we can run the appropriate command

    if (getSetupMode() == 0) { //In normal mode, so renew all interfaces
        printf("renewDHCPLeases(): SetupMode=0, renewing ALL interfaces\n");
        char commandResponse[FIELD] = {0};

        //Use built in system command, otherwise we're left waiting (no backgrounding with my sysCmd2() function)
        if (installedDHCPClient == dhclient) {
            printf("renewDHCPLeases(): Using dhclient\n");
            system("dhclient -v &"); //Request new lease, in theory, for all interfaces
            system("dhclient wlan0 -r -v &"); //Release wlan0 (seem to have to be explicit here,
            system("dhclient wlan0 -v &"); //Request new lease (seem to have to be explicit here,
            //otherwise wlan0 gets left off the list)
        } else if (installedDHCPClient == udhcpc) {
            printf("renewDHCPLeases(): Using udhcpc\n");
            system("udhcpc -b"); //Request new lease, in theory, for all interfaces
            //system("udhcpc -b -i wlan0 -R"); //Release wlan0 (seem to have to be explicit here,
            system("udhcpc -b -i wlan0"); //Request new lease (seem to have to be explicit here,
            //otherwise wlan0 gets left off the list)  
        }
    } else {
        // In setup mode, so only renew eth0 (and wlan1, if it exists)
        printf("renewDHCPLeases(): SetupMode=1, renewing dhcp for eth0\n");
        if (installedDHCPClient == dhclient) {
            printf("renewDHCPLeases(): Using dhclient\n");
            system("dhclient eth0 -v &"); //Request new lease
            if (isWlan1Present() == 1) {
                printf("renewDHCPLeases(): SetupMode=1, renewing dhcp for wlan1\n");
                system("dhclient wlan1 -v &");
            }
        } else if (installedDHCPClient == udhcpc) {
            printf("renewDHCPLeases(): Using udhcpc\n");
            system("udhcpc -i eth0"); //Request new lease

            if (isWlan1Present() == 1) {
                printf("renewDHCPLeases(): SetupMode=1, renewing dhcp for wlan1\n");
                system("udhcpc -i");
            }
        }
    }
    return 1;
}

int setSetupMode(int mode) {
    /*
     * if mode=2, sets the global state variable setupMode to '2' and starts hostapd Access Point mode
     * if mode=1, sets the global state variable setupMode to '1' and starts Adhoc Access Point mode
     * if mode=0, disables Access Point mode
     * 
     * 
     * This is the only function that should modify the global setupMode;
     * 
     * Returns '1' on a successful change, else -1
     *      
     */
    if (getSetupMode() != mode) { //Check to see what mode we're already in
        if (mode == 1) { //Activate adhoc wlan setup mode
            printf("setSetupMode():Attempting to start Wifi Access point - Adhoc mode\n");
            int ret = setAdhocWlanMode(1);
            if (ret>-1) {
                printf("Setting setupMode to '1'\n");
                setupMode = 1;
                //Now start dhcp server thread
                /*
                pthread_t _dhcpServerThread;
                if (pthread_create(&_dhcpServerThread, NULL, dhcpServerThread, NULL)) {
                    printf("Error creating dhcp server thread.\n");
                    return -1;
                }
                 */
                if (startDHCPServer() == -1) return -1; //Start DHCP server
                return 1;
            } else return -1;
        } else if (mode == 2) { //Activate hostapd Access Point mode
            printf("setSetupMode():Attempting to start Wifi Access point - HostAP mode\n");
            int ret = setHostAPWlanMode(1);
            if (ret>-1) {
                printf("Setting setupMode to '2'\n");
                setupMode = 2;
                //Now start dhcp server thread
                /*
                pthread_t _dhcpServerThread;
                if (pthread_create(&_dhcpServerThread, NULL, dhcpServerThread, NULL)) {
                    printf("Error creating dhcp server thread.\n");
                    return -1;
                }
                 */
                if (startDHCPServer() == -1) return -1; //Start DHCP server
                return 1;
            } else return -1;

        } else { //Deactivate setup mode
            printf("setSetupMode():Attempting to stop Wlan AP mode\n");
            stopDHCPServer(); //Stop DHCP server
            while (getDhcpServerRunningStatus() == 1) {
                printf("setSetupMode():Waiting for DHCPServer to stop..\n");
                sleep(1);
            }

            if (getSetupMode() == 2) { //Are we in APHost AP mode?
                if (setHostAPWlanMode(0)>-1) { //Signal APHost mode to stop
                    printf("setSetupMode(): Successfully stopped APHost AP mode, renewing leases\n");
                    setupMode = 0;
                    renewDHCPLeases();
                    return 1;
                } else {
                    printf("setSetupMode(): Could not exit APHost AP mode");
                    return -1;
                }
            }
            if (getSetupMode() == 1) { //Are we in Adhoc AP mode?
                if (setAdhocWlanMode(0)>-1) {
                    printf("setSetupMode(): Successfully stopped Ad-Hoc AP mode, renewing leases\n");
                    setupMode = 0;
                    renewDHCPLeases();
                    return 1;
                } else {
                    printf("setSetupMode(): Could not exit Ad-Hoc AP mode");
                    return -1;
                }
            }
        }
    } else {
        int ret = getSetupMode();
        printf("setSetupMode(). Mode %d already set, ignoring request\n", ret);
        return 1;
    }
}

int isWlan1Present() {
    /*
     *  This function determines whether wlan1 interface is installed.
     * Returns '1' if it is, or '0' if not detected
     */
    char commandResponse[FIELD] = {0};
    char interfaceList[20][2][20]; //Can store 20 addresses
    int noOfInterfaces = listAllInterfaces(interfaceList, 20); //Display all local ethernet interfaces
    int l;
    for (l = 0; l < noOfInterfaces; l++) {
        //Iterate through all the interface names to see if we have one called 'wlan1'
        if (strcmp("wlan1", interfaceList[l][0]) == 0) {
            //If strcmp returns '0', match found
            printf("isWlan1Present():Interface wlan1 installed\n");
            return 1;
            //l = noOfInterfaces; //Force for() loop to terminate
        }
    }
    printf("isWlan1Present(): wlan1 not present\n");
    return 0;


}

int extractString(char input[], char preambleToFind[], char startDelimit[], char endDelimit[], char output[], int outputLength) {
    /*
     * Searches input[] for the string 'preambleToFind'. It then uses this as a start port to extract a string
     * delimited by startDeleimit[] and the first char of endDelimit[]. The string between those delimiters is copied into output[]
     * 
     * Returns the length of the extracted string or -1 if the string not found
     */
    char *startPos, *endPos;
    unsigned int length = 0;
    unsigned int inputLength = strlen(input); //Get length of input string
    startPos = strstr(input, preambleToFind); //Get start of block
    if (startPos == NULL) return -1;
    startPos = strstr(startPos, startDelimit); //Detect start delimiter
    if (startPos == NULL) return -1;
    startPos += strlen(startDelimit); //Shunt along to one character after the end of the delimiter string (i.e the actual start of the substring)

    unsigned int arrayPosition = startPos - input; //Convert strstr() mem pointer to an array position)
    while ((input[arrayPosition] != endDelimit[0])&&(arrayPosition < outputLength) && (arrayPosition < inputLength)) { //Scroll through string to detect end delimit
        //printf("Value at %u:%c\n", arrayPosition, input[arrayPosition]);
        arrayPosition++;
        length++;
    }
    //printf("startPos: %p\n", startPos);
    //printf("length: %u\n", length);
    //printf("outputLength: %u\n",outputLength);
    if ((length < outputLength) && (length > 0)) { //Only copy in contents if not too large for output buffer
        strlcpy(output, startPos, length + 1); //Copy passphrase into array (length+1 needed to allow for terminating '\0')
        //printf("extractString():output: %s\n", output);
        return (length + 1); //Return length (including null char)
    } else return -1;
}

void *simpleHTTPServerThread(void *arg) {

    signal(SIGPIPE, SIG_IGN); //NOTE THIS LINE IS ESSENTIAL TO STOP THE SERVER CRASHING IF THE REMOTE CLIENT
    //UNEXPECTEDLY CLOSES THE TCP CONNECTION. IN THIS CASE write() will fail 
    //(because the socket is no longer valid) and the prog will crash    

    int forceRedirect = 0; //If you submit from the web page via POST, if the user does a page refresh
    //it will try and resubmit the values. Annoying. Solution: Send an intermediate page which forces the
    //browser back to the home page. This will clear the browser cache

    int scheduleEnterSetupMode = 0; //Required because of the issue above: You have to issue the redirect 
    int scheduleExitSetupMode = 0; //BEFORE you meddle with the WiFi adapter, because in doing so
    //You'll typically break the http connection (therefore the browser POST
    //cache won't be flushed BEFORE the connection breaks).
    //SOLUTION: Capture the request to start/stop (by setting the
    //relevent Enter/Exit flag, issue an http redirection and only then
    //put the wlan adapter into ad-hoc (access point) mode).
    //If you then do a subsequent refresh of the web page, it won't
    //resubmit the stop/start command


    int portNo = *((int*) arg); //Tale local copy of arg
    free(arg); //Free up memory requested by malloc in pulseGPO()
    printf("simpleHTTPServer() supplied portNo: %d\n", portNo);


    //char htmlOutput[2048] = {0}; //Holds formatted output strings
    char *htmlHeader = "HTTP/1.1 200 OK\n"
            "Content-Type: text/html\r\n\n"
            "<html><body><H1>Pi Config</H1><br>\0";

    char *htmlForm = "<FORM>"
            "<INPUT TYPE=\"button\" VALUE=\"Clickable link button\" onClick=\"parent.location='http://xxxxxx:8042'\">"
            "</FORM>"
            "<form name=\"input\"  method=\"post\">\n"
            "Type Something: <input type=\"text\" name=\"myField1\">\n"
            "<input type=\"submit\" value=\"Submit\">\n\0"
            "</form>"
            "<br>";


    char *htmlHeaderbuttons = "<form method=\"POST\" />"
            "<input type=\"submit\" value=\"Restart WPA Supplicant\" name=\"button\" />"
            "<input type=\"submit\" value=\"WiFi Scan\" name=\"button\" />"
            "<input type=\"submit\" value=\"Renew DHCP lease\" name=\"button\" />"
            "<input type=\"submit\" value=\"Start Adhoc mode on wlan0\" name=\"button\" />"
            "<input type=\"submit\" value=\"Start APHost mode on wlan0\" name=\"button\" />"
            "<input type=\"submit\" value=\"Exit Adhoc or APHost Mode\" name=\"button\" />"
            "<input type=\"submit\" value=\"Backup\" name=\"button\" />"
            "<input type=\"submit\" value=\"Reboot\" name=\"button\" />"
            "</form>";


    char htmlStatus[SECTION] = {0}; //General status information

    char htmlNetworksFound[SECTION] = {0};

    char htmlKnownNetworks[SECTION] = {0};

    char htmlSetNetworkCard[SECTION] = {0};

    char *htmlAddSSIDField = "<br><form name=\"AddSSID\"  method=\"post\" action=\"AddSSID\">"
            "<fieldset>"
            "<legend>Connect to network:</legend>"
            "SSID:<br>"
            "<input type=\"text\" name=\"addSSID\" value=\"ssid\"><br>"
            "Pasphrase:<br>"
            "<input type=\"text\" name=\"passPhrase\" value=\"passphrase\"><br><br>"
            "<input type=\"submit\" value=\"Add/Modify wpa supplicant config\">"
            "</fieldset>"
            "</form>";

    char *htmlRemoveSSIDField = "<br><form name=\"RemoveSSID\"  method=\"post\" action=\"removeSSID\">"
            "<fieldset>"
            "<legend>Remove network:</legend>"
            "SSID:<br>"
            "<input type=\"text\" name=\"removeSSID\" value=\"ssid\"><br>"
            "<input type=\"submit\" value=\"Modify wpa supplicant\">"
            "</fieldset>"
            "</form>";

    char *htmlSetInterface = "<br><form name=\"SetInterface (not permanent)\"  method=\"post\" action=\"setInterface\">"
            "<fieldset>"
            "<legend>Set Interface:</legend>"
            "Interface"
            "<input type=\"text\" name=\"interface\" value=\"eg. wlan0\">"
            "   Address"
            "<input type=\"text\" name=\"address\" value=\"eg. 192.168.3.6\">"
            "   Mask"
            "<input type=\"text\" name=\"mask\" value=\"eg. 255.255.255.0\">"
            "   Gateway (optional)"
            "<input type=\"text\" name=\"Gateway\" value=\"eg. 192.168.3.1\">"
            "<br>"
            "<input type=\"submit\" value=\"Apply\">"
            "</fieldset>"
            "</form>";

    char *htmlFooter = "<br></body></html>\n\0";

    char timeAsString[FIELD] = {0}; //Buffer to hold human readable time

    //printf("%s\n", htmlHeader);
    //Create TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0); //Specify 'Reliable'
    if (sockfd < 0) {
        perror("socket()");
    }
    //Set socket options so that we can reuse the socket address/port
    //That way we won't be inhibited by the OS's TIME_WAIT state (and we can always bind to the supplied port)
    //Code from here: http://stackoverflow.com/questions/24194961/how-do-i-use-setsockoptso-reuseaddr
    //you can check the current port state using netstat -a | grep 20000 (where 20000 is the port you're interested in)

    int reuse = 0; //If reuse set to 0, the followling lines will have no effect
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*) &reuse, sizeof (reuse)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (const char*) &reuse, sizeof (reuse)) < 0)
        perror("setsockopt(SO_REUSEPORT) failed");
    ////////////

    /* Initialize socket structure */
    struct sockaddr_in serv_addr, cli_addr;

    memset((char *) &serv_addr, 0, sizeof (serv_addr)); //Clear memory beforehand
    //portno = 5001;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portNo);

    /* Now bind the host address using bind() call.*/
    int bindRet = 0;
    do {
        bindRet = bind(sockfd, (struct sockaddr *) &serv_addr, sizeof (serv_addr));
        if (bindRet < 0) {
            perror("ERROR on binding"); //If unable to bind to supplied port, try the next one
            printf("Can't bind to port %d, trying %d instead.\n", portNo, portNo + 1);
            portNo++; //Increment porNo
            serv_addr.sin_port = htons(portNo);
            sleep(1); //Delay to stop it getting out of hand
        }
    } while (bindRet < 0);
    httpListeningPort = portNo; //Update global listening port variable
    int clilen;
    char buffer[FIELD];

    while (1) {
        /* Now start listening for the clients, here process will
         * go in sleep mode and will wait for the incoming connection
         */
        printf("webServerThread():Waiting for data...\n");

        listen(sockfd, 5); //This is a blocking call (5 is the backlog)
        clilen = sizeof (cli_addr);

        /* Accept actual connection from the client */
        int newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        printf("simpleHTTPServerThread:accept(): newsockfd: %d\n", newsockfd);
        if (newsockfd < 0) {
            perror("ERROR on accept");
        }
        /* If connection is established then start communicating */
        memset(buffer, 0, FIELD);
        int n = read(newsockfd, buffer, FIELD);

        if (n < 0) {
            perror("ERROR reading from socket");
        }
        //printf("Incoming message: %s\n", buffer);
        //int bufferLength = strlen(buffer);
        //printf("bufferLength: %d\n", bufferLength);
        /*
        int l;
        for (l = 0; l < (bufferLength + 3); l++) printf("%c,%d:", buffer[l], (int) buffer[l]);
        printf("\n");
         */

        //Now test incoming message 
        char *startPos, *endPos;
        unsigned int length = 0; //Length of substring to be extracted

        //Detect HTML button presses
        startPos = strstr(buffer, "button=Restart+WPA+Supplicant");
        if (startPos != NULL) {
            printf("\x1B[31mbutton=Restart+WPA+Supplicant\x1B[0m\n");
            restartWPASupplicant();
            forceRedirect = 1;
        }


        startPos = strstr(buffer, "button=WiFi+Scan");
        if (startPos != NULL) {
            printf("\x1B[31mbutton=WiFi+Scan\x1B[0m\n");
            scanForNetworks(htmlNetworksFound, SECTION); //Scan for networks, update appropriate html section
            forceRedirect = 1; //Force redirection to clear POST data on next web refresh
        }
        startPos = strstr(buffer, "button=Renew+DHCP+lease");
        if (startPos != NULL) {
            printf("\x1B[31mbutton=Renew+DHCP+lease\x1B[0m\n");
            /*
                        printf("Re-requesting DHCP leases for all interfaces\n");
                        int removedGateways = removeAllGateways(); //Remove gateways in advance of the dhcp lease renewal
                        printf("%d gateway(s) removed from routing table. Re-requesting DHCP leases\n", removedGateways);
                        char output[FIELD] = {0};
                        sysCmd2("dhclient -v &", output, FIELD);
                        printf("dhclient response: %s\n", output);
             */
            renewDHCPLeases();
            sleep(2);
            forceRedirect = 1; //Force redirection to clear POST data on next web refresh
        }

        startPos = strstr(buffer, "button=Start+Adhoc+mode+on+wlan0");
        if (startPos != NULL) {
            printf("\x1B[31mbutton=Start+Adhoc+mode+on+wlan0\x1B[0m\n");

            printf("Entering setup mode (via http website request)\n");
            if (getSetupMode() == 0) { //Only act if NOT already in setup mode
                scheduleEnterSetupMode = 1; //Set flag 
            } else
                printf("...Button ignored. Already in setupMode \n");
            forceRedirect = 1; //Force redirection to clear POST data on next web refresh

        }

        startPos = strstr(buffer, "button=Start+APHost+mode+on+wlan0");
        if (startPos != NULL) {
            printf("\x1B[31mbutton=Start+APHost+mode+on+wlan0\x1B[0m\n");

            printf("Entering setup mode (via http website request)\n");
            if (getSetupMode() == 0) { //Only act if NOT already in setup mode
                scheduleEnterSetupMode = 2; //Set flag 
            } else
                printf("...Button ignored. Already in setupMode\n");
            forceRedirect = 1; //Force redirection to clear POST data on next web refresh

        }

        startPos = strstr(buffer, "button=Exit+Adhoc+or+APHost+Mode");
        if (startPos != NULL) {
            printf("\x1B[31mbutton=Exit+Adhoc+or+APHost+Mode\x1B[0m\n");

            printf("Leaving setup mode (via http website request)\n");
            if (getSetupMode() > 0) { //Only act if already in setup mode
                scheduleExitSetupMode = 1;
            } else
                printf("...Button ignored as setupMode not currently active\n");
            forceRedirect = 1; //Force redirection to clear POST data on next web refresh
        }

        startPos = strstr(buffer, "button=Backup");
        if (startPos != NULL) {
            printf("\x1B[31mbutton=Backup\x1B[0m\n");
            char output[FIELD] = {0};
            printf("Initiating backup\n");
            sysCmd2("filetool.sh -b", output, FIELD);       //This is for TinyCore. Won't work on Raspian
            forceRedirect = 1; //Force redirection to clear POST data on next web refresh
        }
        startPos = strstr(buffer, "button=Reboot");
        if (startPos != NULL) {
            printf("\x1B[31mbutton=Reboot\x1B[0m\n");
            char output[FIELD] = {0};
            printf("Rebooting now\n");
            sysCmd2("reboot", output, FIELD);
            forceRedirect = 1; //Force redirection to clear POST data on next web refresh
        }

        ///////New code
        if (strstr(buffer, "POST /AddSSID") != NULL) {
            printf("\x1B[31mPOST /AddSSID\x1B[0m\n");
            char ssid[FIELD] = {0};
            char passPhrase[FIELD] = {0};
            if (extractString(buffer, "POST /AddSSID", "addSSID=", "&", ssid, FIELD) > 0) { //Does field contain any data?
                if (extractString(buffer, "POST /AddSSID", "&passPhrase=", "\0", passPhrase, FIELD) > 0) { //Does field contain any data?
                    //ssid and passPhrase html fields have data.
                    reformatHTMLString(ssid, strlen((ssid))); //Remoce http encoding
                    reformatHTMLString(passPhrase, strlen((passPhrase))); //Remoce http encoding
                    printf("After: SSID: %s, Passphrase: %s\n", ssid, passPhrase);
                    
                    //Now modify WPA config file
                    //First enable read-write mode on the filesystem
                    system("sudo rw");   
                    int ret = createWPASupplicantConfig(ssid, passPhrase, wpa_supplicantConfigPath);
                    if (ret == -1)
                        printf("Couldn't modify file: %s\n", wpa_supplicantConfigPath);
                    
                    //Put filesystem back into readonly mode
                    system("sudo ro"); 
                    forceRedirect = 1; //Force redirection to clear POST data on next web refresh
                }
            }
        }

        ///////New code ends

        //////////New code begins
        if (strstr(buffer, "POST /removeSSID") != NULL) {
            printf("\x1B[31mPOST /removeSSID\x1B[0m\n");
            char ssid[FIELD] = {0};
            if (extractString(buffer, "POST /removeSSID", "removeSSID=", "\0", ssid, FIELD) > 0) { //Does field contain any data?
                //ssid and passPhrase html fields have data.
                reformatHTMLString(ssid, strlen((ssid))); //Remoce http encoding
                printf("removeSSID= SSID: %s\n", ssid);
                //Now remove SSID (and associated network block) from WPA config file
                //First put Raspian filesystem into RW mode
                system("rw");
                int ret = deleteESSIDfromConfigFileByName(wpa_supplicantConfigPath, ssid);
                if (ret == -1)
                    printf("Couldn't modify file: %s\n", wpa_supplicantConfigPath);
                //Revert Raspian filesystem to read-only mode
                system("ro");
                forceRedirect = 1; //Force redirection to clear POST data on next web refresh

            }
        }
        //////////New code ends


        ////////////Manually Set ip address
        if (strstr(buffer, "POST /setInterface") != NULL) {
            printf("\x1B[31mPOST /setInterface\x1B[0m\n");
            char interfaceName[FIELD] = {0};
            char manualAddress[FIELD] = {0};
            char manualMask[FIELD] = {0};
            char manualGateway[FIELD] = {0};
            int retLength;
            //Now test that the supplied addresses are valid ipv4
            struct sockaddr_in addrToTest; //Create address structure
            addrToTest.sin_family = AF_INET; //Specify 'internet'

            if (extractString(buffer, "POST /setInterface", "interface=", "&", interfaceName, FIELD) > 0)
                if (extractString(buffer, "POST /setInterface", "&address=", "&", manualAddress, FIELD) > 0)
                    if (extractString(buffer, "POST /setInterface", "&mask=", "&", manualMask, FIELD) > 0) {
                        //extractString(buffer, "POST /setInterface", "&Gateway=", "\0", manualGateway, FIELD);
                        //Now that we know we have some kind of input for all fields, remove the html browser encoding
                        reformatHTMLString(interfaceName, strlen((interfaceName)));
                        reformatHTMLString(manualAddress, strlen((manualAddress)));
                        reformatHTMLString(manualMask, strlen((manualMask)));
                        //reformatHTMLString(manualGateway, strlen((manualGateway)));
                        printf("New interface name: %s\n address: %s\n mask: %s\n gateway: %s\n", interfaceName, manualAddress, manualMask, manualGateway);


                        if (inet_aton(manualAddress, &addrToTest.sin_addr.s_addr) == 0) { //Test string is a valid address using inet_aton()
                            printf("simpleHTTPServerThread:set Interface: badly formed address supplied\n");
                        } else {// supplied address seems okay, now test the mask
                            if (inet_aton(manualMask, &addrToTest.sin_addr.s_addr) == 0) { //Test string is a valid address using inet_aton()
                                printf("simpleHTTPServerThread:set Interface: badly formed subnet mask supplied\n");
                            } else { //supplied mask seems okay, now test the gateway

                                printf("address and mask seem correct. Attempting to set the nic\n");
                                char commandString[FIELD] = {0};
                                char commandResponse[FIELD] = {0};
                                snprintf(commandString, FIELD, "ifconfig %s %s netmask %s 2>&1", interfaceName, manualAddress, manualMask);
                                printf("simpleHTTPServerThread:set Interface:commandString: %s\n", commandString);
                                sysCmd2(commandString, commandResponse, FIELD);
                                /*
                                                                //Gateway is optional. If it's supplied, use it
                                                                if (inet_aton(manualGateway, &addrToTest.sin_addr.s_addr) == 0) { //Test string is a valid address using inet_aton()
                                                                    printf("simpleHTTPServerThread:set Interface: badly formed gateway supplied\n");
                                                                } else {
                                                                    //Gateway looks okay
                                                                    printf("Adding default gateway: %s to routing table\n", manualGateway);
                                                                    addGateway(manualGateway);
                                                                } */
                            }
                        }

                    }
            //Gateway is optional. If it's supplied, use it
            if (extractString(buffer, "POST /setInterface", "&Gateway=", "\0", manualGateway, FIELD) > 0) {
                reformatHTMLString(manualGateway, strlen((manualGateway)));
                if (inet_aton(manualGateway, &addrToTest.sin_addr.s_addr) == 0) { //Test string is a valid address using inet_aton()
                    printf("simpleHTTPServerThread:set Interface: badly formed gateway supplied\n");
                } else {
                    //Gateway looks okay
                    //Might want to be careful about the next line. May or may not want a newly supplied gateway to replace all existing gateways
                    int gatewaysRemoved = removeAllGateways();
                    printf("%d gateways removed. Adding default gateway: %s to routing table\n", gatewaysRemoved, manualGateway);
                    addGateway(manualGateway);
                }
            }
            forceRedirect = 1; //Force redirection to clear POST data on next web refresh
        }

        //time(&currentTime); //Capture current time, assign it to currentTime
        //strftime(timeAsString, 100, "%H:%M:%S", localtime(&currentTime)); //Format human readable time
        updateTime(timeAsString, FIELD); //Update timestamp
        updateStatus(htmlStatus, SECTION);
        updateKnownNetworks(htmlKnownNetworks, SECTION);

        /* Write a response to the client */
        if (forceRedirect == 1) {
            //This code should run whenever POST has been used to submit some data through the webserver
            //It forces the client browser to redirect to the home page address and NOT rebsbmit the
            //previously entered data
            forceRedirect = 0; //Clear flag


            //User 'referer' field of incoming http data from client to derive previous URL. We can reuse this
            char refererURL[FIELD] = {0}; //Array to hold the URL
            startPos = strstr(buffer, "Referer:");

            if (startPos != NULL) {
                startPos = strstr(startPos, "http:");
                if (startPos != NULL) {
                    endPos = strstr(startPos, "//"); //Ignore first '//' characters
                    if (endPos != NULL) {
                        endPos += 2; //Skip beyond the '//'
                        endPos = strstr(endPos, "/"); //Referer URL delimited by a '/' character
                        if (endPos != NULL)
                            length = endPos - startPos + 1; //Get length of URL string (+1 needed or you lose the last character)
                        if (length < FIELD)
                            strlcpy(refererURL, startPos, length); //Copy referer URL into array
                    }
                }
            }
            printf("\x1B[31mForceredirect=1: Extracted referer URL: %s\x1B[0m\n", refererURL);

            memset(buffer, 0, FIELD);
            /*
            snprintf(buffer, FIELD, "HTTP/1.1 200 OK\n"
                    "Content-Type: text/html\r\n\n"
                    "<!DOCTYPE HTML>"
                    "<html lang=\"en-US\">"
                    "<head>"
                    "<meta charset=\"UTF-8\">"
                    //"<meta http-equiv=\"refresh\" content=\"1;url=http://192.168.2.6:20000\">"
                    "<meta http-equiv=\"refresh\" content=\"1;url=%s\">"
                    "<script type=\"text/javascript\">"
                    //"window.location.href = \"http://192.168.2.6:20000\""
                    "window.location.href = \"%s\""
                    "</script>"
                    "<title>Page Redirection</title>"
                    "</head>"
                    "<body>"
                    "<!-- Note: don't tell people to `click` the link, just tell them that it is a link. -->"
                    //"If you are not redirected automatically, follow the <a href='http://192.168.2.6:20000'>link to example</a>"
                    "If you are not redirected automatically, follow the <a href='%s'>link to example</a>"
                    "</body>"
                    "</html>", refererURL, refererURL, refererURL);
             */
            snprintf(buffer, FIELD, "HTTP/1.1 303 See Other\n"
                    "Location: %s\n", refererURL);

            printf("Referer html output: %s\n", buffer);
            n = write(newsockfd, buffer, strlen(buffer));
            printf("%d chars written\n", n);
            if (n < 0) {
                perror("ERROR writing to socket");
            }


        } else { //Or else output web page html as normal
            int charsWritten = 0;
            n = write(newsockfd, htmlHeader, strlen(htmlHeader));
            charsWritten += n;
            if (n < 0) {
                perror("ERROR writing to socket:01");
            }
            n = write(newsockfd, timeAsString, strlen(timeAsString));
            charsWritten += n;
            if (n < 0) {
                perror("ERROR writing to socket:02");
            }
            n = write(newsockfd, htmlHeaderbuttons, strlen(htmlHeaderbuttons));
            charsWritten += n;
            if (n < 0) {
                perror("ERROR writing to socket:03");
            }
            n = write(newsockfd, htmlStatus, strlen(htmlStatus));
            charsWritten += n;
            if (n < 0) {
                perror("ERROR writing to socket:04");
            }
            n = write(newsockfd, htmlNetworksFound, strlen(htmlNetworksFound));
            charsWritten += n;
            if (n < 0) {
                perror("ERROR writing to socket:05");
            }
            n = write(newsockfd, htmlKnownNetworks, strlen(htmlKnownNetworks));
            charsWritten += n;
            if (n < 0) {
                perror("ERROR writing to socket:06");
            }
            n = write(newsockfd, htmlAddSSIDField, strlen(htmlAddSSIDField));
            if (n < 0) {
                perror("ERROR writing to socket:07");
            }
            n = write(newsockfd, htmlRemoveSSIDField, strlen(htmlRemoveSSIDField));
            charsWritten += n;
            if (n < 0) {
                perror("ERROR writing to socket:08");
            }
            n = write(newsockfd, htmlSetInterface, strlen(htmlSetInterface));
            charsWritten += n;
            if (n < 0) {
                perror("ERROR writing to socket:09");
            }
            n = write(newsockfd, htmlFooter, strlen(htmlFooter));
            charsWritten += n;
            printf("%d chars written\n", charsWritten);
            if (n < 0) {
                perror("ERROR writing to socket:10");
            }
        }
        sleep(1); //Allow tx buffer to be sent before closing newsockfd
        memset(buffer, 0, FIELD);
        updateTime(buffer, FIELD);
        printf("%s%s%s\n", KMAG, buffer, KNRM); //Print current time
        printf("Closing newsockfd: %d\n", newsockfd);
        if (close(newsockfd) == -1) {
            printf("Couldn't close newsockfd: %d\n", newsockfd);
        }

        //Now act on 'schedule flags' set by earlier web button presses
        if (scheduleEnterSetupMode > 0) {
            int temp = scheduleEnterSetupMode; //Take a local copy
            scheduleEnterSetupMode = 0; //Clear global flag
            //Pass value of scheduleEnterSetupMode (1 for Adhoc, 2, for hostAP to setSetupMode)
            if (setSetupMode(temp) < 1) { //Invoke setup mode
                printf("Couldn't start setupMode\n");
            }
        }

        if (scheduleExitSetupMode == 1) {
            scheduleExitSetupMode = 0; //Clear flag
            if (setSetupMode(0) < 1) { //Invoke normal (i.e non-setup mode)

                printf("Couldn't exit setupMode\n");
            }
        }

    }
}

int stopHttpConfigServer() {
    /**
     * Gracefully attempts to stop the http server - closes the http listening socket
     * and performs general housekeeping
     * 
     *  Sets used GPO's back to inputs (for safety)
     *  Stops access point mode
     *  Stops the DHCP server
     *  Closes the http listening socket
     *
     * @return 
     */


    setSetupMode(0); //Stops Access Point mode (if enabled) and the dhcp server (if enabled)
    if (close(sockfd) == -1) { //Close http listening socket
        perror("httpConfigServer:stopHttpConfigServer(): close()");
        printf("Couldn't close http listening socket file descriptor sockfd: %d\n", sockfd);
    }
    /*
    if (close(newsockfd) == -1) { //Close http listening socket
        perror("httpConfigServer:stopHttpConfigServer(): close()");
        printf("Couldn't close http listening socket file descriptor newsockfd: %d\n", sockfd);
    }
     */
    gpioWrite(23, LOW); //Set GPO23 low
    gpioSetMode(23, PI_INPUT); //GPIO 23 as input (so it can't be shorted out)
    //gpioWrite(24, LOW); //Set GPO24 low
    //gpioSetMode(24, PI_INPUT); //GPIO 24 as input (so it can't be shorted out)
    exit(1);
}
//startHttpConfigServer(port,wpa_supplicantConfigPath,hostapdPath);

int startHttpConfigServer(unsigned int _port, char _wpa_supplicantConfigPath[], char _hostapdPath[], int useAlternativeDHCPClient, int setupModeGPIPin, int ledGPOPin) {
    /*
     *  Starts the HTTP config server thread.
     * 
     * Takes the following args:
     * _port -The http listening port
     * _wpa_supplicantConfigPath[] - Path/filename to wpa_supplicant file (moves around depending upon os type)
     * _hostapdPath[]       - Path to hostapd binary (used to for hostap (wpa) access point mode
     * useAlternativeDHCPClient: If 0, dhcpclient set to dhclient, otherwise udhcpc
     * 
     *  Returns -1 on failure
     */

    memset(wpa_supplicantConfigPath, 0, FIELD); //Clear global wpa_supplicantConfigPath array
    strlcpy(wpa_supplicantConfigPath, _wpa_supplicantConfigPath, FIELD); //Copy supplied path into global var

    memset(hostapdPath, 0, FIELD); //Clear global wpa_supplicantConfigPath array
    strlcpy(hostapdPath, _hostapdPath, FIELD); //Copy supplied path into global var

    if (useAlternativeDHCPClient == 1) //Determine which dhcp client to use
        installedDHCPClient = udhcpc;
    else installedDHCPClient = dhclient;

    if (gpioInitialise() < 0) { //Set up memory mapped access or exit if fail
        printf("startHttpConfigServer(): Can't initialise gpio pins.\n");
        return -1;
    }

    //Create 'square-wave' signal threads

    pthread_t _twoSecSqThread;
    if (pthread_create(&_twoSecSqThread, NULL, twoSecSqThread, NULL)) {
        printf("Error creating twoSecSq thread.\n");
        return -1;
    }
    pthread_detach(_twoSecSqThread); //Don't care what happens to thread afterwards



    pthread_t _oneSecSqThread;
    if (pthread_create(&_oneSecSqThread, NULL, oneSecSqThread, NULL)) {
        printf("Error creating oneSecSq thread.\n");
        return -1;
    }
    pthread_detach(_oneSecSqThread); //Don't care what happens to thread afterwards

    pthread_t _halfSecSqThread;
    if (pthread_create(&_halfSecSqThread, NULL, halfSecSqThread, NULL)) {
        printf("Error creating halfSecSq thread.\n");
        return -1;
    }
    pthread_detach(_halfSecSqThread); //Don't care what happens to thread afterwards

    pthread_t _quartSecSqThread;
    if (pthread_create(&_quartSecSqThread, NULL, quartSecSqThread, NULL)) {
        printf("Error creating halfSecSq thread.\n");
        return -1;
    }
    pthread_detach(_quartSecSqThread); //Don't care what happens to thread afterwards

    //Create button-monitoring thread, but only if supplied pin value !=-1 
    if (setupModeGPIPin != -1) {
        //Create pointer from supplied statusGPIPin parameter to pass to monitorButtonThread
        int *setupModeGPIPinPtr = malloc(sizeof (*setupModeGPIPinPtr));
        *setupModeGPIPinPtr = setupModeGPIPin;
        pthread_t _monitorButtonThread;
        if (pthread_create(&_monitorButtonThread, NULL, monitorButtonThread, (void*) setupModeGPIPinPtr)) {
            printf("Error creating monitorButtonThread thread.\n");
            return -1;
        }
        pthread_detach(_monitorButtonThread); //Don't care what happens to thread afterwards
    }


    //Create status LED thread, , but only if supplied pin value !=-1 
    if (ledGPOPin != -1) {
        //Create pointer from supplied ledGPOPin parameter to pass to statusLEDThread
        //printf("pthread_create()Status_LED: ledGPOPin: %d\n", ledGPOPin);
        int *ledGPOPinPtr = malloc(sizeof (*ledGPOPinPtr));
        *ledGPOPinPtr = ledGPOPin;
        pthread_t _statusLEDThread;
        if (pthread_create(&_statusLEDThread, NULL, statusLEDThread, (void*) ledGPOPinPtr)) {
            printf("Error creating statusLEDThread thread.\n");
            return -1;
        }
        pthread_detach(_statusLEDThread); //Don't care what happens to thread afterwards
    }
    
    //*wiFiConnectedThread
    pthread_t _wiFiConnectedThread;
    if (pthread_create(&_wiFiConnectedThread, NULL, wiFiConnectedThread, NULL)) {
        printf("Error creating wiFiConnectedThread thread.\n");
        return -1;
    }
    pthread_detach(_wiFiConnectedThread); //Don't care what happens to thread afterwards
    //exit(1);
    //Create webserver thread
    int *portNo = malloc(sizeof (*portNo)); //Create space for an integer pointer
    //*portNo = 20000; //Assign supplied value to that pointer
    *portNo = _port; //Assign supplied port value to that pointer
    pthread_t httpServer;
    if (pthread_create(&httpServer, NULL, simpleHTTPServerThread, (void*) portNo)) {
        printf("Error creating http server thread.\n");
        return -1;
    }

    return 1;
}

void *httpWebSocketServerThread(void *arg) {

    signal(SIGPIPE, SIG_IGN); //NOTE THIS LINE IS ESSENTIAL TO STOP THE SERVER CRASHING IF THE REMOTE CLIENT
    //UNEXPECTEDLY CLOSES THE TCP CONNECTION. IN THIS CASE write() will fail 
    //(because the socket is no longer valid) and the prog will crash    

    int forceRedirect = 0; //If you submit from the web page via POST, if the user does a page refresh
    //it will try and resubmit the values. Annoying. Solution: Send an intermediate page which forces the
    //browser back to the home page. This will clear the browser cache

    int portNo = *((int*) arg); //Tale local copy of arg
    free(arg); //Free up memory requested by malloc in pulseGPO()
    printf("httpWebSocketServer() supplied portNo: %d\n", portNo);


    //char htmlOutput[2048] = {0}; //Holds formatted output strings
    char *htmlHeader = "HTTP/1.1 200 OK\n"
            "Content-Type: text/html\r\n\n";

    char *htmlPageSource = "<!DOCTYPE html>"
            "<html>"
            "<head>"
            "<title>Websocket client</title>"
            "<link href=\"http://netdna.bootstrapcdn.com/twitter-bootstrap/2.3.1/css/bootstrap-combined.min.css\" rel=\"stylesheet\">"
            "<script src=\"http://code.jquery.com/jquery.js\"></script>"
            "</head>"
            "<body>"
            "<div class=\"container\">"
            "<h1 class=\"page-header\">Websocket client</h1>"
            "<form action=\"\" class=\"form-inline\" id=\"connectForm\">"
            "<div class=\"input-append\">"
            "<input type=\"text\" class=\"input-large\" value=\"ws://localhost:8088/echo\" id=\"wsServer\">"
            "<button class=\"btn\" type=\"submit\" id=\"connect\">Connect</button>"
            "<button class=\"btn\" disabled=\"disabled\" id=\"disconnect\">Disconnect</button>"
            "</div>"
            "</form>"
            "<form action=\"\" id=\"sendForm\">"
            "<div class=\"input-append\">"
            "<input class=\"input-large\" type=\"text\" placeholder=\"message\" id=\"message\" disabled=\"disabled\">"
            "<button class=\"btn btn-primary\" type=\"submit\" id=\"send\" disabled=\"disabled\">send</button>"
            "</div>"
            "</form>"
            "<hr>"
            "<ul class=\"unstyled\" id=\"log\"></ul>"
            "</div>"
            "<script type=\"text/javascript\">"
            "$(document).ready(function() {"
            "var ws;"
            "$('#connectForm').on('submit', function() {"
            "if (\"WebSocket\" in window) {"
            "ws = new WebSocket($('#wsServer').val());"
            "ws.onopen = function() {"
            "$('#log').append('<li><span class=\"badge badge-success\">websocket opened</span></li>');"
            "$('#wsServer').attr('disabled', 'disabled');"
            "$('#connect').attr('disabled', 'disabled');"
            "$('#disconnect').removeAttr('disabled');"
            "$('#message').removeAttr('disabled').focus();"
            "$('#send').removeAttr('disabled');"
            "};"
            "ws.onerror = function() {"
            "$('#log').append('<li><span class=\"badge badge-important\">websocket error</span></li>');"
            "};"
            "ws.onmessage = function(event) {"
            "$('#log').append('<li>recieved: <span class=\"badge\">' + event.data + '</span></li>');"
            "};"
            "ws.onclose = function() {"
            "$('#log').append('<li><span class=\"badge badge-important\">websocket closed</span></li>');"
            "$('#wsServer').removeAttr('disabled');"
            "$('#connect').removeAttr('disabled');"
            "$('#disconnect').attr('disabled', 'disabled');"
            "$('#message').attr('disabled', 'disabled');"
            "$('#send').attr('disabled', 'disabled');"
            "};"
            "} else {"
            "$('#log').append('<li><span class=\"badge badge-important\">WebSocket NOT supported in this browser</span></li>');"
            "}"
            "return false;"
            "});"
            "$('#sendForm').on('submit', function() {"
            "var message = $('#message').val();"
            "ws.send(message);"
            "$('#log').append('<li>sended: <span class=\"badge\">' + message + '</span></li>');"
            "return false;"
            "});"
            "$('#disconnect').on('click', function() {"
            "ws.close();"
            "return false;"
            "});"
            "});"
            "</script>"
            "</body>"
            "</html>";

    char timeAsString[FIELD] = {0}; //Buffer to hold human readable time

    //printf("%s\n", htmlHeader);
    //Create TCP socket
    web_sockfd = socket(AF_INET, SOCK_STREAM, 0); //Specify 'Reliable'
    if (web_sockfd < 0) {
        perror("socket()");
    }
    //Set socket options so that we can reuse the socket address/port
    //That way we won't be inhibited by the OS's TIME_WAIT state (and we can always bind to the supplied port)
    //Code from here: http://stackoverflow.com/questions/24194961/how-do-i-use-setsockoptso-reuseaddr
    //you can check the current port state using netstat -a | grep 30000 (where 30000 is the port you're interested in)

    int reuse = 0; //If reuse set to 0, the followling lines will have no effect
    if (setsockopt(web_sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*) &reuse, sizeof (reuse)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");
    if (setsockopt(web_sockfd, SOL_SOCKET, SO_REUSEPORT, (const char*) &reuse, sizeof (reuse)) < 0)
        perror("setsockopt(SO_REUSEPORT) failed");
    ////////////

    /* Initialize socket structure */
    struct sockaddr_in serv_addr, cli_addr;

    memset((char *) &serv_addr, 0, sizeof (serv_addr)); //Clear memory beforehand

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portNo);

    /* Now bind the host address using bind() call.*/
    int bindRet = 0;
    do {
        bindRet = bind(web_sockfd, (struct sockaddr *) &serv_addr, sizeof (serv_addr));
        if (bindRet < 0) {
            perror("ERROR on binding"); //If unable to bind to supplied port, try the next one
            printf("Can't bind to port %d, trying %d instead.\n", portNo, portNo + 1);
            portNo++; //Increment porNo
            serv_addr.sin_port = htons(portNo);
            sleep(1); //Delay to stop it getting out of hand
        }
    } while (bindRet < 0);

    int clilen;
    char buffer[FIELD];

    while (1) {
        /* Now start listening for the clients, here process will
         * go in sleep mode and will wait for the incoming connection
         */
        printf("httpWebSocketServerThread():Waiting for data...\n");

        listen(web_sockfd, 5); //This is a blocking call (5 is the backlog)
        clilen = sizeof (cli_addr);

        /* Accept actual connection from the client */
        int newsockfd = accept(web_sockfd, (struct sockaddr *) &cli_addr, &clilen);
        printf("httpWebSocketServerThread():accept(): newsockfd: %d\n", newsockfd);
        if (newsockfd < 0) {
            perror("ERROR on accept");
        }
        /* If connection is established then start communicating */
        memset(buffer, 0, FIELD);
        int n = read(newsockfd, buffer, FIELD);

        if (n < 0) {
            perror("ERROR reading from socket");
        }
        printf("httpWebSocketServerThread(): Incoming message: %s\n", buffer);

        //Now test incoming message 
        char *startPos, *endPos;
        unsigned int length = 0; //Length of substring to be extracted

        //Detect HTML button presses
        //startPos = strstr(buffer, "button=Restart+WPA+Supplicant");

        //time(&currentTime); //Capture current time, assign it to currentTime
        //strftime(timeAsString, 100, "%H:%M:%S", localtime(&currentTime)); //Format human readable time
        updateTime(timeAsString, FIELD); //Update timestamp


        /* Write a response to the client */
        if (forceRedirect == 1) {
            //This code should run whenever POST has been used to submit some data through the webserver
            //It forces the client browser to redirect to the home page address and NOT rebsbmit the
            //previously entered data
            forceRedirect = 0; //Clear flag


            //User 'referer' field of incoming http data from client to derive previous URL. We can reuse this
            char refererURL[FIELD] = {0}; //Array to hold the URL
            startPos = strstr(buffer, "Referer:");

            if (startPos != NULL) {
                startPos = strstr(startPos, "http:");
                if (startPos != NULL) {
                    endPos = strstr(startPos, "//"); //Ignore first '//' characters
                    if (endPos != NULL) {
                        endPos += 2; //Skip beyond the '//'
                        endPos = strstr(endPos, "/"); //Referer URL delimited by a '/' character
                        if (endPos != NULL)
                            length = endPos - startPos + 1; //Get length of URL string (+1 needed or you lose the last character)
                        if (length < FIELD)
                            strlcpy(refererURL, startPos, length); //Copy referer URL into array
                    }
                }
            }
            printf("Extracted referer URL: %s\n", refererURL);

            memset(buffer, 0, FIELD);
            snprintf(buffer, FIELD, "HTTP/1.1 200 OK\n"
                    "Content-Type: text/html\r\n\n"
                    "<!DOCTYPE HTML>"
                    "<html lang=\"en-US\">"
                    "<head>"
                    "<meta charset=\"UTF-8\">"
                    //"<meta http-equiv=\"refresh\" content=\"1;url=http://192.168.2.6:20000\">"
                    "<meta http-equiv=\"refresh\" content=\"1;url=%s\">"
                    "<script type=\"text/javascript\">"
                    //"window.location.href = \"http://192.168.2.6:20000\""
                    "window.location.href = \"%s\""
                    "</script>"
                    "<title>Page Redirection</title>"
                    "</head>"
                    "<body>"
                    "<!-- Note: don't tell people to `click` the link, just tell them that it is a link. -->"
                    //"If you are not redirected automatically, follow the <a href='http://192.168.2.6:20000'>link to example</a>"
                    "If you are not redirected automatically, follow the <a href='%s'>link to example</a>"
                    "</body>"
                    "</html>", refererURL, refererURL, refererURL);

            //printf("Referer html output: %s\n", refererURL);
            n = write(newsockfd, buffer, strlen(buffer));
            printf("%d chars written\n", n);
            if (n < 0) {
                perror("ERROR writing to socket");
            }


        } else { //Or else output web page html as normal
            int totalCharsWritten = 0;
            n = write(newsockfd, htmlHeader, strlen(htmlHeader));
            if (n < 0) {
                perror("ERROR writing to socket:01");
            } else totalCharsWritten += n; //Update running total of chars written
            /*
                        n = write(newsockfd, timeAsString, strlen(timeAsString));
                        if (n < 0) {
                            perror("ERROR writing to socket:02");
                        } else totalCharsWritten += n; //Update running total of chars written
             */
            n = write(newsockfd, htmlPageSource, strlen(htmlPageSource));
            if (n < 0) {
                perror("ERROR writing to socket:02");
            } else totalCharsWritten += n;
            /*
                        n = write(newsockfd, htmlFooter, strlen(htmlFooter));
                        if (n < 0) {
                            perror("ERROR writing to socket:04");
                        } else totalCharsWritten += n;
             */
            printf("%d chars written\n", totalCharsWritten);
        }

        close(newsockfd);
    }
}

int stopHttpWebSocketServerThread(void *arg) {
    /**
     * Gracefully attempts to stop the http server - closes the http listening socket
     * and performs general housekeeping
     * 
     *  
     *  Closes the http listening socket
     *
     * @return 
     */

    if (close(web_sockfd) == -1) { //Close http listening socket
        perror("httpWebSocketServerThread():stopHttpConfigServer(): close()");
        printf("Couldn't close http listening socket file descriptor web_sockfd: %d\n", web_sockfd);
    }

    exit(1);
}

int startHttpWebSocketServer(unsigned int port) {
    /*
     *  Starts the httpWebSocketServerThread.
     * 
     *  Returns -1 on failure
     */


    //Create webserver thread
    int *portNo = malloc(sizeof (*portNo)); //Create space for an integer pointer
    *portNo = 30000; //Assign supplied value to that pointer
    pthread_t webSocketServer;
    if (pthread_create(&webSocketServer, NULL, httpWebSocketServerThread, (void*) portNo)) {
        printf("Error creating httpWebSocketserver thread.\n");
        return -1;
    }
    return 1;
}
