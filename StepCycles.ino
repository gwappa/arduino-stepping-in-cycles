/**
 * StepCycles
 * 
 * runs stepping motor at specified amplitude/speed/number of waves.
 * 
 * pin #13 (PB5) -> clock out
 * pin #12 (PB4) -> direction
 * 
 * serial command format: <command>; <command>; ...
 * 
 * available commands:
 * RUN : <amp_halfsteps> x <nwaves> @ <clock_us>
 *     runs once
 * WAIT: -
 *     waits for 1 second
 * 
 * @author gwappa
 */
 
#include <TimerOne.h>

#define BAUD 9600

#define DO13 B00100000
#define DO12 B00010000
#define DO11 B00001000

//#define DEBUG_PARSE
//#define DEBUG_READ

#define SLEEPS_ON_NONOP

uint8_t  fullamp_halfsteps = 0;
uint8_t  num_waves         = 0;
uint32_t clock_us          = 0;


struct Parse {
  static const uint8_t AMP  = 1;
  static const uint8_t NUM  = 2;
  static const uint8_t CLK  = 3;
  static const uint8_t DON  = 4;
  static const uint8_t ERR  = 5;
  
  uint32_t value;
  uint8_t  offset;
  char     buf;
  uint8_t  mode;

  Parse(): value(0), offset(0), mode(AMP) {}
  
  void reset() {
    value  = 0;
    offset = 0;
    mode   = AMP;
  }

  bool isError() {
    return (mode == ERR);
  }

  void parseNumber(const char& ch) {
    uint8_t num;
    switch (ch) {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        num = uint8_t(ch) - 48;
        value = value * 10 + num;
#ifdef DEBUG_PARSE
        Serial.print("> value=");
        Serial.println(value);
#endif
        offset++;
        break;
      default:
     
#ifdef DEBUG_PARSE
          Serial.println("> ERR");
#endif
        buf  = ch;
        mode = ERR;
    }
  }

  /**
   * returns true if the parser needs more characters.
   */
  bool push(const char& ch) {
    if (mode == ERR) {
#ifdef DEBUG_READ
      Serial.println("(error)");
#endif
      return (ch != '\n');
    } else if ((ch == ' ') || (ch == ';')) {
#ifdef DEBUG_READ
      Serial.println("(skip)");
#endif
      offset++;
      return true;
    }

    uint16_t fullamp_deg;
    uint32_t clock_hz;
    uint32_t cps;
    uint32_t duration_ms;
    
    switch(mode) {
      case AMP:
        if (ch == 'x') {
#ifdef DEBUG_PARSE
          Serial.println("> NUM");
#endif
          fullamp_halfsteps = value;
          value             = 0;
          mode              = NUM;
          offset++;
          Serial.print("@fullamp_halfsteps=");
          Serial.println(fullamp_halfsteps);
          fullamp_deg = 360 * fullamp_halfsteps / 400;
          Serial.print("@fullamp_deg=");
          Serial.println(fullamp_deg);
        } else {
          parseNumber(ch);
        }
        return true;
      case NUM:
        if (ch == '@') {
#ifdef DEBUG_PARSE
          Serial.println("> CLK");
#endif
          num_waves = value;
          value     = 0;
          mode      = CLK;
          offset++;
          Serial.print("@num_waves=");
          Serial.println(num_waves);
        } else {
          parseNumber(ch);
        }
        return true;
      case CLK:
        if ((ch == '\r') || (ch == '\n') || (ch == ';')) {
#ifdef DEBUG_PARSE
          Serial.println("> DON");
#endif
          clock_us  = value;
          value     = 0;
          mode      = DON;
          offset++;
          Serial.print("@clock_us=");
          Serial.println(clock_us);
          clock_hz  = 500000 / clock_us;
          Serial.print("@clock_hz=");
          Serial.println(clock_hz);
          cps = 250000 / (clock_us * fullamp_halfsteps);
          Serial.print("@cps=");
          Serial.println(cps);
          duration_ms = clock_us * fullamp_halfsteps * num_waves
                    / 250;
          Serial.print("@duration_ms=");
          Serial.println(duration_ms);

          switch (ch) {
            case '\n':
            case ';':
              return false;
            default:
              return true;
          }
        } else  {
          parseNumber(ch);
        }
        return true;
      case DON:
        switch (ch) {
          case '\n':
            return false;
          case '\r':
            offset++;
            return true;
          default:
#ifdef DEBUG_PARSE
            Serial.println("> ERR");
#endif
            mode = ERR;
            return true;
        }
      default:
        // do nothing
        switch (ch) {
          case '\n':
            return false;
          default:
#ifdef DEBUG_PARSE
            Serial.println("> ERR");
#endif
            mode = ERR;
            return true;
        }
    }
  }
};

Parse parser;

void setup() {
  // put your setup code here, to run once:
  DDRB  |= DO11 | DO12 | DO13;
  PORTB &= ~(DO11 | DO12 | DO13);
#ifndef SLEEPS_ON_NONOP
  PORTB |= DO11;
#endif
  
  Serial.begin(BAUD);
  Timer1.initialize(1000);
  Serial.println(">READY");
}

void loop() {
  // put your main code here, to run repeatedly:
  int c;
  char ch;
  
  if (Serial.available() > 0) {
    c = Serial.read();
    if (c > 0) {
      ch = (char)c;
#ifdef DEBUG_READ
      Serial.print("read: ");
      Serial.println(ch);
#endif
      if (ch == '-') {
        delay(1);
        return;
      }
      if (!parser.push(ch)) {
        // end of parse
        if (parser.isError()) {
          Serial.print("***error at char ");
          Serial.print(parser.offset+1); 
          Serial.print(" (");
          Serial.print(parser.buf);
          Serial.print(")");
          Serial.println("; format: <amp> x <nwaves> @ <clock_us>");
        } else {
          runOnce();
        }
        parser.reset();
      }
    }
  }
}

volatile bool pulsing = false;
volatile bool    rev  = false;
volatile bool    high = false;
volatile uint8_t waves = 0;
volatile uint8_t steps = 0;

void runOnce() {
  Serial.println(">START");
  if (clock_us * fullamp_halfsteps * num_waves != 0) {
    Timer1.setPeriod(clock_us);
    rev     = false;
    high    = false;
    waves   = 0;
    steps   = 0;
    
    pulsing = true;
#ifdef SLEEPS_ON_NONOP
    PORTB  |= DO11;
#endif

    Timer1.attachInterrupt(timeout);
    Timer1.start();
    while(pulsing);
  } else {
    Serial.println(">NO_OUTPUT");
  }
  Serial.println(">DONE");
}

void timeout() {
  if (high) {
    // end of one clock
    // turn clock low
    high   = false;
    PORTB &= ~DO13;
    
    if ((++steps) == fullamp_halfsteps) {
      // end of one slope
      // reset steps
      steps = 0;
      
      if (rev) {
        // end of one wave
        // unassert rev
        rev   = false;
        PORTB &= ~DO12;
        
        if ((++waves) == num_waves) {
          // done, detach callback and stop timer
          Timer1.stop();
          Timer1.detachInterrupt();
          pulsing = false;
#ifdef SLEEPS_ON_NONOP
          PORTB  &= ~DO11;
#endif
        }
      } else {
        // switch back
        // assert rev
        rev   = true;
        PORTB |= DO12;
      }
    }
  } else {
    // start clock
    // turn high
    high   = true;
    PORTB |= DO13;
  }
}

