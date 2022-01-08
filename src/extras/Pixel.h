
////////////////////////////////////
//      Addressable LED Pixel     //
////////////////////////////////////

#pragma once

#include "RFControl.h"
#include "PwmPin.h"

typedef uint32_t color_t;

class Pixel {
  private:
    uint32_t pattern[2];           // storage for zero-bit and one-bit pulses
    uint32_t resetTime;            // minimum time (in usec) between pulse trains
    uint32_t nTrain;               // number of Pixels to transmit per pulse train batch
    
    RFControl *rf;
    void loadColor(color_t c, uint32_t *p);    // creates pulse pattern for pixel color (encoded as RGB in low 24-bits of *p)
  
  public:
    Pixel(int pin, uint32_t nPixels=1);                             // creates addressable single-wire RGB LED on pin (such as the SK68 or WS28), with OPTIONAL reserve of memory for nPixels
    
    void setTiming(float high0, float low0, float high1, float low1, uint32_t lowReset);    // changes default timings for bit pulse - note parameters are in MICROSECONDS
      
    void setRGB(uint8_t r, uint8_t g, uint8_t b, int nPixels=1);    // sets color of nPixels to RGB values (0-255)
    void setHSV(float h, float s, float v, int nPixels=1);          // sets color of nPixels to HSV values where h=[0,360], s=[0,100], v=[0,100]   
    void setColors(color_t *color, int nPixels);                    // sets colors of nPixels from array of Colors
    int getPin(){return(rf->getPin());}                             // returns pixel pin if valid, else returns -1
    
    static color_t getColorRGB(uint8_t r, uint8_t g, uint8_t b);    // return pixel Color from RGB values
    static color_t getColorHSV(float h, float s, float v);          // return pixel Color from HSV values

    operator bool(){         // override boolean operator to return true/false if creation succeeded/failed
      return(*rf);
    }
};
