/*
 * Pi Config Server
 * 
 * By default, the http config server will be listening on port 20000 (but if it can't bind to that, it will increment
 * the port no until it finds an available port)
 * 
 * Also included:
 * --An incomplete second http server (httpWebSocketServer) listening on port 30000. It shows a page, but otherwise doesn't
 * do anything useful
 * 
 * --The Adhoc WEP LAN mode is temperamental. Sometimes you can connect to it, other times not.
 * If you do manage to connect, you should be given one of three possible IP addresses (192.168.0.16-18). The wlan0 card itself
 * is statically assigned 192.168.0.11 when in this mode, so going to the web 192.168.0.11:20000 should give you the config page
 * The dhcp server is bound to wlan0 only
 * 
 * Some function descriptions:-
 * 
 * The global variable 'setupMode' reflects whether AP mode is currently active. If it is, certain functions test this variable
 * and act accordingly so as not to collide with the wlan0 card (which is the interface designated for AP connections)
 * 
 * restartWPASupplicant()
 * ----------------------
 * This function should be called after the wpa_supplicant.conf file has been modified as it will force wpa_supplicant to 
 * read in the newly added/modified wpa network ssids and keys
 *      If setupMode=0: Restarts wpa_supplicant for wlan0 AND wlan1 (if it is installed)
 *      If setupMode=1: Ignores wlan0 AND only restarts wlan1 (if it is installed)   
 * 
 * setAdhocWlanMode()
 * ------------------
 * -Kills wpa_supplicant process for wlan0 and attempts to create an Adhoc network secured by WEP.
 * -The SSID is automatically generated based on the hostname and serial number of the pi (and prefaced by a random number
 *  between 0 and 100). The wep key is hardwired as 0123456789
 * -If called with value (0) it will put wlan0 back into 'managed' mode and restart wpa_supplicant
 * 
 *
 * 
 * 
 * setSetupMode()
 * --------------
 * if called with a (2):-
 *      -Attempts to start hostapd (to provide a wpa secured access point mode)
 * 
 * If called with a (1):-
 *      -invokes Adhoc mode (see setAdhocWlanMode())
 *      -starts the DHCP server
 *      -sets the global setupMode variable
 * 
 * If called with a (0):-
 *      -Disables Adhoc mode
 *      -Stops the DHCP server
 *      -Clears the global setupMode variable
 *      -Initiates a DHCP renew lease on all interfaces (see renewDHCPLeases())
 * 
 * renewDHCPLease()
 * ----------------
 * Invokes the terminal command dhclient -v to renew leases. However:-
 *          If setupMode=0: Renews leases for ALL interfaces (via dhclient -v)
 *          If setupMode=1: Assumes wlan0 is in use for Adhoc mode so only renews lease for eth0 and wlan1 (if installed)
 * 
 * Known issues:-
 *          Raspian uses dhclient, whereas PiCore uses 'udhcpc -i wlan0' as it runs a different dhcp client daemon
 * 
 * 
 * 
 * GPI inputs and outputs
 * ----------------------
 * Input GPI18 (corresponding to header pin 12) - Internal Pull-up
 *      -Pulling this pin low for three seconds will invoke setup (adhoc) mode
 *      -If already in setup mode, pulling high for three seconds will exit setup mode
 * 
 * Output GPO23 (corresponding to physical pin 16)
 *      -Blinks slowly (2 second period) if no current wlan0 is not currently associated with a wireless network
 *      -Blinks at medium rate (0.5 second period) if wlan0 IS associated (i.e connected) to a wireless network
 *      -Blinks fast (0.25 second period) if program in setup (adhoc) mode
 * 
 * ***Compile essentials****:
 * -------------------
 * Remember to compile as 'default C' as opposed to C99, C11 etc (otherwise compiler will complain
 * about sigset_t stuff.
 * 
 * Also, include linker options: -lpthread -lm
 * 
 * *** known issues ****
 * ----------------------
 * Once having reverted back to managed mode, wlan0 doesn't always seem to be able to acquire an ip address (even though it is able 
 * to successfully associate with a wifi network. Successively clicking 'renew dhcp' should eventually fix this
 * 
 * Using PiCore:-
 * --------------
 * Host AP mode won’t work (on any dongle)
 * White dongle won’t work
 * Black (Edimax) dongle allows adhoc mode but won’t pass broadcast packets onto the client, so the dhcp client can’t respond
 * Seems to be lots of packet loss (or something?) in Adhoc mode so http web buttons using POST are unreliable (if they work at all)


 * White Dongle:-
 * --------------
 * Adhoc WiFi mode -can’t connect reliably (if ever) with iOS devices but okay with laptop  using Edimax WiFi dongle 
 * (not built in WiFi, for o[some reason, that keeps mistaking the adhoc (wep) network for wpa one so the authentication fails)
 * Seems to pass broadcast dhcp packets so would do both modes, but since it won’t work with PiCore, a non starter


 * Black Edimax dongle
 * Seemingly won’t pass broadcast ip packets in Adhoc mode -they get 'lost' (which means the dhcp server can't respond)
 * to the client

 * Conclusion:-
 * Use Raspian. WiFi drivers on PiCore aren’t up to it:-	
 * 	hosted won’t run
 * 	Adhoc isn’t usable because the only supported dongle (the Edimax) won’t pass broadcast dhcp packets in adhoc mode

 * If you want both modes to work (adhoc and hosted), use the white dongle (necessitates Raspian) - iOS devices can connect using hostAP mode.

 * However, the black dongle is much quicker to reconnect back to the main wifi, so nominally would be preferred to use.


 * web buttons work over wifi (on a mobile)? when not in adhoc mode? yes. Pretty reliable. (whereas in adhoc mode, there 
 * seems to be some sort of packet loss whereby the 
 
 * edimax on red pi? reliable adhoc? just prob with white dongle?

 * Edimax much quicker to respond,/reconnect. But dhcp response packets lost on adhoc mode. AP mode works fine.
 * 
 * Control via USR1 and USR2
 * -------------------------
 * Send a message to piconfigserver using
 *      sudo kill -s USR1 [pid] or sudo kill -s USR2 [pid]
 * 
 * USR1 mimics the GPI button press on pin 12 (causes piconfigserver to flip from normal to setup mode (or vice versa)
 * USR2 prints useful info to the stdin (like what the current mode is, listening port, etc).
 * 
 * --------
 * **To do**
 * --------
 * If you keep switching between normal/AP mode, you end up with 'stale' TCP connections which jam up the http server
 * Do sudo netstat -np | grep [listening port] and you'll see them listed.
 * SOLUTION: either reboot, or even better, rewrite the code so that whenever it switches between modes, the http listen
 * socket gets closed and then recreated again. The only problem with this, is that the original port might not be available
 * 
 * Check the status of current (or stale) tcp connections with:
 *      watch 'sudo netstat -np | grep [port]'
 * 
 * mobile browsers seem to cause most problems. You end up with 'FIN1' states which don't always release
 * 
 * Get hostapd to successfully run on PiCore (this will mean that adhoc AP mode can be abandoned)
 * 
 * Buttons (POSTS) via  mobile web browsers are unreliable. Desktop browsers seem fine.
 * 
 * startup arguments to allow selection of input and output pins
 * 
 * ------
 * 
 * Changlog
 * --------
 * PiConfigServer1.1
 * -----------------
 * Incorporated iptools2.3
 * 
 * PiConfigServer1.2
 * -----------------
 * httpConfigServer::setHostAPWlanMode()
 *      Changed location of conf file to "/tmp/httpConfigServer_hostapd.conf" to suit
 *      read-only Raspian mode (this is the only writeable folder with the readonly filesystem)
 * 
 * Added int unsavedChangesFlag: global var to signal whether changes to wpa_supplicant.conf 
 * need to be made permanent (by backing up). Ready, but not implemented, because not needed yet
 * 
 * Added system("rw") and system ("ro") commands to httpServerThread(), to AddSSID and RemoveSSID buttons
 * to allow filesystem to be temporarily put into read-write mode such that /etc/wpa_supplicant.conf
 * can be modified
 *      
 * Note: This functionality is really dependant on having a read-only FS set up using this tutorial:-
 * http://petr.io/en/blog/2015/11/09/read-only-raspberry-pi-with-jessie/
 *      
 * Added: isFileSystemWriteable()
 *        This checks to see whether the file system is currently writable
 * 
 * Reduced DHCP lease time to 600 secs (10 mins), renewal time (500 secs) and rebinding time (550 secs)
 * 
 *  Serious bug in http listener routine...
 *          It can only parse on tcp packet at a time. ios (iphone) browser typically breaks up an HTTP POST
 *          into multiple packets, therefor my program can't make anything of it. Doesn't seem to be a problem
 *          with desktop Chrome
 */

/* 
 * James Turner, 
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
#include <sys/types.h> 
#include <fcntl.h>

/*
 * 
 */

#define  FIELD          1024     //used for user entry field buffers
#define DEFAULT_WPA_CONFIG_FILENAME "/etc/wpa_supplicant/wpa_supplicant.conf"       //Used as a default path if none supplied
#define DEFAULT_HOSTAPD_FILENAME "hostapd"                        //Default location/path for hostapd (if none supplied)
#define DEFAULT_HTTP_CONFIG_TCP_LISTEN_PORT 20000                           //Used as a default listening port for http config server

void main(int argc, char** argv) {

    //Set main thread's signal mask to block all signals
    //All other threads will inherit the mask and have it blocked too
    //We manually set the flags in shutDownThread() to override these defaults

    /*
    char currentTime[FIELD]={0}; 
    int txPort=68;
    while(1){
        //Sending broadcast message
        memset(currentTime,0,FIELD);
        updateTime(currentTime,FIELD);
        printf("transmit: current time: %s\n",currentTime);
        sendBroadcastUDP(currentTime,txPort);
        sleep(1);
    }
     */
    printf("piconfigserver 1.0 (c) James Turner 2016 18/12/16-01\n");
    /*
    int ret=isFileSystemWriteable();
    printf("ret: %d\n",ret);
    exit(1);
    */
    
    sigset_t sigsToBlock;
    sigemptyset(&sigsToBlock); //initialise and empty the signal set
    sigaddset(&sigsToBlock, SIGTERM); //Add SIGTERM to our signal set
    sigaddset(&sigsToBlock, SIGINT); //Add SIGINT to our signal set
    sigaddset(&sigsToBlock, SIGUSR1); //Add SIGUSR1 to our signal set
    sigaddset(&sigsToBlock, SIGUSR2); //Add SIGUSR2 to our signal set
    sigaddset(&sigsToBlock, SIGKILL); //Add SIGKILL to our signal set

    pthread_sigmask(SIG_BLOCK, &sigsToBlock, NULL); //Apply the mask to this and daughter threads

    char wpa_supplicantConfigPath[FIELD] = {0}; //Holds the path/name of the target wpa_supplicant file (supplied at runtime) 
    char hostapdPath[FIELD] = {0}; //Holds the path/name of the hostapd file to be invoked in 'host ap' setup mode
    int port = 0;
    int useAlternativeDHCPClient = 0; //Determines whether we use (dhclient (Raspian) or udhcpc (TinyCore))
    int setupModeGPIPin=0;           //Which pin to connect the switch to
    int ledGPOPin=0;                //Which pin to connect the LED to
    //parse incoming arguments
    if (argc > 1) {
        int n = 1;
        ///////// Extract wpa_supplicant path
        for (n = 1; n < argc; n++) {
            if ((strstr(argv[n], "-wpa") != NULL) || (strstr(argv[n], "-w") != NULL)) { //Check for '-wpa'
                if (argc >= (n + 2)) {//now check that there is at least one more argument
                    if (strlen(argv[n + 1]) > 0) { //Does the field after this exist? And is it a string of length>0?
                        strlcpy(wpa_supplicantConfigPath, argv[n + 1], FIELD); //Take a copy of the supplied string
                        printf("Supplied wpa path: %s\n", wpa_supplicantConfigPath);

                    }
                } else printf("Missing wpa_supplicant path\n");
            }
        }
        ////////// Extract hostapd path
        //printf("%d args supplied\n", argc);
        for (n = 1; n < argc; n++) {
            if ((strstr(argv[n], "-hostapd") != NULL) || (strstr(argv[n], "-h") != NULL)) { //Check for '-hostapd'
                if (argc >= (n + 2)) {//now check that there is at least one more argument
                    if (strlen(argv[n + 1]) > 0) { //Does the field after this exist? And is it a string of length>0?
                        strlcpy(hostapdPath, argv[n + 1], FIELD); //Take a copy of the supplied string
                        printf("Supplied hostapd path: %s\n", hostapdPath);

                    }
                } else printf("Missing hostapd arg\n");
            }
        }

        ///////// Extract starting port no
        char portString[FIELD] = {0};
        for (n = 1; n < argc; n++) {
            if ((strstr(argv[n], "-port") != NULL) || (strstr(argv[n], "-p") != NULL)) { //Check for '-port'
                if (argc >= (n + 2)) {//now check that there is at least one more argument
                    if (strlen(argv[n + 1]) > 0) { //Does the field after this exist? And is it a string of length>0?
                        strlcpy(portString, argv[n + 1], FIELD); //Take a copy of the supplied string
                        port = strtol(portString, NULL, 10);
                        //printf("Supplied port: %s>>>>%d\n", portString, port);
                    }
                } else printf("Missing http listening port arg\n");
            }
        }
        ////// Extract '-use-udhcpc' DHCP client
        for (n = 1; n < argc; n++) {
            if (strstr(argv[n], "-udhcpc") != NULL) { //Check for '-port'
                printf("-udhcpc specified. udhcpc will be used instead of dhclient\n");
                useAlternativeDHCPClient = 1;
            }
        }

        
        ////// Extract Input GPI pin
        char gpiPin[FIELD]={0};
        for (n = 1; n < argc; n++) {
            if ((strstr(argv[n], "-gpi") != NULL) || (strstr(argv[n], "-i") != NULL)) { //Check for '-gpi or -i'
                if (argc >= (n + 2)) {//now check that there is at least one more argument
                    if (strlen(argv[n + 1]) > 0) { //Does the field after this exist? And is it a string of length>0?
                        strlcpy(gpiPin, argv[n + 1], FIELD); //Take a copy of the supplied string
                        setupModeGPIPin = strtol(gpiPin, NULL, 10);
                        //printf("Supplied GPI pin: %s>>>>%d\n", gpiPin, setupModeGPIPin);
                    }
                } else printf("Missing gpi pin arg\n");
            }
        }
        ////// Extract Output GPO pin
        memset(gpiPin,0,FIELD);
        for (n = 1; n < argc; n++) {
            if ((strstr(argv[n], "-gpo") != NULL) || (strstr(argv[n], "-o") != NULL)) { //Check for '-gpo or -o'
                if (argc >= (n + 2)) {//now check that there is at least one more argument
                    if (strlen(argv[n + 1]) > 0) { //Does the field after this exist? And is it a string of length>0?
                        strlcpy(gpiPin, argv[n + 1], FIELD); //Take a copy of the supplied string
                        ledGPOPin = strtol(gpiPin, NULL, 10);
                        //printf("Supplied GPO pin for LED: %s>>>>%d\n", gpiPin, ledGPOPin);
                    }
                } else printf("Missing gpo LED pin arg\n");
            }
        }
        
        ////// No GPI mode (whereby no pins are used)
        for (n = 1; n < argc; n++) {
            if (strstr(argv[n], "-nogpio") != NULL) { //Check for '-nogpio'
                setupModeGPIPin=-1;     //'-1' means 'don't use'
                ledGPOPin=-1;           //'-1' means 'don't use'
                
            }
        }
        
        
        ////// Extract usage/help
        for (n = 1; n < argc; n++) {

            if ((strstr(argv[n], "-?") != NULL) || (strstr(argv[n], "--help") != NULL)) { //Check for '-p?' or '-help'
                printf("Usage:\n------\n");
                printf("\t-w (-wpa) [path/filename]     Specify alternative wpa_supplicant.conf file location. (Default is %s)\n\n"
                        , DEFAULT_WPA_CONFIG_FILENAME);
                printf("\t-h (-hostapd) [path/filename]     Specify alternative hostapd program location. (Default is %s)\n\n"
                        , DEFAULT_HOSTAPD_FILENAME);

                printf("\t-p (-port) [port no]     Specify alternative http config server listening port. (Default is %d)\n\n"
                        , DEFAULT_HTTP_CONFIG_TCP_LISTEN_PORT);

                printf("\t-? or --help for this message\n");

                printf("Options:\n--------\n");
                printf("\t-udhcpc                  Use udhcpc (TinyCore) rather than dhclient (default) dhcp client\n");
                printf("\t-nogpio                  Disable setup mode switch input and status LED output\n");
                printf("\t-gpi [pin] or -i [pin]   Specify  (native) gpi pin for mode switch (active low)\n");
                printf("\t-gpo [pin] or -o [pin]   Specify  (native) gpo pin for status LED\n");                        
                printf("\nSignals\n--------\n");
                printf("\tUSR1: Set/Unset setup mode (mimics gpi button press. Tries hostapd mode, backs off to adhoc mode if unsuccesful.\n");
                printf("\tUSR2: Get (display) current mode and other info.\n");
                printf("\t\te.g kill -s USR1 [pid] \n");
                exit(1);
            }

        }
    }

    
    
    //Set default values if no arguments supplied
    if (strlen(wpa_supplicantConfigPath) == 0) {
        printf("Using default wpa_supplicant path: %s\n", DEFAULT_WPA_CONFIG_FILENAME);
        strlcpy(wpa_supplicantConfigPath, DEFAULT_WPA_CONFIG_FILENAME, FIELD); //Copy default path into path variable
    }
    if (strlen(hostapdPath) == 0) {
        printf("Using default hostapd path: %s\n", DEFAULT_HOSTAPD_FILENAME);
        strlcpy(hostapdPath, DEFAULT_HOSTAPD_FILENAME, FIELD); //Copy default path into path variable
    }

    if (port == 0) {
        printf("Using default http config port: %d\n", DEFAULT_HTTP_CONFIG_TCP_LISTEN_PORT);
        port = DEFAULT_HTTP_CONFIG_TCP_LISTEN_PORT;
    }
    
    
    if(setupModeGPIPin==-1){
        printf("GPI setup mode input switch disabled\n");
    }
    else if(setupModeGPIPin==0){        //If no pin supplied
        setupModeGPIPin=18;
        printf("Using default input GPI18 (corresponding to header pin 12)\n");
    }
    else
        printf("Using gpi input pin: %d\n",setupModeGPIPin);
    
    if(ledGPOPin==-1){
        printf("GPO pin for Status LED disabled\n");
    }
    else if(ledGPOPin==0){
        ledGPOPin=23;
        printf("Using default output GPO23 (corresponding to header pin 16)\n");
    }
    else
        printf("Using gpo output pin: %d\n",ledGPOPin);
    //Now check that the same gpio pins haven't been specified for input and output
    if((setupModeGPIPin==ledGPOPin)&&(setupModeGPIPin!=-1)){
        printf("Same gpio pins specified for mode switch(%d) and led status(%d), disabling mode switch input\n",
                setupModeGPIPin,ledGPOPin);
        setupModeGPIPin=-1;
    }
    //exit(1);

    startHttpConfigServer(port, wpa_supplicantConfigPath, hostapdPath, 
            useAlternativeDHCPClient, setupModeGPIPin,ledGPOPin);
    startHttpWebSocketServer(30000);


    //Now sit in endless loop waiting to intercept a system signal]
    //The signal mask imposed earlier should mean that this thread is the only
    //one that can intercept signal messages

    //Send signal from terminal using kill -s USR1 [pid]]
    sigset_t sigsToCatch; //Signal set

    sigemptyset(&sigsToCatch); //initialise and empty the signal set
    sigaddset(&sigsToCatch, SIGUSR1); //Add USR1 to our signal set
    sigaddset(&sigsToCatch, SIGUSR1); //Add USR1 to our signal set
    sigaddset(&sigsToCatch, SIGUSR2); //Add USR2 to our signal set
    sigaddset(&sigsToCatch, SIGKILL); //Add SIGKILL to our signal set
    sigaddset(&sigsToCatch, SIGTERM); //Add SIGTERM to our signal set
    sigaddset(&sigsToCatch, SIGINT); //Add SIGINT to our signal set

    int caught; //Will store the returned 'signal number'

    while (1) {
        sigwait(&sigsToCatch, &caught); //Blocking call to wait for SIGUSR1
        switch (caught) { //Determine which of the messages has been received
            case SIGUSR1: //Put prog into setup mode
                printf("SIGUSR1\n");
                if (getSetupMode() == 0) {
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

                /////////////
                break;
            case SIGUSR2: //Get Current mode
                printf("SIGUSR2\n");

                printf("piconfigserver\n----------------\n");
                char host[FIELD] = {0};
                getHostName(host, FIELD);
                printf("Host name: %s\n", host);

                unsigned int serialNo = getSerialNumber();
                printf("Serial no: %x\n", serialNo);

                int mode = getSetupMode();
                int httpListeningPort = getHTTPListeningPort();
                char ssid[FIELD] = {0};
                switch (mode) {
                    case 0:
                        printf("Normal mode. Current http listening port: %d\n", httpListeningPort);
                        break;
                    case 1:
                        getAPssid(ssid, FIELD);
                        printf("Adhoc AP mode. SSID: %s\n", ssid);
                        break;
                    case 2:
                        getAPssid(ssid, FIELD);
                        printf("hostapd AP mode. SSID: %s\n", ssid);
                        break;
                    default:
                        break;
                }


                break;
            case SIGKILL:
                printf("SIGKILL: Stopping HttpConfigServer\n");
                stopHttpConfigServer();
                exit(1);
                break;
            case SIGTERM:
                printf("SIGTERM: Stopping HttpConfigServer\n");
                stopHttpConfigServer();
                exit(1);
                break;
            case SIGINT:
                printf("SIGINT: Stopping HttpConfigServer\n");
                stopHttpConfigServer();
                exit(1);
                break;
                //usleep(20 * 1000); //100mS delay
        }
    }
}



