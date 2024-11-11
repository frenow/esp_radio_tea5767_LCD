#include <Arduino.h>
#include <Wire.h>
#include <radio.h>
#include <TEA5767.h>
#include <LiquidCrystal_I2C.h>

/// The band that will be tuned by this sketch is FM.
#define FIX_BAND RADIO_BAND_FM

/// The station that will be tuned by this sketch is 99.10 MHz.
#define FIX_STATION 9910

// Set the LCD address to 0x27 for a 16 chars and 2 line display
LiquidCrystal_I2C lcd(0x27, 16, 2);

TEA5767 radio;    // Create an instance of Class for TEA5767 Chip

RADIO_FREQ preset[] = {
  8870,   // Sender:<  TRANSAMERICA  >
  9030,   // Sender:<  MIX   >
  9170,   // Sender:<  O TEMPO   >
  9290,   // Sender:<  LIBERDADE  >
  9570,   // Sender:<  ITATIFRANGA >
  9830,   // Sender:<  98 FM   >
  9910,   // Sender:<  JOVEM PAN >
  10210,  // Sender:<  BH FM  >
  10390,   // Sender:<  LIGHT >
  10510   // Sender:<  ANTENA1 >
};

uint16_t presetIndex = 6;  ///< Start at Station with index

/// State of Keyboard input for this radio implementation.
enum RADIO_STATE {
  STATE_PARSECOMMAND,  ///< waiting for a new command character.
  STATE_PARSEINT,      ///< waiting for digits for the parameter.
  STATE_EXEC           ///< executing the command.
};

RADIO_STATE kbState;  ///< The state of parsing input characters.
char kbCommand;
int16_t kbValue;

bool lowLevelDebug = false;

/// Execute a command identified by a character and an optional number.
/// See the "?" command for available commands.
/// \param cmd The command character.
/// \param value An optional parameter for the command.
void runSerialCommand(char cmd, int16_t value) {
  if (cmd == '?') {
    Serial.println();
    Serial.println("? Help");
    Serial.println("+ increase volume");
    Serial.println("- decrease volume");
    Serial.println("> next preset");
    Serial.println("< previous preset");
    Serial.println(". scan up   : scan up to next sender");
    Serial.println(", scan down ; scan down to next sender");
    Serial.println("fnnnnn: direct frequency input");
    Serial.println("i station status");
    Serial.println("s mono/stereo mode");
    Serial.println("b bass boost");
    Serial.println("u mute/unmute");
  }

  // ----- control the volume and audio output -----

  else if (cmd == '+') {
    // increase volume
    int v = radio.getVolume();
    radio.setVolume(++v);
    Serial.print("Volume: ");
    Serial.print(v);
  } else if (cmd == '-') {
    // decrease volume
    int v = radio.getVolume();
    if (v > 0)
      radio.setVolume(--v);

    Serial.print("Volume: ");
    Serial.print(v);

  }

  else if (cmd == 'u') {
    // toggle mute mode
    radio.setMute(!radio.getMute());
  }

  // toggle stereo mode
  else if (cmd == 's') {
    radio.setMono(!radio.getMono());
  }

  // toggle bass boost
  else if (cmd == 'b') {
    radio.setBassBoost(!radio.getBassBoost());
  }

  // ----- control the frequency -----

  else if (cmd == '>') {
    // next preset
    if (presetIndex < (sizeof(preset) / sizeof(RADIO_FREQ)) - 1) {
      presetIndex++;
      radio.setFrequency(preset[presetIndex]);
    }  // if
  } else if (cmd == '<') {
    // previous preset
    if (presetIndex > 0) {
      presetIndex--;
      radio.setFrequency(preset[presetIndex]);
    }  // if

  } else if (cmd == 'f') {
    radio.setFrequency(value);
  }

  else if (cmd == '.') {
    radio.seekUp(false);
  } else if (cmd == ':') {
    radio.seekUp(true);
  } else if (cmd == ',') {
    radio.seekDown(false);
  } else if (cmd == ';') {
    radio.seekDown(true);
  }


  else if (cmd == '!') {
    // not in help
    RADIO_FREQ f = radio.getFrequency();
    if (value == 0) {
      radio.term();
    } else if (value == 1) {
      radio.initWire(Wire);
      radio.setBandFrequency(RADIO_BAND_FM, f);
      radio.setVolume(10);
    }

  } else if (cmd == 'i') {
    // info
    char s[12];
    radio.formatFrequency(s, sizeof(s));
    Serial.print("Station:");
    Serial.println(s);
    Serial.print("Radio:");
    radio.debugRadioInfo();
    Serial.print("Audio:");
    radio.debugAudioInfo();

  } else if (cmd == 'x') {
    radio.debugStatus();  // print chip specific data.

  } else if (cmd == '*') {
    lowLevelDebug = !lowLevelDebug;
    radio._wireDebug(lowLevelDebug);
  }
}  // runSerialCommand()

/// Update the Frequency on the LCD display.
void DisplayFrequency() {
  char s[12];
  radio.formatFrequency(s, sizeof(s));
  Serial.print("FREQ:");
  Serial.println(s);
  lcd.setCursor(1, 1);
  lcd.print(">>");
  lcd.print(s);

}  // DisplayFrequency()

/// Setup a FM only radio configuration
/// with some debugging on the Serial port
void setup() {
	// initialize LCD
  lcd.init();
  // turn on LCD backlight                      
  //lcd.backlight();
  //lcd.home();                // At column=0, row=0

  lcd.setCursor(0, 0);
  lcd.print(" ...ESP Radio...");

  // open the Serial port
  delay(3000);
  Serial.begin(9600);
  Serial.println("Radio...");
  delay(200);

  // Initialize the Radio 
  radio.init();

  // Enable information to the Serial port
  radio.debugEnable();

  // HERE: adjust the frequency to a local sender
  radio.setBandFrequency(FIX_BAND, FIX_STATION); // hr3 nearby Frankfurt in Germany
  radio.setVolume(2);
  radio.setMono(false);

  debugRadio();

  runSerialCommand('?', 0);
  kbState = STATE_PARSECOMMAND;  
} // setup



void loop() {

  unsigned long now = millis();
  static unsigned long nextFreqTime = 0;

  // some internal static values for parsing the input
  static RADIO_FREQ lastFrequency = 0;
  RADIO_FREQ f = 0;

  if (Serial.available() > 0) {
    // read the next char from input.
    char c = Serial.peek();

    if ((kbState == STATE_PARSECOMMAND) && (c < 0x20)) {
      // ignore unprintable chars
      Serial.read();

    } else if (kbState == STATE_PARSECOMMAND) {
      // read a command.
      kbCommand = Serial.read();
      kbState = STATE_PARSEINT;

    } else if (kbState == STATE_PARSEINT) {
      if ((c >= '0') && (c <= '9')) {
        // build up the value.
        c = Serial.read();
        kbValue = (kbValue * 10) + (c - '0');
      } else {
        // not a value -> execute
        runSerialCommand(kbCommand, kbValue);
        kbCommand = ' ';
        kbState = STATE_PARSECOMMAND;
        kbValue = 0;
      }  // if
    }    // if
  }      // if

  // update the display from time to time
  if (now > nextFreqTime) {
    f = radio.getFrequency();
    if (f != lastFrequency) {
      // print current tuned frequency
      DisplayFrequency();
      lastFrequency = f;
    }  // if
    nextFreqTime = now + 400;
  }  // if

} // loop

/// show the current chip data every 3 seconds.
void debugRadio() {
  char s[12];
  radio.formatFrequency(s, sizeof(s));
  Serial.print("Station:"); 
  Serial.println(s);
  
  Serial.print("Radio:"); 
  radio.debugRadioInfo();
  
  Serial.print("Audio:"); 
  radio.debugAudioInfo();

  delay(3000);
}

// End.
