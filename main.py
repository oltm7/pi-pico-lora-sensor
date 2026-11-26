# Monitoring script
from collections import deque
import matplotlib.animation as animation
import matplotlib.pyplot as plt
import sys
import serial
import serial.tools.list_ports

def get_serial_port():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("No serial ports found. Check your USB connection.")
        sys.exit(1)

    print("Available ports:")
    for i, port in enumerate(ports):
        print(f"[{i}] {port.device} - {port.description}")

    while True:
        try:
            choice = int(input("\nSelect port index: "))
            return ports[choice].device
        except (ValueError, IndexError):
            print("Invalid selection. Try again.")

def monitor_serial(port, baudrate=115200):
    try:
        # timeout=1 prevents readline() from blocking forever on exit
        with serial.Serial(port, baudrate, timeout=1) as ser:
            print(f"\nConnected to {port} at {baudrate} baud. Press Ctrl+C to stop.\n")
            while True:
                if ser.in_waiting > 0:
                    # Read line, decode bytes to string, and strip trailing newlines
                    line = ser.readline().decode('utf-8', errors='replace').rstrip()
                    print(line)
    except serial.SerialException as e:
        print(f"\nSerial error: {e}")
    except KeyboardInterrupt:
        print("\nMonitoring stopped by user.")

def parse_packet():
    # look for "\nRSSI," as start of CSV titles, and the next line is the values
    # parse into a dictionary, also add timestamp based on PC time to dict
    pass

def update_graphs():
    # update graphs live, takes in a dictionary, x axis is timestamp for all graphs
    pass

if __name__ == "__main__":
    selected_port = get_serial_port()
    print("Defaulting to 115200 baud")
    baud = 115200
    monitor_serial(selected_port, baud)