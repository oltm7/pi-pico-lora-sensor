# Monitoring script
import matplotlib.pyplot as plt
import sys
import serial
import serial.tools.list_ports
from datetime import datetime
import numpy as np

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

def monitor_serial(port, graphs, baudrate=115200):
    try:
        # timeout=1 prevents readline() from blocking forever on exit
        with serial.Serial(port, baudrate, timeout=1) as ser:
            print(f"\nConnected to {port} at {baudrate} baud. Press Ctrl+C to stop.\n")
            while True:
                if ser.in_waiting > 0:
                    # Read line, decode bytes to string, and strip trailing newlines
                    line = ser.readline().decode('utf-8', errors='replace').rstrip()
                    if (line[0:5] == "RSSI,"):
                        data_line = ser.readline().decode('utf-8', errors='replace').rstrip()
                        data = parse_packet(data_line)
                        update_graphs(data, graphs)
                    print(line)
    except serial.SerialException as e:
        print(f"\nSerial error: {e}")
    except KeyboardInterrupt:
        print("\nMonitoring stopped by user.")

def parse_packet(line):
    # parse values into a dictionary, also add timestamp based on PC time to dict
    data = {}
    line_items = line.split(",")
    data["TIMESTAMP"] = datetime.now()
    data["RSSI"] = float(line_items[0])
    data["SNR"] = float(line_items[1])
    data["VBAT"] = float(line_items[2])
    data["TEMP"] = int(line_items[3])
    data["STATUS"] = line_items[4]
    data["SENSA"] = float(line_items[5])
    data["SENSB"] = float(line_items[6])
    return data

def init_graphs():
    plt.ion()
    fig, ax = plt.subplots()
    x_data, y_data = [], []
    line, = ax.plot(x_data, y_data, 'b-o')
    ax.set_xlabel("Time")
    ax.set_ylabel("Resistance (kOhms)")
    ax.set_title("Sensor A soil resistance")
    plt.show(block=False)
    plt.pause(0.1)

    # TODO: data structure needs work lol
    graphs = {}
    graphs["SENSA"] = [x_data, y_data, line, ax, fig]
    return graphs

def update_graphs(data, graphs):
    # TODO: loop through all items in DATA and update the corresponding graph
    # = For SENSA =
    graph = graphs["SENSA"]
    # update graphs live, takes in a dictionary, x axis is timestamp for all graphs
    graph[0].append(data["TIMESTAMP"]) # X DATA
    graph[1].append(data["SENSA"]) # Y DATA
    
    # Update existing line
    graph[2].set_xdata(graph[0])
    graph[2].set_ydata(graph[1])

    # Rescale axes
    graph[3].relim()
    graph[3].autoscale_view()

    # Update Matplotlib GUI
    graph[4].canvas.draw_idle()
    plt.pause(0.001)


if __name__ == "__main__":
    selected_port = get_serial_port()
    print("Defaulting to 115200 baud")
    baud = 115200
    graphs = init_graphs()
    monitor_serial(selected_port, graphs, baud)