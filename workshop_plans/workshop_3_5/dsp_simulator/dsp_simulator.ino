// This program reads samples from either an analog input or signal "emulator" and prints it to Serial.
//
// If on Windows, use the Serial Plotter under Tools in the Arduino IDE to view a real-time plot of the output.
// Serial Plotter is also supposed to be available on Linux, but I can't find it at the moment.
//
// This program has two modes:
// 1. use_emulator = false
//     This mode is essentially what the Arduino will do in the EMG pipeline. It reads one sample from
//     pin A5 (which will connect to the circuit output) and sends it to a computer with Serial.println().
//     If you don't have the EMG circuit, any analog sensor can serve as a replacement.
//
// 2. use_emulator = true;
//    In this mode, instead of taking samples from an input pin, samples are taken from a Tone_Gen object that
//    basically spouts out samples of a sine wave. Generating test vectors (or signals) to test pipelines independently
//    of other components (the EMG circuit in our case) is an industry practice. In this program, I include the
//    option to simulate "noise" in a signal by setting inject_noise to true and changing the noise_frequency and SNR parameters.
//    
//    Note: Theoretically any signal can be generated by summing together the outputs of Tone_Gen objects and varying the amplitude
//          and frequency parameters. 
//
//    I've coded two "effects": rectification (only applies to emulator), saturation (applies to emulator and real input signal).
//    Effects can be turned on/off by setting the appropriate variable under Options to true or false. 
//    Filters can also be created by passing an array of filter coefficients to the convolve_signal() function.
//
// In order to compile you need to make sure you copy the Tone_Gen folder 
// into your Arduino libaries folder.
// On Windows, this would most likely be in Program Files/Arduino/libraries/. On Linux, this would be wherever you installed Arduino.
// To check if the library was properly added, in the Arduino IDE, check that "Tone_Gen" is found under
// Sketch > Include Library > Contributed Libraries

#include <Tone_Gen.h>

////////////////////// Options //////////////////////////////

// Set to true if you want to generate your own input wave.
// Set to false if you want to read data from your analog input.
boolean use_emulator = true;

///////////////////// Effects ///////////////////////////////

// Set to true if you want to limit the emulator output between 0V and 5V.
// If not using the emulator, the Arduino ADC will do this automatically
boolean saturate = true;

// Set to true to introduce some noise into the emulator
boolean inject_noise = true;

// Set to true if you want to rectify the emulator output, which means
// that any sample with a value below 0 will have its sign reversed.
// If not using the emulator, analogRead() will not return a negative number,
// so this will have no effect.
boolean rectify = false;

// Moving average filter
boolean average = true;

// This effect is toggled on or off with the switch
boolean *switch_effect = &(average);

//////// Configurable parameters //////////

// Frequency of the sine wave generated by the emulator
double emulator_frequency = 10;

// Noise is usually random, but here I just use a frequency much higher than the emulator frequency.
double noise_frequency = 300;

// Make sure the sample rate is greater than twice the emulator frequency.
// If equal or lower, you will get aliasing, and the output will not be
// as intended. 
// To observe aliasing:
//    1. Set use_emulator to true
//    2. Set emulator frequency to an integer greater than half the sample_rate.
// 
// Note: 2400 SPS was the best case sampling rate achievable on the Arduino with no buffering
int sample_rate = 2400;

// SNR = Signal to Noise Ratio ~ The ratio in amplitudes between a signal of interest and noise
double SNR = 1;

// LED threshold
// Flash an LED, if the reading is above this threshold
double threshold = 2.5;

/////////// Constants /////////////////////
const int BITS_PER_SAMPLE = 10;

// Peak to peak voltage of incoming signal
const double VPP = 5.0;

int led_pin = 2;
int ana_port = A5;
int switch_pin = 9;
///////////////////////////////////////////

Tone_Gen *signal_emulator;
Tone_Gen *noise_emulator;

int resolution;
const int N_TAPS = 20;

int window_pntr = 0;
double window[N_TAPS];

void setup() {

  // Make sure what ever you put here matches your Serial Monitor setting
  Serial.begin(9600);
  Serial.flush();
  pinMode(ana_port, INPUT);
  pinMode(switch_pin, INPUT);
  pinMode(led_pin, OUTPUT);

  resolution = pow(2, BITS_PER_SAMPLE);

  // Signal of interest
  signal_emulator = new Tone_Gen(sample_rate);

  // Noise
  noise_emulator = new Tone_Gen(sample_rate);
  randomSeed(analogRead(0));
  
} // end setup()

void loop() {
  // Coomment this out if you don't have a switch
  *switch_effect = digitalRead(switch_pin);

  double sample;

  if (use_emulator) {
    sample = get_next_emulator_sample();
  } else {
    // Sample the analog input into pin A5 and convert it to a real value
    sample = analogRead(ana_port);
    sample *= VPP / resolution;
  }

  // Inject noise into the analog sensor reading. Randomize the amplitude and frequency
  if (inject_noise) add_noise(sample);

  // Rectify signal
  if (rectify) rectify_signal(sample);

  // Saturate signals at 0V and 5V
  if (saturate) saturate_signal(sample);

  // Average the signal
  if (average) average_signal(sample);

  Serial.println(sample);

  if (sample > threshold) digitalWrite(led_pin, HIGH);
  else digitalWrite(led_pin, LOW);
} // end loop()

///////////////////// DO NOT MODIFY THE CODE BELOW //////////////////////////////
//////////////// (If you want to experiment, go ahead!) //////////////////////////

/////////// Functions ///////////////////

double get_next_emulator_sample() {
  double sample;
  // Get the next sample of a <emulator_frequency> Hz sine wave with a peak amplitude of 2.5 v
  sample = signal_emulator->nextSample(emulator_frequency, VPP / 2);

  // If not rectifying, add a DC offset
  if (!rectify) {
    // Add a DC offset of 2.5 to shift output between 0V and 5V
    sample += VPP / 2;
  }

  return sample;
}

// Add noise to the analog sensor reading. Randomize the amplitude and frequency to emulate the randomness of noise
void add_noise(double &sample) {
  double freq = random(noise_frequency * 0.8, noise_frequency * 1.2);
  double amp = random(0, VPP);
  sample += noise_emulator->nextSample(freq, (amp / 2) / SNR);
}

void rectify_signal(double &sample) {
  if (sample < 0)
    sample = -sample;
}

void saturate_signal(double &sample) {
  if (sample < 0)
    sample = 0;
  else if (sample > VPP)
    sample = VPP;
}

// Perform convolution to filter the signal:
void convolve_signal(double &sample, double coefficients [N_TAPS]) {
  double sum = 0;
  double new_sample = sample;
  double conv[N_TAPS];

  if (window_pntr < N_TAPS) {
    window[window_pntr] = sample;
    window_pntr++;
   
  } else {
    for (int m = N_TAPS - 1; m >= 0; m--) {

      // Move window
      if (m == 0) 
        window[m] = new_sample;
      else
        window[m] = window[m-1];

      // Accumulate convolution
      conv[m] = window[m] * coefficients[m];
      sum += conv[m];
    }
  }
  sample = sum;
}

void average_signal(double &sample) {

  double coeff [N_TAPS];
  // Create filter
  for (int t = 0; t < N_TAPS; t++) {
    coeff[t] = 1.0 / N_TAPS;
  }
  
  convolve_signal(sample, coeff);

}

// The coefficients for this filter were generated by http://www.micromodeler.com/dsp/
void high_pass_signal(double &sample) {

  double coeff [N_TAPS] = {
  0.0000000, 0.0000000, -0.0085160825, -0.035945730, 0.044057272, 0.0029687059,
  0.11244717, -0.069637615, 0.033938639, -0.41136178, 0.35087880, 0.35087880,
  -0.41136178, 0.033938639, -0.069637615, 0.11244717, 0.0029687059, 0.044057272,
  -0.035945730, -0.0085160825
};

  convolve_signal(sample, coeff);
}










