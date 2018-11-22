//A étudier: Pour télécharger un fichier HEX: Xloader / AVRDUDESS 

// FOR ARDUINO LEONARDO


#include <Wire.h>
#include <EEPROM.h>

#include "DMXSerial.h"  // http://www.mathertel.de/Arduino
#include "Adafruit_PWMServoDriver.h"      

#include "./PWM_coarse.h"  //Tables générée avec le script perl "perl/SL5CS_exponential_100.pl" et retouchée à la main (début et fin)
#include "./PWM_fine.h"
#include "./PWM_color_fine.h"


//#include <avr/pgmspace.h>  

/*********************************************/
/***/                                     /***/
/***/       #define VERSION "1.00"        /***/
/***/                                     /***/
/*********************************************/

// 22.8.2015 / Version 1.00  / edz / Ajout des table propres, systeme à deu vitesses (fine et coarse) sans mélange entre les deux. Ajout d'un effet lumineu au changement d'address,
//                                 / ajout de __DATE__ et __TIME__ dans le terminal de debug.


// Set the TMP100 Address and Resolution here
int TMP100_Address = B1001000;
int TMP100_ResolutionBits = 12;//9;
float celsius = 20.0;

//The address of the DMX is stored in the EEPROM
#define EEPROM_DMX_ADDRESS 0
//The DMX address we use to confidure the DMX device MSB address (DMX_SETUP_ADDRESS+1 for LSB)
#define DMX_SETUP_ADDRESS 511

/*************************************************************************/
unsigned int DMX_address = 
   EEPROM.read(EEPROM_DMX_ADDRESS)*256 + EEPROM.read(EEPROM_DMX_ADDRESS+1);  

/*************************************************************************/

const int ButtonPin = 4;         // Button input. 
                                 //    1: set Address (value) on DMX Channel DMX_SETUP_ADDRESS.
                                 //    2: push the button and the new address is set.
#define buttonPushed !digitalRead(ButtonPin)

//Temperature Thresold Hight and Low
#define TTHH 55
#define TTHL 50

//State machine
#define ST_NORMAL 1
#define ST_OVERTEMP 2
#define ST_BUTTON 3

unsigned char CurrentState = ST_NORMAL;



Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();


/////////////////////////////////////////////////////////
///     SETUP       /////////////////////////////////////
/////////////////////////////////////////////////////////
void setup () 
{
   DMXSerial.init(DMXReceiver);
    
   //Communication with thermal sensor TM100
   Wire.begin();        // join i2c bus (address optional for master)
   TMP100_SetResolution(12);     //of the thermal sensor
   
   //virtual RS232 over USB for debug/configure
   Serial.begin(9600);
   Serial.println("Hello");

   pwm.begin();  //Adafruit servo board
   pwm.setPWMFreq(100);  //  Adafruit servo board
   
   TWBR = 12; // upgrade to 400KHz!
}



/////////////////////////////////////////////////////////
///     MAIN LOOP   /////////////////////////////////////
/////////////////////////////////////////////////////////
void loop() 
{
   static unsigned long int t;    
   static unsigned char blk = 1; //blink boolean variable 
   
   if (t++ >= 60000)  //Not ask for temperature alwas
   {
      TMP100_getTemperature();  //Read sensor using I2C and print temperature to debug consol
      t = 0;
      blk = !blk;
      const char *build_str = "Ver[" VERSION " " __DATE__ " " __TIME__ "] ";
      Serial.print(build_str);
      Serial.print("Temp[");Serial.print(celsius);Serial.print("] ");
      Serial.print("Button[");if (buttonPushed) Serial.print("ON ]"); else Serial.print("OFF] ");
      Serial.print (" Addr["); Serial.print(DMX_address); Serial.print("] ");
      Serial.print (" St["); Serial.print(CurrentState); Serial.print("] ");
      
      Serial.print (" X["); Serial.print(DMXSerial.read(DMX_address+1)); Serial.print("] ");
      
      Serial.println();
   }

   // Calculate how long no data packet was received
   unsigned long lastPacket = DMXSerial.noDataSince();
   if (CurrentState != ST_BUTTON)
   {
      
      if ( ((celsius>TTHH) && (CurrentState==ST_NORMAL) )          //Overtemp  
           || ( (celsius>TTHL) && (CurrentState==ST_OVERTEMP) ) )    //cooling down
      //if (celsius >= 30.0)
             
      {
         CurrentState = ST_OVERTEMP;
         SetRGBXW(0,0,0,0,20);
      }
      else 
      
      {
         CurrentState = ST_NORMAL; 
         //if (lastPacket < 5000) 
         if (lastPacket < 5000) 
         {
            // read recent DMX values and set PWM levels
           if (t%100==0)
            SetRGBXW(
               DMXSerial.read(DMX_address),
               DMXSerial.read(DMX_address+1),
               DMXSerial.read(DMX_address+2),
               DMXSerial.read(DMX_address+3),
               DMXSerial.read(DMX_address+4));
         } 
         else 
         {
           unsigned long lastPacket2 = DMXSerial.noDataSince(); 
          // delay(10);
           if (lastPacket2 > 5000) //is it true??? If I don't put this condition 
                                   //sometime the Spotlight blink for a short time.
                                   //probably when we reatch the long int size ...
              if (t%100==0)                                   
              SetRGBXW(0,0,20,0,0);// Show pure red color, when no data was received since 5 seconds or more.
            
         }
      }
   }
   
   //If button is pushe we set the new device address with the value of DMX channel 255 (exemple DMX_SETUP_ADDRESS = probably 255)
   if ( buttonPushed )
   {
      unsigned char DMX_address_MSB = DMXSerial.read(DMX_SETUP_ADDRESS);
      unsigned char DMX_address_LSB = DMXSerial.read(DMX_SETUP_ADDRESS+1);  
      
    
      if (CurrentState == ST_NORMAL & (DMX_address_MSB * 256 + DMX_address_LSB) != 0 )
      {

         DMX_address = DMX_address_MSB * 256 + DMX_address_LSB;        
         Serial.print("Sore new address ("); Serial.print(DMX_address); Serial.println(")");
         EEPROM.write(EEPROM_DMX_ADDRESS  ,DMX_address_MSB); 
         EEPROM.write(EEPROM_DMX_ADDRESS+1,DMX_address_LSB); 
         ShowColorAnimation();
         //delay(500);
      }
      CurrentState = ST_BUTTON;
   }
   else
      CurrentState = ST_NORMAL;
      
   
} //end main loop


//This version use the 12bits PWM: PCA9685
//edz 14.11.2014 -> 1 color
//edz 24.1.2015 -> 5 colors
void SetRGBXW(unsigned char W, unsigned char X, unsigned char R, unsigned char G, unsigned char B)
{
   #define OUT_WHITE_COARSE 11
   #define OUT_WHITE_FINE   10
   
   #define OUT_COLD_COARSE  13
   #define OUT_COLD_FINE    12
   
   #define OUT_RED_COARSE   5
   #define OUT_RED_FINE     6
   
   #define OUT_GREEN_COARSE 3
   #define OUT_GREEN_FINE   4
   
   #define OUT_BLUE_COARSE  15
   #define OUT_BLUE_FINE    14
  
   //Warm white
   if (W<80)
   {  
      pwm.setPWM(OUT_WHITE_FINE,  0, PWMTable_fine[W]);
      pwm.setPWM(OUT_WHITE_COARSE, 0, 0);
   }
   else 
   {
       pwm.setPWM(OUT_WHITE_FINE,  0, 0);
       pwm.setPWM(OUT_WHITE_COARSE,  0, PWMTable_coarse[W]);
   }

   setColorLed(X,OUT_COLD_FINE,OUT_COLD_COARSE);
   setColorLed(R,OUT_RED_FINE,OUT_RED_COARSE);
   setColorLed(G,OUT_GREEN_FINE,OUT_GREEN_COARSE);
   setColorLed(B,OUT_BLUE_FINE,OUT_BLUE_COARSE);

   SetLED_D5(R/10);  //Back of the Spot light

}

//Show a short annimation in order to validate new address is set.
void ShowColorAnimation()
{
   unsigned char d = 500;
   unsigned char level = 100;
   setColorLed(0,OUT_COLD_FINE,OUT_COLD_COARSE);
   setColorLed(0,OUT_BLUE_FINE,OUT_COLD_COARSE);   
   setColorLed(0,OUT_RED_FINE,OUT_RED_COARSE);     
   setColorLed(0,OUT_GREEN_FINE,OUT_GREEN_COARSE);   
   
   setColorLed(level,OUT_COLD_FINE,OUT_COLD_COARSE);
   delay(d);
   setColorLed(0,OUT_COLD_FINE,OUT_COLD_COARSE);   
   setColorLed(level,OUT_RED_FINE,OUT_RED_COARSE);  
   delay(d);
   setColorLed(0,OUT_RED_FINE,OUT_RED_COARSE);     
   setColorLed(level,OUT_GREEN_FINE,OUT_GREEN_COARSE);
   delay(d);
   setColorLed(0,OUT_GREEN_FINE,OUT_GREEN_COARSE);   
   setColorLed(level,OUT_BLUE_FINE,OUT_BLUE_COARSE);
   delay(d);
   setColorLed(0,OUT_BLUE_FINE,OUT_BLUE_COARSE);   

}
         

//Set color LED.
//Edz 22.8.2015
void setColorLed(unsigned char level, unsigned char OutputFine, unsigned char OutputCoarse)
{
   if (level<80)
   {  
      pwm.setPWM(OutputFine,  0, cPWMTable_fine[level]);
      pwm.setPWM(OutputCoarse, 0, 0);
   }
   else 
   {
       pwm.setPWM(OutputFine,  0, 0);
       pwm.setPWM(OutputCoarse,  0, PWMTable_coarse[level]);
   }
}

//Indication LED D5 using  12bits PWM: PCA9685
//By default this LED is on. 
//edz 24.1.2015 -> 5 colors
void SetLED_D5(unsigned char value /*0-255*/)
{
   #define OUT_LED_D5 0
   pwm.setPWM(OUT_LED_D5, 0, PWMTable_coarse[255-value]);
}



//////////////////////////////////////////////////
//Temperature mesurement 
/////////////////////////////////////////////////
float TMP100_getTemperature(){
   Wire.requestFrom(TMP100_Address,2);
   byte MSB = Wire.read();
   byte LSB = Wire.read();
 
   int TemperatureSum = ((MSB << 8) | LSB) >> 4;
 
   celsius = TemperatureSum*0.0625;
   float fahrenheit = (1.8 * celsius) + 32;
}

void TMP100_SetResolution(unsigned char resolution){

   if (resolution < 9 || resolution > 12) exit;
   Wire.beginTransmission(TMP100_Address);
   Wire.write(B00000001); //addresses the configuration register
   Wire.write((resolution-9) << 5); //writes the resolution bits
   Wire.endTransmission();
 
   Wire.beginTransmission(TMP100_Address); //resets to reading the temperature
   Wire.write((byte)0x00);
   Wire.endTransmission();
}


