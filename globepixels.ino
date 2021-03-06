#include <FastLED.h>
#include <Wire.h>

#ifdef __AVR__
  #include <avr/power.h>
#endif

#define VERSION			33
#define	NODEBUG				//define DEBUG to get debug output.

#define PIN             	8
#define NUMPIXELS       	150
#define GLOBE_SIZE      	2	//How many leds are inside of one globe
#define GLOBE_SPACING   	10	//this minus GLOBE_SIZE equals the amount of LEDs between globes
#define GLOBE_COUNT     	30	//just to save RAM - we should calculate this on the fly though
#define FRAMERATE       	60	//how many frames per second to we ideally want to run
#define MAX_LOAD_MA     	2000	//how many mA are we allowed to draw, at 5 volts
#define TEMPORAL_DITHERING	false	//use temporal dithering from FastLED

#define EFFECT_TIMEOUT_IN_USE	30000	//how long before we can change effects while in use
#define EFFECT_TIMEOUT_IDLE	7000	//how long before we can change effects when the user is not giving input
#define EFFECT_TIMEOUT_ATTRACT	60000	//how long before changing to a new attract mode

#define S_FIRE_COOLING  	75	//How much does the air cool as it rises?
#define S_FIRE_SPARKING 	180	//What chance (out of 255) is there that a new spark will be lit?

#define S_DRIP_BLANK		true	//Do we blank every unused globe while dripping

unsigned long lastFrame;
unsigned long lastCleanup;
unsigned long frameCount;
unsigned long sloshCount;
unsigned long lastEffectChange;
unsigned long lastUserInput;
CRGB globes[GLOBE_COUNT];
CRGB pixels[NUMPIXELS];

int g_offset = 0;
int s_snake_offset = 0;
int s_snake_end = 0;
bool s_single_color = false;
bool s_snake_scanner = false; //Larson scanner
int s_drip_pos = 0;
byte s_drip_scale = 0;
bool s_drip_flip = false;

bool inAttractMode = false;

typedef enum {
  G_NOTOUCH,
  G_STRIP, //don't touch the globes, but render them. This is so the strip effect can write to the globes.
  G_RAINBOW,
  G_BLANK,
  G_COLOR,
  G_STROBEONCE,
  G_VERSION
} gstate_t;
gstate_t g = G_NOTOUCH;

typedef enum {
  S_NOTOUCH,
  S_SNAKE,
  S_BLANK,
  S_FADE,
  S_RAIN,
  S_PAPARAZZI,
  S_COLOR,
  S_SPARKLE,
  S_DRIP,
  S_FIRE,
  S_DRIPBOW
} sstate_t;
sstate_t s = S_NOTOUCH;
#define S_SNAKE_LENGTH 15

void setup() {

  pinMode(13, OUTPUT); 

  Serial.begin(38400);
  Serial.setTimeout(100);
  //Send bytes faster than this timeout when setting colors, etc.
  Serial.println("#serial up");
  Serial.print("#GLOBEPIXELS version "); Serial.println(VERSION);

  FastLED.addLeds<NEOPIXEL, PIN>(pixels, NUMPIXELS);
  set_max_power_in_volts_and_milliamps(5,MAX_LOAD_MA); //assuming 5 volts
  set_max_power_indicator_LED(13); //blink led 13 when would've overdrawn
  FastLED.setCorrection(TypicalSMD5050);
  runG_VERSION(); writeGlobes(); show_at_max_brightness_for_power(); delay(1000); //quickly write the version out for a second
  g = G_BLANK;
  s = S_RAIN;

  Serial.println("#leds up");
  
  Wire.begin(8);
  //We have to register a handler because of a bug in the arduino wire library.
  //https://github.com/arduino/Arduino/blob/master/hardware/arduino/avr/libraries/Wire/src/Wire.cpp#L279
  //If you don't have a user handler registered, the onReceiveService() fn
  //immediately returns. It doesn't copy the received stuff out of the buffer
  //or really do anything unless we have a handler registered. The handler can
  //literally be a noop... ugh
  Wire.onReceive(handleWire);
  Wire.setTimeout(100);
  Serial.println("#wire up");

  lastFrame = millis();
  lastCleanup = millis();
  lastEffectChange = millis();
  lastUserInput = millis();
  frameCount = 0;
  sloshCount = 0;
  
  Serial.print("#up at "); Serial.println(lastFrame);
  
}

void setGlobe(int x, CRGB color) {
  globes[x] = color;
}
CRGB getGlobe(int x) {
  return globes[x];
}

void setAllGlobes(uint32_t color) {
  for ( int i=0; i<GLOBE_COUNT; i++ ) {
    setGlobe(i,color);
  }
}

void writeGlobes() {
  for ( int globe_num=0; globe_num<GLOBE_COUNT; globe_num++ ) {
    int globe_pos = globe_num*GLOBE_SPACING;
    for ( int led_pos=globe_pos; led_pos<globe_pos+GLOBE_SIZE; led_pos++ ) {
      if ( led_pos < NUMPIXELS ) { //don't overrun the strand you idiot
        pixels[NUMPIXELS-1-led_pos] = globes[globe_num];
      }
    }
  }
}

bool isInGlobe(int pos) {
  return (pos%GLOBE_SPACING) < GLOBE_SIZE;
}

int whichGlobe(int pos) {
  return pos/GLOBE_SPACING;
}

void runG_RAINBOW() {
  
  if ( g_offset > 255 ) {
    g_offset=0;
  }
  
  for ( int i=0; i<GLOBE_COUNT; i++ ) {
    setGlobe(i, wheelForPos(i*GLOBE_SPACING,g_offset) );
  }
  
  g_offset += 5;
  
}
void runG_BLANK() {
  for ( int i=0; i<GLOBE_COUNT; i++ ) {
    setGlobe(i,0);
  }
}
void runG_STRIP() {
  //While our strip effect is running, let's set the globe values in RAM
  //to whatever the strip is writing to the inside of the globes. That
  //way when we change effects it will not just blank all the pixels
  //or do something else undesireable.
  for ( int globe_num=0; globe_num<GLOBE_COUNT; globe_num++ ) {
    setGlobe(globe_num, pixels[NUMPIXELS-1-globe_num*GLOBE_SPACING]);
  }
}

CRGB g_color = CRGB(80,141,172);
void runG_COLOR() {
  for ( int i=0; i<GLOBE_COUNT; i++ ) {
    setGlobe(i,g_color);
  }
}
void runG_STROBEONCE() {
  runG_COLOR();
  g=G_BLANK;
}
void runG_VERSION() {
  runG_BLANK();
  for ( int i=0; i<8; i++ ) {
    if ( i < GLOBE_COUNT ) {
      if ( bitRead(VERSION,i) ) {
        setGlobe(i,g_color);
      } else {
        setGlobe(i,0);
      }
    }
  }
  /*
  if ( millis() > 10000 ) {
    //It has been 10 seconds. Do something more interesting with our life.
    if ( s == S_NOTOUCH ) {
      s=S_FIRE;
      g=G_NOTOUCH;
    }
  }
  */
}

CRGB s_color = CRGB(80,141,172);

void runS_SNAKE() {

  if ( s_snake_scanner ) {
    runS_BLANK(); //blank the strip first
  } else { 
    runS_FADE(); //fade the entire strip first
  }

  //now we start drawing the new snake on top of the existing pixels.
  //after a while the tail will fade all the way down.

  if ( s_snake_offset > NUMPIXELS ) {
    s_snake_offset = 0;
  }

  //Serial.print("#Snake top:"); Serial.println(s_snake_offset);
  for ( int i = 0; i < S_SNAKE_LENGTH; i++ ) {
    int px = s_snake_offset + i;
    if ( px >= NUMPIXELS ) {
      //we're off the top of the strip... write to the corresponding pixel at the bottom instead
      px = px-NUMPIXELS;
    }

    CRGB c;
    if ( s_single_color ) {
      c = s_color;
    } else {
      c = wheelForPos(s_snake_offset).fadeToBlackBy(128);
    }
    pixels[NUMPIXELS-1-px] = c;
    if ( s_snake_scanner ) {
      pixels[px] = c;
    }
  }
 
  s_snake_offset++;
  if ( s_snake_scanner ) {
    s_snake_offset++;
  }

}
void runS_BLANK() {
  for ( int i=0; i<NUMPIXELS; i++ ) {
    pixels[NUMPIXELS-1-i] = 0;
  }
}
void runS_FADE() {
  for ( int i=0; i<NUMPIXELS; i++ ) {
    pixels[NUMPIXELS-1-i].fadeToBlackBy(32); //operates in place
  }
}
void runS_RAIN() {
  
  //fade out existing pixels
  runS_FADE();

  //decide if we want to add a new raindrop
  if ( random(0,2) == 0 ) {
    //we do
    pixels[NUMPIXELS-1-random(0,NUMPIXELS)] = s_color;
  }
}
void runS_PAPARAZZI() {
  runS_BLANK();
  //decide if we want to add a new raindrop
  if ( random(0,100) < 40 ) {
    //we do
    pixels[NUMPIXELS-1-random(0,NUMPIXELS)] = s_color;
  }
}
void runS_COLOR() {
  for ( int i=0; i<NUMPIXELS; i++ ) {
    pixels[NUMPIXELS-1-i] = s_color;
  }
}
void runS_SPARKLE() {
  for ( int i=0; i<25; i++ ) { 
    if ( random(0,100) < 15 ) {
      pixels[NUMPIXELS-1-random(0,NUMPIXELS)] = s_color; //light one
    } else {
      pixels[NUMPIXELS-1-random(0,NUMPIXELS)] = 0; //extinguish one
    }
  }
}
void runS_DRIP() {

  //Serial.print("#Drip pos = "); Serial.println(s_drip_pos);

  if ( s_drip_pos <= 0 ) {
    s_drip_pos = NUMPIXELS-1;
  }

  //Serial.print("#DRIP pos="); Serial.println(s_drip_pos);

  if ( isInGlobe(s_drip_pos) ) {
    //Serial.print("#DRIP in globe ");
    /*if ( s_drip_pos+1 < NUMPIXELS )*/ pixels[NUMPIXELS-1-s_drip_pos-1] = 0; //blank the one above this because it's not in the globe. TODO: can this overrun our array?
    int which = whichGlobe(s_drip_pos);
    //Serial.print(which); Serial.print(" Scaling factor was "); Serial.print(s_drip_scale);
    if ( s_drip_flip == false ) {
      if ( s_drip_scale < 245 ) {
        s_drip_scale += 10;
      } else {
        s_drip_scale=255;
        s_drip_flip = true;
	//Serial.print(" flip!");
      }
    } else { //s_drip_flip is true; let's fade out instead;
      if ( s_drip_scale > 10 ) {
        s_drip_scale-=10;
      } else {
        s_drip_scale=0;
        s_drip_flip=false;
        while (s_drip_pos > 0 and isInGlobe(s_drip_pos)) {
          s_drip_pos--;
        }
      }
    }
    //Serial.print(" new scale "); Serial.println(s_drip_scale);
    //Serial.print("#DRIP setting globe "); Serial.println(which);
    if ( S_DRIP_BLANK ) {
      setAllGlobes(0);
    }
    setGlobe(which,g_color.scale8(CRGB(s_drip_scale,s_drip_scale,s_drip_scale)));
  } else { //it's not a globe, do something cool in between.
    pixels[NUMPIXELS-1-s_drip_pos] = g_color.scale8(CRGB(128,128,128));
    if ( !isInGlobe(s_drip_pos+1) ) {
      pixels[NUMPIXELS-1-s_drip_pos-1] = 0;
    }
    s_drip_pos--;
  }
  
}
//from pastebin.com/xYEpxqgq 
void runS_FIRE() {
  // Array of temperature readings at each simulation cell
  static byte heat[NUMPIXELS];
  // Step 1.  Cool down every cell a little
  for( int i = 0; i < NUMPIXELS; i++) {
    heat[i] = qsub8( heat[i],  random8(0, ((S_FIRE_COOLING * 10) / NUMPIXELS) + 2));
  }
		    
  // Step 2.  Heat from each cell drifts 'up' and diffuses a little
  for( int k= NUMPIXELS - 3; k > 0; k--) {
    heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2] ) / 3;
  }
			         
  // Step 3.  Randomly ignite new 'sparks' of heat near the bottom
  if( random8() < S_FIRE_SPARKING ) {
    int y = random8(7);
    heat[y] = qadd8( heat[y], random8(160,255) );
  }
								  
  // Step 4.  Map from heat cells to LED colors
  for( int j = 0; j < NUMPIXELS; j++) {
    pixels[NUMPIXELS-1-j] = HeatColor( heat[j]);
  }
}
void runS_DRIPBOW() {
  runS_SNAKE();
  runS_DRIP();
}

void runGlobes() {
  if ( g == G_RAINBOW ) {
    runG_RAINBOW();
  } else if ( g == G_BLANK ) {
    runG_BLANK();
  } else if ( g == G_STRIP ) {
    runG_STRIP();
  } else if ( g == G_COLOR ) {
    runG_COLOR();
  } else if ( g == G_STROBEONCE ) {
    runG_STROBEONCE();
  } else if ( g == G_VERSION ) {
    runG_VERSION();
  }
}
void runStrip() {
  if ( s == S_SNAKE ) {
    runS_SNAKE();
  } else if ( s == S_RAIN ) {
    runS_RAIN();
  } else if ( s == S_PAPARAZZI ) {
    runS_PAPARAZZI();
  } else if ( s == S_BLANK ) {
    runS_BLANK();
  } else if ( s == S_FADE ) {
    runS_FADE();
  } else if ( s == S_COLOR ) {
    runS_COLOR();
  } else if ( s == S_SPARKLE ) {
    runS_SPARKLE();
  } else if ( s == S_DRIP ) {
    runS_DRIP();
  } else if ( s == S_FIRE ) {
    runS_FIRE();
  } else if ( s == S_DRIPBOW ) {
    runS_DRIPBOW();
  }
}

void handleWire(int count) {
  #ifdef DEBUG
  Serial.print("#Got i2c (");
  Serial.print(count);
  Serial.println(" bytes)");
  #endif /*DEBUG*/
}

void loop() {

  if ( (millis() - lastCleanup) > 1000 ) {
    //it's time to do our every-1s tasks.

    if ( (millis() - lastUserInput) < EFFECT_TIMEOUT_ATTRACT ) { //user touched us within this time, normal effect
      inAttractMode = false;
      if ( ( (millis() - lastUserInput) > EFFECT_TIMEOUT_IDLE ) && ( (millis() - lastEffectChange) > EFFECT_TIMEOUT_IDLE ) ) { //user hasn't touched me for 7 seconds and my effect has run for 7 seconds
        #ifdef DEBUG
	Serial.println("#Bored user effect change");
	#endif /*DEBUG*/
        changePresetEffect();
      } else if ( (millis() - lastEffectChange) > EFFECT_TIMEOUT_IN_USE ) { //user is constantly touching me for 30 seconds so I can't change effect by the previous rule
        #ifdef DEBUG
	Serial.println("#Overstimulated user effect change");
	#endif /*DEBUG*/
        changePresetEffect();
      }
    } else if ( ((millis() - lastEffectChange) > EFFECT_TIMEOUT_ATTRACT) or (!inAttractMode) ) { //either we've been in attract for 60 seconds already, or it is just time to get to attract.
      inAttractMode = true;
      #ifdef DEBUG
      Serial.println("#Attract mode effect change");
      #endif /*DEBUG*/
      changeAttractEffect();
    }

    #ifdef DEBUG
    double fr = (double)frameCount/((double)(millis()-lastCleanup)/1000);
    Serial.print("#FRAME RATE: "); Serial.print(fr);
    Serial.print(" - SLOSH: "); Serial.print(sloshCount);
    uint32_t loadmw = calculate_unscaled_power_mW(pixels,NUMPIXELS);
    Serial.print(" - LOAD: "); Serial.print(loadmw); Serial.print("mW ("); Serial.print(loadmw/5); Serial.print("mA) - ");
    Serial.print("Last user input "); Serial.print(millis() - lastUserInput); Serial.print(" - Last effect change "); Serial.println(millis() - lastEffectChange);
    #endif /*DEBUG*/
    
    lastCleanup = millis();
    frameCount = 0; sloshCount = 0;
  }

  if ( (millis() - lastFrame) > (1000/FRAMERATE) ) {
    lastFrame = millis();
    frameCount++;
    
    //Serial.print("### BEGIN FRAME ### "); Serial.println(lastFrame);
  
    digitalWrite(13,LOW); //start of LED processing

    //Serial.print("#begin globes at "); Serial.println(millis());
    runGlobes();
    //Serial.print("#begin strip at "); Serial.println(millis());
    runStrip();
  
    //Serial.print("#begin other at "); Serial.println(millis());
    digitalWrite(13,HIGH); //tell the user we're done
  
    //Serial.print("#begin write at "); Serial.println(millis());
    if ( g != G_NOTOUCH ) {
        writeGlobes();
    }

    //FastLED.show();
    show_at_max_brightness_for_power();

  } else {
    sloshCount++; //we didn't do anything so let's indicate that we had a spare mS.
    if ( TEMPORAL_DITHERING ) {
      FastLED.delay(1); //give it time to do temporal dithering
    } else {
      delay(1); //or don't
    }
  }

  processControlStream(Serial);
  processControlStream(Wire);

}

//My utilities below

void processControlStream(Stream &stream) {

  switch ( stream.peek() ) {
  
    case -1: break; //do nothing. There is nothing to read
    //we must remember to pop the command char off, if it's a single-byte command.
    case 'r': g=G_RAINBOW; stream.read(); break;
    case 'b': g=G_BLANK; stream.read(); break;
    case 'c': g_color = getColorFromStream(stream); break; //set color
    case 'o': g=G_COLOR; stream.read(); break; //color mode
    case 's': g=G_STROBEONCE; stream.read(); break;
    case 'n': g=G_NOTOUCH; stream.read(); break;
    case 'v': g=G_VERSION; stream.read(); break; //This won't work for long. It bails after a few millis.
  
    case 'R': s_single_color = false; s_snake_scanner = false; s=S_SNAKE; stream.read(); break; //rainbow
    case 'E': s_single_color = true; s_snake_scanner = false; s=S_SNAKE; stream.read(); break; //snake
    case 'L': s_single_color = true; s_snake_scanner = true; s=S_SNAKE; stream.read(); break; //Larson scanner
    case 'B': s=S_BLANK; stream.read(); break;
    case 'F': s=S_FADE; stream.read(); break;
    case 'C': s_color = getColorFromStream(stream); break; //set color
    case 'O': s=S_COLOR; stream.read(); break; //color mode
    case 'A': s=S_RAIN; stream.read(); break;
    case 'P': s=S_PAPARAZZI; stream.read(); break;
    case 'K': s=S_SPARKLE; stream.read(); break;
    case 'N': s=S_NOTOUCH; stream.read(); break;
    case 'D': s=S_DRIP; g=G_STRIP; stream.read(); break;
    case 'I': s=S_FIRE; g=G_NOTOUCH; stream.read(); break;
    case 'W': s=S_DRIPBOW; g=G_STRIP; stream.read(); break; //drip on globes; rainbow on the rest

    case '1': handleUserInput(CRGB::Fuchsia); stream.read(); break;	//start on purple. if you don't keep up with me, you're finished
    case '2': handleUserInput(CRGB::Red); stream.read(); break;	//red
    case '3': handleUserInput(CRGB::Green); stream.read(); break;	//green
    case '4': handleUserInput(CRGB::Blue); stream.read(); break;	//blue
    case '5': handleUserInput(CRGB::Yellow); stream.read(); break;	//yellow
    case '6': handleUserInput(CRGB::OrangeRed); stream.read(); break;	//orange
  
    case '<': softwareReset(); break;
    
    default: Serial.print((char)stream.read()); Serial.println("?");
  
  }

}

void handleUserInput(CRGB c) {
  s_color = c;
  g_color=s_color; //set the globe color to the same as the strip color

  if ( ( (millis() - lastUserInput) > EFFECT_TIMEOUT_IDLE ) && ( (millis() - lastEffectChange) > EFFECT_TIMEOUT_IDLE ) ) { //user hasn't touched me for 7 seconds and my effect has run for 7 seconds
    changePresetEffect();
  }
  
  lastUserInput = millis(); //we just got user input
}
void changePresetEffect() {
  sstate_t new_state = s;
  g=G_COLOR; //display the color on the globes by default
  while ( s == new_state ) {
    switch (rand() % 5) { //pick a new effect
      case 0: s_single_color = true; s_snake_scanner = false; s=S_SNAKE; break;
      case 1: s=S_RAIN; break;
      case 2: s=S_PAPARAZZI; break;
      case 3: s=S_SPARKLE; break;
      case 4: s_single_color = true; s_snake_scanner = true; s=S_SNAKE; break;
    }
  }
  lastEffectChange = millis(); //we just set the effect
}
void changeAttractEffect() {
  sstate_t new_state = s;
  g_color = s_color;
  g=G_COLOR; 
  while ( new_state == s ) {
    switch ( rand() % 3 ) { //n possible effects, 0 through n-1
      case 0: g=G_RAINBOW; s_single_color = false; s=S_SNAKE; break;
      case 1: runGlobes(); s_single_color = false; s=S_DRIPBOW; g=G_STRIP; break;
      case 2: s=S_FIRE; g=G_BLANK; break;
    }
  }
  lastEffectChange = millis();
}


CRGB getColorFromStream(Stream &stream) {
  //we have the 'c' character and the three color bytes.
  stream.read(); //throw away the 'c'
  int r=stream.parseInt(); 
  int g=stream.parseInt(); 
  int b=stream.parseInt();
  #ifdef DEBUG
  Serial.print("#Set color to ");
  Serial.print(r); Serial.print(","); Serial.print(g); Serial.print(","); Serial.println(b);
  #endif /*DEBUG*/
  return CRGB(r,g,b);
}

CRGB wheelForPos(int x) { return wheelForPos(x,0); }
CRGB wheelForPos(int x, int offset) {
  int thing = ((float)x/(float)NUMPIXELS)*255;
  thing += offset;
  while ( thing > 255 ) {
    thing -= 255;
  }
  return Wheel((byte)thing);
}

CRGB ReduceColor(uint32_t color, int percent) {
  //broken
  int red = (int)(color>>16 & 0xFF);
  int green = (int)(color>>8 & 0xFF);
  int blue = (int)(color & 0xFF);
  //Serial.print(red); Serial.print(","); Serial.print(green); Serial.print(","); Serial.println(blue);
  return CRGB((byte)red,(byte)green,(byte)blue);
}

//Stolen utilities below

//from strandtest
CRGB Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return CRGB(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return CRGB(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return CRGB(WheelPos * 3, 255 - WheelPos * 3, 0);
}

//from https://learn.adafruit.com/multi-tasking-the-arduino-part-3/put-it-all-together-dot-dot-dot
CRGB HalfColor(uint32_t color, int times) {
  // Shift R, G and B components one bit to the right
  uint32_t dimColor = CRGB(Red(color) >> 1, Green(color) >> 1, Blue(color) >> 1);
  if ( times == 1 ) {
    return dimColor;
  } else {
    return HalfColor(dimColor,times-1);
  }
}
// Returns the Red component of a 32-bit color
uint8_t Red(uint32_t color)
{
  return (color >> 16) & 0xFF;
}
     
// Returns the Green component of a 32-bit color
uint8_t Green(uint32_t color)
{
  return (color >> 8) & 0xFF;
}
     
// Returns the Blue component of a 32-bit color
uint8_t Blue(uint32_t color)
{
  return color & 0xFF;
}

//from http://forum.arduino.cc/index.php?topic=49581.0
void softwareReset() { // Restarts program from beginning but does not reset the peripherals and registers
  asm volatile ("  jmp 0");  
} 


