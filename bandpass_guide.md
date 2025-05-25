# Digital Band Pass Filtering with RP2040

## Real-Time Frequency-Selective Signal Processing

The RP2040's special processors enable sophisticated **digital band pass filtering** that can isolate specific frequency bands with surgical precision - all through a single GPIO wire!

---

## 🎯 **Band Pass Filter Types Available**

### **1. IIR Butterworth Band Pass**
```
Characteristics:
- Maximally flat passband response
- Smooth, predictable rolloff  
- Lower order than FIR for same specs
- Non-linear phase (group delay varies)

Applications:
- General-purpose frequency selection
- Audio processing
- Communications receivers
```

### **2. FIR Windowed Sinc Band Pass**
```
Characteristics:
- Linear phase (constant group delay)
- Always stable (no feedback)
- Higher order required for sharp cutoff
- More computational load

Applications:
- Digital communications
- High-fidelity audio
- Measurement systems
```

### **3. Elliptic (Cauer) Band Pass**
```
Characteristics:
- Sharpest transition for given order
- Ripple in both pass and stop bands
- Minimum order for specifications
- Complex design equations

Applications:
- Professional receivers
- Interference rejection
- Spectrum analysis
```

### **4. Adaptive Band Pass**
```
Characteristics:
- Self-adjusting coefficients
- Tracks changing signal conditions
- High computational requirements
- Optimal performance over time

Applications:
- Software defined radio
- Noise cancellation
- Dynamic environments
```

---

## 🔧 **RP2040 Implementation Advantages**

### **Hardware Interpolator Magic**
```c
// Fast multiply-accumulate for filter calculations
interp0->accum[0] = (int32_t)(sample * 65536);
interp0->base[0] = (int32_t)(coefficient * 65536);
float result = (float)interp0->result[0] / (65536.0f * 65536.0f);
```
- **MAC operations in 1 cycle** vs dozens for software
- **Q15/Q31 fixed-point** arithmetic acceleration
- **Real-time filtering** at high sample rates

### **Dual-Core Processing**
- **Core 0**: User interface, filter design, analysis
- **Core 1**: Real-time signal processing loop
- **No dropouts** during filter reconfiguration
- **Parallel processing** of multiple filter banks

### **PIO Output Processing**
- **High-speed 1-bit output** for RF applications
- **Sigma-delta modulation** for improved SNR
- **Variable pulse width** for amplitude control
- **Deterministic timing** independent of CPU load

---

## 📊 **Filter Performance Comparison**

| Filter Type | Order | Transition BW | Passband Ripple | Stopband Atten | Phase |
|-------------|-------|---------------|-----------------|----------------|--------|
| **Butterworth** | 4 | Wide | 0 dB | -80 dB | Non-linear |
| **FIR Windowed** | 64 | Medium | 0 dB | -60 dB | Linear |
| **Elliptic** | 4 | Narrow | ±0.5 dB | -80 dB | Non-linear |
| **Adaptive** | Variable | Optimal | Variable | Variable | Depends |

---

## 🎓 **Educational Applications**

### **Multi-Band Signal Separation**
```c
// Simultaneously filter multiple frequency bands
float band1 = process_bandpass(&filter_500kHz, input);  // 500 kHz
float band2 = process_bandpass(&filter_774kHz, input);  // 774 kHz  
float band3 = process_bandpass(&filter_1MHz, input);    // 1 MHz

// Combine or select specific bands
float output = band2;  // Select only ABC Melbourne
```

### **Interference Rejection**
```c
// Design notch filters to remove interference
design_elliptic_bandpass(&notch_filter, 
                        interference_freq, 
                        narrow_bandwidth,
                        0.1, 80);  // Sharp notch

float clean_signal = process_bandpass(&notch_filter, noisy_input);
```

### **Spectrum Analysis**
```c
// Real-time spectrum analyzer using filter bank
for (int i = 0; i < NUM_BANDS; i++) {
    float band_power = 0;
    for (int j = 0; j < WINDOW_SIZE; j++) {
        float filtered = process_bandpass(&filter_bank[i], input[j]);
        band_power += filtered * filtered;
    }
    spectrum[i] = 10 * log10f(band_power);
}
```

---

## 🔬 **Advanced Techniques**

### **Cascaded Biquad Sections**
```c
// High-order filters as cascaded 2nd-order sections
typedef struct {
    float b0, b1, b2;  // Numerator coefficients
    float a1, a2;      // Denominator coefficients  
    float x1, x2;      // Input delay line
    float y1, y2;      // Output delay line
} biquad_t;

float process_biquad(biquad_t* bq, float input) {
    float output = bq->b0*input + bq->b1*bq->x1 + bq->b2*bq->x2
                                - bq->a1*bq->y1 - bq->a2*bq->y2;
    
    // Update delay lines
    bq->x2 = bq->x1; bq->x1 = input;
    bq->y2 = bq->y1; bq->y1 = output;
    
    return output;
}
```

### **Multi-Rate Processing**
```c
// Efficient filtering with decimation/interpolation
// 1. Decimate input signal
// 2. Filter at lower sample rate  
// 3. Interpolate back to original rate

float decimate_by_4(float input) {
    static int counter = 0;
    static float accumulator = 0;
    
    accumulator += input;
    if (++counter >= 4) {
        float output = accumulator / 4.0f;
        accumulator = 0;
        counter = 0;
        return output;
    }
    return 0;  // No output this sample
}
```

### **Adaptive Filter Coefficients**
```c
// LMS (Least Mean Squares) adaptive filtering
void update_adaptive_filter(float error, float input) {
    const float mu = 0.01f;  // Learning rate
    
    for (int i = 0; i < FILTER_LENGTH; i++) {
        // Update coefficient based on error gradient
        adaptive_coeffs[i] += mu * error * delay_line[i];
    }
}
```

---

## 📈 **Frequency Response Analysis**

### **Magnitude Response**
```
|H(f)| = |Numerator(f)| / |Denominator(f)|

For band pass filter:
- Passband: |H(f)| ≈ 1 (0 dB)
- Stopband: |H(f)| << 1 (-40 to -100 dB)
- Transition: Rolloff rate depends on order
```

### **Phase Response**
```
∠H(f) = ∠Numerator(f) - ∠Denominator(f)

IIR Filters: Non-linear phase
- Group delay varies with frequency
- Can cause signal distortion

FIR Filters: Linear phase  
- Constant group delay
- No phase distortion
```

### **Group Delay**
```
τ(f) = -dφ/df (where φ = phase)

Critical for:
- Digital communications
- Audio quality
- Measurement accuracy
```

---

## 🛠 **Practical Implementation Tips**

### **Coefficient Scaling**
```c
// Prevent overflow in fixed-point arithmetic
void scale_biquad_coefficients(biquad_t* bq) {
    // Find maximum coefficient magnitude
    float max_coeff = fmaxf(fabsf(bq->b0), 
                           fmaxf(fabsf(bq->b1), fabsf(bq->b2)));
    
    // Scale to prevent overflow
    if (max_coeff > 0.5f) {
        float scale = 0.5f / max_coeff;
        bq->b0 *= scale; bq->b1 *= scale; bq->b2 *= scale;
    }
}
```

### **Stability Monitoring**
```c
// Check filter stability in real-time
bool is_filter_stable(biquad_t* bq) {
    // Check if poles are inside unit circle
    float discriminant = bq->a1*bq->a1 - 4*bq->a2;
    
    if (discriminant >= 0) {
        // Real poles
        float pole1 = (-bq->a1 + sqrtf(discriminant)) / 2;
        float pole2 = (-bq->a1 - sqrtf(discriminant)) / 2;
        return (fabsf(pole1) < 1.0f && fabsf(pole2) < 1.0f);
    } else {
        // Complex poles  
        float magnitude = sqrtf(bq->a2);
        return (magnitude < 1.0f);
    }
}
```

### **Real-Time Performance**
```c
// Optimize for real-time operation
void optimize_filter_processing() {
    // 1. Use fixed-point arithmetic where possible
    // 2. Pre-compute constant values
    // 3. Use hardware interpolator for MAC
    // 4. Minimize branch instructions in inner loops
    // 5. Use DMA for continuous data flow
}
```

---

## 🎯 **Educational Experiments**

### **1. Filter Design Comparison**
- Design same specifications with different types
- Compare transition bandwidth vs order
- Analyze computational complexity
- Measure phase distortion effects

### **2. Real-Time Spectrum Analyzer**
- Create bank of overlapping band pass filters
- Display frequency content in real-time
- Study aliasing and Nyquist criteria
- Implement waterfall displays

### **3. Communications Receiver**
- Design channel selection filters
- Implement automatic gain control
- Add noise reduction filtering
- Study adjacent channel interference

### **4. Audio Processing**
- Create graphic equalizer with multiple bands
- Implement crossover networks for speakers
- Design anti-aliasing filters
- Study psychoacoustic effects

---

## 🔍 **Measurement and Verification**

### **Filter Testing Setup**
```
Signal Generator → RP2040 Band Pass Filter → Spectrum Analyzer
                           ↓
                    Oscilloscope (time domain)
```

### **Key Measurements**
- **Magnitude Response**: Frequency sweep analysis
- **Phase Response**: Group delay measurement  
- **Impulse Response**: Filter characterization
- **Step Response**: Transient behavior
- **Noise Performance**: SNR improvement

### **Educational Verification**
```c
// Generate test signals for filter verification
float generate_test_signal(float t, int test_type) {
    switch(test_type) {
        case 0: // Single tone
            return sinf(2*M_PI*test_freq*t);
            
        case 1: // Multi-tone
            return 0.3f*sinf(2*M_PI*f1*t) + 0.3f*sinf(2*M_PI*f2*t) + 
                   0.3f*sinf(2*M_PI*f3*t);
            
        case 2: // Chirp (frequency sweep)
            float chirp_rate = 1000; // Hz/s
            return sinf(2*M_PI*(start_freq + 0.5f*chirp_rate*t)*t);
            
        case 3: // White noise
            return 2.0f * ((float)rand()/RAND_MAX) - 1.0f;
    }
}
```

---

## 💡 **Advanced Applications**

### **Software Defined Radio (SDR)**
- **Channel Selection**: Isolate specific radio stations
- **Interference Rejection**: Remove unwanted signals
- **Demodulation**: Extract audio from RF carriers
- **Signal Intelligence**: Analyze spectrum usage

### **Measurement Instruments**
- **Spectrum Analyzer**: Real-time frequency analysis
- **Network Analyzer**: Transfer function measurement
- **Signal Generator**: Precise test signals
- **Oscilloscope**: Digital signal processing

### **Research Applications**
- **Algorithm Development**: Test new filter designs
- **Performance Analysis**: Compare theoretical vs practical
- **Real-Time Systems**: Study timing constraints
- **Digital Communications**: Protocol implementation

The RP2040's ability to perform real-time digital band pass filtering through a single GPIO wire demonstrates the power of modern embedded signal processing. This opens up possibilities for sophisticated RF applications that were previously only possible with expensive dedicated hardware.