// Assignment 2 - Embedded Systems
// Group 6
// Gabriele Nicchiarelli - S4822677
// Veronica Gavagna - S5487110
// Andrea Bolla - S4482930


/* TODO:
 * Duty cycle calculation
 * Compute correct buffer sizes
 * Fix interrupt handling
*/

#include <xc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "parser.h"

#define TIMER1 1
#define TIMER2 2
#define FOSC 144000000
#define OC_MAX 14400.0
#define BUFF_SIZE 104

/* How to compute the circular buffer size:
 * baud rate = 9600
 * read time = 100ms
 * bits transmitted = 1start + 1end + 8data = 10bits
 * (9600bps*0.1s) / 10bits = 96 --> a little bit bigger: 104
 */

/* How to compute the duty cylce:
 * 
 */

// Circular buffer structure
typedef struct {
    char buff[BUFF_SIZE];
    int readIdx;
    int writeIdx;
} CircBuff;

CircBuff circBuffTx;
CircBuff circBuffRx;

int start = 0;
int startCount = 0;
int countTx = 0;
int blinkRightLight = 0;

void tmr_setup_period(int timer, int ms) {
    int prescaler = 1;
    long fcy = (FOSC / 2) * (ms / 1000.0); // number of clocks in one second
    long fcy_new = fcy;

    if (fcy > 65535) {
        fcy_new = fcy / 8;
        prescaler = 1; // prescaler 1:8
    }
    if (fcy_new > 65535) {
        fcy_new = fcy / 64;
        prescaler = 2; // prescaler 1:64
    }
    if (fcy_new > 65535) {
        fcy_new = fcy / 256;
        prescaler = 3; // prescaler 1:256
    }

    switch(timer) {
        case TIMER1: {
            TMR1 = 0; // reset T1 counter
            T1CONbits.TCKPS = prescaler;
            PR1 = fcy_new;
            T1CONbits.TON = 1; // start T1
        }
        break;
        
        case TIMER2: {
            TMR2 = 0; // reset T2 counter
            T2CONbits.TCKPS = prescaler;
            PR2 = fcy_new;
            T2CONbits.TON = 1; // start T2
        }
        break;
    }
}

void tmr_wait_period(int timer) {
    switch(timer) {
        case TIMER1: {
            while(!IFS0bits.T1IF);
            IFS0bits.T1IF = 0;
        }
        break;

        case TIMER2: {
            while(!IFS0bits.T2IF);
            IFS0bits.T2IF = 0;
        }
        break;
    }
}

void tmr_setup_ms(int timer) {
    switch (timer) {
        case TIMER1: {
            TMR1 = 0; // reset T1 counter
            IFS0bits.T1IF = 0; // reset T1 flag
            T1CONbits.TON = 1; // start T1
        }
        break;
        
        case TIMER2: {
            TMR2 = 0; // reset T2 counter
            IFS0bits.T2IF = 0; // reset T2 flag
            T2CONbits.TON = 1; // start T2
        }
        break;
    }
}

void tmr_wait_ms(int timer, int ms) {
    int prescaler = 1;
    long fcy = (FOSC / 2) * (ms / 1000.0);
    long fcy_new = fcy;

    if (fcy > 65535) {
        fcy_new = fcy / 8;
        prescaler = 1; // prescaler 1:8
    }
    if (fcy_new > 65535) {
        fcy_new = fcy / 64;
        prescaler = 2; // prescaler 1:64
    }
    if (fcy_new > 65535) {
        fcy_new = fcy / 256;
        prescaler = 3; // prescaler 1:256
    }

    switch (timer) {
        case TIMER1: {
            T1CONbits.TCKPS = prescaler;
            PR1 = fcy_new;

            // wait...
            while(!IFS0bits.T1IF);
            IFS0bits.T1IF = 0;
        }
        break;

        case TIMER2: {
            T2CONbits.TCKPS = prescaler;
            PR2 = fcy_new;

            // wait...
            while(!IFS0bits.T2IF);
            IFS0bits.T2IF = 0;
        }
        break;
    }
}

void __attribute__ (( __interrupt__ , __auto_psv__ )) _INT1Interrupt() {
    IFS1bits.INT1IF = 0; // reset interrupt flag

    // start timer form 10ms
    tmr_setup_period(TIMER2, 10);
}

void __attribute__ (( __interrupt__ , __auto_psv__ )) _T2Interrupt() {
    IFS0bits.T2IF = 0; // reset interrupt flag

    int pinValue = PORTEbits.RE8;

    T2CONbits.TON = 0; // stop T2
    if (!pinValue) {
        start = !start;
        startCount = 0;
        turnOffLights();
    }
}

// UART2 interrupt
void __attribute__((__interrupt__, __auto_psv__)) _U2RXInterrupt() {
    IFS1bits.U2RXIF = 0; // Reset del flag di interrupt

    buffWrite(&circBuffRx, U2RXREG);
}

int parse_byte(parser_state* ps, char byte) {
    switch (ps->state) {
        case STATE_DOLLAR:
            if (byte == '$') {
                ps->state = STATE_TYPE;
                ps->index_type = 0;
            }
            break;
        case STATE_TYPE:
            if (byte == ',') {
                ps->state = STATE_PAYLOAD;
                ps->msg_type[ps->index_type] = '\0';
                ps->index_payload = 0; // initialize properly the index
            } else if (ps->index_type == 6) { // error! 
                ps->state = STATE_DOLLAR;
                ps->index_type = 0;
			} else if (byte == '*') {
				ps->state = STATE_DOLLAR; // get ready for a new message
                ps->msg_type[ps->index_type] = '\0';
				ps->msg_payload[0] = '\0'; // no payload
                return NEW_MESSAGE;
            } else {
                ps->msg_type[ps->index_type] = byte; // ok!
                ps->index_type++; // increment for the next time;
            }
            break;
        case STATE_PAYLOAD:
            if (byte == '*') {
                ps->state = STATE_DOLLAR; // get ready for a new message
                ps->msg_payload[ps->index_payload] = '\0';
                return NEW_MESSAGE;
            } else if (ps->index_payload == 100) { // error
                ps->state = STATE_DOLLAR;
                ps->index_payload = 0;
            } else {
                ps->msg_payload[ps->index_payload] = byte; // ok!
                ps->index_payload++; // increment for the next time;
            }
            break;
    }
    return NO_MESSAGE;
}

int extract_integer(const char* str) {
    int i = 0, sign = 1, number = 0;

    if (str[i] == '-') {
        sign = -1;  
        i++;
    }
    else if (str[i] == '+') {
        sign = 1;
        i++;
    }

    while (str[i] != ',' && str[i] != '\0') {
        number *= 10; // multiply the current number by 10;
        number += str[i] - '0'; // converting character to decimal number
        i++;
    }
    
    return sign*number;
}

int next_value(const char* msg, int i) {
    while (msg[i] != ',' && msg[i] != '\0') i++;
    if (msg[i] == ',') i++;
    return i;
}

void parse_pcth(const char* msg, double* minth, double* maxth) {
    int i = 0, minCM, maxCM;

    minCM = extract_integer(msg);
    i = next_value(msg, i);
    maxCM = extract_integer(msg+i);

    // Convert from CM to M
    *minth = minCM / 100;
    *maxth = maxCM / 100;
}

// Function to initialize the circular buffer
void buffInit(CircBuff* buff) {
    buff->readIdx = 0;
    buff->writeIdx = 0;
}

// Function to write a char to the circular buffer
void buffWrite(CircBuff *buff, char data) {
    // If writeIdx - readIdx == 1, then thorugh away the oldest character
    if ((buff->readIdx % BUFF_SIZE) - (buff->writeIdx % BUFF_SIZE) <= 1 && buff->writeIdx < buff->readIdx)
        buff->readIdx = (buff->readIdx + 1) % BUFF_SIZE;

    buff->buff[buff->writeIdx] = data;
    buff->writeIdx = (buff->writeIdx + 1) % BUFF_SIZE;
}

// Function to read a char from the circular buffer
char buffRead(CircBuff *buff) {
    char data = buff->buff[buff->readIdx];
    buff->readIdx = (buff->readIdx + 1) % BUFF_SIZE;
    return data;
}

// Function to check if there are characters to flush on the LCD
int checkAvailableBytes(CircBuff* buff) {
    if (buff->readIdx <= buff->writeIdx)
        return buff->writeIdx - buff->readIdx;
    else
        return BUFF_SIZE - buff->readIdx + buff->writeIdx;
}

void initPins() {
    TRISAbits.TRISA0 = 0; // led A0 as output
    TRISEbits.TRISE8 = 1; // Btn E8 as input
    TRISBbits.TRISB8 = 0; // Left lights
    TRISFbits.TRISF1 = 0; // Right lights
    TRISFbits.TRISF0 = 0; // Breaks
    TRISGbits.TRISG1 = 0; // Low intensity lights
    TRISAbits.TRISA7 = 0; // Beam headlights
}

void initUART2() {
    const int baund = 9600;
    U2BRG = (FOSC / 2) / (16L * baund) - 1;
    U2MODEbits.UARTEN = 1; // enable UART2
    U2STAbits.UTXEN = 1; // enable U2TX (must be after UARTEN)
}

void remapUARTPins() {
    RPOR0bits.RP64R = 0x03;
    RPINR19bits.U2RXR = 0x4B;
}

void initADC1() {    
    // IR sensor analog configuration AN15
    TRISBbits.TRISB15 = 1;
    ANSELBbits.ANSB15 = 1;
    // Battery sensing analog configuration AN11
    TRISBbits.TRISB11 = 1;
    ANSELBbits.ANSB11 = 1;

    AD1CON3bits.ADCS = 14; // 14*Tcy
    AD1CON1bits.ASAM = 1; // automatic sampling start
    AD1CON1bits.SSRC = 7; // automatic conversion
    AD1CON3bits.SAMC = 16; // sampling lasts 16 Tad
    AD1CON2bits.CHPS = 0; // use CH0 2-channels sequential sampling mode
    AD1CON1bits.SIMSAM = 0; // sequential sampling

	// Scan mode specific configuration
	AD1CON2bits.CSCNA = 1; // scan mode enabled
    AD1CSSLbits.CSS11 = 1; // scan for AN11 battery
    AD1CSSLbits.CSS15 = 1; // scan for AN15 ir sensor
	AD1CON2bits.SMPI = 1; // N-1 channels

    AD1CON1bits.ADON = 1; // turn on ADC

    // IR distance sensor enable line
    TRISAbits.TRISA3 = 0;
    LATAbits.LATA3 = 1;
}

void initOCPWM() {
    // OC1
    OC1CON1bits.OCTSEL = 7; // Peripheral clock
    OC1CON2bits.SYNCSEL = 0x1F; // No sync source
    OC1CON1bits.OCM = 6; // Edge-aligned PWM mode
    
    // OC2
    OC2CON1bits.OCTSEL = 7;
    OC2CON2bits.SYNCSEL = 0x1F;
    OC2CON1bits.OCM = 6;

    // OC3
    OC3CON1bits.OCTSEL = 7;
    OC3CON2bits.SYNCSEL = 0x1F;
    OC3CON1bits.OCM = 6;

    // OC4
    OC4CON1bits.OCTSEL = 7;
    OC4CON2bits.SYNCSEL = 0x1F;
    OC4CON1bits.OCM = 6;

    OC1RS = OC_MAX;
    OC2RS = OC_MAX;
    OC3RS = OC_MAX;
    OC4RS = OC_MAX;
}

void remapOCPins() {
    RPOR0bits.RP65R = 0x10;
    RPOR1bits.RP66R = 0x11;
    RPOR1bits.RP67R = 0x12;
    RPOR2bits.RP68R = 0x13;
}

void remapINTPins() {
    // Remap INT1 to btn E8
    RPINR0bits.INT1R = 0x58;
    INTCON2bits.GIE = 1;
    INTCON2bits.INT1EP = 1;
    IFS1bits.INT1IF = 0;
}

void stopMotors() {
    OC1R = 0;
    OC2R = 0;
    OC3R = 0;
    OC4R = 0;
}

void blinkLights(const int ms) {
    if (startCount == ms) {
        LATAbits.LATA0 = !LATAbits.LATA0;

        if (!start) {
            LATBbits.LATB8 = !LATBbits.LATB8;
            LATFbits.LATF1 = !LATFbits.LATF1;
        }

        if (blinkRightLight && start)
            LATFbits.LATF1 = !LATFbits.LATF1;

        startCount = 0;
    }
    startCount++;
}

double computeDist(double read_val) {
    double v = read_val * 3.3 / 1024.0;
    return 2.34 - 4.74*v + 4.06 * v*v - 1.60 * v*v*v + 0.24 * v*v*v*v;
}

double computeBattVolt(double read_val) {
    const double R1 = 200.0, R2 = 100.0;
    double v = read_val * 3.3 / 1024.0;
    return v * (R1 + R2) / R2;
}

void setPWM_Left(int pwm) {
    if (pwm > 0) {
        if (pwm < OC_MAX / 4) pwm = OC_MAX / 4;

        // Forward motion
        OC1R = 0;
        OC2R = pwm;
    }
    else {
        if (pwm > -OC_MAX / 4) pwm = -OC_MAX / 4;

        // Backward motion
        OC1R = pwm;
        OC2R = 0;
    }
}
 
void setPWM_Right(int pwm) {

    if (pwm > 0) {
        if (pwm < OC_MAX / 4) pwm = OC_MAX / 4;

        // Forward motion
        OC3R = 0;
        OC4R = pwm;
    }
    else {
        if (pwm > -OC_MAX / 4) pwm = -OC_MAX / 4;

        // Backward motion
        OC3R = pwm;
        OC4R = 0;
    }
}

double computeSurge(const double dist, const double minth, const double maxth) {
    double surge = 0.0;
    
    if (dist < minth) surge = 0.0;
    else if (dist > maxth) surge = 0.7;
    else {
        surge = OC_MAX / 4 + (OC_MAX - OC_MAX / 4) * (dist - minth) / (maxth - minth);
        surge = surge / OC_MAX;
    }

    return surge;
}

double computeYaw(const double dist, const double minth, const double maxth) {
    double yaw_rate = 0.0;
    
    if (dist < minth) yaw_rate = 0.5;
    else if (dist > maxth) yaw_rate = 0.0;
    else {
        yaw_rate = OC_MAX * (1.0 / 4 + (1.0 - 1.0 / 4) * (dist - minth) / (maxth - minth));
        yaw_rate = yaw_rate / OC_MAX;
    }

    return yaw_rate;
}

void sendDistUART(double value) {
    char buff[16];

    if (countTx % 100 == 0) {
        value *= 100;
        sprintf(buff, "$MDIST,%d*\n", (int)value);

        for (int i = 0; i < strlen(buff); i++)
            buffWrite(&circBuffTx, buff[i]);

        while (checkAvailableBytes(&circBuffTx) > 0) {
            while (U2STAbits.UTXBF);
                U2TXREG = buffRead(&circBuffTx);
        }
    }
}

void sendBattUART(double value) {
    char buff[16];

    if (countTx == 1000) {
        sprintf(buff, "$MBATT,%.2f*\n", value);
        countTx = 0;

        for (int i = 0; i < strlen(buff); i++)
            buffWrite(&circBuffTx, buff[i]);
        
        while (checkAvailableBytes(&circBuffTx) > 0) {
            while (U2STAbits.UTXBF);
                U2TXREG = buffRead(&circBuffTx);
        }
    }
}

void sendDcsUART(double* values) {
    char buff[16];

    // Compute duty cycles
    for (int i = 0; i < 4; i++) {
        values[i] = values[i] * 100 / OC_MAX;
    }

    if (countTx % 100 == 0) {
        sprintf(buff, "$MPWM,%d,%d,%d,%d*\n", (int)values[0], (int)values[1], (int)values[2], (int)values[3]);

        for (int i = 0; i < strlen(buff); i++)
            buffWrite(&circBuffTx, buff[i]);

        while (checkAvailableBytes(&circBuffTx) > 0) {
            while (U2STAbits.UTXBF);
                U2TXREG = buffRead(&circBuffTx);
        }
    }
}

void checkLights(const double surge, const double yaw_rate) {
    if (surge > 0.5) {
        LATAbits.LATA7 = 1;
        LATFbits.LATF0 = 0;
        LATGbits.LATG1 = 0;
    }
    else {
        LATAbits.LATA7 = 0;
        LATFbits.LATF0 = 1;
        LATGbits.LATG1 = 1;
    }
    
    if (yaw_rate < 0.15) {
        LATBbits.LATB8 = 0;
        blinkRightLight = 1;
    }
    else {
        blinkRightLight = 0;
        LATBbits.LATB8 = 0;
        LATFbits.LATF1 = 0;
    }
}

void turnOffLights() {
    LATAbits.LATA0 = 0;
    LATBbits.LATB8 = 0;
    LATFbits.LATF1 = 0;
    LATFbits.LATF0 = 0;
    LATGbits.LATG1 = 0;
    LATAbits.LATA7 = 0;
}

int main() {
    ANSELA = ANSELB = ANSELC = ANSELD = ANSELE = ANSELG = 0x0000;

    initPins();
    initUART2();
    initADC1();
    initOCPWM();

    remapUARTPins();
    remapOCPins();
    remapINTPins();
    
    // Enable interrupts
    IEC0bits.T2IE = 1;
    IEC1bits.INT1IE = 1;
    IEC1bits.U2RXIE = 1;
    U2STAbits.URXISEL = 1; // UART2 interrupt mode (1: every char received, 2: 3/4 char buffer, 3: full)

    // Init circular buffer
    buffInit(&circBuffTx);
    buffInit(&circBuffRx);

    // Parser initialization
    parser_state pstate;
	pstate.state = STATE_DOLLAR;
	pstate.index_type = 0; 
	pstate.index_payload = 0;

    double minth = 0.2, maxth = 0.5;
    double dist, batt_val, dcs[4] = { 1.0 };
    double surge, yaw_rate;
    int ret;

    tmr_setup_period(TIMER1, 1);

    while(1) {
        while (!AD1CON1bits.DONE);
        dist = computeDist(ADC1BUF1);
        batt_val = computeBattVolt(ADC1BUF0);

        if (!start) {
            stopMotors();
        }
        else {
            surge = computeSurge(dist, minth, maxth);
            yaw_rate = computeYaw(dist, minth, maxth);

            checkLights(surge, yaw_rate);

            setPWM_Left(OC_MAX * (surge+yaw_rate));
            setPWM_Right(OC_MAX * (surge-yaw_rate));
        }

        sendDistUART(dist);
        sendDcsUART(dcs);
        sendBattUART(batt_val);
        
        // IEC1bits.U2RXIE = 0;
        while (checkAvailableBytes(&circBuffRx) > 0) {
            ret = parse_byte(&pstate, buffRead(&circBuffRx));
            if (ret == NEW_MESSAGE) {
                if (strcmp(pstate.msg_type, "PCTH") == 0)
                    parse_pcth(pstate.msg_payload, &minth, &maxth);
            }
        }
        // IEC1bits.U2RXIE = 1;

        blinkLights(1000);
        countTx++;

        tmr_wait_period(TIMER1);
    };

    return 0;
}