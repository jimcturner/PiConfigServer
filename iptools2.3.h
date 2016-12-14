/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   iptools2.3.h
 * Author: turnej04
 *
 * Created on July 14, 2016, 7:22 PM
 */

#ifndef IPTOOLS2_0_H
#define IPTOOLS2_0_H

#ifdef __cplusplus
extern "C" {
#endif




#ifdef __cplusplus
}
#endif

//ADD MY OWN STUFF AFTER HERE
//REMEMBER TO ADD: #include "iptools2.0.h" TO THE SOURCE FILE

#define ARG_LENGTH  1024

typedef struct Nic {
    //Struct to describe a network interface
    char name[ARG_LENGTH]; //eg wlan0 etc (normally the same as the system name))
    char address[ARG_LENGTH];
    char netmask[ARG_LENGTH];
    char broadcastAddress[ARG_LENGTH];
    char gateway[ARG_LENGTH];
    int priority;
    int status; //Is interface up or down?
    int configured; //Has the interface been configured yet?
} nic;

typedef struct WiFiNetwork {
    char essid[ARG_LENGTH];
    int sigQuality;
    int sigLevel;
    char passPhrase[ARG_LENGTH];
    char key[ARG_LENGTH];
    int priority;
    char desc[ARG_LENGTH];
    char encryption[ARG_LENGTH];
} wifiNetwork; //Define new 'objects' with 'wifiNetwork', not 'WifiNetwork'

//AND BEFORE HERE
#endif /* IPTOOLS2_0_H */

