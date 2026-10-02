/*
 * ============================================================
 * SAVER BENCH POSITION CONTROLLER
 * Teensy 4.1 + VESC 75100 + Linear Position Sensor
 *
 * CONTROL:
 * Linear position -> PID -> VESC motor current
 *
 * BENCH / UNLOADED FIXTURE TEST ONLY
 * NOT FOR HUMAN USE
 * ============================================================
 *
 * VESC:
 *   VESC TX -> Teensy 28 (Serial7 RX)
 *   VESC RX -> Teensy 29 (Serial7 TX)
 *   GND     -> Teensy GND
 *
 * Linear sensor:
 *   OUT -> A16
 *
 * Safety inputs:
 *   Upper / zero limit -> pin 3
 *   Lower / depth limit -> pin 4
 *   E-stop logic input -> pin 6
 *
 * Assumption:
 *   safety inputs are ACTIVE LOW.
 *
 * Commands:
 *   STATUS
 *   ZERO
 *   CAL known_mm
 *   MOTORFLIP
 *   TARGET depth_mm cpm
 *   ILIMIT motor_A
 *   INLIMIT input_A
 *   GAINS kp ki kd
 *   ARM
 *   RUN [cycles]
 *   STOP
 *   HELP
 *
 * ============================================================
 */

#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#if !defined(ARDUINO_TEENSY41)
#error "Select Teensy 4.1"
#endif


// ============================================================
// PINS
// ============================================================

constexpr uint8_t PIN_VESC_RX = 28;
constexpr uint8_t PIN_VESC_TX = 29;

constexpr uint8_t PIN_LINEAR = A16;

constexpr uint8_t PIN_LIMIT_ZERO  = 3;
constexpr uint8_t PIN_LIMIT_DEPTH = 4;
constexpr uint8_t PIN_ESTOP       = 6;


// ============================================================
// SERIAL
// ============================================================

constexpr uint32_t USB_BAUD  = 115200;
constexpr uint32_t VESC_BAUD = 115200;


// ============================================================
// VESC PACKET IDs
// ============================================================

constexpr uint8_t COMM_FW_VERSION  = 0;
constexpr uint8_t COMM_GET_VALUES  = 4;
constexpr uint8_t COMM_SET_CURRENT = 6;


// ============================================================
// CRC
// ============================================================

uint16_t crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0;

    for (size_t i = 0; i < length; i++)
    {
        crc ^= static_cast<uint16_t>(data[i]) << 8;

        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }

    return crc;
}


void putInt32(uint8_t *p, int32_t value)
{
    p[0] = (value >> 24) & 0xFF;
    p[1] = (value >> 16) & 0xFF;
    p[2] = (value >> 8) & 0xFF;
    p[3] = value & 0xFF;
}


int16_t readInt16BE(const uint8_t *p)
{
    uint16_t v =
        (static_cast<uint16_t>(p[0]) << 8) |
        static_cast<uint16_t>(p[1]);

    return static_cast<int16_t>(v);
}


int32_t readInt32BE(const uint8_t *p)
{
    uint32_t v =
        (static_cast<uint32_t>(p[0]) << 24) |
        (static_cast<uint32_t>(p[1]) << 16) |
        (static_cast<uint32_t>(p[2]) << 8) |
        static_cast<uint32_t>(p[3]);

    return static_cast<int32_t>(v);
}


// ============================================================
// VESC TRANSMIT
// ============================================================

bool sendVescPayload(const uint8_t *payload, uint8_t length)
{
    if (length == 0)
        return false;

    uint8_t frame[160];

    size_t i = 0;

    frame[i++] = 2;
    frame[i++] = length;

    for (uint8_t n = 0; n < length; n++)
        frame[i++] = payload[n];

    const uint16_t crc = crc16(payload, length);

    frame[i++] = (crc >> 8) & 0xFF;
    frame[i++] = crc & 0xFF;
    frame[i++] = 3;

    if (Serial7.availableForWrite() < static_cast<int>(i))
        return false;

    return Serial7.write(frame, i) == i;
}


bool requestFirmware()
{
    uint8_t p[1] = {COMM_FW_VERSION};

    return sendVescPayload(
        p,
        sizeof(p)
    );
}


bool requestValues()
{
    uint8_t p[1] = {COMM_GET_VALUES};

    return sendVescPayload(
        p,
        sizeof(p)
    );
}


/*
 * VESC COMM_SET_CURRENT:
 *
 * 1 A -> 1000
 */
bool sendMotorCurrent(float amps)
{
    const int32_t raw =
        static_cast<int32_t>(
            roundf(amps * 1000.0f)
        );

    uint8_t p[5];

    p[0] = COMM_SET_CURRENT;

    putInt32(
        &p[1],
        raw
    );

    return sendVescPayload(
        p,
        sizeof(p)
    );
}


bool sendZeroCurrent()
{
    return sendMotorCurrent(0.0f);
}


// ============================================================
// VESC TELEMETRY
// ============================================================

struct VescValues
{
    bool valid = false;

    float fetTempC = 0.0f;

    float motorCurrentA = 0.0f;
    float inputCurrentA = 0.0f;

    float dutyPct = 0.0f;

    int32_t erpm = 0;

    float inputV = 0.0f;

    uint8_t fault = 0;
};


VescValues vesc;

bool firmwareKnown = false;

uint8_t fwMajor = 0;
uint8_t fwMinor = 0;

uint32_t lastValuesMs = 0;
uint32_t lastValuesQueryMs = 0;
uint32_t lastFirmwareQueryMs = 0;


// ============================================================
// VESC RX PARSER
// ============================================================

enum class RxState
{
    WAIT_START,
    LENGTH,
    PAYLOAD,
    CRC_HIGH,
    CRC_LOW,
    END
};


RxState rxState = RxState::WAIT_START;

uint8_t rxPayload[128];

uint8_t rxLength = 0;
uint8_t rxIndex = 0;

uint8_t rxCrcHigh = 0;
uint8_t rxCrcLow = 0;

uint32_t lastRxByteMs = 0;


void resetRx()
{
    rxState = RxState::WAIT_START;

    rxLength = 0;
    rxIndex = 0;

    rxCrcHigh = 0;
    rxCrcLow = 0;
}


void handleVescPayload(const uint8_t *p, uint8_t n)
{
    if (n == 0)
        return;


    // Firmware response
    if (p[0] == COMM_FW_VERSION)
    {
        if (n >= 3)
        {
            fwMajor = p[1];
            fwMinor = p[2];

            firmwareKnown = true;

            Serial.print("VESC_FW,major=");
            Serial.print(fwMajor);

            Serial.print(",minor=");
            Serial.println(fwMinor);
        }

        return;
    }


    // GET_VALUES response
    if (p[0] == COMM_GET_VALUES)
    {
        if (n < 54)
            return;

        vesc.fetTempC =
            readInt16BE(&p[1]) / 10.0f;

        vesc.motorCurrentA =
            readInt32BE(&p[5]) / 100.0f;

        vesc.inputCurrentA =
            readInt32BE(&p[9]) / 100.0f;

        vesc.dutyPct =
            readInt16BE(&p[21]) / 10.0f;

        vesc.erpm =
            readInt32BE(&p[23]);

        vesc.inputV =
            readInt16BE(&p[27]) / 10.0f;

        vesc.fault =
            p[53];

        vesc.valid = true;

        lastValuesMs = millis();
    }
}


void feedVescByte(uint8_t b)
{
    const uint32_t now = millis();

    if (
        rxState != RxState::WAIT_START &&
        now - lastRxByteMs > 100
    )
    {
        resetRx();
    }

    lastRxByteMs = now;


    switch (rxState)
    {
        case RxState::WAIT_START:

            if (b == 2)
                rxState = RxState::LENGTH;

            break;


        case RxState::LENGTH:

            rxLength = b;

            if (
                rxLength == 0 ||
                rxLength > sizeof(rxPayload)
            )
            {
                resetRx();
                break;
            }

            rxIndex = 0;

            rxState = RxState::PAYLOAD;

            break;


        case RxState::PAYLOAD:

            rxPayload[rxIndex++] = b;

            if (rxIndex >= rxLength)
                rxState = RxState::CRC_HIGH;

            break;


        case RxState::CRC_HIGH:

            rxCrcHigh = b;

            rxState = RxState::CRC_LOW;

            break;


        case RxState::CRC_LOW:

            rxCrcLow = b;

            rxState = RxState::END;

            break;


        case RxState::END:
        {
            const uint16_t received =
                (static_cast<uint16_t>(rxCrcHigh) << 8) |
                rxCrcLow;

            const uint16_t calculated =
                crc16(rxPayload, rxLength);

            if (
                b == 3 &&
                received == calculated
            )
            {
                handleVescPayload(
                    rxPayload,
                    rxLength
                );
            }

            resetRx();

            break;
        }
    }
}


void pollVesc()
{
    while (Serial7.available())
    {
        feedVescByte(
            static_cast<uint8_t>(
                Serial7.read()
            )
        );
    }
}


// ============================================================
// VESC / ELECTRICAL SAFETY
// ============================================================

constexpr uint32_t VALUES_QUERY_MS = 100;
constexpr uint32_t VALUES_TIMEOUT_MS = 500;

constexpr float MIN_INPUT_V = 18.0f;
constexpr float MAX_INPUT_V = 26.5f;

constexpr float MAX_FET_TEMP_C = 80.0f;


/*
 * PID가 명령할 수 있는 모터전류.
 *
 * 기본 6 A.
 * 런타임 ILIMIT으로 변경 가능.
 */
float currentLimitA = 6.0f;


/*
 * 현재 모터 사양을 기준으로
 * 벤치에서 허용하는 명령 상한.
 */
constexpr float MAX_ALLOWED_COMMAND_CURRENT_A =
    15.0f;


/*
 * 명령값과 별개의 비상 보호값.
 *
 * 17 A 이상이 100 ms 지속되면 정지.
 */
constexpr float MOTOR_HARD_TRIP_A = 17.0f;
constexpr uint32_t MOTOR_HARD_TRIP_TIME_MS = 100;

uint32_t motorOvercurrentSinceMs = 0;


/*
 * DC source input-current protection.
 *
 * 현재 벤치 PSU가 3 A라면
 * 2.8 A 정도가 적절.
 *
 * 배터리 시험 시 실제 시스템 허용치에 맞춰
 * INLIMIT 명령으로 변경.
 */
float inputCurrentTripA = 2.8f;

constexpr float MAX_ALLOWED_INPUT_TRIP_A = 10.0f;

constexpr uint32_t INPUT_OVERCURRENT_TIME_MS = 100;

uint32_t inputOvercurrentSinceMs = 0;


bool vescFresh()
{
    return
        vesc.valid &&
        millis() - lastValuesMs <
        VALUES_TIMEOUT_MS;
}


bool electricalSafe()
{
    if (!vescFresh())
        return false;

    if (vesc.fault != 0)
        return false;

    if (
        vesc.inputV < MIN_INPUT_V ||
        vesc.inputV > MAX_INPUT_V
    )
        return false;

    if (
        vesc.fetTempC >
        MAX_FET_TEMP_C
    )
        return false;


    // Motor current hard trip with delay
    if (
        fabsf(vesc.motorCurrentA) >
        MOTOR_HARD_TRIP_A
    )
    {
        if (motorOvercurrentSinceMs == 0)
            motorOvercurrentSinceMs = millis();

        if (
            millis() -
            motorOvercurrentSinceMs >=
            MOTOR_HARD_TRIP_TIME_MS
        )
        {
            return false;
        }
    }
    else
    {
        motorOvercurrentSinceMs = 0;
    }


    // Input-current trip with delay
    if (
        fabsf(vesc.inputCurrentA) >
        inputCurrentTripA
    )
    {
        if (inputOvercurrentSinceMs == 0)
            inputOvercurrentSinceMs = millis();

        if (
            millis() -
            inputOvercurrentSinceMs >=
            INPUT_OVERCURRENT_TIME_MS
        )
        {
            return false;
        }
    }
    else
    {
        inputOvercurrentSinceMs = 0;
    }


    return true;
}


void serviceVescQueries()
{
    const uint32_t now = millis();

    if (!firmwareKnown)
    {
        if (
            now -
            lastFirmwareQueryMs >=
            1000
        )
        {
            requestFirmware();

            lastFirmwareQueryMs =
                now;
        }

        return;
    }


    if (
        now -
        lastValuesQueryMs >=
        VALUES_QUERY_MS
    )
    {
        requestValues();

        lastValuesQueryMs =
            now;
    }
}


// ============================================================
// PHYSICAL SAFETY INPUTS
// ============================================================

bool estopActive()
{
    return
        digitalRead(PIN_ESTOP) ==
        LOW;
}


bool zeroLimitActive()
{
    return
        digitalRead(PIN_LIMIT_ZERO) ==
        LOW;
}


bool depthLimitActive()
{
    return
        digitalRead(PIN_LIMIT_DEPTH) ==
        LOW;
}


// ============================================================
// LINEAR SENSOR
// ============================================================

constexpr float SENSOR_TRAVEL_MM = 75.0f;

constexpr int ADC_RAIL_MARGIN = 5;


bool zeroKnown = false;
bool scaleKnown = false;

int zeroRaw = 0;

float sensorDirection = 1.0f;

/*
 * ADC counts corresponding to
 * equivalent 75 mm full travel.
 */
float fullStrokeRaw = 4095.0f;


float rawPositionMm = 0.0f;
float positionMm = 0.0f;


/*
 * Small low-pass filter.
 */
constexpr float POSITION_FILTER_TAU_S =
    0.010f;

bool positionFilterInitialized =
    false;


int readLinearRaw()
{
    return analogRead(PIN_LINEAR);
}


bool adcNotRaided(int raw)
{
    return
        raw > ADC_RAIL_MARGIN &&
        raw < (4095 - ADC_RAIL_MARGIN);
}


float rawToMm(int raw)
{
    if (
        !zeroKnown ||
        !scaleKnown ||
        fullStrokeRaw <= 1.0f
    )
        return 0.0f;


    const float counts =
        sensorDirection *
        static_cast<float>(
            raw - zeroRaw
        );


    return
        counts *
        SENSOR_TRAVEL_MM /
        fullStrokeRaw;
}


void updatePosition(float dt)
{
    const int raw =
        readLinearRaw();

    rawPositionMm =
        rawToMm(raw);


    if (!positionFilterInitialized)
    {
        positionMm =
            rawPositionMm;

        positionFilterInitialized =
            true;

        return;
    }


    const float alpha =
        dt /
        (
            POSITION_FILTER_TAU_S +
            dt
        );


    positionMm +=
        alpha *
        (
            rawPositionMm -
            positionMm
        );
}


/*
 * 50 mm operating target,
 * small margin only.
 */
constexpr float SOFT_MIN_MM = -2.0f;
constexpr float SOFT_MAX_MM = 65.0f;


bool positionSafe()
{
    return
        positionMm >= SOFT_MIN_MM &&
        positionMm <= SOFT_MAX_MM;
}


// ============================================================
// TARGET PROFILE
// ============================================================

float targetDepthMm = 5.0f;
float targetCpm = 10.0f;

constexpr float MAX_TARGET_DEPTH_MM = 50.0f;
constexpr float MAX_TARGET_CPM = 110.0f;

constexpr float PI_F =
    3.14159265358979323846f;


/*
 * Smooth:
 *
 * 0 mm -> target -> 0 mm
 */
float referencePosition(
    uint32_t elapsedUs
)
{
    const float period =
        60.0f /
        targetCpm;


    const float time =
        elapsedUs /
        1000000.0f;


    const float phase =
        fmodf(
            time,
            period
        )
        /
        period;


    return
        0.5f *
        targetDepthMm *
        (
            1.0f -
            cosf(
                2.0f *
                PI_F *
                phase
            )
        );
}


uint32_t profileDurationUs(
    uint32_t cycles
)
{
    const float seconds =
        (
            60.0f /
            targetCpm
        )
        *
        cycles;


    return
        static_cast<uint32_t>(
            seconds *
            1000000.0f
        );
}


// ============================================================
// PID
// ============================================================

/*
 * PID output unit = motor current [A]
 *
 * D term is derivative-on-measurement.
 * This avoids a large derivative kick from the reference.
 */

float Kp = 0.80f;    // A/mm
float Ki = 0.30f;    // A/(mm*s)
float Kd = 0.020f;   // A/(mm/s)


constexpr float INTEGRAL_STATE_LIMIT =
    25.0f;

constexpr float VELOCITY_FILTER_TAU_S =
    0.025f;


float integralState = 0.0f;

float previousPositionMm = 0.0f;
float filteredVelocityMmS = 0.0f;


float pTermA = 0.0f;
float iTermA = 0.0f;
float dTermA = 0.0f;

float lastReferenceMm = 0.0f;
float lastErrorMm = 0.0f;
float lastCommandCurrentA = 0.0f;


float motorDirection = 1.0f;


void resetPid()
{
    integralState = 0.0f;

    previousPositionMm =
        positionMm;

    filteredVelocityMmS =
        0.0f;

    pTermA = 0.0f;
    iTermA = 0.0f;
    dTermA = 0.0f;
}


float calculatePidCurrent(
    float reference,
    float measured,
    float dt
)
{
    const float error =
        reference -
        measured;


    lastErrorMm =
        error;


    /*
     * Measured linear velocity.
     */

    const float rawVelocity =
        (
            measured -
            previousPositionMm
        )
        /
        dt;


    previousPositionMm =
        measured;


    const float velocityAlpha =
        dt /
        (
            VELOCITY_FILTER_TAU_S +
            dt
        );


    filteredVelocityMmS +=
        velocityAlpha *
        (
            rawVelocity -
            filteredVelocityMmS
        );


    /*
     * P
     */

    pTermA =
        Kp *
        error;


    /*
     * D on measurement
     */

    dTermA =
        -Kd *
        filteredVelocityMmS;


    /*
     * Integral candidate
     */

    float candidateIntegral =
        integralState +
        error *
        dt;


    candidateIntegral =
        constrain(
            candidateIntegral,
            -INTEGRAL_STATE_LIMIT,
            INTEGRAL_STATE_LIMIT
        );


    const float candidateI =
        Ki *
        candidateIntegral;


    const float unsaturated =
        pTermA +
        candidateI +
        dTermA;


    /*
     * Conditional anti-windup.
     */

    bool allowIntegration =
        true;


    if (
        unsaturated >
        currentLimitA &&
        error >
        0.0f
    )
        allowIntegration =
            false;


    if (
        unsaturated <
        -currentLimitA &&
        error <
        0.0f
    )
        allowIntegration =
            false;


    if (allowIntegration)
        integralState =
            candidateIntegral;


    iTermA =
        Ki *
        integralState;


    float output =
        pTermA +
        iTermA +
        dTermA;


    output =
        constrain(
            output,
            -currentLimitA,
            currentLimitA
        );


    /*
     * Motor electrical polarity.
     */

    output *=
        motorDirection;


    return output;
}


// ============================================================
// RUN STATE
// ============================================================

bool armed = false;
bool running = false;

uint32_t armedAtMs = 0;
uint32_t runStartUs = 0;

uint32_t requestedCycles = 1;

constexpr uint32_t ARM_TIMEOUT_MS =
    10000;


/*
 * PID at 200 Hz.
 */
constexpr uint32_t CONTROL_PERIOD_US =
    5000;


/*
 * VESC SET_CURRENT at 50 Hz.
 */
constexpr uint32_t CURRENT_TX_MS =
    20;


uint32_t lastControlUs = 0;
uint32_t lastCurrentTxMs = 0;

uint32_t lastZeroTxMs = 0;
uint32_t lastStatusMs = 0;


/*
 * After the theoretical waveform ends,
 * continue commanding zero until
 * actual position returns close to zero.
 */

constexpr float RETURN_POSITION_TOLERANCE_MM =
    0.7f;

constexpr uint32_t RETURN_TIMEOUT_MS =
    1500;


// ============================================================
// STOP
// ============================================================

void stopMotor(const char *reason)
{
    running = false;
    armed = false;

    lastCommandCurrentA =
        0.0f;


    sendZeroCurrent();


    resetPid();


    motorOvercurrentSinceMs =
        0;

    inputOvercurrentSinceMs =
        0;


    Serial.print(
        "STOP,reason="
    );

    Serial.println(
        reason
    );
}


// ============================================================
// CONTROL LOOP
// ============================================================

void controlTick()
{
    const uint32_t nowUs =
        micros();


    if (
        nowUs -
        lastControlUs <
        CONTROL_PERIOD_US
    )
        return;


    float dt =
        (
            nowUs -
            lastControlUs
        )
        /
        1000000.0f;


    lastControlUs =
        nowUs;


    if (dt <= 0.0f || dt > 0.050f)
        dt = 0.005f;


    if (
        zeroKnown &&
        scaleKnown
    )
    {
        updatePosition(dt);
    }


    if (!running)
        return;


    if (estopActive())
    {
        stopMotor(
            "ESTOP"
        );

        return;
    }


    if (!electricalSafe())
    {
        stopMotor(
            "VESC_OR_ELECTRICAL_SAFETY"
        );

        return;
    }


    if (!positionSafe())
    {
        stopMotor(
            "POSITION_SOFT_LIMIT"
        );

        return;
    }


    const uint32_t elapsedUs =
        nowUs -
        runStartUs;


    const uint32_t nominalDuration =
        profileDurationUs(
            requestedCycles
        );


    float reference;


    if (
        elapsedUs <
        nominalDuration
    )
    {
        reference =
            referencePosition(
                elapsedUs
            );
    }
    else
    {
        reference = 0.0f;


        if (
            fabsf(positionMm) <=
            RETURN_POSITION_TOLERANCE_MM
        )
        {
            stopMotor(
                "PROFILE_COMPLETE"
            );

            return;
        }


        const uint32_t returnElapsedMs =
            (
                elapsedUs -
                nominalDuration
            )
            /
            1000;


        if (
            returnElapsedMs >
            RETURN_TIMEOUT_MS
        )
        {
            stopMotor(
                "RETURN_ZERO_TIMEOUT"
            );

            return;
        }
    }


    lastReferenceMm =
        reference;


    /*
     * Logical PID output:
     *
     * positive = move deeper
     * negative = move toward zero
     */

    const float logicalCurrent =
        calculatePidCurrent(
            reference,
            positionMm,
            dt
        )
        /
        motorDirection;


    /*
     * Directional mechanical limit checking.
     *
     * ZERO limit blocks further retraction only.
     * DEPTH limit blocks further compression only.
     */

    if (
        logicalCurrent < 0.0f &&
        zeroLimitActive()
    )
    {
        stopMotor(
            "ZERO_LIMIT"
        );

        return;
    }


    if (
        logicalCurrent > 0.0f &&
        depthLimitActive()
    )
    {
        stopMotor(
            "DEPTH_LIMIT"
        );

        return;
    }


    /*
     * Apply motor polarity.
     */

    float commandCurrent =
        logicalCurrent *
        motorDirection;


    commandCurrent =
        constrain(
            commandCurrent,
            -currentLimitA,
            currentLimitA
        );


    lastCommandCurrentA =
        commandCurrent;


    const uint32_t nowMs =
        millis();


    if (
        nowMs -
        lastCurrentTxMs >=
        CURRENT_TX_MS
    )
    {
        if (
            !sendMotorCurrent(
                commandCurrent
            )
        )
        {
            stopMotor(
                "UART_TX_FAIL"
            );

            return;
        }


        lastCurrentTxMs =
            nowMs;
    }
}


// ============================================================
// STATUS
// ============================================================

void printStatus()
{
    const int raw =
        readLinearRaw();


    if (
        zeroKnown &&
        scaleKnown
    )
    {
        rawPositionMm =
            rawToMm(raw);
    }


    Serial.print("STATUS");


    Serial.print(",raw=");
    Serial.print(raw);


    Serial.print(",zero_known=");
    Serial.print(
        zeroKnown ? 1 : 0
    );


    Serial.print(",scale_known=");
    Serial.print(
        scaleKnown ? 1 : 0
    );


    Serial.print(",zero_raw=");
    Serial.print(
        zeroRaw
    );


    Serial.print(",span_raw=");
    Serial.print(
        fullStrokeRaw,
        1
    );


    Serial.print(",position_mm=");
    Serial.print(
        positionMm,
        2
    );


    Serial.print(",ref_mm=");
    Serial.print(
        lastReferenceMm,
        2
    );


    Serial.print(",error_mm=");
    Serial.print(
        lastErrorMm,
        2
    );


    Serial.print(",velocity_mm_s=");
    Serial.print(
        filteredVelocityMmS,
        1
    );


    Serial.print(",P_A=");
    Serial.print(
        pTermA,
        2
    );


    Serial.print(",I_A=");
    Serial.print(
        iTermA,
        2
    );


    Serial.print(",D_A=");
    Serial.print(
        dTermA,
        2
    );


    Serial.print(",cmd_A=");
    Serial.print(
        lastCommandCurrentA,
        2
    );


    Serial.print(",ILIMIT_A=");
    Serial.print(
        currentLimitA,
        1
    );


    Serial.print(",INLIMIT_A=");
    Serial.print(
        inputCurrentTripA,
        1
    );


    Serial.print(",target_mm=");
    Serial.print(
        targetDepthMm,
        1
    );


    Serial.print(",cpm=");
    Serial.print(
        targetCpm,
        1
    );


    Serial.print(",running=");
    Serial.print(
        running ? 1 : 0
    );


    Serial.print(",armed=");
    Serial.print(
        armed ? 1 : 0
    );


    Serial.print(",estop=");
    Serial.print(
        estopActive() ? 1 : 0
    );


    Serial.print(",zero_limit=");
    Serial.print(
        zeroLimitActive() ? 1 : 0
    );


    Serial.print(",depth_limit=");
    Serial.print(
        depthLimitActive() ? 1 : 0
    );


    Serial.print(",vesc=");
    Serial.print(
        vescFresh() ? 1 : 0
    );


    if (vesc.valid)
    {
        Serial.print(",Vin=");
        Serial.print(
            vesc.inputV,
            1
        );


        Serial.print(",motor_A=");
        Serial.print(
            vesc.motorCurrentA,
            2
        );


        Serial.print(",input_A=");
        Serial.print(
            vesc.inputCurrentA,
            2
        );


        Serial.print(",duty_pct=");
        Serial.print(
            vesc.dutyPct,
            1
        );


        Serial.print(",erpm=");
        Serial.print(
            vesc.erpm
        );


        Serial.print(",fet_C=");
        Serial.print(
            vesc.fetTempC,
            1
        );


        Serial.print(",fault=");
        Serial.print(
            vesc.fault
        );
    }


    Serial.println();
}


// ============================================================
// ZERO
// ============================================================

void zeroSensor()
{
    if (running)
    {
        Serial.println(
            "ERROR,STOP_FIRST"
        );

        return;
    }


    const int raw =
        readLinearRaw();


    if (!adcNotRaided(raw))
    {
        Serial.println(
            "ERROR,ADC_TOO_CLOSE_TO_RAIL"
        );

        return;
    }


    zeroRaw =
        raw;


    zeroKnown =
        true;


    scaleKnown =
        false;


    sensorDirection =
        1.0f;


    positionMm =
        0.0f;

    rawPositionMm =
        0.0f;


    positionFilterInitialized =
        false;


    armed =
        false;


    Serial.print(
        "OK,ZERO,raw="
    );

    Serial.println(
        zeroRaw
    );


    Serial.println(
        "NEXT,move_axis_in_compression_direction_known_distance_then_CAL"
    );
}


// ============================================================
// CALIBRATION
// ============================================================

void calibrateSensor(
    float knownMm
)
{
    if (running)
    {
        Serial.println(
            "ERROR,STOP_FIRST"
        );

        return;
    }


    if (!zeroKnown)
    {
        Serial.println(
            "ERROR,ZERO_FIRST"
        );

        return;
    }


    if (
        knownMm < 5.0f ||
        knownMm > 60.0f
    )
    {
        Serial.println(
            "ERROR,CAL_RANGE_5_TO_60_MM"
        );

        return;
    }


    const int raw =
        readLinearRaw();


    if (!adcNotRaided(raw))
    {
        Serial.println(
            "ERROR,ADC_TOO_CLOSE_TO_RAIL"
        );

        return;
    }


    const int delta =
        raw -
        zeroRaw;


    if (
        abs(delta) <
        30
    )
    {
        Serial.println(
            "ERROR,CAL_MOVEMENT_TOO_SMALL"
        );

        return;
    }


    /*
     * The direction in which CAL was performed
     * becomes positive compression direction.
     */

    sensorDirection =
        delta > 0
            ? +1.0f
            : -1.0f;


    const float observedCounts =
        fabsf(
            static_cast<float>(
                delta
            )
        );


    fullStrokeRaw =
        observedCounts *
        SENSOR_TRAVEL_MM /
        knownMm;


    if (
        fullStrokeRaw <
        100.0f ||
        fullStrokeRaw >
        10000.0f
    )
    {
        scaleKnown =
            false;


        Serial.println(
            "ERROR,CAL_SPAN_IMPLAUSIBLE"
        );

        return;
    }


    scaleKnown =
        true;


    positionFilterInitialized =
        false;


    rawPositionMm =
        rawToMm(raw);

    positionMm =
        rawPositionMm;


    Serial.print(
        "OK,CAL,known_mm="
    );

    Serial.print(
        knownMm,
        2
    );


    Serial.print(
        ",raw="
    );

    Serial.print(
        raw
    );


    Serial.print(
        ",span_raw="
    );

    Serial.print(
        fullStrokeRaw,
        1
    );


    Serial.print(
        ",position_mm="
    );

    Serial.println(
        positionMm,
        2
    );


    Serial.println(
        "NEXT,return_axis_to_zero"
    );
}


// ============================================================
// MOTOR DIRECTION
// ============================================================

void flipMotorDirection()
{
    if (running)
    {
        Serial.println(
            "ERROR,STOP_FIRST"
        );

        return;
    }


    motorDirection *=
        -1.0f;


    Serial.print(
        "OK,MOTOR_DIR="
    );

    Serial.println(
        motorDirection >
        0.0f
            ? "+"
            : "-"
    );
}


// ============================================================
// ARM
// ============================================================

void armSystem()
{
    if (!zeroKnown)
    {
        Serial.println(
            "ERROR,ZERO_REQUIRED"
        );

        return;
    }


    if (!scaleKnown)
    {
        Serial.println(
            "ERROR,CAL_REQUIRED"
        );

        return;
    }


    if (estopActive())
    {
        Serial.println(
            "ERROR,ESTOP_ACTIVE"
        );

        return;
    }


    if (!vescFresh())
    {
        Serial.println(
            "ERROR,VESC_LINK"
        );

        return;
    }


    if (!electricalSafe())
    {
        Serial.println(
            "ERROR,VESC_OR_ELECTRICAL_SAFETY"
        );

        return;
    }


    positionFilterInitialized =
        false;


    updatePosition(
        0.005f
    );


    if (
        fabsf(positionMm) >
        1.5f
    )
    {
        Serial.println(
            "ERROR,RETURN_TO_ZERO_BEFORE_ARM"
        );

        return;
    }


    motorOvercurrentSinceMs =
        0;

    inputOvercurrentSinceMs =
        0;


    armed =
        true;


    armedAtMs =
        millis();


    Serial.println(
        "OK,ARMED,expires_ms=10000"
    );
}


// ============================================================
// RUN
// ============================================================

void startRun(
    uint32_t cycles
)
{
    if (!armed)
    {
        Serial.println(
            "ERROR,ARM_REQUIRED"
        );

        return;
    }


    if (
        millis() -
        armedAtMs >
        ARM_TIMEOUT_MS
    )
    {
        armed =
            false;


        Serial.println(
            "ERROR,ARM_EXPIRED"
        );

        return;
    }


    updatePosition(
        0.005f
    );


    if (
        fabsf(positionMm) >
        1.5f
    )
    {
        Serial.println(
            "ERROR,NOT_AT_ZERO"
        );

        return;
    }


    if (estopActive())
    {
        Serial.println(
            "ERROR,ESTOP_ACTIVE"
        );

        return;
    }


    if (!electricalSafe())
    {
        Serial.println(
            "ERROR,VESC_OR_ELECTRICAL_SAFETY"
        );

        return;
    }


    requestedCycles =
        cycles;


    resetPid();


    lastReferenceMm =
        0.0f;

    lastErrorMm =
        -positionMm;

    lastCommandCurrentA =
        0.0f;


    motorOvercurrentSinceMs =
        0;

    inputOvercurrentSinceMs =
        0;


    running =
        true;

    armed =
        false;


    runStartUs =
        micros();


    lastCurrentTxMs =
        millis() -
        CURRENT_TX_MS;


    Serial.print(
        "OK,RUN,cycles="
    );

    Serial.print(
        requestedCycles
    );


    Serial.print(
        ",depth_mm="
    );

    Serial.print(
        targetDepthMm,
        1
    );


    Serial.print(
        ",cpm="
    );

    Serial.print(
        targetCpm,
        1
    );


    Serial.print(
        ",ILIMIT="
    );

    Serial.println(
        currentLimitA,
        1
    );
}


// ============================================================
// COMMANDS
// ============================================================

char commandBuffer[128];
uint8_t commandLength = 0;


void printHelp()
{
    Serial.println("COMMANDS");
    Serial.println("STATUS");
    Serial.println("ZERO");
    Serial.println("CAL known_mm");
    Serial.println("MOTORFLIP");
    Serial.println("TARGET depth_mm cpm");
    Serial.println("ILIMIT motor_A");
    Serial.println("INLIMIT input_A");
    Serial.println("GAINS kp ki kd");
    Serial.println("ARM");
    Serial.println("RUN [cycles]");
    Serial.println("STOP");
}


void handleCommand(
    char *line
)
{
    char *ctx = nullptr;


    char *cmd =
        strtok_r(
            line,
            " \t",
            &ctx
        );


    if (!cmd)
        return;


    for (char *p = cmd; *p; p++)
    {
        *p =
            static_cast<char>(
                toupper(
                    static_cast<unsigned char>(
                        *p
                    )
                )
            );
    }


    if (!strcmp(cmd, "STATUS"))
    {
        printStatus();
        return;
    }


    if (!strcmp(cmd, "STOP"))
    {
        stopMotor(
            "USER_STOP"
        );

        return;
    }


    if (!strcmp(cmd, "ZERO"))
    {
        zeroSensor();
        return;
    }


    if (!strcmp(cmd, "CAL"))
    {
        char *a =
            strtok_r(
                nullptr,
                " \t",
                &ctx
            );


        if (!a)
        {
            Serial.println(
                "ERROR,CAL known_mm"
            );

            return;
        }


        calibrateSensor(
            atof(a)
        );

        return;
    }


    if (!strcmp(cmd, "MOTORFLIP"))
    {
        flipMotorDirection();
        return;
    }


    if (!strcmp(cmd, "TARGET"))
    {
        char *depthText =
            strtok_r(
                nullptr,
                " \t",
                &ctx
            );


        char *cpmText =
            strtok_r(
                nullptr,
                " \t",
                &ctx
            );


        if (
            !depthText ||
            !cpmText
        )
        {
            Serial.println(
                "ERROR,TARGET depth_mm cpm"
            );

            return;
        }


        const float depth =
            atof(depthText);


        const float cpm =
            atof(cpmText);


        if (
            depth <= 0.0f ||
            depth >
            MAX_TARGET_DEPTH_MM
        )
        {
            Serial.println(
                "ERROR,DEPTH_0_TO_50"
            );

            return;
        }


        if (
            cpm < 5.0f ||
            cpm >
            MAX_TARGET_CPM
        )
        {
            Serial.println(
                "ERROR,CPM_5_TO_110"
            );

            return;
        }


        targetDepthMm =
            depth;

        targetCpm =
            cpm;


        Serial.print(
            "OK,TARGET,depth="
        );

        Serial.print(
            targetDepthMm
        );


        Serial.print(
            ",cpm="
        );

        Serial.println(
            targetCpm
        );

        return;
    }


    if (!strcmp(cmd, "ILIMIT"))
    {
        if (running)
        {
            Serial.println(
                "ERROR,STOP_FIRST"
            );

            return;
        }


        char *a =
            strtok_r(
                nullptr,
                " \t",
                &ctx
            );


        if (!a)
        {
            Serial.println(
                "ERROR,ILIMIT motor_A"
            );

            return;
        }


        const float amps =
            atof(a);


        if (
            amps < 1.0f ||
            amps >
            MAX_ALLOWED_COMMAND_CURRENT_A
        )
        {
            Serial.println(
                "ERROR,ILIMIT_RANGE_1_TO_15"
            );

            return;
        }


        currentLimitA =
            amps;


        Serial.print(
            "OK,ILIMIT="
        );

        Serial.println(
            currentLimitA,
            2
        );

        return;
    }


    if (!strcmp(cmd, "INLIMIT"))
    {
        if (running)
        {
            Serial.println(
                "ERROR,STOP_FIRST"
            );

            return;
        }


        char *a =
            strtok_r(
                nullptr,
                " \t",
                &ctx
            );


        if (!a)
        {
            Serial.println(
                "ERROR,INLIMIT input_A"
            );

            return;
        }


        const float amps =
            atof(a);


        if (
            amps < 0.5f ||
            amps >
            MAX_ALLOWED_INPUT_TRIP_A
        )
        {
            Serial.println(
                "ERROR,INLIMIT_RANGE_0.5_TO_10"
            );

            return;
        }


        inputCurrentTripA =
            amps;


        Serial.print(
            "OK,INLIMIT="
        );

        Serial.println(
            inputCurrentTripA,
            2
        );

        return;
    }


    if (!strcmp(cmd, "GAINS"))
    {
        if (running)
        {
            Serial.println(
                "ERROR,STOP_FIRST"
            );

            return;
        }


        char *a =
            strtok_r(
                nullptr,
                " \t",
                &ctx
            );


        char *b =
            strtok_r(
                nullptr,
                " \t",
                &ctx
            );


        char *c =
            strtok_r(
                nullptr,
                " \t",
                &ctx
            );


        if (
            !a ||
            !b ||
            !c
        )
        {
            Serial.println(
                "ERROR,GAINS kp ki kd"
            );

            return;
        }


        const float kp =
            atof(a);

        const float ki =
            atof(b);

        const float kd =
            atof(c);


        if (
            kp < 0.0f ||
            kp > 5.0f ||
            ki < 0.0f ||
            ki > 3.0f ||
            kd < 0.0f ||
            kd > 0.5f
        )
        {
            Serial.println(
                "ERROR,GAIN_RANGE"
            );

            return;
        }


        Kp = kp;
        Ki = ki;
        Kd = kd;


        resetPid();


        Serial.print(
            "OK,GAINS="
        );

        Serial.print(Kp);

        Serial.print(",");

        Serial.print(Ki);

        Serial.print(",");

        Serial.println(Kd);

        return;
    }


    if (!strcmp(cmd, "ARM"))
    {
        armSystem();
        return;
    }


    if (!strcmp(cmd, "RUN"))
    {
        char *a =
            strtok_r(
                nullptr,
                " \t",
                &ctx
            );


        uint32_t cycles =
            1;


        if (a)
            cycles =
                static_cast<uint32_t>(
                    atoi(a)
                );


        if (
            cycles < 1 ||
            cycles > 20
        )
        {
            Serial.println(
                "ERROR,CYCLES_1_TO_20"
            );

            return;
        }


        startRun(
            cycles
        );

        return;
    }


    if (!strcmp(cmd, "HELP"))
    {
        printHelp();
        return;
    }


    Serial.println(
        "ERROR,UNKNOWN_COMMAND"
    );
}


// ============================================================
// USB PARSER
// ============================================================

void pollUsb()
{
    while (Serial.available())
    {
        const char c =
            Serial.read();


        if (c == '\r')
            continue;


        if (c == '\n')
        {
            if (commandLength > 0)
            {
                commandBuffer[
                    commandLength
                ] = '\0';


                handleCommand(
                    commandBuffer
                );
            }


            commandLength = 0;

            continue;
        }


        if (
            commandLength <
            sizeof(commandBuffer) - 1
        )
        {
            commandBuffer[
                commandLength++
            ] = c;
        }
    }
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
    pinMode(
        PIN_LINEAR,
        INPUT
    );


    pinMode(
        PIN_LIMIT_ZERO,
        INPUT_PULLUP
    );


    pinMode(
        PIN_LIMIT_DEPTH,
        INPUT_PULLUP
    );


    pinMode(
        PIN_ESTOP,
        INPUT_PULLUP
    );


    analogReadResolution(
        12
    );


    analogReadAveraging(
        16
    );


    Serial.begin(
        USB_BAUD
    );


    Serial7.setRX(
        PIN_VESC_RX
    );


    Serial7.setTX(
        PIN_VESC_TX
    );


    Serial7.begin(
        VESC_BAUD,
        SERIAL_8N1
    );


    delay(100);


    sendZeroCurrent();


    Serial.println();

    Serial.println(
        "SAVER_POSITION_PID_CURRENT_V1_READY"
    );


    Serial.println(
        "CONTROL=LINEAR_POSITION_PID_TO_VESC_CURRENT"
    );


    Serial.print(
        "PID="
    );

    Serial.print(Kp);

    Serial.print(",");

    Serial.print(Ki);

    Serial.print(",");

    Serial.println(Kd);


    Serial.print(
        "DEFAULT_ILIMIT="
    );

    Serial.println(
        currentLimitA
    );


    Serial.print(
        "DEFAULT_INLIMIT="
    );

    Serial.println(
        inputCurrentTripA
    );


    Serial.println(
        "STEP: ZERO -> CAL -> RETURN ZERO -> DIRECTION TEST"
    );
}


// ============================================================
// MAIN LOOP
// ============================================================

void loop()
{
    pollUsb();

    pollVesc();

    serviceVescQueries();

    controlTick();


    /*
     * Keep VESC at zero current while stopped.
     */
    if (
        !running &&
        millis() -
        lastZeroTxMs >=
        100
    )
    {
        sendZeroCurrent();

        lastZeroTxMs =
            millis();
    }


    /*
     * ARM timeout.
     */
    if (
        armed &&
        millis() -
        armedAtMs >
        ARM_TIMEOUT_MS
    )
    {
        armed = false;

        Serial.println(
            "EVENT,ARM_EXPIRED"
        );
    }


    /*
     * Automatic log while moving.
     */
    if (
        running &&
        millis() -
        lastStatusMs >=
        200
    )
    {
        lastStatusMs =
            millis();

        printStatus();
    }
}