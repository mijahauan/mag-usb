//=========================================================================
// cmdmgr.c
//
// commandline / configuration management routines for runMag utility.
//
// Author:      David Witten, KD0EAG
// Date:        December 18, 2023
// Updates:     Sept. 9, 2025
//
// License:     GPL 3.0
//=========================================================================
#include <stdio.h>
#include "main.h"
#include "magdata.h"
#include "cmdmgr.h"
#include "i2c-pololu.h"   // -P chip-readback (CC/NOS/TMRC/REVID)

// RM3100 register addresses, mirrored from rm3100.h.  We can't
// #include "rm3100.h" here because it defines these as file-scope
// const symbols with external linkage and i2c.c already includes
// it -- a second include would multiply-define them at link time.
// Same pattern sensor_tests.c and magdata.c already use.
static const uint8_t RM3100_REG_CCX_1 = 0x04; // CC, 6 contiguous bytes
static const uint8_t RM3100_REG_NOS   = 0x0A;
static const uint8_t RM3100_REG_TMRC  = 0x0B;
static const uint8_t RM3100_REG_REVID = 0x36;

//------------------------------------------
// showChipRegisters()
//
// Optional -P companion: open the Pololu adapter, read back the
// RM3100's cycle-count / NOS / TMRC / REVID registers, and print
// them next to the host-side values that showSettings() already
// printed.  Useful for catching the failure mode where the host
// thinks it programmed a register (e.g. cc_x=400) but the chip
// never actually got the write -- without a readback that goes
// undetected until somebody notices that scaled magnitudes look
// wrong.
//
// Best-effort: if the adapter isn't present or the device isn't a
// supported Pololu USB-I2C bridge, print a single "(unavailable: ...)"
// line and return without erroring.  -P in offline contexts (e.g.
// confirming what a binary *would* do on a machine without
// hardware) still works as before.
//------------------------------------------
static void showChipRegisters(pList *p)
{
    fprintf(OUTPUT_PRINT, "\nChip register readback (RM3100 over Pololu @ %s, address 0x%02X):\n",
            p->portpath ? p->portpath : "(no path)", (unsigned)(p->magAddr & 0xFF));

    if(!p->use_I2C_converter || !p->adapter || !p->portpath || p->portpath[0] == '\0')
    {
        fprintf(OUTPUT_PRINT, "   (skipped: USB-I2C converter disabled or no port path)\n\n");
        return;
    }

    int rv = i2c_pololu_check_device_available(p->portpath, 200);
    if(rv != 0)
    {
        fprintf(OUTPUT_PRINT, "   (unavailable: %s not present or busy)\n\n", p->portpath);
        return;
    }
    if(i2c_pololu_is_device_valid(p->portpath) != 0)
    {
        fprintf(OUTPUT_PRINT, "   (unavailable: %s is not a recognized Pololu USB-I2C adapter)\n\n", p->portpath);
        return;
    }

    if(i2c_pololu_init(p->adapter) < 0)
    {
        fprintf(OUTPUT_PRINT, "   (open failed: adapter init error)\n\n");
        return;
    }
    if(i2c_pololu_connect(p->adapter, p->portpath) < 0)
    {
        fprintf(OUTPUT_PRINT, "   (open failed: connect to %s)\n\n", p->portpath);
        return;
    }

    const uint8_t addr = (uint8_t)p->magAddr;
    uint8_t cc[6] = {0}, nos = 0, tmrc = 0, revid = 0;

    int rv_cc    = i2c_pololu_read_from(p->adapter, addr, RM3100_REG_CCX_1, cc,     6);
    int rv_nos   = i2c_pololu_read_from(p->adapter, addr, RM3100_REG_NOS,   &nos,   1);
    int rv_tmrc  = i2c_pololu_read_from(p->adapter, addr, RM3100_REG_TMRC,  &tmrc,  1);
    int rv_revid = i2c_pololu_read_from(p->adapter, addr, RM3100_REG_REVID, &revid, 1);

    if(rv_cc >= 0)
    {
        int chip_cc_x = ((int)cc[0] << 8) | cc[1];
        int chip_cc_y = ((int)cc[2] << 8) | cc[3];
        int chip_cc_z = ((int)cc[4] << 8) | cc[5];
        fprintf(OUTPUT_PRINT, "   Chip cycle counts (X,Y,Z):            %d, %d, %d   (host wants %d, %d, %d%s)\n",
                chip_cc_x, chip_cc_y, chip_cc_z,
                p->cc_x, p->cc_y, p->cc_z,
                (chip_cc_x == p->cc_x && chip_cc_y == p->cc_y && chip_cc_z == p->cc_z) ? "" : " -- MISMATCH");
    }
    else
    {
        fprintf(OUTPUT_PRINT, "   Chip cycle counts:                    (read failed: %s)\n", i2c_pololu_error_string(-rv_cc));
    }

    if(rv_nos >= 0)
    {
        fprintf(OUTPUT_PRINT, "   Chip NOS register:                    %d (0x%02X)   (host wants %d%s)\n",
                nos, nos, p->NOSRegValue,
                (nos == (uint8_t)(p->NOSRegValue & 0xFF)) ? "" : " -- MISMATCH");
    }
    else
    {
        fprintf(OUTPUT_PRINT, "   Chip NOS register:                    (read failed: %s)\n", i2c_pololu_error_string(-rv_nos));
    }

    if(rv_tmrc >= 0)
    {
        fprintf(OUTPUT_PRINT, "   Chip TMRC register:                   0x%02X   (host wants 0x%02X%s)\n",
                tmrc, (unsigned)(p->TMRCRate & 0xFF),
                (tmrc == (uint8_t)(p->TMRCRate & 0xFF)) ? "" : " -- MISMATCH");
    }
    else
    {
        fprintf(OUTPUT_PRINT, "   Chip TMRC register:                   (read failed: %s)\n", i2c_pololu_error_string(-rv_tmrc));
    }

    if(rv_revid >= 0)
    {
        fprintf(OUTPUT_PRINT, "   Chip REVID register:                  0x%02X   (RM3100 expected 0x22)\n",
                revid);
    }
    else
    {
        fprintf(OUTPUT_PRINT, "   Chip REVID register:                  (read failed: %s)\n", i2c_pololu_error_string(-rv_revid));
    }

    fprintf(OUTPUT_PRINT, "\n");

    i2c_pololu_disconnect(p->adapter);
}

//------------------------------------------
// showSettings()
//------------------------------------------
void showSettings(pList *p)
{
    fprintf(OUTPUT_PRINT, "\nVersion = %s\n", p->Version ? p->Version : "");
    fprintf(OUTPUT_PRINT, "\nCurrent Parameters:\n\n");

    // Node info
    fprintf(OUTPUT_PRINT, "   Maintainer:                           %s\n",  p->maintainer ? p->maintainer : "(null)");
    fprintf(OUTPUT_PRINT, "   Maintainer email:                     %s\n",  p->maintainer_email ? p->maintainer_email : "(null)");

    // Location
    fprintf(OUTPUT_PRINT, "   Latitude:                             %s\n",  p->latitude ? p->latitude : "(null)");
    fprintf(OUTPUT_PRINT, "   Longitude:                            %s\n",  p->longitude ? p->longitude : "(null)");
    fprintf(OUTPUT_PRINT, "   Elevation:                            %s\n",  p->elevation ? p->elevation : "(null)");
    fprintf(OUTPUT_PRINT, "   Grid square:                          %s\n",  p->grid_square ? p->grid_square : "(null)");

#if(USE_POLOLU)
    fprintf(OUTPUT_PRINT, "   Use external USB->I2C (Pololu):       %s\n",  p->use_I2C_converter ? "TRUE" : "FALSE");
    fprintf(OUTPUT_PRINT, "   I2C adapter device path:              %s\n",  p->portpath ? p->portpath : "(null)");
#else
    fprintf(OUTPUT_PRINT, "   Linux I2C bus number:                 %d\n",  p->i2cBusNumber);
#endif
    fprintf(OUTPUT_PRINT, "   Scan I2C bus on startup:              %s\n",  p->scanI2CBUS ? "TRUE" : "FALSE");
    fprintf(OUTPUT_PRINT, "   Verify Pololu adaptor:                %s\n",  p->checkPololuAdaptor ? "TRUE" : "FALSE");
    fprintf(OUTPUT_PRINT, "   Verify Temp sensor:                   %s\n",  p->checkTempSensor ? "TRUE" : "FALSE");
    fprintf(OUTPUT_PRINT, "   Verify Mag sensor:                    %s\n",  p->checkMagSensor ? "TRUE" : "FALSE");

    // Output/logging
    fprintf(OUTPUT_PRINT, "   Write logs:                           %s\n",  p->write_logs ? "TRUE" : "FALSE");
    fprintf(OUTPUT_PRINT, "   Log output path:                      %s\n",  p->log_output_path ? p->log_output_path : "(null)");
    fprintf(OUTPUT_PRINT, "   Create log path if empty:             %s\n",  p->create_log_path_if_empty ? "TRUE" : "FALSE");
    fprintf(OUTPUT_PRINT, "   Use named pipes:                      %s\n",  p->usePipes ? "TRUE" : "FALSE");
    fprintf(OUTPUT_PRINT, "   Pipe IN path:                         %s\n",  p->pipeInPath ? p->pipeInPath : "(null)");
    fprintf(OUTPUT_PRINT, "   Pipe OUT path:                        %s\n",  p->pipeOutPath ? p->pipeOutPath : "(null)");
    fprintf(OUTPUT_PRINT, "   Use WebSocket output:                 %s\n",  p->useWebSocket ? "TRUE" : "FALSE");
    fprintf(OUTPUT_PRINT, "   WebSocket bind address:               %s\n",  p->webSocketBindAddr ? p->webSocketBindAddr : "(null)");
    fprintf(OUTPUT_PRINT, "   WebSocket port:                       %d\n",  p->webSocketPort);

    // Magnetometer
    fprintf(OUTPUT_PRINT, "   Magnetometer I2C address:             0x%02X (hex)\n",  (unsigned)(p->magAddr & 0xFF));
    fprintf(OUTPUT_PRINT, "   Cycle counts (X,Y,Z):                 %d, %d, %d\n",  p->cc_x, p->cc_y, p->cc_z);
    fprintf(OUTPUT_PRINT, "   Gains (X,Y,Z):                        %d, %d, %d\n",  p->x_gain, p->y_gain, p->z_gain);
    fprintf(OUTPUT_PRINT, "   TMRC register value:                  0x%02X (hex)\n",  (unsigned)(p->TMRCRate & 0xFF));
    fprintf(OUTPUT_PRINT, "   NOS register value:                   %d\n",  p->NOSRegValue);
    fprintf(OUTPUT_PRINT, "   DRDY delay (us):                      %d\n",  p->DRDYdelay);
    fprintf(OUTPUT_PRINT, "   Sampling mode:                        %s\n",  (p->samplingMode == CMM) ? "CMM" : "POLL");
    fprintf(OUTPUT_PRINT, "   CMM sample rate (Hz):                 %d\n",  p->CMMSampleRate);
    fprintf(OUTPUT_PRINT, "   Read back CC registers:               %s\n",  p->readBackCCRegs ? "TRUE" : "FALSE");
    fprintf(OUTPUT_PRINT, "   Orientation translate (deg XYZ):      %d, %d, %d\n",  p->mag_translate_x, p->mag_translate_y, p->mag_translate_z);

    // Temperature
    fprintf(OUTPUT_PRINT, "   Remote temperature I2C address:       0x%02X (hex)\n",  (unsigned)(p->remoteTempAddr & 0xFF));

    fprintf(OUTPUT_PRINT, "\n");

    // Chip-side readback.  Best-effort -- prints "(unavailable: ...)"
    // when the hardware isn't reachable so offline -P still works.
    showChipRegisters(p);
}

//------------------------------------------
// getCommandLine()
//------------------------------------------
int getCommandLine(int argc, char** argv, pList *p)
{
    int c;

    while((c = getopt(argc, argv, "h?B:c:CD:g:PMSQTVO:ui:o:Ww:a:")) != -1)
    {
        //int this_option_optind = optind ? optind : 1;
        switch(c)
        {
            case 'W':
                p->useWebSocket = TRUE;
                break;
            case 'w':
            {
                int port = (int) strtol(optarg, NULL, 10);
                if(port <= 0 || port > 65535)
                {
                    fprintf(OUTPUT_ERROR, "\n ERROR Invalid: WebSocket port out of range.\n\n");
                    exit(1);
                }
                p->webSocketPort = port;
                p->useWebSocket = TRUE;
                break;
            }
            case 'a':
                if(p->webSocketBindAddr) free(p->webSocketBindAddr);
                p->webSocketBindAddr = strdup(optarg);
                p->useWebSocket = TRUE;
                break;
            case 'u':
                p->usePipes = TRUE;
                break;
            case 'i':
                if(p->pipeInPath) free(p->pipeInPath);
                p->pipeInPath = strdup(optarg);
                break;
            case 'o':
                if(p->pipeOutPath) free(p->pipeOutPath);
                p->pipeOutPath = strdup(optarg);
                break;
            case 'B':
                p->doBistMask = atoi(optarg);
                break;
            case 'C':
                p->readBackCCRegs = TRUE;
                break;
            case 'c':
                p->cc_x = p->cc_y = p->cc_z = (int) strtol(optarg, NULL, 10);
                if(p->cc_x > 0x320 || p->cc_x <= 0)
                {
                    fprintf(OUTPUT_ERROR, "\n ERROR Invalid: cycle count > 800 (dec) or cycle count  <= 0.\n\n");
                    exit(1);
                }
                p->x_gain = p->y_gain = p->z_gain = getCCGainEquiv(p->cc_x);
                break;
            case 'D':
                p->CMMSampleRate = (int) strtol(optarg, NULL, 10);
                break;
            case 'g':
                p->samplingMode = (int) strtol(optarg, NULL, 10);
                break;
            case 'O':
                if(p->portpath)
                {
                    snprintf(p->portpath, MAXPATHBUFLEN, "%s", optarg);
                }
                break;
            case 'P':
                p->showSettingsOnly = TRUE;
                break;
            case 'Q':
                p->checkPololuAdaptor = TRUE;
                break;
            case 'M':
                p->checkMagSensor = TRUE;
                break;
            case 'S':
                p->scanI2CBUS = TRUE;
                break;
            case 'T':
                p->checkTempSensor = TRUE;
                break;
            case 'V':
                fprintf(OUTPUT_PRINT, "\nVersion: %s\n", p->Version);
                exit(0);
            case 'h':
            case '?':
                fprintf(OUTPUT_PRINT, "\nParameters:\n\n");
                fprintf(OUTPUT_PRINT, "   -B <reg mask>          :  Do built in self test (BIST).         [ Not implemented ]\n");
                fprintf(OUTPUT_PRINT, "   -C                     :  Read back cycle count registers before sampling.\n");
                fprintf(OUTPUT_PRINT, "   -c <count>             :  Set cycle counts as integer.          [ default: 200 decimal]\n");
                fprintf(OUTPUT_PRINT, "   -D <rate>              :  Set magnetometer sample rate.         [ TMRC reg 96 hex default ].\n");
                fprintf(OUTPUT_PRINT, "   -g <mode>              :  Device sampling mode.                 [ POLL=0 (default), CONTINUOUS=1 ]\n");
#if(USE_POLOLU)
                fprintf(OUTPUT_PRINT, "   -O                     :  Path to Pololu port in /dev.          [ default: /dev/ttyMAG0 ]\n");
                fprintf(OUTPUT_PRINT, "   -Q                     :  Verify presence of Pololu adaptor and exit.\n");
#endif
                fprintf(OUTPUT_PRINT, "   -M                     :  Verify Magnetometer presence and version.\n");
                fprintf(OUTPUT_PRINT, "   -P                     :  Show all current settings (host-side config + RM3100 chip register readback) and exit.\n");
                fprintf(OUTPUT_PRINT, "   -S                     :  List devices seen on i2c bus and exit.\n");
                fprintf(OUTPUT_PRINT, "   -T                     :  Verify Temperature sensor presence and version and exit.\n");
                fprintf(OUTPUT_PRINT, "   -u                     :  Use named pipes for output.\n");
                fprintf(OUTPUT_PRINT, "   -i <path>              :  Path for input named pipe.\n");
                fprintf(OUTPUT_PRINT, "   -o <path>              :  Path for output named pipe.\n");
                fprintf(OUTPUT_PRINT, "   -W                     :  Enable WebSocket output.\n");
                fprintf(OUTPUT_PRINT, "   -w <port>              :  WebSocket server port.\n");
                fprintf(OUTPUT_PRINT, "   -a <addr>              :  WebSocket bind address.\n");
                fprintf(OUTPUT_PRINT, "   -V                     :  Display software version and exit.\n");
                fprintf(OUTPUT_PRINT, "   -h or -?               :  Display this help.\n\n");
                exit(0);
                break;
            default:
                fprintf(OUTPUT_PRINT, "\n?? getopt returned character code 0x%2X ??\n", c);
                break;
        }
    }
    if(optind < argc)
    {
        printf("non-option ARGV-elements: ");
        while(optind < argc)
        {
            printf("%s ", argv[optind++]);
        }
        printf("\n");
    }
    return 0;
}
