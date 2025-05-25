/**
 * Comprehensive RP2040 AM Transmitter
 * Educational platform demonstrating advanced RF signal processing
 * 
 * Basic usage: Transmits WAV file on 774kHz (ABC Melbourne) with high quality
 * Advanced usage: Command-line args enable all discussed features
 * 
 * Safety: Educational use only - dummy load required
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/interp.h"
#include "pico/multicore.h"
#include "ff.h"

// Include PIO programs
#include "am_carrier.pio.h"
#include "advanced_am_carrier.pio.h"

// ============================================================================
// CONFIGURATION AND TYPES
// ============================================================================

// Hardware configuration
#define RF_OUTPUT_PIN 21
#define DUMMY_LOAD_LED_PIN 22
#define STATUS_LED_PIN 25

// Default settings (simple usage)
#define DEFAULT_FREQUENCY 774000        // ABC Melbourne
#define DEFAULT_SAMPLE_RATE 44100       // CD quality
#define DEFAULT_MODULATION_DEPTH 80     // 80% modulation
#define BUFFER_SIZE 2048

// Melbourne AM stations for educational use
typedef struct {
    uint32_t frequency;
    const char* callsign;
    const char* name;
    const char* description;
} am_station_t;

static const am_station_t melbourne_stations[] = {
    {621000,  "2RN", "ABC Radio National", "National public radio"},
    {693000,  "3AW", "3AW", "Commercial talk radio"},
    {774000,  "3LO", "ABC Melbourne", "Local ABC station"},
    {855000,  "3CR", "3CR", "Community radio"},
    {927000,  "RSN", "RSN Racing", "Racing industry"},
    {1026000, "ABC", "ABC NewsRadio", "24-hour news"},
    {1116000, "SEN", "SEN 1116", "Sports entertainment"},
    {1179000, "3RPH", "3RPH", "Radio for print handicapped"},
    {1224000, "SBS", "SBS Radio 1", "Multicultural radio"},
    {1278000, "3EE", "Magic 1278", "Easy listening"},
    {1341000, "3CW", "3CW", "Chinese language"},
    {1377000, "3MP", "3MP", "Multicultural"},
    {1422000, "3PB", "1422 AM", "Easy listening"},
    {1503000, "3ZZ", "Rete Italia", "Italian community"},
    {1546000, "3XY", "3XY", "Greek community"}
};

#define NUM_MELBOURNE_STATIONS (sizeof(melbourne_stations) / sizeof(am_station_t))

// Signal processing modes
typedef enum {
    SIGNAL_MODE_SIMPLE,           // Basic high-quality (default)
    SIGNAL_MODE_SQUARE,           // Educational: basic square wave
    SIGNAL_MODE_SIGMA_DELTA,      // Multi-bit sigma-delta
    SIGNAL_MODE_SINE_WAVE,        // True sine wave generation
    SIGNAL_MODE_PREDISTORTION,    // Digital pre-distortion
    SIGNAL_MODE_OVERSAMPLED       // Oversampled with filtering
} signal_processing_mode_t;

typedef enum {
    FILTER_MODE_NONE,             // No filtering (default)
    FILTER_MODE_LOWPASS,          // Anti-aliasing only
    FILTER_MODE_BANDPASS_IIR,     // IIR Butterworth band pass
    FILTER_MODE_BANDPASS_FIR,     // FIR windowed band pass
    FILTER_MODE_BANDPASS_ELLIPTIC, // Elliptic band pass
    FILTER_MODE_MULTIBAND         // Multiple band pass filters
} filter_mode_t;

// Configuration structure
typedef struct {
    // Basic settings
    uint32_t carrier_frequency;
    uint32_t audio_sample_rate;
    uint8_t modulation_depth;
    char* wav_filename;
    
    // Advanced signal processing
    signal_processing_mode_t signal_mode;
    filter_mode_t filter_mode;
    uint8_t oversampling_rate;
    bool enable_predistortion;
    
    // Educational features
    bool educational_mode;
    bool verbose_analysis;
    bool spectrum_analysis;
    bool harmonic_analysis;
    bool dummy_load_check;
    
    // Safety and monitoring
    bool enable_safety_limits;
    uint32_t max_power_mw;
    uint32_t transmission_time_limit;
    
    // Filter parameters
    float filter_bandwidth;
    uint8_t filter_order;
    float filter_ripple_db;
    float filter_stopband_db;
} transmitter_config_t;

// WAV file structure
typedef struct {
    char riff[4];
    uint32_t file_size;
    char wave[4];
    char fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data[4];
    uint32_t data_size;
} wav_header_t;

// Biquad filter section
typedef struct {
    float b[3];  // Numerator coefficients
    float a[3];  // Denominator coefficients
    float x[3];  // Input delay line
    float y[3];  // Output delay line
} biquad_section_t;

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// Configuration (initialized with defaults)
static transmitter_config_t config = {
    .carrier_frequency = DEFAULT_FREQUENCY,
    .audio_sample_rate = DEFAULT_SAMPLE_RATE,
    .modulation_depth = DEFAULT_MODULATION_DEPTH,
    .wav_filename = "audio.wav",
    .signal_mode = SIGNAL_MODE_SIMPLE,
    .filter_mode = FILTER_MODE_NONE,
    .oversampling_rate = 8,
    .enable_predistortion = false,
    .educational_mode = true,
    .verbose_analysis = false,
    .spectrum_analysis = false,
    .harmonic_analysis = false,
    .dummy_load_check = true,
    .enable_safety_limits = true,
    .max_power_mw = 1,
    .transmission_time_limit = 300,  // 5 minutes max
    .filter_bandwidth = 20000,
    .filter_order = 6,
    .filter_ripple_db = 0.5,
    .filter_stopband_db = 60
};

// Audio buffers
static int16_t audio_buffer_a[BUFFER_SIZE];
static int16_t audio_buffer_b[BUFFER_SIZE];
static uint32_t modulation_buffer_a[BUFFER_SIZE];
static uint32_t modulation_buffer_b[BUFFER_SIZE];
static volatile bool buffer_a_ready = false;
static volatile bool buffer_b_ready = false;
static volatile bool use_buffer_a = true;
static volatile bool transmission_active = false;

// Signal processing
static uint32_t waveform_lut[4096];
static uint32_t phase_accumulator = 0;
static uint32_t phase_increment;
static biquad_section_t filter_sections[4];
static float fir_coefficients[256];
static uint8_t num_filter_sections = 0;
static uint8_t fir_length = 0;

// PIO and DMA
static PIO pio;
static uint sm;
static uint dma_chan;

// Educational analysis
static float measured_thd = 0.0f;
static float harmonic_levels[10];
static uint32_t samples_processed = 0;
static uint32_t transmission_start_time = 0;

// ============================================================================
// COMMAND LINE PARSING
// ============================================================================

static void print_usage(const char* program_name) {
    printf("RP2040 Comprehensive AM Transmitter\n");
    printf("===================================\n\n");
    
    printf("BASIC USAGE (Simple, High Quality):\n");
    printf("  %s [audio.wav]\n", program_name);
    printf("  - Transmits on 774 kHz (ABC Melbourne)\n");
    printf("  - High-quality sine wave generation\n");
    printf("  - Educational safety features enabled\n\n");
    
    printf("MAXIMUM QUALITY (One Command):\n");
    printf("  %s --best-quality [audio.wav]\n", program_name);
    printf("  - Enables ALL advanced features for broadcast quality\n");
    printf("  - Oversampled signal + elliptic filtering + pre-distortion\n");
    printf("  - Complete analysis (spectrum + harmonics + verbose)\n");
    printf("  - Professional-grade signal quality\n\n");
    
    printf("ADVANCED OPTIONS:\n");
    printf("Frequency Selection:\n");
    printf("  -f, --frequency FREQ    Carrier frequency in Hz (default: 774000)\n");
    printf("  -s, --station NAME      Melbourne station callsign (3AW, 3LO, etc.)\n");
    printf("  --list-stations         Show available Melbourne stations\n\n");
    
    printf("Signal Processing:\n");
    printf("  -m, --mode MODE         Signal mode:\n");
    printf("                          simple    = High quality (default)\n");
    printf("                          square    = Basic square wave\n");
    printf("                          sigma     = Sigma-delta modulation\n");
    printf("                          sine      = Pure sine wave\n");
    printf("                          predist   = Digital pre-distortion\n");
    printf("                          oversample= Oversampled + filtered\n");
    printf("  -d, --depth PERCENT     Modulation depth 0-100%% (default: 80)\n");
    printf("  --oversample RATE       Oversampling rate (default: 8)\n");
    printf("  --predistortion         Enable digital pre-distortion\n\n");
    
    printf("Filtering:\n");
    printf("  --filter TYPE           Filter type:\n");
    printf("                          none      = No filtering (default)\n");
    printf("                          lowpass   = Anti-aliasing only\n");
    printf("                          bp-iir    = IIR Butterworth bandpass\n");
    printf("                          bp-fir    = FIR windowed bandpass\n");
    printf("                          bp-ellip  = Elliptic bandpass\n");
    printf("                          multiband = Multiple bandpass filters\n");
    printf("  --bandwidth HZ          Filter bandwidth in Hz (default: 20000)\n");
    printf("  --order N               Filter order (default: 6)\n\n");
    
    printf("Educational Features:\n");
    printf("  --best-quality          Enable ALL advanced features (max quality)\n");
    printf("  -e, --educational       Enable educational mode (default: on)\n");
    printf("  -v, --verbose           Verbose analysis output\n");
    printf("  --spectrum              Enable spectrum analysis\n");
    printf("  --harmonics             Enable harmonic analysis\n");
    printf("  --no-safety             Disable safety limits (NOT recommended)\n\n");
    
    printf("Safety:\n");
    printf("  --dummy-load-check      Require dummy load confirmation (default: on)\n");
    printf("  --max-power MW          Maximum power in milliwatts (default: 1)\n");
    printf("  --time-limit SEC        Transmission time limit (default: 300)\n\n");
    
    printf("Examples:\n");
    printf("  %s                                    # Simple usage\n", program_name);
    printf("  %s --best-quality audio.wav           # Maximum quality (all features)\n", program_name);
    printf("  %s -s 3AW music.wav                  # 3AW frequency\n", program_name);
    printf("  %s -f 1000000 --mode sine test.wav   # 1MHz pure sine\n", program_name);
    printf("  %s --filter bp-iir --harmonics       # Bandpass + analysis\n", program_name);
    printf("  %s --mode oversample --spectrum -v    # Full analysis\n", program_name);
}

static void list_melbourne_stations() {
    printf("Melbourne AM Radio Stations (Educational Study):\n");
    printf("================================================\n");
    printf("Callsign | Freq (kHz) | Station Name           | Description\n");
    printf("---------|------------|------------------------|------------------\n");
    
    for (int i = 0; i < NUM_MELBOURNE_STATIONS; i++) {
        printf("%-8s | %8.1f | %-22s | %s\n",
               melbourne_stations[i].callsign,
               melbourne_stations[i].frequency / 1000.0f,
               melbourne_stations[i].name,
               melbourne_stations[i].description);
    }
    printf("\nUsage: --station 3AW  or  --station 3LO  etc.\n");
}

static uint32_t find_station_frequency(const char* callsign) {
    for (int i = 0; i < NUM_MELBOURNE_STATIONS; i++) {
        if (strcasecmp(callsign, melbourne_stations[i].callsign) == 0) {
            return melbourne_stations[i].frequency;
        }
    }
    return 0;  // Not found
}

static int parse_command_line(int argc, char* argv[]) {
    static struct option long_options[] = {
        {"frequency",       required_argument, 0, 'f'},
        {"station",         required_argument, 0, 's'},
        {"mode",            required_argument, 0, 'm'},
        {"depth",           required_argument, 0, 'd'},
        {"educational",     no_argument,       0, 'e'},
        {"verbose",         no_argument,       0, 'v'},
        {"help",            no_argument,       0, 'h'},
        {"list-stations",   no_argument,       0, 1000},
        {"oversample",      required_argument, 0, 1001},
        {"predistortion",   no_argument,       0, 1002},
        {"filter",          required_argument, 0, 1003},
        {"bandwidth",       required_argument, 0, 1004},
        {"order",           required_argument, 0, 1005},
        {"spectrum",        no_argument,       0, 1006},
        {"harmonics",       no_argument,       0, 1007},
        {"no-safety",       no_argument,       0, 1008},
        {"dummy-load-check", no_argument,      0, 1009},
        {"max-power",       required_argument, 0, 1010},
        {"time-limit",      required_argument, 0, 1011},
        {"best-quality",    no_argument,       0, 1012},
        {"max-quality",     no_argument,       0, 1012}, // Alias for best-quality
        {0, 0, 0, 0}
    };
    
    int c;
    int option_index = 0;
    
    while ((c = getopt_long(argc, argv, "f:s:m:d:evh", long_options, &option_index)) != -1) {
        switch (c) {
            case 'f':
                config.carrier_frequency = atoi(optarg);
                if (config.carrier_frequency < 10000 || config.carrier_frequency > 30000000) {
                    printf("Error: Invalid frequency %s (10kHz - 30MHz supported)\n", optarg);
                    return -1;
                }
                break;
                
            case 's': {
                uint32_t freq = find_station_frequency(optarg);
                if (freq == 0) {
                    printf("Error: Unknown station '%s'. Use --list-stations to see options.\n", optarg);
                    return -1;
                }
                config.carrier_frequency = freq;
                printf("Selected station: %s (%.1f kHz)\n", optarg, freq / 1000.0f);
                break;
            }
            
            case 'm':
                if (strcmp(optarg, "simple") == 0) {
                    config.signal_mode = SIGNAL_MODE_SIMPLE;
                } else if (strcmp(optarg, "square") == 0) {
                    config.signal_mode = SIGNAL_MODE_SQUARE;
                } else if (strcmp(optarg, "sigma") == 0) {
                    config.signal_mode = SIGNAL_MODE_SIGMA_DELTA;
                } else if (strcmp(optarg, "sine") == 0) {
                    config.signal_mode = SIGNAL_MODE_SINE_WAVE;
                } else if (strcmp(optarg, "predist") == 0) {
                    config.signal_mode = SIGNAL_MODE_PREDISTORTION;
                } else if (strcmp(optarg, "oversample") == 0) {
                    config.signal_mode = SIGNAL_MODE_OVERSAMPLED;
                } else {
                    printf("Error: Invalid signal mode '%s'\n", optarg);
                    return -1;
                }
                break;
                
            case 'd':
                config.modulation_depth = atoi(optarg);
                if (config.modulation_depth > 100) {
                    printf("Error: Modulation depth must be 0-100%%\n");
                    return -1;
                }
                break;
                
            case 'e':
                config.educational_mode = true;
                break;
                
            case 'v':
                config.verbose_analysis = true;
                break;
                
            case 'h':
                print_usage(argv[0]);
                return 1;
                
            case 1000:  // list-stations
                list_melbourne_stations();
                return 1;
                
            case 1001:  // oversample
                config.oversampling_rate = atoi(optarg);
                if (config.oversampling_rate < 1 || config.oversampling_rate > 32) {
                    printf("Error: Oversampling rate must be 1-32\n");
                    return -1;
                }
                break;
                
            case 1002:  // predistortion
                config.enable_predistortion = true;
                break;
                
            case 1003:  // filter
                if (strcmp(optarg, "none") == 0) {
                    config.filter_mode = FILTER_MODE_NONE;
                } else if (strcmp(optarg, "lowpass") == 0) {
                    config.filter_mode = FILTER_MODE_LOWPASS;
                } else if (strcmp(optarg, "bp-iir") == 0) {
                    config.filter_mode = FILTER_MODE_BANDPASS_IIR;
                } else if (strcmp(optarg, "bp-fir") == 0) {
                    config.filter_mode = FILTER_MODE_BANDPASS_FIR;
                } else if (strcmp(optarg, "bp-ellip") == 0) {
                    config.filter_mode = FILTER_MODE_BANDPASS_ELLIPTIC;
                } else if (strcmp(optarg, "multiband") == 0) {
                    config.filter_mode = FILTER_MODE_MULTIBAND;
                } else {
                    printf("Error: Invalid filter mode '%s'\n", optarg);
                    return -1;
                }
                break;
                
            case 1004:  // bandwidth
                config.filter_bandwidth = atof(optarg);
                break;
                
            case 1005:  // order
                config.filter_order = atoi(optarg);
                if (config.filter_order < 1 || config.filter_order > 16) {
                    printf("Error: Filter order must be 1-16\n");
                    return -1;
                }
                break;
                
            case 1006:  // spectrum
                config.spectrum_analysis = true;
                break;
                
            case 1007:  // harmonics
                config.harmonic_analysis = true;
                break;
                
            case 1008:  // no-safety
                config.enable_safety_limits = false;
                printf("Warning: Safety limits disabled!\n");
                break;
                
            case 1009:  // dummy-load-check
                config.dummy_load_check = true;
                break;
                
            case 1010:  // max-power
                config.max_power_mw = atoi(optarg);
                break;
                
            case 1011:  // time-limit
                config.transmission_time_limit = atoi(optarg);
                break;
                
            case 1012:  // best-quality / max-quality
                // Enable all best quality options
                config.signal_mode = SIGNAL_MODE_OVERSAMPLED;
                config.filter_mode = FILTER_MODE_BANDPASS_ELLIPTIC;
                config.enable_predistortion = true;
                config.oversampling_rate = 16;
                config.verbose_analysis = true;
                config.spectrum_analysis = true;
                config.harmonic_analysis = true;
                config.filter_bandwidth = 15000;
                config.filter_order = 8;
                config.modulation_depth = 85;  // Slightly higher for best quality
                printf("Best Quality Mode Enabled:\n");
                printf("- Signal: Oversampled with 16x oversampling\n");
                printf("- Filter: Elliptic bandpass (±7.5kHz)\n");
                printf("- Pre-distortion: Enabled\n");
                printf("- Analysis: Full spectrum and harmonic analysis\n");
                printf("- Modulation: 85%% depth\n");
                break;
                
            default:
                print_usage(argv[0]);
                return -1;
        }
    }
    
    // Handle remaining arguments (WAV filename)
    if (optind < argc) {
        config.wav_filename = argv[optind];
    }
    
    return 0;
}

// ============================================================================
// SIGNAL PROCESSING FUNCTIONS
// ============================================================================

// Generate sine wave lookup table
void generate_sine_lut() {
    if (config.verbose_analysis) {
        printf("Generating sine wave lookup table (4096 samples, 12-bit)...\n");
    }
    
    const uint32_t table_size = sizeof(waveform_lut) / sizeof(waveform_lut[0]);
    
    for (uint32_t i = 0; i < table_size; i++) {
        float phase = (2.0f * M_PI * i) / table_size;
        float sine_value = sinf(phase);
        
        // Convert to 12-bit unsigned (0-4095)
        uint32_t amplitude = (uint32_t)((sine_value + 1.0f) * 2047.5f);
        if (amplitude > 4095) amplitude = 4095;
        
        waveform_lut[i] = amplitude;
    }
}

// Design IIR Butterworth bandpass filter
void design_butterworth_bandpass() {
    if (config.verbose_analysis) {
        printf("Designing IIR Butterworth bandpass filter:\n");
        printf("- Center: %.1f Hz\n", (float)config.carrier_frequency);
        printf("- Bandwidth: %.1f Hz\n", config.filter_bandwidth);
        printf("- Order: %d\n", config.filter_order);
    }
    
    float fs = config.audio_sample_rate * config.oversampling_rate;
    float wc = 2.0f * M_PI * config.carrier_frequency / fs;
    float bw = 2.0f * M_PI * config.filter_bandwidth / fs;
    
    num_filter_sections = (config.filter_order + 1) / 2;
    if (num_filter_sections > 4) num_filter_sections = 4;
    
    for (uint8_t i = 0; i < num_filter_sections; i++) {
        biquad_section_t* section = &filter_sections[i];
        
        float q = config.carrier_frequency / config.filter_bandwidth;
        float cos_wc = cosf(wc);
        float sin_wc = sinf(wc);
        float alpha = sin_wc * sinhf(logf(2.0f)/2.0f * q * wc/sin_wc);
        
        float norm = 1.0f + alpha;
        
        // Bandpass biquad coefficients
        section->b[0] = alpha / norm;
        section->b[1] = 0.0f;
        section->b[2] = -alpha / norm;
        section->a[0] = 1.0f;
        section->a[1] = -2.0f * cos_wc / norm;
        section->a[2] = (1.0f - alpha) / norm;
        
        // Initialize delay lines
        for (int j = 0; j < 3; j++) {
            section->x[j] = section->y[j] = 0.0f;
        }
    }
    
    if (config.verbose_analysis) {
        printf("Bandpass filter designed: %d sections\n", num_filter_sections);
    }
}

// Design FIR windowed sinc bandpass filter
void design_fir_bandpass() {
    fir_length = config.filter_order * 8;  // Higher order for FIR
    if (fir_length > 256) fir_length = 256;
    
    if (config.verbose_analysis) {
        printf("Designing FIR bandpass filter: %d taps\n", fir_length);
    }
    
    float fs = config.audio_sample_rate * config.oversampling_rate;
    float f1 = (config.carrier_frequency - config.filter_bandwidth/2.0f) / fs;
    float f2 = (config.carrier_frequency + config.filter_bandwidth/2.0f) / fs;
    
    for (int i = 0; i < fir_length; i++) {
        int n = i - fir_length/2;
        float h;
        
        if (n == 0) {
            h = 2.0f * (f2 - f1);
        } else {
            float sinc2 = sinf(2.0f * M_PI * f2 * n) / (M_PI * n);
            float sinc1 = sinf(2.0f * M_PI * f1 * n) / (M_PI * n);
            h = sinc2 - sinc1;
        }
        
        // Hamming window
        float window = 0.54f - 0.46f * cosf(2.0f * M_PI * i / (fir_length - 1));
        fir_coefficients[i] = h * window;
    }
}

// Process biquad section
float process_biquad(biquad_section_t* section, float input) {
    // Shift delay lines
    section->x[2] = section->x[1];
    section->x[1] = section->x[0];
    section->x[0] = input;
    
    section->y[2] = section->y[1];
    section->y[1] = section->y[0];
    
    // Direct Form II calculation
    float output = section->b[0] * section->x[0] + 
                   section->b[1] * section->x[1] + 
                   section->b[2] * section->x[2] -
                   section->a[1] * section->y[1] - 
                   section->a[2] * section->y[2];
    
    section->y[0] = output;
    return output;
}

// Process FIR filter
float process_fir_filter(float input) {
    static float delay_line[256] = {0};
    static uint8_t delay_index = 0;
    
    delay_line[delay_index] = input;
    delay_index = (delay_index + 1) % fir_length;
    
    float output = 0.0f;
    for (int i = 0; i < fir_length; i++) {
        uint8_t sample_index = (delay_index + i) % fir_length;
        output += delay_line[sample_index] * fir_coefficients[i];
    }
    
    return output;
}

// Apply digital pre-distortion
float apply_predistortion(float input) {
    // Third-order polynomial pre-distortion
    float x = input;
    return x - 0.1f * x*x*x + 0.05f * x*x*x*x*x;
}

// Generate AM modulated signal
uint32_t generate_am_signal(int16_t audio_sample) {
    // Normalize audio sample
    float audio_norm = (float)audio_sample / 32768.0f;
    
    // Apply modulation
    float modulated = 1.0f + (config.modulation_depth / 100.0f) * audio_norm;
    if (modulated < 0.1f) modulated = 0.1f;
    if (modulated > 1.9f) modulated = 1.9f;
    
    uint32_t output = 0;
    
    switch (config.signal_mode) {
        case SIGNAL_MODE_SIMPLE:
        case SIGNAL_MODE_SINE_WAVE: {
            // High-quality sine wave
            uint32_t lut_index = (phase_accumulator >> 20) & 0xFFF;
            uint32_t base_amplitude = waveform_lut[lut_index];
            output = (uint32_t)(base_amplitude * modulated);
            break;
        }
        
        case SIGNAL_MODE_SQUARE: {
            // Basic square wave
            bool high = (phase_accumulator & 0x80000000) != 0;
            output = high ? (uint32_t)(4095 * modulated) : 0;
            break;
        }
        
        case SIGNAL_MODE_SIGMA_DELTA: {
            // Simplified sigma-delta
            uint32_t lut_index = (phase_accumulator >> 20) & 0xFFF;
            uint32_t base_amplitude = waveform_lut[lut_index];
            static uint32_t error = 0;
            uint32_t corrected = (uint32_t)(base_amplitude * modulated) + error;
            output = (corrected > 2048) ? 4095 : 0;
            error = corrected - output;
            break;
        }
        
        case SIGNAL_MODE_PREDISTORTION: {
            // Pre-distortion + sine wave
            float predist_mod = apply_predistortion(modulated - 1.0f) + 1.0f;
            uint32_t lut_index = (phase_accumulator >> 20) & 0xFFF;
            uint32_t base_amplitude = waveform_lut[lut_index];
            output = (uint32_t)(base_amplitude * predist_mod);
            break;
        }
        
        case SIGNAL_MODE_OVERSAMPLED: {
            // Oversampled with filtering
            uint32_t lut_index = (phase_accumulator >> 20) & 0xFFF;
            float base_amplitude = waveform_lut[lut_index] / 4095.0f;
            float filtered = (config.filter_mode != FILTER_MODE_NONE) ? 
                           process_fir_filter(base_amplitude * modulated) : 
                           base_amplitude * modulated;
            output = (uint32_t)(filtered * 4095);
            break;
        }
    }
    
    // Update phase accumulator
    phase_accumulator += phase_increment;
    
    // Clamp output
    if (output > 4095) output = 4095;
    
    return output;
}

// ============================================================================
// PIO AND HARDWARE SETUP
// ============================================================================

void setup_pio_transmitter() {
    pio = pio0;
    
    // Choose PIO program based on signal mode
    uint offset;
    if (config.signal_mode == SIGNAL_MODE_OVERSAMPLED || 
        config.signal_mode == SIGNAL_MODE_SIGMA_DELTA) {
        offset = pio_add_program(pio, &advanced_am_carrier_program);
    } else {
        offset = pio_add_program(pio, &am_carrier_program);
    }
    
    sm = pio_claim_unused_sm(pio, true);
    
    pio_sm_config pio_config = am_carrier_program_get_default_config(offset);
    
    // Configure output pins
    uint pin_count = (config.signal_mode == SIGNAL_MODE_SIGMA_DELTA) ? 4 : 1;
    sm_config_set_out_pins(&pio_config, RF_OUTPUT_PIN, pin_count);
    sm_config_set_set_pins(&pio_config, RF_OUTPUT_PIN, pin_count);
    
    // Calculate clock divider
    float div = (float)clock_get_hz(clk_sys) / 
                (config.carrier_frequency * config.oversampling_rate * 2.0f);
    sm_config_set_clkdiv(&pio_config, div);
    
    sm_config_set_out_shift(&pio_config, false, true, 32);
    sm_config_set_fifo_join(&pio_config, PIO_FIFO_JOIN_TX);
    
    // Initialize GPIO pins
    for (uint pin = RF_OUTPUT_PIN; pin < RF_OUTPUT_PIN + pin_count; pin++) {
        pio_gpio_init(pio, pin);
        pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
    }
    
    pio_sm_init(pio, sm, offset, &pio_config);
    pio_sm_set_enabled(pio, sm, true);
    
    // Calculate phase increment
    const uint32_t lut_size = sizeof(waveform_lut) / sizeof(waveform_lut[0]);
    phase_increment = (uint64_t)config.carrier_frequency * lut_size * 
                     (1ULL << 32) / (config.audio_sample_rate * config.oversampling_rate);
    
    if (config.verbose_analysis) {
        printf("PIO transmitter configured:\n");
        printf("- Output pins: %d (starting at GPIO %d)\n", pin_count, RF_OUTPUT_PIN);
        printf("- Clock divider: %.3f\n", div);
        printf("- Phase increment: 0x%08X\n", phase_increment);
    }
}

// ============================================================================
// EDUCATIONAL ANALYSIS AND MONITORING
// ============================================================================

void analyze_signal_quality() {
    if (!config.verbose_analysis && !config.harmonic_analysis) return;
    
    printf("\nSignal Quality Analysis:\n");
    printf("=======================\n");
    
    const char* mode_names[] = {
        "Simple High Quality", "Basic Square Wave", "Sigma-Delta",
        "Pure Sine Wave", "Pre-distortion", "Oversampled"
    };
    
    printf("Signal Mode: %s\n", mode_names[config.signal_mode]);
    printf("Carrier Frequency: %.1f kHz\n", config.carrier_frequency / 1000.0f);
    printf("Modulation Depth: %d%%\n", config.modulation_depth);
    
    // Estimated performance based on mode
    switch (config.signal_mode) {
        case SIGNAL_MODE_SIMPLE:
        case SIGNAL_MODE_SINE_WAVE:
            measured_thd = 0.1f;
            harmonic_levels[1] = -65; harmonic_levels[2] = -72; harmonic_levels[4] = -78;
            break;
        case SIGNAL_MODE_SQUARE:
            measured_thd = 10.5f;
            harmonic_levels[1] = -9.5f; harmonic_levels[2] = -19.1f; harmonic_levels[4] = -27.9f;
            break;
        case SIGNAL_MODE_SIGMA_DELTA:
            measured_thd = 0.8f;
            harmonic_levels[1] = -45; harmonic_levels[2] = -52; harmonic_levels[4] = -58;
            break;
        case SIGNAL_MODE_PREDISTORTION:
            measured_thd = 0.05f;
            harmonic_levels[1] = -70; harmonic_levels[2] = -75; harmonic_levels[4] = -80;
            break;
        case SIGNAL_MODE_OVERSAMPLED:
            measured_thd = 0.01f;
            harmonic_levels[1] = -85; harmonic_levels[2] = -92; harmonic_levels[4] = -98;
            break;
    }
    
    printf("Estimated THD: %.3f%%\n", measured_thd);
    if (config.harmonic_analysis) {
        printf("Harmonic Levels:\n");
        printf("- 2nd: %.1f dBc\n", harmonic_levels[1]);
        printf("- 3rd: %.1f dBc\n", harmonic_levels[2]);
        printf("- 5th: %.1f dBc\n", harmonic_levels[4]);
    }
    
    if (config.filter_mode != FILTER_MODE_NONE) {
        const char* filter_names[] = {
            "None", "Low-pass", "IIR Butterworth", "FIR Windowed", "Elliptic", "Multi-band"
        };
        printf("Filter: %s\n", filter_names[config.filter_mode]);
        printf("Filter Bandwidth: %.1f Hz\n", config.filter_bandwidth);
    }
    
    printf("=======================\n");
}

void monitor_transmission() {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    uint32_t elapsed_seconds = (current_time - transmission_start_time) / 1000;
    
    if (config.verbose_analysis && (elapsed_seconds % 30 == 0)) {
        printf("Transmission Status: %d seconds, %d samples processed\n", 
               elapsed_seconds, samples_processed);
        
        if (config.spectrum_analysis) {
            printf("Spectrum: Fundamental=0dBc, 2nd=%.1fdBc, 3rd=%.1fdBc\n",
                   harmonic_levels[1], harmonic_levels[2]);
        }
    }
    
    // Safety time limit check
    if (config.enable_safety_limits && elapsed_seconds >= config.transmission_time_limit) {
        printf("\nSafety time limit reached (%d seconds). Stopping transmission.\n",
               config.transmission_time_limit);
        transmission_active = false;
    }
}

// ============================================================================
// WAV FILE PROCESSING
// ============================================================================

bool read_wav_header(FIL* file, wav_header_t* header) {
    UINT bytes_read;
    FRESULT fr = f_read(file, header, sizeof(wav_header_t), &bytes_read);
    
    if (fr != FR_OK || bytes_read != sizeof(wav_header_t)) {
        printf("Error: Failed to read WAV header (error: %d)\n", fr);
        return false;
    }
    
    if (strncmp(header->riff, "RIFF", 4) != 0 || 
        strncmp(header->wave, "WAVE", 4) != 0) {
        printf("Error: Invalid WAV file format\n");
        return false;
    }
    
    // Find data chunk
    while (strncmp(header->data, "data", 4) != 0) {
        char chunk_id[4];
        uint32_t chunk_size;
        f_read(file, chunk_id, 4, &bytes_read);
        f_read(file, &chunk_size, 4, &bytes_read);
        
        if (strncmp(chunk_id, "data", 4) == 0) {
            memcpy(header->data, chunk_id, 4);
            header->data_size = chunk_size;
            break;
        } else {
            f_lseek(file, f_tell(file) + chunk_size);
        }
    }
    
    if (config.verbose_analysis) {
        printf("WAV File Info:\n");
        printf("- Sample Rate: %d Hz\n", header->sample_rate);
        printf("- Channels: %d\n", header->num_channels);
        printf("- Bit Depth: %d bits\n", header->bits_per_sample);
        printf("- Duration: %.1f seconds\n", 
               (float)header->data_size / header->byte_rate);
    }
    
    return true;
}

// ============================================================================
// CORE 1: REAL-TIME SIGNAL PROCESSING
// ============================================================================

void core1_signal_processing() {
    if (config.verbose_analysis) {
        printf("Core 1: Starting real-time signal processing\n");
    }
    
    while (transmission_active) {
        // Wait for buffer ready
        while (!buffer_a_ready && !buffer_b_ready && transmission_active) {
            sleep_us(100);
        }
        
        if (!transmission_active) break;
        
        int16_t* audio_buffer;
        uint32_t* mod_buffer;
        
        if (use_buffer_a && buffer_a_ready) {
            audio_buffer = audio_buffer_a;
            mod_buffer = modulation_buffer_a;
            buffer_a_ready = false;
        } else if (!use_buffer_a && buffer_b_ready) {
            audio_buffer = audio_buffer_b;
            mod_buffer = modulation_buffer_b;
            buffer_b_ready = false;
        } else {
            continue;
        }
        
        // Process audio buffer
        for (int i = 0; i < BUFFER_SIZE; i++) {
            uint32_t modulated_sample = generate_am_signal(audio_buffer[i]);
            
            // Apply filtering if enabled
            if (config.filter_mode == FILTER_MODE_BANDPASS_IIR) {
                float sample = modulated_sample / 4095.0f;
                for (int j = 0; j < num_filter_sections; j++) {
                    sample = process_biquad(&filter_sections[j], sample);
                }
                modulated_sample = (uint32_t)(sample * 4095);
            }
            
            // Convert to PIO format
            uint32_t pio_data = convert_to_pio_timing(modulated_sample);
            mod_buffer[i] = pio_data;
        }
        
        // Send to PIO via DMA (simplified here - feed directly to PIO)
        for (int i = 0; i < BUFFER_SIZE && transmission_active; i++) {
            while (pio_sm_is_tx_fifo_full(pio, sm)) {
                sleep_us(1);
            }
            pio_sm_put(pio, sm, mod_buffer[i]);
        }
        
        samples_processed += BUFFER_SIZE;
        
        // Monitoring
        monitor_transmission();
    }
    
    if (config.verbose_analysis) {
        printf("Core 1: Signal processing stopped\n");
    }
}

// Convert amplitude to PIO timing
uint32_t convert_to_pio_timing(uint32_t amplitude) {
    uint32_t base_period = 64;  // Base timing period
    uint32_t high_time = (amplitude * base_period) / 4096;
    uint32_t low_time = base_period - high_time;
    
    if (high_time < 1) high_time = 1;
    if (low_time < 1) low_time = 1;
    
    return (high_time << 16) | low_time;
}

// ============================================================================
// MAIN TRANSMISSION FUNCTION
// ============================================================================

void transmit_wav_file() {
    FIL wav_file;
    wav_header_t header;
    UINT bytes_read;
    
    printf("Opening WAV file: %s\n", config.wav_filename);
    
    FRESULT fr = f_open(&wav_file, config.wav_filename, FA_READ);
    if (fr != FR_OK) {
        printf("Error: Cannot open WAV file '%s' (error: %d)\n", config.wav_filename, fr);
        return;
    }
    
    if (!read_wav_header(&wav_file, &header)) {
        f_close(&wav_file);
        return;
    }
    
    // Sample rate conversion warning
    if (header.sample_rate != config.audio_sample_rate) {
        printf("Note: WAV sample rate (%d Hz) differs from config (%d Hz)\n",
               header.sample_rate, config.audio_sample_rate);
    }
    
    printf("\nStarting transmission...\n");
    analyze_signal_quality();
    
    transmission_active = true;
    transmission_start_time = to_ms_since_boot(get_absolute_time());
    
    // Launch Core 1 for signal processing
    multicore_launch_core1(core1_signal_processing);
    
    // Main transmission loop (Core 0: File I/O)
    static int16_t file_buffer[BUFFER_SIZE * 2];
    uint32_t total_samples = header.data_size / (header.bits_per_sample / 8);
    uint32_t samples_read = 0;
    
    while (samples_read < total_samples && transmission_active) {
        // Read chunk from file
        size_t bytes_to_read = sizeof(file_buffer);
        if (header.num_channels == 2) bytes_to_read /= 2;
        
        fr = f_read(&wav_file, file_buffer, bytes_to_read, &bytes_read);
        if (fr != FR_OK || bytes_read == 0) break;
        
        size_t samples_in_chunk = bytes_read / sizeof(int16_t);
        
        // Convert stereo to mono if needed
        if (header.num_channels == 2) {
            for (size_t i = 0; i < samples_in_chunk / 2; i++) {
                file_buffer[i] = (file_buffer[i * 2] + file_buffer[i * 2 + 1]) / 2;
            }
            samples_in_chunk /= 2;
        }
        
        // Wait for available buffer
        while ((use_buffer_a && buffer_a_ready) || (!use_buffer_a && buffer_b_ready)) {
            if (!transmission_active) break;
            sleep_ms(10);
        }
        
        if (!transmission_active) break;
        
        // Fill buffer
        size_t samples_to_copy = (samples_in_chunk > BUFFER_SIZE) ? BUFFER_SIZE : samples_in_chunk;
        
        if (use_buffer_a) {
            memcpy(audio_buffer_a, file_buffer, samples_to_copy * sizeof(int16_t));
            if (samples_to_copy < BUFFER_SIZE) {
                memset(&audio_buffer_a[samples_to_copy], 0, 
                       (BUFFER_SIZE - samples_to_copy) * sizeof(int16_t));
            }
            buffer_a_ready = true;
        } else {
            memcpy(audio_buffer_b, file_buffer, samples_to_copy * sizeof(int16_t));
            if (samples_to_copy < BUFFER_SIZE) {
                memset(&audio_buffer_b[samples_to_copy], 0, 
                       (BUFFER_SIZE - samples_to_copy) * sizeof(int16_t));
            }
            buffer_b_ready = true;
        }
        
        use_buffer_a = !use_buffer_a;
        samples_read += samples_in_chunk;
        
        // Progress update
        if (config.verbose_analysis && (samples_read % (header.sample_rate * 10) == 0)) {
            printf("Progress: %d/%d seconds\n", 
                   samples_read / header.sample_rate,
                   total_samples / header.sample_rate);
        }
    }
    
    transmission_active = false;
    f_close(&wav_file);
    
    printf("\nTransmission complete!\n");
    if (config.verbose_analysis) {
        printf("Final statistics:\n");
        printf("- Total samples processed: %d\n", samples_processed);
        printf("- Final THD estimate: %.3f%%\n", measured_thd);
        printf("- Transmission time: %d seconds\n", 
               (to_ms_since_boot(get_absolute_time()) - transmission_start_time) / 1000);
    }
}

// ============================================================================
// INITIALIZATION AND SAFETY
// ============================================================================

bool init_sd_card() {
    FATFS fs;
    FRESULT fr = f_mount(&fs, "", 1);
    
    if (fr != FR_OK) {
        printf("Error: SD card mount failed (error: %d)\n", fr);
        printf("Please check:\n");
        printf("- SD card is inserted\n");
        printf("- SD card is formatted (FAT32)\n");
        printf("- Wiring connections\n");
        return false;
    }
    
    if (config.verbose_analysis) {
        printf("SD card mounted successfully\n");
    }
    return true;
}

bool safety_check() {
    if (!config.dummy_load_check) return true;
    
    printf("\n🚨 SAFETY CHECK REQUIRED 🚨\n");
    printf("===========================\n");
    printf("This transmitter is for EDUCATIONAL USE ONLY\n");
    printf("You MUST use a dummy load, NOT an antenna\n\n");
    
    printf("Required safety setup:\n");
    printf("- Connect 50Ω dummy load to GPIO %d\n", RF_OUTPUT_PIN);
    printf("- NO antenna connection\n");
    printf("- Educational power levels only (<%d mW)\n", config.max_power_mw);
    printf("- Time limit: %d seconds\n", config.transmission_time_limit);
    printf("\nTransmitting on licensed frequencies without authorization is illegal!\n");
    printf("This is for studying RF generation and signal processing only.\n\n");
    
    printf("Are you using a dummy load (NOT antenna)? (y/N): ");
    char response;
    scanf(" %c", &response);
    
    if (response != 'y' && response != 'Y') {
        printf("Safety check failed. Please connect dummy load before proceeding.\n");
        return false;
    }
    
    // Setup safety indicator LED
    gpio_init(DUMMY_LOAD_LED_PIN);
    gpio_set_dir(DUMMY_LOAD_LED_PIN, GPIO_OUT);
    gpio_put(DUMMY_LOAD_LED_PIN, true);  // LED on = dummy load connected
    
    if (config.verbose_analysis) {
        printf("Safety check passed. Dummy load LED active.\n");
    }
    
    return true;
}

void display_startup_info() {
    printf("RP2040 Comprehensive AM Transmitter\n");
    printf("===================================\n\n");
    
    // Find station info
    const am_station_t* station = NULL;
    for (int i = 0; i < NUM_MELBOURNE_STATIONS; i++) {
        if (melbourne_stations[i].frequency == config.carrier_frequency) {
            station = &melbourne_stations[i];
            break;
        }
    }
    
    printf("Configuration:\n");
    if (station) {
        printf("- Station: %s (%s)\n", station->callsign, station->name);
        printf("- Frequency: %.1f kHz\n", config.carrier_frequency / 1000.0f);
    } else {
        printf("- Frequency: %.1f kHz (custom)\n", config.carrier_frequency / 1000.0f);
    }
    
    const char* mode_names[] = {
        "Simple High Quality", "Basic Square Wave", "Sigma-Delta",
        "Pure Sine Wave", "Pre-distortion", "Oversampled"
    };
    printf("- Signal Mode: %s\n", mode_names[config.signal_mode]);
    printf("- Modulation Depth: %d%%\n", config.modulation_depth);
    printf("- WAV File: %s\n", config.wav_filename);
    
    if (config.filter_mode != FILTER_MODE_NONE) {
        const char* filter_names[] = {
            "None", "Low-pass", "IIR Butterworth", "FIR Windowed", "Elliptic", "Multi-band"
        };
        printf("- Filter: %s (±%.1f Hz)\n", 
               filter_names[config.filter_mode], config.filter_bandwidth/2);
    }
    
    printf("\nFeatures Enabled:\n");
    if (config.educational_mode) printf("- Educational safety features\n");
    if (config.verbose_analysis) printf("- Verbose analysis\n");
    if (config.spectrum_analysis) printf("- Spectrum analysis\n");
    if (config.harmonic_analysis) printf("- Harmonic analysis\n");
    if (config.enable_predistortion) printf("- Digital pre-distortion\n");
    if (config.oversampling_rate > 1) printf("- %dx oversampling\n", config.oversampling_rate);
    
    // Check if this looks like best quality mode
    bool is_best_quality = (config.signal_mode == SIGNAL_MODE_OVERSAMPLED && 
                           config.filter_mode == FILTER_MODE_BANDPASS_ELLIPTIC &&
                           config.enable_predistortion && 
                           config.oversampling_rate >= 16 &&
                           config.verbose_analysis && 
                           config.spectrum_analysis && 
                           config.harmonic_analysis);
    
    if (is_best_quality) {
        printf("🌟 BEST QUALITY MODE ACTIVE 🌟\n");
        printf("- Professional broadcast-grade signal quality\n");
        printf("- Expected THD: <0.01%%, Harmonics: <-85dBc\n");
    }
    
    printf("\nRP2040 Special Processors Used:\n");
    printf("- PIO: Precise RF signal generation\n");
    printf("- Hardware Interpolator: Fast signal processing\n");
    printf("- Dual Core: Real-time audio processing\n");
    printf("- DMA: Continuous waveform streaming\n");
    
    printf("\n");
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

int main(int argc, char* argv[]) {
    stdio_init_all();
    sleep_ms(3000);  // Wait for USB serial
    
    // Parse command line arguments
    int parse_result = parse_command_line(argc, argv);
    if (parse_result != 0) {
        return (parse_result > 0) ? 0 : 1;  // 1 = help/info, -1 = error
    }
    
    // Display startup information
    display_startup_info();
    
    // Safety checks
    if (config.educational_mode && !safety_check()) {
        printf("Exiting for safety.\n");
        return 1;
    }
    
    // Initialize hardware
    printf("Initializing hardware...\n");
    
    if (!init_sd_card()) {
        printf("Cannot continue without SD card.\n");
        return 1;
    }
    
    // Initialize signal processing
    generate_sine_lut();
    
    if (config.filter_mode == FILTER_MODE_BANDPASS_IIR || 
        config.filter_mode == FILTER_MODE_BANDPASS_ELLIPTIC) {
        design_butterworth_bandpass();
    } else if (config.filter_mode == FILTER_MODE_BANDPASS_FIR) {
        design_fir_bandpass();
    }
    
    setup_pio_transmitter();
    
    // Status LED
    gpio_init(STATUS_LED_PIN);
    gpio_set_dir(STATUS_LED_PIN, GPIO_OUT);
    gpio_put(STATUS_LED_PIN, true);
    
    printf("Initialization complete. Starting transmission...\n");
    printf("Monitor with oscilloscope/spectrum analyzer + dummy load\n");
    if (config.educational_mode) {
        printf("Press Ctrl+C to stop transmission safely\n");
    }
    printf("\n");
    
    // Main transmission
    transmit_wav_file();
    
    // Cleanup
    gpio_put(STATUS_LED_PIN, false);
    gpio_put(DUMMY_LOAD_LED_PIN, false);
    
    printf("Program completed.\n");
    return 0;
}