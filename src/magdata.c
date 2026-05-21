//=========================================================================
// magdata.c
// 
// An interface for the RM3100 3-axis magnetometer from PNI Sensor Corp.
// 
// Author:      David Witten, KD0EAG
// Date:        December 18, 2023
// License:     GPL 3.0
//=========================================================================
#include "main.h"
#include "magdata.h"
#include "i2c.h"

//------------------------------------------
// Static variables
//------------------------------------------
extern char *Version;

//------------------------------------------
// setMagSampleRate()
//------------------------------------------
unsigned short setMagSampleRate(pList *p, unsigned short sample_rate)
{
    size_t i;
    const unsigned short int supported_rates[][2] = 
    {
        /* [Hz], register value */
        {   2,  0x0A},   // up to 2Hz
        {   4,  0x09},   // up to 4Hz
        {   8,  0x08},   // up to 8Hz
        {  16,  0x07},   // up to 16Hz
        {  31,  0x06},   // up to 31Hz
        {  62,  0x05},   // up to 62Hz
        { 125,  0x04},   // up to 125Hz
        { 220,  0x03}    // up to 250Hz
    };
    const size_t count = sizeof(supported_rates) / sizeof(supported_rates[0]);
    for(i = 0; i + 1 < count; i++)
    {
        if(sample_rate <= supported_rates[i][0])
        {
            break;
        }
    }
    if (i >= count) { i = count - 1; }
    p->CMMSampleRate = supported_rates[i][0];
    // i2c_write(p->pi, RM3100I2C_TMRC, p->CMMSampleRate);
    return p->CMMSampleRate;
}

//------------------------------------------
// getMagSampleRate();
// The actual sample rate of the sensor.
//------------------------------------------
unsigned short getMagSampleRate(pList *p)
{
    return p->CMMSampleRate;
}

//---------------------------------------------------------------
// void termGPIO(volatile pList p)
//---------------------------------------------------------------
void termGPIO(pList *p)
{
    // Close the adaptor here.
    i2c_close(p);
    // p->localTempHandle = i2c_close(p->po, p->localTempHandle);
    // p->remoteTempHandle = i2c_close(p->po, p->remoteTempHandle);
    // rgpiod_stop(p->po);

#if(__DEBUG)
    fprintf(OUTPUT_PRINT, "    [Child]: termGPIO(pList p)...\n");
    fflush(OUTPUT_PRINT);
#endif
}

//---------------------------------------------------------------
// void showErrorMsg(int rv)
//---------------------------------------------------------------
void showErrorMsg(int rv)
{
    (void)rv; // may be unused depending on build flags
    char    utcStr[UTCBUFLEN] = "";
    struct  tm *utcTime = getUTC();
    strftime(utcStr, UTCBUFLEN, "%d %b %Y %T", utcTime);

#if(USE_PIGPIO || USE_PIGPIO_IF2)
    #if(CONSOLE_OUTPUT)
        fprintf(OUTPUT_PRINT, "    [Child]: { \"ts\": \"%s\", \"lastError\": \"%s\" }\n", utcStr, lgpio_error(rv));
        fflush(OUTPUT_PRINT);
    #else
        char errstr[MAXPATHBUFLEN] = "";
        sprintf(errstr, "    [Child]: { \"ts\": \"%s\", \"lastError\": \"%s\" }\n", utcStr, lgpio_error(rv));
        write(PIPEOUT, errstr);
    #endif
#endif
#if(_USE_POLOLU_I2C)
    fprintf(OUTPUT_PRINT, "    [Child]: { \"ts\": \"%s\", \"lastError\": \"%s\" }\n", utcStr,  pololu_i2c_error_string(rv));
    fflush(OUTPUT_PRINT);
#endif
}

//------------------------------------------
// setNOSReg(volatile pList *p)
//
// Writes the NOS (Number Of Samples) register on the RM3100 from
// p->NOSRegValue.  NOS only affects continuous-measurement mode
// (CMM): the chip internally averages NOS samples before raising
// DRDY.  In POLL mode (the binary's default) this register has no
// effect on a single-shot read; we still program it so it's in a
// known state if/when CMM averaging is exercised.  The host-side
// XYZ scaling in main.c does NOT divide by NOS in POLL mode -- see
// the comment above formatOutput().
//------------------------------------------
int setNOSReg(pList *p)
{
    if(p == NULL) return -1;
    // Local literal: see same-TU note in setCycleCountRegs() and
    // sensor_tests.c about why we don't #include "rm3100.h" here.
    const uint8_t REG_NOS = 0x0A; // RM3100I2C_NOS
    uint8_t v = (uint8_t)(p->NOSRegValue & 0xFF);
    int rv = i2c_pololu_write_to(p->adapter,
                                 (uint8_t)p->magAddr,
                                 REG_NOS,
                                 &v, 1);
    if(rv < 0)
    {
        fprintf(OUTPUT_ERROR,
                "setNOSReg: i2c write to NOS reg failed: %s\n",
                i2c_pololu_error_string(-rv));
    }
    return rv;
}

//------------------------------------------
// runBIST()
// Runs the Built In Self Test.
//------------------------------------------
int runBIST(pList *p)
{
    (void)p;
    return 0;
    //return i2c_read(p->pi, RM3100I2C_TMRC);
}

//------------------------------------------
// startCMM()
// Starts Continuous Measurement Mode
//------------------------------------------
int startCMM(pList *p)
{
    (void)p;
    int rv = 0;
//    short cmmMode = (CMMMODE_ALL);   // 71 d
//    rv = i2c_write(p->pi, RM3100I2C_CMM, cmmMode);
    return rv;
}

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// Dave,
//
// Here is a general equation for gain taken directly from correspondence with PNI which I use in my Python scripts.
// Gn=(Aval*(0.3671*Cycnt+1.5)/1000)
// (0.3671*cycle count + 1.5)  when divided into the X Y or Z result with no averaging gives the correct value in micro teslas
// Aval/1000 times (0.3671*cycle count + 1.5) when divided into the X Y or Z result gives the correct value in nano teslas.
// Conversely, you can multiply the X Y or Z values by  1000/(Aval*(0.3671*Cycnt+1.5))
// As far as I know, it is an exact gain equation for the RM3100 and works for ANY cycle count .... like 375, 405, 125, etc, etc.  No error prone lookup tables.
//
// Jules
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

//------------------------------------------
// getCCGainEquiv()
//   Gn=(Aval*(0.3671*Cycnt+1.5)/1000)
//------------------------------------------
unsigned short getCCGainEquiv(unsigned short CCVal)
{
    unsigned short gain = 0;
    double dGain = (0.3671 * CCVal + 1.5); 
    gain = (unsigned short) dGain;
    return gain;
}

//------------------------------------------
// setCycleCountRegs()
//
// Programs the RM3100 cycle-count registers from p->cc_x/y/z and
// updates the host-side gain values so the XYZ-scaling math in
// main.c uses the same gain the chip is actually applying.
//
// The chip stores each axis cycle count as a 16-bit value across
// two adjacent registers (high byte first):
//     CCX_1 (0x04), CCX_0 (0x05)
//     CCY_1 (0x06), CCY_0 (0x07)
//     CCZ_1 (0x08), CCZ_0 (0x09)
//
// All six bytes are written in one I2C transaction starting at
// CCX_1 so the chip's register auto-increment carries us through.
// The RM3100 only acts on CC writes when it is idle (not actively
// measuring); callers should issue this before kicking off the
// first POLL or before entering CMM.
//
// Prior versions of this function had every i2c_write commented
// out, so configured cc_x/y/z values updated only the host-side
// gain and never the chip, leaving the chip at its power-on
// default of CC=200/axis (gain ≈75).  That silently broke any
// per-axis tuning the operator attempted via config.
//------------------------------------------
void setCycleCountRegs(pList *p)
{
    if(p == NULL) return;

    // rm3100.h defines these as file-scope const symbols with
    // external linkage, so this TU can't include it without
    // multiply-defining them at link time (see the same note in
    // sensor_tests.c).  Use local literals matching rm3100.h.
    const uint8_t REG_CCX_1 = 0x04; // RM3100I2C_CCX_1
    uint8_t cc_bytes[6];
    cc_bytes[0] = (uint8_t)((p->cc_x >> 8) & 0xFF);
    cc_bytes[1] = (uint8_t)( p->cc_x       & 0xFF);
    cc_bytes[2] = (uint8_t)((p->cc_y >> 8) & 0xFF);
    cc_bytes[3] = (uint8_t)( p->cc_y       & 0xFF);
    cc_bytes[4] = (uint8_t)((p->cc_z >> 8) & 0xFF);
    cc_bytes[5] = (uint8_t)( p->cc_z       & 0xFF);

    int rv = i2c_pololu_write_to(p->adapter,
                                 (uint8_t)p->magAddr,
                                 REG_CCX_1,
                                 cc_bytes, sizeof(cc_bytes));
    if(rv < 0)
    {
        fprintf(OUTPUT_ERROR,
                "setCycleCountRegs: burst write to CCX_1..CCZ_0 failed: %s\n",
                i2c_pololu_error_string(-rv));
    }

    // Update host-side gains AFTER the chip write so they match what
    // the chip is now applying.  Out-of-range or write-failed cc
    // values still leave the host's gain consistent with the value
    // we *attempted* to program -- callers that want to verify the
    // chip really took it should issue a read-back (-P).
    p->x_gain = getCCGainEquiv(p->cc_x);
    p->y_gain = getCCGainEquiv(p->cc_y);
    p->z_gain = getCCGainEquiv(p->cc_z);
//        fprintf(OUTPUT_ERROR, "Gains        - X: %u, Y: %u, Z: %u.\n", p->x_gain, p->y_gain, p->z_gain);
//        fprintf(OUTPUT_ERROR, "NOS Register - %2X.\n", p->NOSRegValue);
}

//------------------------------------------
// readCycleCountRegs()
//------------------------------------------
void readCycleCountRegs(pList *p)
{
    (void)p;
    uint8_t regCC[7]= { 0, 0, 0, 0, 0, 0, 0 };

    fprintf(OUTPUT_PRINT, "regCC[%i]: 0x%X\n",    0, (uint8_t)regCC[0]);
    fprintf(OUTPUT_PRINT, "regCC[%i]: 0x%X\n",    1, (uint8_t)regCC[1]);
    fprintf(OUTPUT_PRINT, "regCC[%i]: 0x%X\n",    2, (uint8_t)regCC[2]);
    fprintf(OUTPUT_PRINT, "regCC[%i]: 0x%X\n",    3, (uint8_t)regCC[3]);
    fprintf(OUTPUT_PRINT, "regCC[%i]: 0x%X\n",    4, (uint8_t)regCC[4]);
    fprintf(OUTPUT_PRINT, "regCC[%i]: 0x%X\n",    5, (uint8_t)regCC[5]);
    fprintf(OUTPUT_PRINT, "regCC[%i]: 0x%X\n\n",  6, (uint8_t)regCC[6]);
}

