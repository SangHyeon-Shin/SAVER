import serial
import time

# ESP32 USB 시리얼 포트 확인 후 변경
ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
time.sleep(2)  # ESP32 초기화 대기

while True:
    if ser.in_waiting > 0:
        line = ser.readline().decode('utf-8').strip()
        print(f"Received from ESP32: {line}")

    # Jetson → ESP32로 테스트 메시지 보내기
    ser.write(b"Hello ESP32\n")
    time.sleep(1)
