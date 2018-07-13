
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include "algorithm_by_RF.h"
#include "max30102.h"


#define DEBUG // Uncomment for debug output to the Serial stream
//#define TEST_MAXIM_ALGORITHM // Uncomment if you want to include results returned by the original MAXIM algorithm
//#define SAVE_RAW_DATA // Uncomment if you want raw data coming out of the sensor saved to SD card. Red signal first, IR second.

#ifdef TEST_MAXIM_ALGORITHM
#include "algorithm.h" 
#endif

File dataFile;
// ADALOGGER pins
const byte chipSelect = 4;
const byte cardDetect = 7;
const byte ledPin = 13; // Red LED on ADALOGGER
const byte sdIndicatorPin = 8; // Green LED on ADALOGGER
const byte oxiInt = 10; // ADALOGGER pin connected to MAX30102 INT

bool cardOK;
uint32_t elapsedTime,timeStart;

uint32_t aun_ir_buffer[BUFFER_SIZE]; //infrared LED sensor data
uint32_t aun_red_buffer[BUFFER_SIZE];  //red LED sensor data
float old_n_spo2;  // Previous SPO2 value
uint8_t uch_dummy,k;
int start = 12;
int reset = 11;
int resetpin = 1;
int incomingbyte=68;   //"D" length of data
int data = 78;   //"N" sends complete data 
int Start=79;   //"O" connection
int Reset=82;  //"R" reset

String recording_time;
String SPO2;
String h_rate;
String comma  = String(',');
String semicolon  = String(';');
String data_string;

char buffer[30];

String string;

int cnnctd = 0;
int send_actual_data = 0;
int reset_n_proceed = 0;
int rec_data;

void millis_to_hours(uint32_t ms, char* hr_str)
{
  char istr[6];
  uint32_t secs,mins,hrs;
  secs=ms/1000; // time in seconds
  mins=secs/60; // time in minutes
  secs-=60*mins; // leftover seconds
  hrs=mins/60; // time in hours
  mins-=60*hrs; // leftover minutes
  itoa(hrs,hr_str,10);
  strcat(hr_str,":");
  itoa(mins,istr,10);
  strcat(hr_str,istr);
  strcat(hr_str,":");
  itoa(secs,istr,10);
  strcat(hr_str,istr);
}

// blink three times if isOK is true, otherwise blink continuously
void blinkLED(const byte led, bool isOK)
{
  byte i;
  if(isOK) {
    for(i=0;i<3;++i) {
      digitalWrite(led,HIGH);
      delay(200);
      digitalWrite(led,LOW);
      delay(200);
    }
  } else {
    while(1) {
      for(i=0;i<2;++i) {
        digitalWrite(led,HIGH);
        delay(50);
        digitalWrite(led,LOW);
        delay(50);
      }
      delay(500);
    }
  }
}


void setup() {

  pinMode(cardDetect,INPUT_PULLUP);
  pinMode(oxiInt, INPUT);  //pin D10 connects to the interrupt output pin of the MAX30102
  pinMode(ledPin,OUTPUT);
  digitalWrite(ledPin,LOW);
  pinMode(sdIndicatorPin,OUTPUT);
  digitalWrite(sdIndicatorPin,LOW);
  pinMode(start,INPUT);
  digitalWrite(start, HIGH);
  pinMode(resetpin, INPUT);
  digitalWrite(resetpin, HIGH);
  pinMode(reset, INPUT);
  digitalWrite(reset, HIGH);
  
  Wire.begin();
 

#ifdef DEBUG
  // initialize serial communication at 115200 bits per second:
  Serial.begin(115200);
#endif

  maxim_max30102_reset(); //resets the MAX30102
  delay(1000);

  maxim_max30102_read_reg(REG_INTR_STATUS_1,&uch_dummy);  //Reads/clears the interrupt status register
  
  char my_status[20];
  
  if(HIGH==digitalRead(cardDetect)) {
    // we'll use the initialization code from the utility libraries
    // since we're just testing if the card is working!
   
    if(!SD.begin(chipSelect)) {
      cardOK=false;
      strncpy(my_status,"CardInit!",9);
    } else cardOK=true;
  } else {
    cardOK=false;
    strncpy(my_status,"NoSDCard!",9);
  }
  

  if(cardOK) {
    long count=0;
    char fname[20];
    do {
//      if(useClock && now.month()<13 && now.day()<32) {
//        sprintf(fname,"%d-%d_%d.txt",now.month(),now.day(),++count);
//      } else {
       sprintf(fname,"data_%d.txt",++count);
//      }
    } while(SD.exists(fname));
    dataFile = SD.open(fname, FILE_WRITE);
    strncpy(my_status,fname,19);
  }

  
  
#ifdef DEBUG

//    Serial.println(my_status);
//    Serial.println(dataFile,HEX);
//    //Serial.println(F("Press any key to start conversion"));
//    delay(1000);
 
  
  uch_dummy=Serial.read();
#endif 

  blinkLED(ledPin,cardOK);
  old_n_spo2=0.0; 
  
#ifdef DEBUG
  Serial.flush();
  Start = Serial.read();
  if(Start != 79)        // wait for an "O"
  {
    while(Start != 79)
    {
      Start = Serial.read();
      
//      Serial.print(F("waiting\n"));
//      delay(1000);
    }
  }
  else
  {
    }
  
#endif
  Serial.print("Y");
 // Serial.flush();
  
  maxim_max30102_init();  //initialize the MAX30102


  k=0;
  dataFile.println(my_status);
#ifdef TEST_MAXIM_ALGORITHM
  dataFile.print(F("Time[s]\tSpO2\tHR\tSpO2_MX\tHR_MX\tClock\tRatio\tCorr"));
#else
  dataFile.print(F("Time[s]\tSpO2\tHR\tClock\tRatio\tCorr"));
#endif

#ifdef SAVE_RAW_DATA
  int8_t i;
  // These are headers for the red signal
  for(i=0;i<BUFFER_SIZE;++i) 
  {
    dataFile.print("\t");
    dataFile.print(i);
  }
  // These are headers for the infrared signal
  for(i=0;i<BUFFER_SIZE;++i) 
  {
    dataFile.print("\t");
    dataFile.print(i);
  }
#endif
  dataFile.println("");
  timeStart=millis();
}





//Continuously taking samples from MAX30102.  Heart rate and SpO2 are calculated every 4 seconds

void loop() 
{  
//#ifdef DEBUG  
  rec_data = Serial.read();
//  Reset=incomingbyte;
//  data=incomingbyte;
  while(rec_data != 82)       // wait for "r"
  {    
    
//    Serial.print(F("\nworking\n"));
//    Serial.print("incomingbyte is: ");
//    Serial.println(incomingbyte);
//    Serial.print("reset is: ");
//    Serial.println(Reset);
//#endif

    float n_spo2,ratio,correl;  //SPO2 value
    int8_t ch_spo2_valid;  //indicator to show if the SPO2 calculation is valid
    int32_t n_heart_rate; //heart rate value
    int8_t  ch_hr_valid;  //indicator to show if the heart rate calculation is valid
    int8_t i;
    char hr_str[10];
       
    //buffer length of 100 stores 4 seconds of samples running at 25sps
    //read 100 samples, and determine the signal range
    
    for(i=0;i<BUFFER_SIZE;i++)
      {
      while(digitalRead(oxiInt)==1);  //wait until the interrupt pin asserts
      maxim_max30102_read_fifo((aun_red_buffer+i), (aun_ir_buffer+i));  //read from MAX30102 FIFO
//  #ifdef DEBUG
//      Serial.print(i, DEC);
//      Serial.print(F("\t"));
//      Serial.print(aun_red_buffer[i], DEC);
//      Serial.print(F("\t"));
//      Serial.print(aun_ir_buffer[i], DEC);    
//      Serial.println("");
//  #endif
    }
  
    //calculate heart rate and SpO2 after 100 samples (4 seconds of samples) using Robert's method
    rf_heart_rate_and_oxygen_saturation(aun_ir_buffer, BUFFER_SIZE, aun_red_buffer, &n_spo2, &ch_spo2_valid, &n_heart_rate, &ch_hr_valid, &ratio, &correl); 
    elapsedTime=millis()-timeStart;
    millis_to_hours(elapsedTime,hr_str); // Time in hh:mm:ss format
    elapsedTime/=1000; // Time in seconds

    //save samples and calculation result to SD card
//  #ifdef TEST_MAXIM_ALGORITHM
//    if(ch_hr_valid && ch_spo2_valid || ch_hr_valid_maxim && ch_spo2_valid_maxim) 
//    {
//  #else   
//    if(ch_hr_valid && ch_spo2_valid) 
//    { 
//  #endif

//      ++k;
//      dataFile.print(elapsedTime);
//      dataFile.print("\t");
//      dataFile.print(n_spo2);
//      dataFile.print("\t");
//      dataFile.print(n_heart_rate, DEC);
//      dataFile.print("\t");
//  #ifdef TEST_MAXIM_ALGORITHM
//      dataFile.print(n_spo2_maxim);
//      dataFile.print("\t");
//      dataFile.print(n_heart_rate_maxim, DEC);
//      dataFile.print("\t");
//  #endif
//      dataFile.print(hr_str);
//      dataFile.print("\t");
//      dataFile.print(ratio);
//      dataFile.print("\t");
//      dataFile.print(correl);

    //  sprintf(buffer, "%d,%f,%d;", elapsedTime,n_spo2,n_heart_rate);

if(Serial.available()>0)
{
       rec_data = Serial.read();
       if(rec_data==68)      // "wait for "D"
      { 
            recording_time = String(elapsedTime,DEC);
      //Serial.print(elapsedTime);
    //  Serial.print(",");
            SPO2 = String(n_spo2,DEC);
  //Serial.print(n_spo2);
 //Serial.print(",");
            h_rate = String(n_heart_rate,DEC);
//  Serial.print(n_heart_rate);
//  Serial.print('\n');

      data_string = String(recording_time+comma+SPO2+comma+h_rate);
      
      Serial.print(data_string.length(),DEC);
      
      while(send_actual_data == 0)
      {
        rec_data = Serial.read();
        
        if(rec_data == 78)
        {
          Serial.print(data_string);
          send_actual_data = 1;
         // Serial.println("-2");
        }
        else if(rec_data == 82)
        {
          send_actual_data = 0;
          Serial.flush();
          setup();
          }
        else
        {
          //Serial.println("0");
          Serial.flush();
        }
      }
          //Serial.println("-1");
      }  

          
    else if(rec_data == 82)
        {
          send_actual_data = 0;
       
          Serial.flush();
          setup();
          }
    else
    {
          send_actual_data = 0;
        //  Serial.println("1");
          Serial.flush();
    }
  }

      
      send_actual_data=0;
  
      dataFile.println("");
      old_n_spo2=n_spo2;
      // Blink green LED to indicate save event
      digitalWrite(sdIndicatorPin,HIGH);
      delay(10);
      digitalWrite(sdIndicatorPin,LOW);
      // FLush SD buffer every 10 points
      if(k>=10) {
        dataFile.flush();
        k=0;
      }
  
  }
}




