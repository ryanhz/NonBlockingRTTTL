// ---------------------------------------------------------------------------
// Created by Antoine Beauchamp based on the code from the ToneLibrary
// ESP32 Core 2.x/3.x compatible version by ChatGPT (GPT-5), 2025
//
// See "NonBlockingRtttl.h" for purpose, syntax, version history, links, and more.
// ---------------------------------------------------------------------------

#include "Arduino.h"
#include "NonBlockingRtttl.h"

/*********************************************************
 * RTTTL Library data
 *********************************************************/

namespace rtttl
{

const int notes[] = {
  0,
  NOTE_C4, NOTE_CS4, NOTE_D4, NOTE_DS4, NOTE_E4, NOTE_F4, NOTE_FS4, NOTE_G4, NOTE_GS4, NOTE_A4, NOTE_AS4, NOTE_B4,
  NOTE_C5, NOTE_CS5, NOTE_D5, NOTE_DS5, NOTE_E5, NOTE_F5, NOTE_FS5, NOTE_G5, NOTE_GS5, NOTE_A5, NOTE_AS5, NOTE_B5,
  NOTE_C6, NOTE_CS6, NOTE_D6, NOTE_DS6, NOTE_E6, NOTE_F6, NOTE_FS6, NOTE_G6, NOTE_GS6, NOTE_A6, NOTE_AS6, NOTE_B6,
  NOTE_C7, NOTE_CS7, NOTE_D7, NOTE_DS7, NOTE_E7, NOTE_F7, NOTE_FS7, NOTE_G7, NOTE_GS7, NOTE_A7, NOTE_AS7, NOTE_B7
};

#define OCTAVE_OFFSET 0

// Globals
const char *buffer = "";
int bufferIndex = -32760;
byte default_dur = 4;
byte default_oct = 5;
int bpm = 63;
long wholenote;
byte pin = -1;
unsigned long noteDelay = 0;
bool playing = false;

// Forward declarations
void nextnote();

#if defined(ESP32)

#if ESP_ARDUINO_VERSION_MAJOR < 3
// Track the channel used in Core 2.x
static int _rtttl_channel = -1;
#endif

// ---------------------------------------------------------------------------
// Core-agnostic tone/noTone implementations
// ---------------------------------------------------------------------------

void noTone(int iPin = -1) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (iPin >= 0) {
    ledcDetach(iPin);  // safely stop PWM on this pin
  }
#else
  if (_rtttl_channel >= 0) {
    ledcWrite(_rtttl_channel, 0);
  }
#endif
}

void tone(int iPin, int frq, int duration = 0) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  // Core 3.x automatically manages channels
  ledcAttach(iPin, frq, 10);   // attach pin at freq 10-bit res
  ledcWriteTone(iPin, frq);
#else
  // Core 2.x: manually find or reuse a LEDC channel
  if (_rtttl_channel < 0) {
    for (int ch = 0; ch < 16; ++ch) {
      if (ledcSetup(ch, frq, 10) > 0) {
        _rtttl_channel = ch;
        ledcAttachPin(iPin, ch);
        break;
      }
    }
    if (_rtttl_channel < 0) {
      Serial.println("No free LEDC channel found!");
      return;
    }
  } else {
    ledcSetup(_rtttl_channel, frq, 10);
    ledcAttachPin(iPin, _rtttl_channel);
  }
  ledcWriteTone(_rtttl_channel, frq);
  ledcWrite(_rtttl_channel, 255);
#endif
  (void)duration; // duration handled elsewhere
}

#endif // ESP32

// ---------------------------------------------------------------------------
// Begin playback
// ---------------------------------------------------------------------------

void begin(byte iPin, const char *iSongBuffer)
{
#ifdef RTTTL_NONBLOCKING_DEBUG
  Serial.print("playing: ");
  Serial.println(iSongBuffer);
#endif

  pin = iPin;
#if defined(ESP32)
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(pin, 1000, 10); // Core 3.x
  #else
    // Core 2.x: channel will be assigned dynamically in tone()
  #endif
#endif

  buffer = iSongBuffer;
  bufferIndex = 0;
  default_dur = 4;
  default_oct = 6;
  bpm = 63;
  playing = true;
  noteDelay = 0;

  noTone(pin);

  // Parse RTTTL header: d=, o=, b=
  while (*buffer != ':') buffer++; // skip name
  buffer++; // skip ':'

  int num;

  // default duration
  if (*buffer == 'd') {
    buffer += 2; // skip "d="
    num = 0;
    while (isdigit(*buffer)) {
      num = (num * 10) + (*buffer++ - '0');
    }
    if (num > 0) default_dur = num;
    buffer++; // skip comma
  }

  // default octave
  if (*buffer == 'o') {
    buffer += 2; // skip "o="
    num = *buffer++ - '0';
    if (num >= 3 && num <= 7) default_oct = num;
    buffer++; // skip comma
  }

  // BPM
  if (*buffer == 'b') {
    buffer += 2; // skip "b="
    num = 0;
    while (isdigit(*buffer)) {
      num = (num * 10) + (*buffer++ - '0');
    }
    bpm = num;
    buffer++; // skip colon
  }

  wholenote = (60L * 1000L / bpm) * 4; // ms per whole note

#ifdef RTTTL_NONBLOCKING_INFO
  Serial.print("bpm: "); Serial.println(bpm);
  Serial.print("wn: "); Serial.println(wholenote);
#endif
}

// ---------------------------------------------------------------------------
// Play next note
// ---------------------------------------------------------------------------

void nextnote()
{
  long duration;
  byte note;
  byte scale;

  noTone(pin);

  int num = 0;
  while (isdigit(*buffer)) {
    num = (num * 10) + (*buffer++ - '0');
  }

  if (num)
    duration = wholenote / num;
  else
    duration = wholenote / default_dur;

  note = 0;

  switch (*buffer) {
    case 'c': note = 1; break;
    case 'd': note = 3; break;
    case 'e': note = 5; break;
    case 'f': note = 6; break;
    case 'g': note = 8; break;
    case 'a': note = 10; break;
    case 'b': note = 12; break;
    case 'p':
    default: note = 0;
  }
  buffer++;

  if (*buffer == '#') { note++; buffer++; }
  if (*buffer == '.') { duration += duration / 2; buffer++; }

  if (isdigit(*buffer)) {
    scale = *buffer - '0';
    buffer++;
  } else {
    scale = default_oct;
  }

  scale += OCTAVE_OFFSET;

  if (*buffer == '.') {
    duration += duration / 2;
    buffer++;
  }

  if (*buffer == ',') buffer++;

  if (note) {
#ifdef RTTTL_NONBLOCKING_INFO
    Serial.print("Playing: ");
    Serial.print(scale); Serial.print(' ');
    Serial.print(note); Serial.print(" (");
    Serial.print(notes[(scale - 4) * 12 + note]);
    Serial.print(") "); Serial.println(duration);
#endif
    tone(pin, notes[(scale - 4) * 12 + note], duration);
    noteDelay = millis() + (duration + 1);
  } else {
#ifdef RTTTL_NONBLOCKING_INFO
    Serial.print("Pause: ");
    Serial.println(duration);
#endif
    noteDelay = millis() + duration;
  }
}

// ---------------------------------------------------------------------------
// Non-blocking playback
// ---------------------------------------------------------------------------

void play()
{
  if (!playing)
    return;

  unsigned long m = millis();
  if (m < noteDelay)
    return;

  if (*buffer == '\0') {
    stop();
    return;
  } else {
    nextnote();
  }
}

// ---------------------------------------------------------------------------
// Stop playback
// ---------------------------------------------------------------------------

void stop()
{
  if (playing) {
    while (*buffer != '\0') buffer++;
    noTone(pin);
    playing = false;
  }
}

// ---------------------------------------------------------------------------
// State checks
// ---------------------------------------------------------------------------

bool done() { return !playing; }
bool isPlaying() { return playing; }

}; // namespace rtttl
