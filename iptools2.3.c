/*
 * Useful IP functions
 * 
 * For these to compile, you will probably need the following lines at the top of your main.c
 
 * #define _POSIX_C_SOURCE 200809L  //This line required for OSX otherwise popen() fails)
 * and/or optionally #define _POSIX_SOURCE
 * 
 * 
 * New with 2.1
 * ------------
 * sendBroadcastUDP() Added the 'interface' argument. 
 *      Allows you to bind send the message out on a specific interface
 * 
 * New with 2.2
 * ------------
 * sendUDP() Added 'interface' argument.
 *          Allows you to specify the sending interface
 * 
 * New with 2.3
 * ------------
 * Added the function nullTermStrlCpy() - Copies a string, checks the dest. array length and always null terminates
 * Replaced all instances of strcpy() with nullTermStrlCpy() 
 * Replaced all instances of sprintf() with snprintf()
 * receiveUDP() changed to include srcIPSize char array length argument
* Was: (char *rxBuffer, int rxBufferSize, int udpPort, char *srcIP, int *srcPort)
* Now: int receiveUDP(char *rxBuffer, int rxBufferSize, int udpPort, char *srcIP, int srcIPSize, int *srcPort) {
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include "iptools2.3.h"

size_t nullTermStrlCpy(char* dst, const char* src, size_t bufsize) {
    /*
     * Safer version of strncpy() which always termainates the copied string with
     * the null character (strncpy() doesn't terminate a string)
     * 
     * Also better than strcpy() which doesn't length check the destination array, so is therefore
     * prone to buffer overruns
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

int listAllInterfaces(char interfaceList[][2][20], int maxEntries) {
    /*
     * The function returns the no. of network interfaces found regardless of
     * whether they have already had their address set (as opposed to 
     * getLocalIPaddr() which only lists interfaces that are already set)
     * maxEntries is the no. of maximum network cards info that can be stored.
     * 
     * This function takes a pointer to a 3D char array of the form 
     * char interfaceList[maxEntries][2][20]; //Will store results of getifaddrs())
     * and will populate the array with the system name and address of ALL
     * interfaces (regardless of whether they are active or not).
     * 
     * 
     * NOTE: Typically getifaddrs() would return the name of a single interface more 
     * than once, because internally, each interface is listed for IPV4 and IPV6
     * seperately. Since we're only interested in the ipV4 capable interfaces (which 
     * would be listed first) we simply wait until we notice the same interface name 
     * being listed twice. At that point, we stop iterating through the list.

     * Interface 0 will always be the loopback, so you'd always expect this
     * function to return a value of 1 or greater
     * 
     * Sample usage:-
     *      char interfaceList[20][2][20];
     *      int ret = listAllInterfaces(interfaceList, 20); //Display local ethernet interfaces
     *      printf("No of unique interfaces found:%d\n", ret);
     *      for (int n = 0; n < ret; n++)
     *      printf("%d: %s %s\n", n, interfaceList[n][0], interfaceList[n][1]);
     *
     */
    struct ifaddrs *ifap, *ifa;
    struct sockaddr_in *sa;
    char *addr;
    char interfaceNo = 0;
    char firstInterfaceName[20] = {0};

    getifaddrs(&ifap);
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {

        //Get the name of the interface
        memset(interfaceList[interfaceNo][0], 0, 20); //Clear name mem first
        strncpy(interfaceList[interfaceNo][0], ifa->ifa_name, 19); //Store name

        if (interfaceNo == 0)strncpy(firstInterfaceName, ifa->ifa_name, 19); //Capture name of FIRST interface found
        if (!strcmp(ifa->ifa_name, firstInterfaceName) && (interfaceNo > 0)) break; //If we get a repeated name, we must have gone around the loop
        //of unique named interfaces, therefore we can break the loop early

        memset(interfaceList[interfaceNo][1], 0, 20); //Clear address mem first
        if (ifa->ifa_addr->sa_family == AF_INET) {
            sa = (struct sockaddr_in *) ifa->ifa_addr;
            addr = inet_ntoa(sa->sin_addr);

            strncpy(interfaceList[interfaceNo][1], addr, 19); //Store address
        }
        //printf("Interface: %s\tAddress: %s\n", ifa->ifa_name, addr);
        if (interfaceNo < maxEntries) interfaceNo++; //increment index    

    }
    freeifaddrs(ifap);
    /*
    printf("No of interfaces found:%d\n",(int)interfaceNo);
    for(int n=0;n<(int)interfaceNo;n++)
        printf("%d: %s %s\n",
                n,
                interfaceList[n][0],
                interfaceList[n][1]);
     */
    return (int) interfaceNo; //Return no. of interfaces found

}

int getLocalIPaddr(char interfaceList[][2][20], int maxEntries) {
    /*
     * This function takes a pointer to a 3D char array of the form 
     * char interfaceList[maxEntries][2][20]; //Will store results of getifaddrs())
     * and will populate the array with the system name and address of the 
     * network interfaces that have an ip address.
     * The function returns the no. of active network interfaces found.
     * maxEntries is the no. of maximum network cards info that can be stored

     * Interface 0 will always be the loopback, so you'd always expect this
     * function to return a value of 1 or greater
     * 
     * Sample usage:-
     *      char interfaceList[20][2][20];
     *      int ret = getLocalIPaddr(interfaceList, 20); //Display local ethernet interfaces
     *      printf("No of interfaces found:%d\n", ret);
     *      for (int n = 0; n < ret; n++)
     *      printf("%d: %s %s\n", n, interfaceList[n][0], interfaceList[n][1]);
     *
     */
    struct ifaddrs *ifap, *ifa;
    struct sockaddr_in *sa;
    char *addr;
    char interfaceNo = 0;

    getifaddrs(&ifap);
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr->sa_family == AF_INET) {
            sa = (struct sockaddr_in *) ifa->ifa_addr;
            addr = inet_ntoa(sa->sin_addr);
            memset(interfaceList[interfaceNo][0], 0, 20); //Clear mem first
            strncpy(interfaceList[interfaceNo][0], ifa->ifa_name, 19); //Store name

            memset(interfaceList[interfaceNo][1], 0, 20); //Clear mem first
            strncpy(interfaceList[interfaceNo][1], addr, 19); //Store address

            //printf("Interface: %s\tAddress: %s\n", ifa->ifa_name, addr);
            if (interfaceNo < maxEntries) interfaceNo++; //increment index    
        }
    }
    freeifaddrs(ifap);
    /*
    printf("No of interfaces found:%d\n",(int)interfaceNo);
    for(int n=0;n<(int)interfaceNo;n++)
        printf("%d: %s %s\n",
                n,
                interfaceList[n][0],
                interfaceList[n][1]);
     */
    return (int) interfaceNo; //Return no. of interfaces found
}

int sendUDP(char txData[], char destIPAddress[], int destPort, char interface[]) {
    /*
     * Sends a string (txData) via UDPto specifed addr:port
     * 
     * the interface parameter will bind the socket to a particular interface
     * eg "wlan0" or "eth0". 
     * 
     * if "0" is specified as an arg, this parameter will be ignored.
     * Note: there is no checking to ensure that the interface is correctly labelled
     * Also: Not all Linux's support this 'binding to interface' function (it won't
     * work on OSX, for example)
    */
    
    //printf("sendUDP() arg supplied: %s\n",txData);
    //char txData[] = "String";
    int txDataSize = (strlen(txData)) + 1; //Get size of string plus null char
    //printf("txDataSize: %d\n", txDataSize);

    //char buf[BUFLEN]; //tx buffer
    socklen_t slen; //Special typdef. Holds size of sockaddr_in struct
    //Request a new socket from the OS and get a reference to it
    int handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle < 0) {
        perror("sendUDP():socket");
        return -1;
    }
    
    //Now force the socket to bind to specified interface (if supplied) 
    //(otherwise broadcast udp reply might be sent out on eth0 instead of wlan0, for example)
    if (interface[0] != '0') {
        int sockOptRetValue = 0;
        if (setsockopt(handle, SOL_SOCKET, SO_BINDTODEVICE, interface, strlen(interface)) == -1) {
                perror("setsockopt - SOL_SOCKET, SO_BINDTODEVICE ");
                printf("sendUDP() Can't bind to device: %s\n",interface);
                close (handle);
                return -1;
        }
    }
    
    
    //Create an ip address/port structure for the remote end
    //Use htons() to convert a decimal port no to a special binary type
    //Use inet_aton() to parse SRV_IP dotted decimal notation
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(destPort);
    if (inet_aton(destIPAddress, &servaddr.sin_addr.s_addr) == 0) {
        perror("iptools.c:sendUDP():inet_aton() failed");
        return -1;
    }
    slen = sizeof (servaddr);
    //Now send the data
    if (sendto(handle, txData, txDataSize, 0, (struct sockaddr*) &servaddr, slen) == -1)
        perror("iptools.c:sendUDP():sendto()");

    close(handle);
    return 0;
}

int sendBroadcastUDP(char txData[], int destPort, char interface[]) {
    /*
     * Sends a string (txData) via UDP to the broadcast address on port destPort
     * Returns -1 on error or 1 if all okay.
     * 
     * the interface parameter will bind the socket to a particular interface
     * eg "wlan0" or "eth0". 
     * 
     * if "0" is specified as an arg, this parameter will be ignored.
     * Note: there is no checking to ensure that the interface is correctly labelled
     * Also: Not all Linux's support this 'binding to interface' function (it won't
     * work on OSX, for example)
     * 
     * This function may require sudo/root privileges
     * 
     * Sample usage for sender:-
     *      #include <stdio.h>
     *      #include <stdlib.h>
     *      #include <string.h>
     *      #include <unistd.h>
     *      #define SRV_PORT 8080
     * 
     *      void main(int argc, char** argv) {   
     *      
     *          printf("Argument supplied: %s, %d\n", argv[1]);
     *          sendBroadcastUDP(argv[1],SRV_PORT,NULL);
     *      }
     * 
     * Sample usage for receiver:-
     *      #include <stdio.h>
     *      #include <stdlib.h>
     *      #include <string.h>
     *      #include <unistd.h>
     *      #include <sys/types.h> 
     *      #include <sys/socket.h>
     *      #include <netinet/in.h>
     *      #include <fcntl.h>
     * 
     *      void main(void) {
     *          printf("Waiting for data\n");
     *          int rxBufferSize=1024;
     *          char rxData[rxBufferSize];
     *          int srcPort;
     *          char srcIP[20];
     *          while (1) {
     *              receiveUDP(rxData, rxBufferSize, 8080, srcIP, ,20,&srcPort);
     *              printf("Received data: %s from %s:%d\n",rxData,srcIP,srcPort);
     *          }
     *      }
     * 
     */

    //printf("sendUDP() arg supplied: %s\n",txData);
    //char txData[] = "String";
    int txDataSize = (strlen(txData)) + 1; //Get size of string plus null char
    //printf("txDataSize: %d\n", txDataSize);
    int broadcast = 1;
    //char buf[BUFLEN]; //tx buffer
    socklen_t slen; //Special typdef. Holds size of sockaddr_in struct
    //Request a new socket from the OS and get a reference to it
    int handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle < 0) {
        perror("sendBroadcastUDP():socket");
        return -1;
    }
    //Now set socket options to allow UDP broadcast
    if ((setsockopt(handle, SOL_SOCKET, SO_BROADCAST,
            &broadcast, sizeof broadcast)) == -1) {
        perror("sendBroadcastUDP():setsockopt - SO_SOCKET ");
        return -1;
    }
    //char *devname = "wlan0";
    //char *devname = "eth0";

    //Now force the socket to bind to specified interface (if supplied) 
    //(otherwise broadcast udp reply might be sent out on eth0 instead of wlan0, for example)
    if (interface[0] != '0') {
        int sockOptRetValue = 0;
        if (setsockopt(handle, SOL_SOCKET, SO_BINDTODEVICE, interface, strlen(interface)) == -1) {
                perror("setsockopt - SOL_SOCKET, SO_BINDTODEVICE ");
                printf("iptools2.1:sendBroadcastUDP() Can't bind to device: %s\n",interface);
                close (handle);
                return -1;
        }
    }
    else printf("iptools2.1:sendBroadcastUDP(): No interface specified\n");
    //Create an ip address/port structure for the remote end
    //Use htons() to convert a decimal port no to a special binary type

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(destPort);
    servaddr.sin_addr.s_addr = INADDR_BROADCAST;

    slen = sizeof (servaddr);
    //Now send the data
    if (sendto(handle, txData, txDataSize, 0, (struct sockaddr*) &servaddr, slen) == -1)
        perror("sendBroadcastUDP()::sendto()");

    close(handle);
    return 1;
}

int receiveUDP(char *rxBuffer, int rxBufferSize, int udpPort, char *srcIP, int srcIPSize, int *srcPort) {
    /*
     * Listens for UDP data on specified port
     * Fills supplied buffer (rxBuffer) with received data
     * The supplied pointers srcIP and srcPort will be populated with the source address
     * and port of the incoming message. *srcIP should be a char array of 20 characters
     * so that it can easily fit an IPv4 address in dot notation 000.000.000.000
     * 
     * Sample usage for receiveUDP() to listen on udp port 8080 :-
     *      #include <stdio.h>
     *      #include <stdlib.h>
     *      #include <string.h>
     *      #include <unistd.h>
     *      #include <sys/types.h> 
     *      #include <sys/socket.h>
     *      #include <netinet/in.h>
     *      #include <fcntl.h>
     * 
     *      void main(void) {
     *          printf("Waiting for data\n");
     *          int rxBufferSize=1024;
     *          char rxData[rxBufferSize];
     *          int srcPort;
     *          char srcIP[20];
     *          while (1) {
     *              receiveUDP(rxData, rxBufferSize, 8080, srcIP, &srcPort);    //Blocking call
     *              printf("Received data: %s from %s:%d\n",rxData,srcIP,srcPort);
     *          }
     *      }
     */

    int handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle < 0) {
        perror("iptools.c:receiveUDP():socket");
        return -1;
    }

    //printf("iptools.c:receiveUDP():supplied handle: %d\n",handle);
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(udpPort);
    servaddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(handle, (struct sockaddr*) &servaddr, sizeof (servaddr)) < 0) {
        perror("iptools.c:receiveUDP():bind");
        return -1;
    }
    struct sockaddr_in cliaddr; //Address of remote end


    //char rxBuffer[1024];

    socklen_t len = sizeof (cliaddr);
    int received_bytes = recvfrom(handle, rxBuffer, rxBufferSize, 0, (struct sockaddr*) &cliaddr, &len);
    if (received_bytes > 0) {
        //    printf("Here is the message: %s\n", rxBuffer);
        //printf("From host: %s:%d\n", inet_ntoa(cliaddr.sin_addr), (int) ntohs(cliaddr.sin_port));
        //strcpy(srcIP, inet_ntoa(cliaddr.sin_addr)); //Populate source address (so you know where packet came from)
        nullTermStrlCpy(srcIP, inet_ntoa(cliaddr.sin_addr),srcIPSize); //Populate source address (so you know where packet came from)
        *srcPort = (int) ntohs(cliaddr.sin_port); //Retrieve  sourceport and assign to srcPort
        //int k=close(handle);
        //printf("\33[2K\riptools:receiveUDP(). close():%d,",k);
        close(handle);
        return received_bytes;
    } else {
        close(handle);
        return -1;
    }

}

int initWiFiNetworkStruct(wifiNetwork *_wifiNetwork) {
    if (_wifiNetwork == NULL) return -1;
    memset(_wifiNetwork, 0, sizeof (wifiNetwork)); //Initialise struct memory to zero
    nullTermStrlCpy(_wifiNetwork->desc, " ",ARG_LENGTH);
    nullTermStrlCpy(_wifiNetwork->essid, " ",ARG_LENGTH);
    nullTermStrlCpy(_wifiNetwork->key, " ",ARG_LENGTH);
    nullTermStrlCpy(_wifiNetwork->passPhrase, " ",ARG_LENGTH);
    nullTermStrlCpy(_wifiNetwork->encryption, " ",ARG_LENGTH);
    _wifiNetwork->priority = 0;
    _wifiNetwork->sigLevel = 0;
    _wifiNetwork->sigQuality = 0;
    return 1;
}

int initNicStruct(nic *_nic) {
    if (_nic == NULL) return -1;
    //Initialises a nic (network interface) struct
    //Clears memory ready for use.
    memset(_nic, 1, sizeof (nic)); //Initialise struct memory to zero
    nullTermStrlCpy(_nic->name, " ",ARG_LENGTH);
    nullTermStrlCpy(_nic->address, "0.0.0.0",ARG_LENGTH);
    nullTermStrlCpy(_nic->netmask, "0.0.0.0",ARG_LENGTH);
    nullTermStrlCpy(_nic->broadcastAddress, "0.0.0.0",ARG_LENGTH);
    nullTermStrlCpy(_nic->gateway, "0.0.0.0",ARG_LENGTH);
    _nic->priority = 0;
    _nic->status = 0;
    _nic->configured = 0;
    return 1;
}

int sysCmd2(char cmdString[], char output[], int outputLength) {
    /*
     * Executes a system command supplied in cmdString places the output
     * into a supplied array output of length outputLength .
     * 
     * The function returns an int containing the size of the return char array, or -1
     * if there is an error or the supplied output buffer is not large enough
     * 
     * If you suffix your command string with '2>&1' the error output can be captured
     * 
     * Sample usage:-
     *      char output[5000]=" "; //NOTICE the " " after the buffer declaration.
     *                              This is needed otherwise the compiler won't 
     *                              'properly' allocate all the memory needed.
     * 
     * int n=sysCmd2("sudo iwlist wlan0 scan",output,5000);
     * printf("output: %d chars: %s\n",n,output);
     * 
     * NOTE: For the popen() function to work (without warning/errors
     * you need this at the top of the file:-
     * 
     * #define _POSIX_C_SOURCE 200809L
     */
    FILE* fp; //Create handle for the file pipe created by popen())
    char newLine[1024]; //Intermediate storage of each new line read by fgets())
    int responseLength = 0; //Stores the total length of the response
    int sizeNeeded = 0;
    fp = popen(cmdString, "r");
    if (fp == NULL) {
        perror("sysCmd2():popen()"); //Run the command and open a pipe to read the output stream
        return -1;
    }
    while (fgets(newLine, sizeof (newLine), fp) != NULL) { //Read the pipe, one line at a time
        //responseLength = responseLength + strlen(newLine); //Capture the length of the new line
        responseLength = responseLength + snprintf(NULL, 0, "%s", newLine); //TRICK: Use snprintf() to 'test write'
        //to a NULL address to see how many characters
        //It would have written (so we can be sure
        //that the output[] buffer is large enough)

        //printf("responseLength: %d\n",responseLength);
        if (responseLength < outputLength) {
            strcat(output, newLine); //Add new data to end of existing char array

        } else {
            printf("sysCmd2():Supplied output buffer too small for response from %s\n: ", cmdString);
            pclose(fp);
            return -1;
        }
    }
    pclose(fp);
    return responseLength; //return total length of received string
}

int iwScanCountNetworks() {
    /*
     * Count how many WiFi networks have been found by counting how many times the word "Cell"
     * appears in the response from iwscan
     * 
     * Hardwired to use wlan0
     */
    char *cmdString = "sudo iwlist wlan0 scan";
    //char temp=0;
    //char *cmdResponse; //Char array holding the output
    int cmdResponseSize; //Holds the length of the response from sysCmd
    int noOfNetworksFound = 0;

    //char *cmdResponse=malloc(30000*sizeof(cmdResponse));
    /*
    if (cmdResponse==NULL){
        printf("iwScanCountNetworks(): malloc\n");
        return -1;
    }
     */
    char cmdResponse[30000] = " "; //iwlist returns lots of data!

    cmdResponseSize = sysCmd2(cmdString, cmdResponse, (30000)); //Execute system command
    //printf("cmdResponseSize: %d\n", cmdResponseSize);
    //printf("%s\n", cmdResponse);

    //Now try parsing the output string
    char searchString[100] = "Cell"; //Thing to search for
    char *result; //Pointer to where the match is found
    //Find initial result
    result = strstr(cmdResponse, searchString);
    if (result == NULL) return 0; //No networks found
    else noOfNetworksFound = 1; //At least one network found
    //Now iterate through the remainder of the cmdResponse string until all instances of 
    //'Cell' have been found

    do {
        result = strstr((result + 1), searchString); //Search for pattern 'Cell'
        if (result != NULL)noOfNetworksFound++; //If found, increment counter
    } while (result != NULL);


    //free(cmdResponse);
    return noOfNetworksFound;
}

int getWiFiConnStatus(wifiNetwork *_wifiNetwork, char interface[]) {
    /*
     * This function calls iwconfig with the supplied interface (eg wlan0).
     * If the wifi interface is associated with a WiFi network, the function
     * will populate the supplied *wifiNetwork struct with info about that 
     * network.
     * 
     * It will return -1 on error, 0 if no network connection or 1 if connected
     */
    char cmdString[100] = {0};
    snprintf(cmdString, 100, "iwconfig %s", interface); //Construct command string
    int cmdResponseSize; //Holds the length of the response from sysCmd
    int noOfNetworksFound = 0;
    char cmdResponse[30000] = " "; //iwlist returns lots of data!
    cmdResponseSize = sysCmd2(cmdString, cmdResponse, 30000); //Execute the command

    //Now try parsing the output string
    char *line, *startPos, *endPos, *ptr;
    unsigned int length;
    startPos = strstr(cmdResponse, "Access Point"); //Get initial start point
    if (startPos == NULL) return -1; //Can't trust output of iwconfig

    startPos = strstr(startPos, ":"); //Move to Colon
    if (startPos == NULL) return -1; //Can't trust output of iwconfig

    //If the response contains the phrase "Not-Associated" after the 'Access Point'
    //field, we can assume we're not connected

    startPos = strstr(startPos, "Not-Associated");
    if (startPos != NULL) return 0; //Phrase was found so not currently associated with any network
    else //Phrase wasn't found so assume we are connected to a network. Now collect info.

        //Get name of ESSID we're connected to    
        startPos = strstr(cmdResponse, "ESSID"); //Get initial start point
    if (startPos == NULL) return -1; //Can't trust output of iwconfig
    startPos = strstr(startPos, "\""); //search for ' " '
    if (startPos == NULL) return -1;
    startPos += 1; //Go to 1 char beyond ' " '
    endPos = strstr(startPos, "\""); //Find next ' " ' delimiter
    if (endPos == NULL) return -1;
    length = endPos - startPos; //Get length of string to extract
    line = (char*) malloc(length + 1); //Allocate memory for a line of text
    if (line != NULL) {
        bzero(line, (length + 1)); //Clear newly allocated memory
        if (length < ARG_LENGTH) //Don't run off the end of the array
            strncpy(line, startPos, length); //Copy value to line
        //printf("getWiFiConnStatus():ESSID: %s\n",line);
        nullTermStrlCpy(_wifiNetwork->essid, line,ARG_LENGTH); //Copy value to struct
        free(line); //deallocate memory
    } else {
        printf("getWiFiConnStatus(): malloc()-'ESSID'\n");
        return -1;
    }
    //The rest of the parameters are tricky as different versions of
    //iwconfig seem to format the data differently.
    //The following is best effort....

    //Get Signal quality parameter
    startPos = strstr(cmdResponse, "Link Quality"); //Get initial start point
    if (startPos != NULL) {
        startPos = strstr(startPos, "="); //Search for ' = '
        if (startPos != NULL) {
            startPos += 1; //Go to 1 char beyond ' = '
            endPos = strstr(startPos, "/"); //Find next ' / ' delimiter
            if (endPos != NULL) {
                length = endPos - startPos; //Get length of string to extract
                line = (char*) malloc(length + 1); //Allocate memory for a line of text
                if (line != NULL) {
                    bzero(line, (length + 1)); //Clear newly allocated memory
                    if (length < ARG_LENGTH) //Don't run off the end of the array
                        nullTermStrlCpy(line, startPos, length); //Copy value to line
                    //printf("getWiFiConnStatus():Quality: %s\n", line);
                    //strcpy(_wifiNetwork->essid,line);   //Copy value to struct
                    _wifiNetwork->sigQuality = (int) strtol(line, &ptr, 10); //Copy numeric string to an int
                    free(line); //deallocate memory
                } else {
                    printf("getWiFiConnStatus(): malloc()-'Sig Quality'\n");
                }
            }
        }
    }

    //Get Signal Strength parameter
    startPos = strstr(cmdResponse, "level"); //Get initial start point
    if (startPos != NULL) {
        startPos = strstr(startPos, "="); //Search for ' = '
        if (startPos == NULL) {
            startPos = strstr(cmdResponse, "level"); //Reacquire  initial start po
            if (startPos != NULL) {// Some versions of iwconfig use ':' so search for ' : ' instead
                startPos = strstr(startPos, ":");
                //printf(": found!\n");
            }
        }
        if (startPos != NULL) {
            startPos += 1; //Go to 1 char beyond ' = ' of ':'

            //Note allow for different versions of iwconfig. Some use dBm, some x/100
            endPos = strstr(startPos, "dBm"); //First try using trailing ' dbm ' as delimiter
            if (endPos == NULL)endPos = strstr(startPos, "/"); //If that fails, use trailing ' dbm ' as delimiter
            if (endPos == NULL)endPos = strstr(startPos, " "); //If that fails, use trailing ' space ' as delimiter
            if (endPos != NULL) { //If successfully delimited

                length = endPos - startPos; //Get length of string to extract
                //printf("getWiFiConnStatus():Length:%d\n", (int) length);
                line = (char*) malloc(length + 1); //Allocate memory for a line of text
                if (line != NULL) {
                    bzero(line, (length + 1)); //Clear newly allocated memory
                    if (length < ARG_LENGTH) //Don't run off the end of the array
                        strncpy(line, startPos, length); //Copy value to line
                    //printf("getWiFiConnStatus():Sig Level: %s\n", line);
                    //strcpy(_wifiNetwork->essid,line);   //Copy value to struct
                    _wifiNetwork->sigLevel = (int) strtol(line, &ptr, 10); //Copy numeric string to an int
                    free(line); //deallocate memory
                } else {
                    printf("getWiFiConnStatus(): malloc()-'Sig Level'\n");
                }
            }
        }
    }
    return 1;

}

int iwscanWrapper(wifiNetwork _wifiNetwork[], int sizeOfwiFiStruct, char interface[]) {
    /*
     * Executes the system iwlist command on the supplied interface (eg "wlan0")
     * Returns the no. of wireless networks founds and populates 
     * the supplied wifiNetwork strct with the SSIDs of those 
     * networks
     * 
     * Sample usage:-
     *      int k;
     *      wifiNetwork networkList[50];
     *      for (k = 0; k < 50; k++)
     *          initWiFiNetworkStruct(&networkList[k]);
     *          int noOfNetworksFound = iwscanWrapper(networkList, 50, "wlan0");
     *      if (k > 0)
     *          for (k = 0; k < noOfNetworksFound; k++)    
     *              printf("%d: %s\n", k, networkList[k].essid);
     */
    char cmdString[100] = {0};
    snprintf(cmdString, 100,"sudo iwlist %s scan", interface); //Construct command string
    int cmdResponseSize; //Holds the length of the response from sysCmd
    int noOfNetworksFound = 0;
    char cmdResponse[30000] = " "; //iwlist returns lots of data!
    cmdResponseSize = sysCmd2(cmdString, cmdResponse, 30000);
    //printf("cmdResponseSize: %d\n", cmdResponseSize);
    //printf("%s\n", cmdResponse);


    //Now try parsing the output string
    int n = 0; //Used to index through the _wifiNetworks parameter array
    char *line, *startPos, *endPos, *ptr, *startOfCell;

    unsigned int length;

    startOfCell = strstr(cmdResponse, "Cell"); //Get initial start point of the detected networks
    if (startOfCell != NULL) {

        do {
            startPos = startOfCell; //Want to start from each 'cell' instance each time
            //Because depending upon which version of wifitools
            //is installed determines the order in which
            //parameters are returned
            //This line stores the start point of each 'cell' within
            //the output of iwscan
            //Step1: Get Retrieve signal Quality
            startPos = strstr(startPos, "Quality"); //Find next instance of Quality
            if (startPos == NULL) break;
            startPos = strstr(startPos, "="); //Scan along to next 'equals sign'. 
            if (startPos == NULL) break;
            startPos += 1; //Scan along to 1 char after  'equals sign'.
            //Actual quality value immediately follows (hence the +1)
            endPos = strstr(startPos, "/"); //value is framed by an '/' character
            if (endPos == NULL) break;
            length = endPos - startPos;
            //printf("Length:%d\n",length);
            line = (char*) malloc(length + 1); //Allocate memory for a line of text
            if (line != NULL) { //If memory successfully allocated
                bzero(line, (length + 1)); //Clear newly allocated memory
                if (length < ARG_LENGTH) //Don't run off the end of the array
                    strncpy(line, startPos, length); //Copy value to line
                int intValue = (int) strtol(line, &ptr, 10); //Copy numeric string to an int
                //printf("Quality: %d,%s,%d\n", n, line, intValue);
                _wifiNetwork[n].sigQuality = (int) strtol(line, &ptr, 10); //Copy numeric string to an int
                free(line); //deallocate memory
            } else {
                printf("iwscanWrapper(): malloc()-'Quality'\n");
                return -1;
            }
            //Step2: Get Retrieve signal Strength
            startPos = startOfCell; //Want to start from each 'cell' instance each time
            startPos = strstr(endPos, "Signal level"); //Find next instance of 'Signal level'
            if (startPos == NULL) break;
            startPos = strstr(startPos, "="); //Scan along to next 'equals sign'.
            if (startPos == NULL) break;
            startPos += 1; //Scan along to 1 char after  'equals sign'.
            //Important: Depending upon which version of wifitools installed, iwscan will return
            //either 'Signal level=xdBm' or 'Signal level=x/100'
            //Need to be able to handle both
            endPos = strstr(startPos, "dBm");
            if (endPos == NULL) endPos = strstr(startPos, "/");
            if (endPos == NULL) break;
            length = endPos - startPos;
            //printf("Length:%d\n",length);
            line = (char*) malloc(length + 1); //Allocate memory for a line of text
            if (line != NULL) {
                bzero(line, (length + 1)); //Clear newly allocated memory
                if (length < ARG_LENGTH) //Don't run off the end of the array
                    strncpy(line, startPos, length); //Copy value to line
                //int intValue = (int) strtol(line, &ptr, 10); //Copy numeric string to an int
                //printf("Strength: %d,%s,%d\n", n, line, intValue);
                _wifiNetwork[n].sigLevel = (int) strtol(line, &ptr, 10); //Copy numeric string to an int
                free(line); //deallocate memory
            } else {
                printf("iwscanWrapper(): malloc()-'Strength'\n");
                return -1;
            }
            //Step3: Get Encryption status 
            startPos = startOfCell; //Want to start from each 'cell' instance each time
            startPos = strstr(startPos, "Encryption key"); //Find next instance of 'Encryption key'
            if (startPos == NULL) break;
            startPos = strstr(startPos, ":"); //Scan along to next 'colon'.
            if (startPos == NULL) break;
            startPos += 1; //Scan along 1 char after colon
            endPos = strstr(startPos, "\n"); //Delimit at end of line
            if (endPos == NULL) break;
            length = endPos - startPos;
            //printf("Status:%d\n",length);
            line = (char*) malloc(length + 1); //Allocate memory for a line of text
            if (line != NULL) {
                bzero(line, (length + 1)); //Clear newly allocated memory
                if (length < ARG_LENGTH) //Don't run off the end of the array
                    strncpy(line, startPos, length); //Copy value to line
                //printf("Encryption: %d,%s\n", n, line);
                nullTermStrlCpy(_wifiNetwork[n].encryption, line,ARG_LENGTH); //Copy retrieved value to supplied struct
                free(line); //deallocate memory
            } else {
                printf("iwscanWrapper(): malloc()-'Encryption'\n");
                return -1;
            }

            //Step4: Retreive ESSID
            startPos = startOfCell; //Want to start from each 'cell' instance each time
            startPos = strstr(startPos, "ESSID:"); //Find next instance of 'ESSID'
            if (startPos == NULL) break;
            startPos = strstr(startPos, "\""); //Scan along to next 'double quote'.
            if (startPos == NULL) break;
            startPos += 1;
            //iwscan bounds the SSID label with double quotes which have to be stripped off
            //It's entirely possible that the SSID might have a 'double quote' in it,
            //Therefore it's safer to delimit at the end of the line.
            //Imperically, then backing off from the end of the line by 1 character
            //strips of the last quote of the SSID value
            endPos = strstr(startPos, "\n"); //Delimit at end of line
            if (endPos == NULL) break;
            length = endPos - startPos;
            //printf("SSID length:%d\n",(int)length);
            line = (char*) malloc(length + 1); //Allocate memory for a line of text
            if (line != NULL) {
                bzero(line, (length + 1)); //Clear newly allocated memory
                if (length < ARG_LENGTH) //Don't run off the end of the array
                    strncpy(line, startPos, (length - 1)); //Copy value to line (but strip off trailing ' " '  
                //printf("ESSID: %d,%s\n", n, line);
                nullTermStrlCpy(_wifiNetwork[n].essid, line,ARG_LENGTH); //Copy retrieved value to supplied struct
                free(line); //deallocate memory
            } else {
                printf("iwscanWrapper(): malloc()-'ESSID'\n");
                return -1;
            }


            //All done
            startOfCell = strstr((startOfCell + 1), "Cell"); //Get next start point, ready for next time round the loop
            n++; //Increment counter
        } while ((startOfCell != NULL) && (n < sizeOfwiFiStruct));
    } else return 0;
    //return noOfNetworksFound;
    return n; // return noOfNetworksFound
}

int ifconfigSetNICStatus(char interface[], int status) {
    /*
     * Wrapper for ifconfig. Sets an interface up (status=1) or down (status=0).
     * 
     * Returns 1 on success, -1 on fail
     */
    char cmdResponse[1024] = " ";
    char cmdString[1024];
    int ret = 0;
    if (strlen(interface) < 1023) { //(Check argument length. length-1 because of eol character)
        if (status > 0) snprintf(cmdString,1024, "ifconfig %s up", interface); //Set interface up, redirect errors to stdout
        else snprintf(cmdString, 1024, "ifconfig %s down", interface); //Set interface down redirect errors to stdout
        printf("cmdString: %s\n", cmdString);
        ret = sysCmd2(cmdString, cmdResponse, 1024); //Execute the command
        //printf("ifconfigSetNICStatus(): ret %d, %s\n",ret,cmdResponse);
        return 1;
    } else {
        printf("ifconfigSetNICStatus(): interface argument too long\n");
        return -1;
    }
}

int ifConfigSetNic(nic *_nic) {
    /*
     * Sets the ip address, mask etc... of the specified interface
     * 
     * Wrapper for system command 'ifconfig eth0 172.16.25.125 netmask 255.255.255.224 2>&1'
     */

    //First, parse the supplied addresses to check that they're sane
    //Now actually parse supplied address to make sure it makes sense
    struct sockaddr_in IPAddr;
    IPAddr.sin_family = AF_INET;
    if (inet_aton(_nic->address, &IPAddr.sin_addr.s_addr) == 0) { //Extract ip address
        printf("ifConfigSetNic(): Invalid IP address supplied.\n");
        return -1;
    }
    if (inet_aton(_nic->address, &IPAddr.sin_addr.s_addr) == 0) { //Extract ip address
        printf("ifConfigSetNic(): Invalid subnet mask supplied.\n");
        return -1;
    }
    if (strlen(_nic->name) > 20) {
        printf("ifConfigSetNic(): Suspiciously named interface. >20 chars in length.\n");
        return -1;
    }

    char cmdString[1024] = {0};
    snprintf(cmdString, 1024,"ifconfig %s %s netmask %s 2>&1", _nic->name, _nic->address, _nic->netmask); //Construct command string
    printf("cmdString: %s\n", cmdString);
    int cmdResponseSize; //Holds the length of the response from sysCmd
    char cmdResponse[10000] = " "; //Large buffer for returned data (just in case))
    cmdResponseSize = sysCmd2(cmdString, cmdResponse, 10000); //Execute the command
    if (cmdResponseSize < 1) {
        _nic->configured = 1; //No response from ifconfigs signals that the ip address has been set
        return 1;
    } else return 0;
}

int addGateway(char gatewayAddress[]) {
    /*
     * Wrapper for the system command 'route add default gw x.x.x.x' 
     * 
     * returns -1 if the supplied address looks wierd
     */
    int length = strlen(gatewayAddress); //Get length of supplied string for bounds checking
    if (length > 16) { //abc.def.ghi.jkl -15 chars plus one for the eol character
        printf("addGateway(): Supplied address looks too long: %d\n", length);
        return -1;
    }
    //Now actually parse supplied address to make sure it makes sense
    struct sockaddr_in IPAddr;
    IPAddr.sin_family = AF_INET;
    if (inet_aton(gatewayAddress, &IPAddr.sin_addr.s_addr) == 0) { //Extract ip address
        printf("addGateway(): Invalid IP address supplied.\n");
        return -1;
    }

    char cmdString[100] = {0};
    snprintf(cmdString, 100,"route add default gw %s", gatewayAddress); //Construct command string
    int cmdResponseSize; //Holds the length of the response from sysCmd
    char cmdResponse[10000] = " "; //Large buffer for returned data (just in case))
    cmdResponseSize = sysCmd2(cmdString, cmdResponse, 10000); //Execute the command
    printf("addGateway(): %s\n", gatewayAddress);
    return 1;
}

int getGateway(char gatewayAddress[], int arraySize) {
    /*
     * Wrapper for the system command 'route -n'
     * 
     * Returns the current default gateway (the first listed) as an an array of chars by populating the supplied array.
     * 
     * Executes the system 'route -n' command and searches the first line starting with '0.0.0.0'
     * (which implies the gateway). It then retrieves the subsequent gateway ip address
     * 
     * Sample output of 'route -n' looks like this:-
     * 
     * Kernel IP routing table
     * Destination     Gateway         Genmask         Flags Metric Ref    Use Iface
     * 0.0.0.0         192.168.3.1     0.0.0.0         UG    0      0        0 wlan0      ***Only this first line will be parsed***
     * 0.0.0.0         192.168.3.1     0.0.0.0         UG    0      0        0 eth0
     * 127.0.0.1       0.0.0.0         255.255.255.255 UH    0      0        0 lo
     * 192.168.3.0     0.0.0.0         255.255.255.0   U     0      0        0 eth0
     * 192.168.3.0     0.0.0.0         255.255.255.0   U     0      0        0 wlan0
     * 
     * strstr() is used for the main searching within the output string. However, we don't know how long
     * the gateway address will be (i.e the no of actual digits) and what those numbers will be. Since C 
     * doesn't have a wildcard search, the easiest way is to scrobble along the (unknown length) whitespace
     * before the gateway address, and then search a delimiting whitespace character after the address.
     * 
     * strstr() returns a memory location. However, we need an array location hence the line:- 
     * 
     * unsigned int arrayPosition=startPos-cmdResponse; 
     * 
     * Once we've found the start of the actual address we convert the array position back to a pointer with 
     * startPos=&cmdResponse[arrayPosition]; so that we can once again use strstr()
     */

    char cmdString[100] = {0};
    snprintf(cmdString, 100,"route -n"); //Construct command string
    int cmdResponseSize; //Holds the length of the response from sysCmd
    char cmdResponse[10000] = " "; //Large buffer for returned data (just in case))
    //printf("getGateway():cmdString: %s\n", cmdString);
    cmdResponseSize = sysCmd2(cmdString, cmdResponse, 10000); //Execute the command
    //printf("getGateway(): %d:%s\n", cmdResponseSize, cmdResponse);
    //Now try parsing the output string
    char *line, *startPos, *endPos, *endOfLineOfInterest;
    unsigned int length;

    startPos = strstr(cmdResponse, "\n0.0.0.0"); //Get initial start point - search for \n0.0.0.0 is EXACTLY what we need
    if (startPos == NULL) return -1; //If not found, can't trust the output of this command

    startPos = strstr(startPos, " "); //Get initial whitespace delimiter
    if (startPos == NULL) return -1; //If not found, can't trust the output of this command

    //Only interested in the top line, otherwise get false positives,

    unsigned int arrayPosition = startPos - cmdResponse; //Convert strstr() mem pointer to an aray position)

    while ((cmdResponse[arrayPosition] == ' ')&&(arrayPosition < 10000)) { //Scroll through white-space
        //printf("Value at %u:%c\n", arrayPosition, cmdResponse[arrayPosition]);
        arrayPosition++;
    }

    startPos = &cmdResponse[arrayPosition]; //convert array position back to mem position
    endPos = strstr(startPos, " "); //gateway delimited by a proceeding space
    if (endPos == NULL) return -1;
    length = endPos - startPos;
    if (length > (arraySize - 1)) {
        printf("getGateway(): Supplied char array not large enough to hold gateway address string)\n");
        return -1;
    }

    if (length < (arraySize - 1)) strncpy(gatewayAddress, startPos, length); //Safely Copy gateway addr to supplied char array
    else return -1;
    gatewayAddress[length] = 0;
    //printf("getGateway(): %u: %s\n", length, gatewayAddress);
    return 1;
}

int removeAllGateways() {
    /*
     * Wrapper for the system command 'route del default 2>&1' 
     * 
     * If executed successfully, (ie a default route is removed) this command will
     * return nothing. If there is no default route in the system, Linux will typically 
     * respond with something like 'SIOCDELRT: No such process'
     * 
     * Since it's useful to remove all gateways in one go, this command runs 
     * 'route del default 2>&1' in a loop until an output is detected. At that point
     * it can be inferred that all default routes have been removed.
     * 
     * The function returns the no. of routes deleted
     */
    char cmdString[100] = {0};
    snprintf(cmdString, 100,"route del default 2>&1"); //Construct command string
    int cmdResponseSize; //Holds the length of the response from sysCmd
    char cmdResponse[10000] = " "; //Large buffer for returned data (just in case))
    int noOfDeletions = 0;
    do {
        cmdResponseSize = sysCmd2(cmdString, cmdResponse, 10000); //Execute the command
        //printf("cmdResponseSize %d\n", cmdResponseSize);
        if (cmdResponseSize < 1)noOfDeletions++;
    } while (cmdResponseSize == 0); //Keep looping until all default routes removed
    printf("%d routes removed\n", noOfDeletions);
    return noOfDeletions;
}

int ifconfigGetNicStatus(nic *_nic) {
    /*
     * Wrapper for system command 'ifconfig [interface] where interface ='wlan0', 'eth0' etc
     * 
     * Populates the supplied struct with the network parameters
     * Returns 0 on if down, 1 on if up, -1 on error
     * 
     * A typical response from ifconfig is:-
     * 
     * eth0      Link encap:Ethernet  HWaddr 00:E0:4C:36:18:14  
     *     inet addr:192.168.3.10  Bcast:192.168.3.255  Mask:255.255.255.0
     *     UP BROADCAST RUNNING MULTICAST  MTU:1500  Metric:1
     *     RX packets:103882 errors:0 dropped:115 overruns:0 frame:0
     *     TX packets:15159 errors:0 dropped:0 overruns:0 carrier:0
     *     collisions:0 txqueuelen:1000 
     *     RX bytes:7130781 (6.7 MiB)  TX bytes:710673 (694.0 KiB)
     */

    //printf("inputString: %s\n",inputString);
    char cmdString[100] = {0};
    snprintf(cmdString, 100,"ifconfig %s", _nic->name); //Construct command string
    int cmdResponseSize; //Holds the length of the response from sysCmd
    int noOfNetworksFound = 0;
    char cmdResponse[10000] = " "; //iwlist returns lots of data!
    cmdResponseSize = sysCmd2(cmdString, cmdResponse, 10000); //Execute the command
    //printf("cmdResponse: %s\n", cmdResponse);
    //Now try parsing the output string
    char *line, *startPos, *endPos, *ptr;
    unsigned int length;



    //First check interface is UP
    startPos = strstr(cmdResponse, _nic->name); //Get initial start point - search for name of interface
    if (startPos == NULL) return -1; //Can't trust output of ifconfig (perhaps no interface with that name?)

    startPos = strstr(startPos, "UP"); //Search for 'UP' string
    if (startPos == NULL) {
        _nic->status = 0; //If not found, assume interface exists, but it's DOWN
        return 0;
    } else _nic->status = 1; //If present, interface is up

    //Now go back to start of string
    startPos = strstr(cmdResponse, _nic->name); //Get initial start point (based on name of iface we're interested in)
    if (startPos == NULL) return -1; //Interface with that name not found. Shouldn't happen!

    startPos = strstr(startPos, "inet addr"); //Search for 'inet addr
    if (startPos == NULL) return 0; //Interface exists, but inet address not set

    startPos = strstr(startPos, ":"); //Start delimited by a colon
    if (startPos == NULL) return 0;
    startPos += 1;
    endPos = strstr(startPos, "  Bcast"); //End delimited by two spaces
    if (endPos == NULL) return 0; //If no sensible delimit detected
    length = endPos - startPos; //Get length of string to extract
    if (length < (ARG_LENGTH - 1)) strncpy(_nic->address, startPos, length); //Safely Extract interface address
    else return -1;
    _nic->address[length] = 0; //Need to re 'null terminate the string' (because we chopped it up)

    startPos = strstr(startPos, "  Bcast"); //Search for broadcast address
    if (startPos == NULL) return 0; //Interface exists, but bcast address not set
    startPos = strstr(startPos, ":"); //Colon delimiter
    if (startPos == NULL) return 0;
    startPos += 1;
    endPos = strstr(startPos, "  Mask"); //End delimited by two spaces
    if (endPos == NULL) return 0;
    length = endPos - startPos; //Get length of string to extract
    if (length < (ARG_LENGTH - 1)) strncpy(_nic->broadcastAddress, startPos, length);
    else return -1;
    _nic->broadcastAddress[length] = 0; //Need to re 'null terminate the string' (because we chopped it up)

    startPos = strstr(startPos, "  Mask"); //Search for broadcast address
    if (startPos == NULL) return 0; //Interface exists, but mask address not set
    startPos = strstr(startPos, ":"); //Colon delimiter   
    if (startPos == NULL) return 0;
    startPos += 1;
    endPos = strstr(startPos, "\n"); //End delimited by lf
    if (endPos == NULL) return 0;
    length = endPos - startPos; //Get length of string to extract
    if (length < (ARG_LENGTH - 1)) strncpy(_nic->netmask, startPos, length);
    else return -1;
    _nic->netmask[length] = 0; //Need to re 'null terminate the string' (because we chopped it up)
    return 1;

}

void *webServerThread(void *arg) {
    /*
     * Simple web server. Listens on supplied port
     * Really good tutorial here:
     * http://www.tutorialspoint.com/unix_sockets/socket_server_example.htm
     */
    //Cast the incoming thread input variable (arg)as a pointer to an int
    int portno = *((int*) arg); //Tale local copy of arg
    free(arg); //Free up memory requested by malloc by calling thread
    printf("Supplied port no: %d\n", portno);
    /*
        char *response
                = "HTTP/1.1 200 OK\n"
                "Content-Type: text/html: charset=utf-8\n\n"
                "<!DOCTYPE html>"
                "<HTML>"
                "<HEAD>"
                "<TITLE>James's Webserver</TITLE>"
                "</HEAD>"
                "<BODY> Hello, I am James's Raspberry Pi at " "+ ipAddress +" " <br>"
                "Thanks for Visiting.<br><br>"
                "The time is " "+ dateString +" "<br>"
                "<FORM>\n"
                "<INPUT TYPE=\"button\" VALUE=\"Clickable link button\" onClick=\"parent.location='http://" "+ ipAddress +" ":8042'\">"
                "</FORM>"
                "<form name=\"input\"  method=\"get\">\n"
                "Type Something: <input type=\"text\" name=\"myField\">\n"
                "<input type=\"submit\" value=\"Submit\">\n"
                "</form>"
                "<br>"
                "<a href=\"http://" "+ ipAddress +" ":8042/UP\">Up</a><br>"
                "<a href=\"http://" "+ ipAddress +" ":8042/DOWN\">Down</a><br>"
                "<a href=\"http://" "+ ipAddress +" ":8042/LED_TOGGLE\">Toggle LED</a><br>"
                //"<IMG SRC=\"pi_logo.jpg\" ALT=\"some text\" WIDTH=32 HEIGHT=32>"
                "You previously clicked " "+ cmdReceived +" "<br>"
                "The current temperature is: " "+ currentTemperature + " "C"
                "</BODY>"
                "</HTML>";
     */
    char *response = "HTTP/1.1 200 OK\n"
            "Content-Type: text/html\r\n\n"
            "<html><body><H1>Hello world</H1></body></html>\n\0";


    printf("%s\n", response);
    //Create TCP socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0); //Specify 'Reliable'
    if (sockfd < 0) {
        perror("socket()");
        //return -1;
    }
    /* Initialize socket structure */
    struct sockaddr_in serv_addr, cli_addr;
    bzero((char *) &serv_addr, sizeof (serv_addr)); //Clear memory beforehand
    //portno = 5001;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    /* Now bind the host address using bind() call.*/
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof (serv_addr)) < 0) {
        perror("ERROR on binding");
        //return -1;
    }

    int newsockfd, clilen;
    char buffer[1024];

    while (1) {
        /* Now start listening for the clients, here process will
         * go in sleep mode and will wait for the incoming connection
         */
        printf("webServerThread():Waiting for data...\n");

        listen(sockfd, 5); //This is a blocking call
        clilen = sizeof (cli_addr);

        /* Accept actual connection from the client */
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) {
            perror("ERROR on accept");
            //return -1;
        }
        /* If connection is established then start communicating */
        memset(buffer, 0, 1024);
        int n = read(newsockfd, buffer, 1024);

        if (n < 0) {
            perror("ERROR reading from socket");
            //return -1;
        }
        printf("Here is the message: %s\n", buffer);

        /* Write a response to the client */
        n = write(newsockfd, response, strlen(response));
        printf("%d chars written\n", n);
        if (n < 0) {
            perror("ERROR writing to socket");
            //return -1;
        }
        close(newsockfd);
    }
    return 0;
}

int readFile(char output[], int outputLength, char fileName[]) {

    /*
     * Simple utility function.
     * Reads a file. Returns -1 on error, otherwise returns the number of chars read
     * 
     * Populates the supplied char buffer. Tests whether the buffer is large enough
     */
    FILE *pFile; //Create pointer to a file   
    //char* buffer;
    size_t result;
    long lSize;

    //pFile = fopen("/home/pi/wpa.conf2a","r");
    pFile = fopen(fileName, "r");
    if (pFile == NULL) {
        fputs("readFile():File error", stderr);
        return -1;
    }

    // obtain file size:
    fseek(pFile, 0, SEEK_END); //Scroll to pointer to end of file position
    lSize = ftell(pFile); //Get file size based on position of end of file 
    rewind(pFile); //Rewind file pointer back to start position

    // Copy the file into the buffer:
    if (lSize < (outputLength - 1)) //Check output buffer is large enough
        result = fread(output, 1, lSize, pFile); //Read the file in its entirety
    else {
        printf("readFile(): Supplied buffer not long enough: %d. Needs: %ld\n", outputLength, (lSize + 1));
        return -1;
    }
    if (result != lSize) {
        fputs("Reading error 2", stderr);
        return -1;
    }

    /* the whole file is now loaded in the memory buffer. */
    fclose(pFile);
    return result;
}

/*
int parseWPASupplicantConfig(wifiNetwork networkList[], int listLength, char fileToParse[]) {
    /*
 * ******NOW DEPRECATED. USE parseWPASupplicantConfig2 instead *******
 * 
 * Parses the supplied file and populates the supplied network list
 * 
 * Allows access to a list of previously know networks
 * Returns the no. of networks read from the file or -1 on error
 * 
 * 
     
    unsigned int length = 0; //Holds total length of string to be extracted
    int networkIndex = 0;
    char fileContents[10000] = {0}; //Array to hold contents of wpa.conf file to be read

    int ret = readFile(fileContents, 10000, fileToParse);
    if (ret < 1) return -1; //If readFile returns 0, return


    //Now we've read the supplied file, parse the contents
    char *startPos, *endPos;
    startPos = strstr(fileContents, "network"); //Get initial start point
    if (startPos == NULL) {
        return 0; //No network parameters identified in this file
    }
    do {
        //Get next SSID
        startPos = strstr(startPos, "ssid"); //Get initial start point
        if (startPos == NULL) break;
        startPos = strstr(startPos, "\""); //Start of SSID delimited by ' " '
        if (startPos == NULL) break;
        startPos += 1;
        endPos = strstr(startPos, "\""); //End of SSID delimited by ' " '
        if (endPos == NULL) break;
        length = endPos - startPos;
        if (length < ARG_LENGTH) //Don't run off the end of the array
            strncpy(networkList[networkIndex].essid, startPos, length); //Copy ESSID

        //Now get next psk
        startPos = strstr(startPos, "psk"); //Get initial start point
        if (startPos == NULL) break;
        startPos = strstr(startPos, "\""); //Start of SSID delimited by ' " '
        if (startPos == NULL) break;
        startPos += 1;
        endPos = strstr(startPos, "\""); //End of SSID delimited by ' " '
        if (endPos == NULL) break;
        length = endPos - startPos;
        if (length < ARG_LENGTH) //Don't run off the end of the array
            strncpy(networkList[networkIndex].passPhrase, startPos, length); //Copy passphrase

        networkIndex++; //Increment counter   
        startPos = strstr(startPos, "network"); //Get start pos    
    } while ((startPos != NULL)&(networkIndex < listLength)); //Don't run off end of array


    return networkIndex; //Return no. of networks identified in file
}
 */
int parseWPASupplicantConfig2(wifiNetwork networkList[], int listLength, char fileToParse[]) {
    /*
     * Parses the supplied file and populates the supplied network list
     * 
     * Allows access to a list of previously know networks
     * Returns the no. of networks read from the file or -1 on error
     * 
     * This (version 2) can handle wpa conf files for open networks (i.e no pasphrase).
     * 
     * A network block with a passphrase should look like this:-
     * 
     * network={
     *      ssid="VodafoneConnect95919722"
     *      psk="cj4h4bkwl4h58jv"
     * }
     * 
     * A network block for an open (unsecured network) should look like this
     * 
     * network={
     *      ssid="Salford Hall Hotel"
     *      proto=RSN
     *      key_mgmt=NONE
     * }
     * 
     * If the phrase "psk=" isn't detected within a network block, the function
     * assumes no passphrase is present and writes " " int the passphrase field
     * 
     * Sample Usage:
     *      wifiNetwork _wifiNetwork[10];               //Create array of structs
     *      int n;
     *      for (n = 0; n < 10; n++) initWiFiNetworkStruct(&_wifiNetwork[n]);   //init the array
     *      int ret = parseWPASupplicantConfig2(_wifiNetwork, 10, "filename");  //Parse supplied file, populate array
     *      printf("no of network blocks found:%d\n", ret);         //Capture return value
     *      for (n = 0; n < ret; n++)
     *      printf("%d: ESSID: %s\t psk: %s\n", n, _wifiNetwork[n].essid, _wifiNetwork[n].passPhrase); //Print retrieved ssid/psk values
     * 
     */
    unsigned int length = 0; //Holds total length of string to be extracted
    int networkIndex = 0;
    char fileContents[10000] = {0}; //Array to hold contents of wpa.conf file to be read

    int ret = readFile(fileContents, 10000, fileToParse);
    if (ret < 1) return -1; //If readFile returns 0, return
    //strcpy(fileContents, "network={\n\tssid=\"VodafoneConne\"ct95919722\"\n\tpsk=\"cj4h4bkwl4h58jv\"\n}");

    //Now we've read the supplied file, parse the contents
    char *startPos, *endPos;
    char *startofNetworkBlock, *endOfNetworkBlock;
    startofNetworkBlock = strstr(fileContents, "network={"); //Get initial start point
    if (startofNetworkBlock == NULL) {
        return 0; //No network parameters identified in this file
    }
    //char testString[100] = {0};
    //strcpy(testString, "cake");
    while ((startofNetworkBlock != NULL)&&(networkIndex < listLength)) { //Iterate through wpa config file contents
        //Get start of network block
        startofNetworkBlock = strstr(startofNetworkBlock, "{"); //Get initial start point
        if (startofNetworkBlock == NULL) return 0; //No network parameters identified in this file
        startofNetworkBlock++; //Shuffle along one character
        endOfNetworkBlock = strstr(startofNetworkBlock, "\n}");

        //Get end of network block
        //Note: It's entirely possible that the SSID or passphrase might have the "}" character in it,
        //therefore we can't use that alone as a delimiting field. Instead, search for "\n}" (a newline
        //followed by '}')
        endOfNetworkBlock = strstr(startofNetworkBlock, "\n}");
        if (endOfNetworkBlock == NULL) return 0; //malformed file

        //Get next SSID
        //SSIDs and passphrases are awkward. Technically, they can contain the '"' character, therefore
        //we can't use that character is a simple delimit. Instead, we have to work backwards from the '\n'
        startPos = strstr(startofNetworkBlock, "ssid"); //Get initial start point
        if (startPos == NULL) break;
        startPos = strstr(startPos, "\""); //Start of SSID delimited by ' " '
        if (startPos == NULL) break;
        startPos += 1;
        endPos = strstr(startPos, "\n"); //End of SSID line delimited by ' \n '
        if (endPos == NULL) break;
        if (endPos > endOfNetworkBlock) break; //Only interested in results within this network block
        //Now rewind to the last '"' character before the newline
        //

        unsigned int arrayPosition = endPos - fileContents; //Convert strstr() mem pointer to an array position)
        //by subracting mem loc'n of fileContents (which
        //represents the start) from the current endstop.
        //This will yield an integer value that can be used
        //to address the array element corresponding with endPos

        while ((fileContents[arrayPosition] != '\"')&&(&fileContents[arrayPosition] > fileContents)) { //Scroll backwards from \n
            //printf("Value at %u:%c\n", arrayPosition, fileContents[arrayPosition]);
            arrayPosition--;
        }
        //Resultant 'arrayPosition' value should point to the last " before the \n
        length = &fileContents[arrayPosition] - startPos;
        if (length < 1) return -1; //Duff length value
        if (length < ARG_LENGTH) { //Don't run off the end of the array
            strncpy(networkList[networkIndex].essid, startPos, length); //Copy ESSID
        } else return -1;

        //printf("SSID: %s\n", networkList[networkIndex].essid);

        //Now need to retrieve passphrase (if it exists)
        startPos = strstr(startofNetworkBlock, "psk="); //Return to start of current network block
        if ((startPos == NULL) || (startPos > endOfNetworkBlock)) { //No psk field found in this network block
            nullTermStrlCpy(networkList[networkIndex].passPhrase, "\0",ARG_LENGTH); //Copy blank space + null terminator into passPhrase field
            if (!strcmp(networkList[networkIndex].passPhrase, "\0"))
                printf("No passphrase\n");
        } else { //psk field found within current network block
            startPos = strstr(startPos, "\""); //Start of passphrase delimited by '"'
            if ((startPos == NULL) || (startPos > endOfNetworkBlock)) break; //No start delimiter found, or not within network block

            startPos++; //psk field starts 1 char after '"' char
            //Now identify end of psk(passphrase) field.
            //Note '"' is a valid passphrase character, so to delimit end of field, need to work backwards from newline char
            endPos = strstr(startPos, "\n"); //End of SSID line delimited by ' \n '
            if (endPos == NULL) break;
            if (endPos > endOfNetworkBlock) break; //Only interested in results within this network block
            //Now rewind to the last '"' character before the newline
            //

            unsigned int arrayPosition = endPos - fileContents; //Convert strstr() mem pointer to an array position)
            //by subracting mem loc'n of fileContents (which
            //represents the start) from the current endstop.
            //This will yield an integer value that can be used
            //to address the array element corresponding with endPos

            while ((fileContents[arrayPosition] != '\"')&&(&fileContents[arrayPosition] > fileContents)) { //Scroll backwards from \n
                //printf("Value at %u:%c\n", arrayPosition, fileContents[arrayPosition]);
                arrayPosition--;
            }
            //Resultant 'arrayPosition' value should point to the last " before the \n
            length = &fileContents[arrayPosition] - startPos;
            if (length < 1) return -1; //Duff length value
            if (length < ARG_LENGTH) //Don't run off the end of the array
                strncpy(networkList[networkIndex].passPhrase, startPos, length); //Copy ESSID
            else return -1;
            //printf("passphrase: %s\n", networkList[networkIndex].passPhrase);
        }

        networkIndex++; //Increment counter
        startofNetworkBlock = strstr(endOfNetworkBlock, "network={"); //Get  start of next network block
    }

    return networkIndex; //Return no. of networks identified in file
}

int createWPASupplicantConfig(char SSID[], char passPhrase[], char fileToWrite[]) {
    /*
     * This function creates a config file (as used by the wpa_supplicant system
     * command to describe a network SSID and wpa key pair) using the supplied
     * SSID and key.
     * 
     * If the file already contains an SSID of that name, the key is overwritten
     * 
     * It will read in, parse, and then rewrite the supplied filename
     */
    FILE *fp; //Create pointer to a file
    char outputBuffer[1024] = {0};
    fp = fopen(fileToWrite, "r"); //open file for reading (just to test whether it exists already))
    if (fp == NULL) { //Easy, file doesn't exist so we can create a new one from scratch
        printf("%s doesn't exist, writing...\n", fileToWrite);
        fp = fopen(fileToWrite, "w+"); //Open file for reading and writing. Truncate to zero first
        if (fp == NULL) {
            perror("createWPASupplicantConfig().fopen(): (file doesn't exist) Error creating file");
            return -1;
        }
        //Create file from scratch
        //Check to see whether a passphrase has been supplied (trying to match a zero length string)
        if (!strcmp(passPhrase, "\0")) //Is passphrase empty?
            snprintf(outputBuffer,1024, "\nnetwork={\n\tssid=\"%s\"\n\tproto=RSN\n\tkey_mgmt=NONE\n}\n", SSID); //Format string
        else //Passhphrase has been supplied
            snprintf(outputBuffer, 1024,"network={\n\tssid=\"%s\"\n\tpsk=\"%s\"\n}\n", SSID, passPhrase); //Format string

        printf("%s", outputBuffer);

        fprintf(fp, outputBuffer); //Write string to disk
        fclose(fp); //Close file
        printf("%s created. ESSID %s added\n", fileToWrite, SSID);
        return 1;

    } else {
        //File already exists, so read contents first, check for existence of supplied SSID
        //If it exists, overwrite the key element,
        //if not, append a new 'network' block to the file with the new ssid/key pair

        //1)Close file
        //2)Parse wpa conf file into an array of WiFiNetwork structs using parseWPASupplicantConfig
        //3)Search ssid field for SSID, if it exists, overwrite the key field in the array
        //4)        Write newly modified array to disk (open for writing, write, close)
        //5)Else:   Append new network block to existing file (open for appending, write, close))

        fclose(fp); //Close file 
        wifiNetwork networkList[100]; //Create network list array (will store parsed SSIDs and keys))
        int n, k;
        for (n = 0; n < 100; n++) initWiFiNetworkStruct(&networkList[n]); //Init the array
        int m = parseWPASupplicantConfig2(networkList, 100, fileToWrite); //Parse the supplied conf file
        if (m > 0) { //If sensible data successfully parsed. 'm' network blocks retrieved
            for (n = 0; n < m; n++) { //Iterate through the key field looking for the supplied SSID
                if (!strcmp(networkList[n].essid, SSID)) {
                    //If SSID matched, strcmp() returns 0, and the following code executes
                    printf("ESSID %s already exists in file %s. passPhrase changed from %s", SSID, fileToWrite, networkList[n].passPhrase);
                    //bzero(networkList[n].passPhrase, ARG_LENGTH); //Empty existing passPhrase contents
                    memset(networkList[n].passPhrase, 0, ARG_LENGTH); //Empty existing passPhrase contents
                    nullTermStrlCpy(networkList[n].passPhrase, passPhrase,ARG_LENGTH); //Write supplied passPhrase into array
                    printf(" to %s\n", networkList[n].passPhrase);

                    //////////////NOW write entire array to disk overwriting original file
                    fp = fopen(fileToWrite, "w+"); //Open file for reading and writing. Truncate to zero first
                    if (fp == NULL) {
                        perror("createWPASupplicantConfig().fopen(): (file can't be re-written). Error creating file");
                        return -1;
                    }
                    fprintf(fp, "#Auto generated by createWPASupplicantConfig(). (reconstructed)\n"); //Write string to disk
                    for (n = 0; n < m; n++) { //'m' networks were retrieved. Write them back to disk (reuse variable n))


                        //Check to see whether a passohrase has been supplied (trying to match a zero length string)
                        if (!strcmp(passPhrase, "\0")) //Is passphrase empty?
                            snprintf(outputBuffer, 1024,"\nnetwork={\n\tssid=\"%s\"\n\tproto=RSN\n\tkey_mgmt=NONE\n}\n", networkList[n].essid); //Format string
                        else //Passhphrase has been supplied
                            snprintf(outputBuffer, 1024,"\nnetwork={\n\tssid=\"%s\"\n\tpsk=\"%s\"\n}\n", networkList[n].essid, networkList[n].passPhrase); //Format string

                        fprintf(fp, outputBuffer); //Write string to disk
                    }
                    fclose(fp); //Close file
                    return 1;
                }
            }
        }
        //However, if the code makes it this far...
        /////////ESSID not present but file exists so append new SSID/key to file
        printf("ESSID %s not present in file %s. New network block appended..\n", SSID, fileToWrite);
        fp = fopen(fileToWrite, "a+"); //Open file for appending and reading. 
        if (fp == NULL) {
            perror("createWPASupplicantConfig().fopen(): (file can't be appended). Error creating file");
            return -1;
        }

        time_t currentTime;
        time(&currentTime); //Capture current time, assign it to currentTime
        char timeAsString[100] = {0}; //Buffer to hold human readable time
        strftime(timeAsString, 100, "%H:%M:%S", localtime(&currentTime)); //Format human readable time
        snprintf(outputBuffer, 1024,"#Auto generated by createWPASupplicantConfig(). (appended): at %s\n", timeAsString); //Format string
        //printf(timeAsString);
        fprintf(fp, outputBuffer); //Write string to disk

        //snprintf(outputBuffer, 1024"network={\n\tssid=\"%s\"\n\tpsk=\"%s\"\n}\n", SSID, passPhrase); //Format string
        if (!strcmp(passPhrase, "\0")) //Is passphrase empty?
            snprintf(outputBuffer, 1024,"\nnetwork={\n\tssid=\"%s\"\n\tproto=RSN\n\tkey_mgmt=NONE\n}\n", SSID); //Format string
        else //Passhphrase has been supplied
            snprintf(outputBuffer, 1024,"network={\n\tssid=\"%s\"\n\tpsk=\"%s\"\n}\n", SSID, passPhrase); //Format string

        fprintf(fp, outputBuffer); //Write string to disk
        fclose(fp); //Close file
    }
    return 1;
}

int findESSIDinConfigFile(wifiNetwork networkList[], int listLength, char fileToSearch[], char ESSIDtoMatch[]) {
    /*
     *Searches the supplied WPA config file for a specifi ESSID.
     * If it finds it, it will return the array index no. where the ESSID is first found,
     * If not found, it will return -1;
     * 
     * It will also populate the supplied wifiNetwork[] array with the list of networks found in the file
     * by using the parseWPASupplicantConfig function
     * 
     */
    int n;
    //1) Populate network list as described in fileToSearch
    int noOfNetworks = parseWPASupplicantConfig2(networkList, listLength, fileToSearch);
    printf("findESSIDinConfigFile(): No. of networks: %d\n", noOfNetworks);
    if (noOfNetworks < 1) {
        printf("findESSIDinConfigFile(): Supplied file contains no networks\n");
        return -1;
    }
    printf("Searching for %s\n", ESSIDtoMatch);
    //2)Now iterate through networkList[] array looking for a match with ]
    for (n = 0; n < noOfNetworks; n++) {
        printf("%d, %s\n", n, networkList[n].essid);
        if (!strcmp(networkList[n].essid, ESSIDtoMatch)) {
            printf("Matching ESSID found at array position: %d\n", n);
            return n;
        }
    }
    //3)ESSID not found, so return -1;
    return -1;

}

int deleteESSIDfromConfigFileByName(char fileName[], char ESSIDtoDelete[]) {
    /*
     * Attempts to remove an ESSID/passphrase key pair from the supplied file
     * Currently it can handle a maximum list size of 250, but this seems a crazy 
     * no. of WiFi networks
     */
#define MAX_WIFI_LIST_LENGTH 250
    wifiNetwork networkList[MAX_WIFI_LIST_LENGTH]; //Create network list array (will store SSIDs and keys))

    int elementToDelete, noOfNetworks, n;
    for (n = 0; n < MAX_WIFI_LIST_LENGTH; n++) initWiFiNetworkStruct(&networkList[n]); //Init the array
    //1) Determine if the file contains the supplied ESSID
    noOfNetworks = parseWPASupplicantConfig2(networkList, MAX_WIFI_LIST_LENGTH, fileName); //Determine how many networks are listed
    printf("deleteESSIDfromConfigFile(): No. of networks: %d\n", noOfNetworks);
    if (noOfNetworks < 1) {
        //No networks present in file or file error
        return noOfNetworks;
    }
    elementToDelete = findESSIDinConfigFile(networkList, MAX_WIFI_LIST_LENGTH, fileName, ESSIDtoDelete);


    //2)
    if (elementToDelete != -1) { //If ESSID has been found
        printf("deleteESSIDfromConfigFile() ESSID %s found at array list location %d\n", ESSIDtoDelete, elementToDelete);

        //First, duplicate the old file..
        char cmdString[1024] = {0}; //Array to hold system command
        char cmdResponse[1024] = {0}; //Array to hold response from command
        snprintf(cmdString, 1024,"cp %s %s%s", fileName, fileName, ".backup"); //Format command string 'cp filename filename.backup'
        printf("deleteESSIDfromConfigFile() cmdString: %s\n", cmdString);
        int ret = sysCmd2(cmdString, cmdResponse, 1024); //Execute system command

        if (ret > 0) {
            printf("deleteESSIDfromConfigFile(). Can't create backup of %s\n", fileName);
        }

        //Next rewrite the file with 'no contents'
        FILE *fp; //Create pointer to a file
        char outputBuffer[1024];
        fp = fopen(fileName, "w+"); //Open file for reading and writing. Truncate to zero first (same as deleting the contents)
        if (fp == NULL) {
            printf("deleteESSIDfromConfigFile(): Can't empty %s\n", fileName);
            return -1;
        }
        fclose(fp);
        //Now recreate the file skipping the array element found above
        fp = fopen(fileName, "a+"); //Open file for appending and reading. 
        if (fp == NULL) {
            printf("deleteESSIDfromConfigFile(): Can't append to %s\n", fileName);
            return -1;
        }
        n = 0;
        time_t currentTime;
        time(&currentTime); //Capture current time, assign it to currentTime
        char timeAsString[100]; //Buffer to hold human readable time
        strftime(timeAsString, 100, "%H:%M:%S", localtime(&currentTime)); //Format human readable time
        snprintf(outputBuffer, 1024,"#Auto modified by deleteESSIDfromConfigFile() at %s\n", timeAsString); //Format string
        fprintf(fp, outputBuffer); //Write string to disk

        while (n < noOfNetworks) {
            if (n != elementToDelete) { //If this ESSID is NOT to be skipped, write it to fileName

                if (!strcmp(networkList[n].passPhrase, "\0")) { //Is passphrase field empty?
                    //If so, write a special string to the file to tell wpa_supplicant that the network is open
                    //Should look like: proto=RSN
                    //                  key_mgmt=NONE
                    snprintf(outputBuffer, 1024,"\nnetwork={\n\tssid=\"%s\"\n\tproto=RSN\n\tkey_mgmt=NONE\n}\n", networkList[n].essid); //Format string
                    fprintf(fp, outputBuffer); //Write string to disk
                } else { //Passphrase is present
                    snprintf(outputBuffer, 1024,"\nnetwork={\n\tssid=\"%s\"\n\tpsk=\"%s\"\n}\n", networkList[n].essid, networkList[n].passPhrase); //Format string
                    fprintf(fp, outputBuffer); //Write string to disk
                }
            }

            n++;
        }
        fclose(fp);
        return 1;
    } else { //ESSID has not been found
        printf("deleteESSIDfromConfigFile() ESSID %s not found.\n", ESSIDtoDelete);
        return 0;
    }

}

void flushstdin(void) {
    /*
     * Flushes stdin. Required if using successive scanf() calls, as scanf caches the '\n' when you
     * enter text, which causes subsequent scanf() lines to skip.
     */
    int c;
    while ((c = fgetc(stdin)) != EOF && c != '\n');
}

void testIPTools() {
    nic _nic;
    initNicStruct(&_nic);
    int lastStatus = 0;
    int ssidGen = 0;
    while (1) {

        printf("\n\ng: ifConfigSetNic()\tn: ifconfigSetNICStatus()\th: ifConfigGetNicStatus\n");
        printf("m: start udhcp\tp: release dhcp\tq: quit\n");
        printf("a: addGateway()\te: getGateway()\tk: removeAllgateways()\n");
        printf("b: createWPASupplicantConfig()\tc: deleteESSIDfromConfigFileByName()\td: findESSIDinConfigFile()\n");
        printf("l: startWPASupplicant\to: stop WPASupplicant\t");
        printf("j: iwScanWrapper()\ti: iwScanCountNetworks()f: get getWiFiConnStatus()\n");

        char key = getch();
        printf("\n");

        if (key == 'a') {
            printf("a: addGateway()\n");
            char addr[50] = {0};
            printf("Enter gateway address\n");
            scanf("%s", addr);
            int n = addGateway(addr);
            if (n == 1)printf("Gateway %s added\n", addr);
            else printf("Invalid gateway\n");
        }
        if (key == 'b') {
            printf("b: createWPASupplicantConfig()\n");
            char ssid[50] = {0};
            char passPhrase[100] = {0};
            char fileToWrite[200] = {0};
            printf("Enter SSID: ");
            scanf("%[^\n]s", ssid);
            flushstdin();
            //strcpy(ssid, "BW Salford Hall Hotel");
            printf("\nEnter passphrase: ");
            scanf("%[^\n]s", passPhrase);
            printf("\nEnter fileToWrite: ");
            flushstdin();
            scanf("%[^\n]s", fileToWrite);
            createWPASupplicantConfig(ssid, passPhrase, fileToWrite);


        }
        if (key == 'c') {
            printf("c: deleteESSIDfromConfigFileByName()\n");
            char ssid[50] = {0};
            char fileToWrite[200] = {0};
            printf("Enter SSID: ");
            scanf("%[^\n]s", ssid);
            flushstdin();
            printf("\nEnter fileToWrite\n");
            scanf("%[^\n]s", fileToWrite);
            deleteESSIDfromConfigFileByName(fileToWrite, ssid);
        }
        if (key == 'd') {
            printf("d: findESSIDinConfigFile()\n");
            wifiNetwork _wifiNetwork[50]; //Array to store wireless networks found
            int n;
            for (n = 0; n < 50; n++)
                initWiFiNetworkStruct(&_wifiNetwork[n]); //Clear struct contents
            char ssid[50] = {0};
            char fileToWrite[200] = {0};
            printf("Enter SSID: ");
            scanf("%[^\n]s", ssid);
            flushstdin();
            printf("\nEnter file to read: ");
            scanf("%[^\n]s", fileToWrite);
            n = findESSIDinConfigFile(_wifiNetwork, 50, fileToWrite, ssid);
            if (n>-1) printf("\nESSID %s found at array pos %d\n", ssid, n);
            else printf("\nESSID not found\n");
        }
        if (key == 'e') {
            printf("e: getGateway()\n");
            char addr[50] = {0};
            getGateway(addr, 50);
            printf("gateway addr: %s\n", addr);
        }
        if (key == 'f') {
            printf("f: get getWiFiConnStatus()\n");
            wifiNetwork _wifiNetwork;
            initWiFiNetworkStruct(&_wifiNetwork);
            int n = getWiFiConnStatus(&_wifiNetwork, "wlan0");
            if (n == 1) printf("Connected to %s\n", _wifiNetwork.essid);
            else printf("Not currently connected.\n");
        }
        if (key == 'g') {
            printf("g: ifConfigSetNic()\n");
            nic _nic;
            initNicStruct(&_nic);
            char addr[100] = {0};
            printf("Enter address: ");
            scanf("%s", addr);
            nullTermStrlCpy(_nic.address, addr,ARG_LENGTH);
            char mask[100] = {0};
            printf("\nEnter Mask: ");
            scanf("%s", mask);
            nullTermStrlCpy(_nic.netmask, mask,ARG_LENGTH);

            char name[100] = {0};
            printf("\nEnter Interface Name: ");
            scanf("%s", name);
            nullTermStrlCpy(_nic.name, name,ARG_LENGTH);
            printf("\n");
            int n = ifConfigSetNic(&_nic);
            if (n == 0) printf("Couldn't configure nic: %s, %s, %s", _nic.name, _nic.address, _nic.netmask);
        }
        if (key == 'h') {
            printf("h: ifConfigGetNicStatus\n");
            nic _nic;
            initNicStruct(&_nic);
            char name[100] = {0};
            printf("Enter interface name:\n");
            scanf("%s", name);
            nullTermStrlCpy(_nic.name, name,ARG_LENGTH);
            int n = ifconfigGetNicStatus(&_nic);
            if (n == 1)printf("Interface %s UP\n", _nic.name);
            printf("%s, %s\n", _nic.address, _nic.netmask);
        }
        if (key == 'i') {
            printf("i: iwScanCountNetworks()\n");
            int x = iwScanCountNetworks();
            printf("iwScanCountNetworks(): %d networks found\n", x);
        }
        if (key == 'j') {
            printf("j: iwScanWrapper()\n");
            int networkIndex;
            wifiNetwork _wifiNetwork[50]; //Array to store wireless networks found
            int n;
            for (n = 0; n < 50; n++)
                initWiFiNetworkStruct(&_wifiNetwork[n]); //Clear struct contents
            networkIndex = iwscanWrapper(_wifiNetwork, 50, "wlan0");

            int z;

            //Display names of networks found
            for (z = 0; z < networkIndex; z++) {
                printf("%d:%s,\tQuality:%d,\tStrength:%ddBm,\tEncryption:%s\n",
                        z, _wifiNetwork[z].essid, _wifiNetwork[z].sigQuality,
                        _wifiNetwork[z].sigLevel, _wifiNetwork[z].encryption);
            }
        }
        if (key == 'k') {
            printf("k: removeAllgateways()\n");
            char gatewayAddress[100] = {0};
            printf("Initial gateway: %s\n", gatewayAddress);
            removeAllGateways();
            bzero(gatewayAddress, 100);
            getGateway(gatewayAddress, 100);
            printf("After removeAllGateway(): %s\n", gatewayAddress);
        }
        if (key == 'l') {
            printf("l: startWPASupplicant\n");
            char name[100] = {0};
            char cmdString[1024] = {0};
            printf("Enter name of wpa conf file\n");
            scanf("%s", name);
            snprintf(cmdString, 1024,"wpa_supplicant -i wlan0 -D nl80211,wext -c %s &", name); //Construct command string
            system("killall wpa_supplicant");
            system(cmdString); //Execute the command
        }
        if (key == 'm') {
            printf("m: start dhclient wlan0\n");
            char name[100] = {0};
            char cmdString[100] = {0};
            printf("Enter interface name\n");
            scanf("%s", name);
            snprintf(cmdString, 100,"dhclient %s", name); //Construct command string
            system(cmdString); //Execute the command
        }
        if (key == 'n') {
            printf("n: ifconfigSetNICStatus()\n");
            if (lastStatus == 0) {
                int n = ifconfigSetNICStatus("wlan0", 1);
                if (n == 1) printf("wlan0 successfully enabled\n");
                lastStatus = 1;
            } else {
                int n = ifconfigSetNICStatus("wlan0", 0);
                if (n == 1) printf("wlan0 successfully disabled\n");
                lastStatus = 0;
            }
        }
        if (key == 'o') {
            printf("o: stop WPASupplicant\n");
            system("killall wpa_supplicant");
        }
        if (key == 'p') {
            printf("p: p: release dhcp\n");
            char name[100] = {0};
            char cmdString[100] = {0};
            printf("Enter interface name:\n");
            scanf("%s", name);
            snprintf(cmdString, 100,"dhclient -r %s", name); //Construct command string
            system(cmdString); //Execute the command

        }

        if (key == 'q') {
            exit(1);
        } else {
            printf("unknown option\n");
            key = '\0';
        }

    }
}

