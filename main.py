# Monitoring script
import matplotlib.pyplot as plt
import sys
import time
import serial
import serial.tools.list_ports
import matplotlib.dates as mdates
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
    expecting_data = False
    try:
        # timeout=1 prevents readline() from blocking forever on exit
        with serial.Serial(port, baudrate, timeout=1) as ser:
            print(f"\nConnected to {port} at {baudrate} baud. Press Ctrl+C to stop.\n")
            while True:
                if ser.in_waiting > 0:
                    # Read line, decode bytes to string, and strip trailing newlines
                    line = ser.readline().decode('utf-8', errors='replace').rstrip()
                    print(line)
                    if line.startswith("RSSI,"):
                        expecting_data = True
                        continue
                    if expecting_data:
                        expecting_data = False
                        try:
                            update_graphs(parse_packet(line), graphs)
                            print(line)
                        except (ValueError, IndexError, KeyError):
                            print("Bad packet, skipping")
                else:
                    plt.pause(0.05)
    except serial.SerialException as e:
        print(f"\nSerial error: {e}, Retrying in 2 s...")
        time.sleep(2)
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
    data["CHG"] = 1 - int(bool(int(line_items[4], 16) & 0x40)) # 1 = charging, 0 = not charging
    data["SENSA"] = float(line_items[5])
    data["SENSB"] = float(line_items[6])
    return data

def init_graphs():
    # = General setup =
    plt.ion()
    fig, ax = plt.subplots(nrows=2, ncols=4, figsize=(14, 6), layout="constrained")
    ax_flat = ax.flatten()
    x_time = []

    # = For SENSA =
    y_sensa = []
    line_sensa, = ax_flat[0].plot(x_time, y_sensa, 'b-o')
    ax_flat[0].set_xlabel("Time")
    # Set the format (e.g., "26 Nov 14:00")
    date_format = mdates.DateFormatter('%d %b %H:%M')
    ax_flat[0].xaxis.set_major_formatter(date_format)
    # Auto-rotate labels so they do not overlap
    fig.autofmt_xdate()
    ax_flat[0].set_ylabel("Resistance (kOhms)")
    ax_flat[0].set_title("Sensor A soil resistance")

    # = For SENSB = 
    y_sensb = []
    line_sensb, = ax_flat[1].plot(x_time, y_sensb, 'b-o')
    ax_flat[1].set_xlabel("Time")
    # Set the format (e.g., "26 Nov 14:00")
    date_format = mdates.DateFormatter('%d %b %H:%M')
    ax_flat[1].xaxis.set_major_formatter(date_format)
    # Auto-rotate labels so they do not overlap
    fig.autofmt_xdate()
    ax_flat[1].set_ylabel("Resistance (kOhms)")
    ax_flat[1].set_title("Sensor B soil resistance")

    # = For RSSI = 
    y_rssi = []
    line_rssi, = ax_flat[2].plot(x_time, y_rssi, 'g-o')
    ax_flat[2].set_xlabel("Time")
    # Set the format (e.g., "26 Nov 14:00")
    date_format = mdates.DateFormatter('%d %b %H:%M')
    ax_flat[2].xaxis.set_major_formatter(date_format)
    # Auto-rotate labels so they do not overlap
    fig.autofmt_xdate()
    ax_flat[2].set_ylabel("RSSI (dBm)")
    ax_flat[2].set_title("Received Signal Strength Indicator (RSSI, dBm)")

    # = For SNR =
    y_snr = []
    line_snr, = ax_flat[3].plot(x_time, y_snr, 'g-o')
    ax_flat[3].set_xlabel("Time")
    # Set the format (e.g., "26 Nov 14:00")
    date_format = mdates.DateFormatter('%d %b %H:%M')
    ax_flat[3].xaxis.set_major_formatter(date_format)
    # Auto-rotate labels so they do not overlap
    fig.autofmt_xdate()
    ax_flat[3].set_ylabel("SNR (dB)")
    ax_flat[3].set_title("Signal to Noise Ratio (SNR, dB)")

    # = For VBAT = 
    y_vbat = []
    line_vbat, = ax_flat[4].plot(x_time, y_vbat, 'r-o')
    ax_flat[4].set_xlabel("Time")
    # Set the format (e.g., "26 Nov 14:00")
    date_format = mdates.DateFormatter('%d %b %H:%M')
    ax_flat[4].xaxis.set_major_formatter(date_format)
    # Auto-rotate labels so they do not overlap
    fig.autofmt_xdate()
    ax_flat[4].set_ylabel("Voltage (V)")
    ax_flat[4].set_title("Cell Voltage (V)")

    # = For TEMP = 
    y_temp = []
    line_temp, = ax_flat[5].plot(x_time, y_temp, 'r-o')
    ax_flat[5].set_xlabel("Time")
    # Set the format (e.g., "26 Nov 14:00")
    date_format = mdates.DateFormatter('%d %b %H:%M')
    ax_flat[5].xaxis.set_major_formatter(date_format)
    # Auto-rotate labels so they do not overlap
    fig.autofmt_xdate()
    ax_flat[5].set_ylabel("Temperature (deg C)")
    ax_flat[5].set_title("Internal Temperature (C)")

    # = For CHG = 
    y_chg = []
    line_chg, = ax_flat[6].plot(x_time, y_chg, 'y-o')
    ax_flat[6].set_xlabel("Time")
    # Set the format (e.g., "26 Nov 14:00")
    date_format = mdates.DateFormatter('%d %b %H:%M')
    ax_flat[6].xaxis.set_major_formatter(date_format)
    # Auto-rotate labels so they do not overlap
    fig.autofmt_xdate()
    ax_flat[6].set_ylabel("Charging Status (Y=1/N=0)")
    ax_flat[6].set_title("Solar Charging Status")

    # Extra plot (disabled)
    ax_flat[7].axis("off")

    # Graphs data structure
    graphs = {}
    graphs["SENSA"] = [x_time, y_sensa, line_sensa, ax_flat[0], fig]
    graphs["SENSB"] = [x_time, y_sensb, line_sensb, ax_flat[1], fig]
    graphs["RSSI"] = [x_time, y_rssi, line_rssi, ax_flat[2], fig]
    graphs["SNR"] = [x_time, y_snr, line_snr, ax_flat[3], fig]
    graphs["VBAT"] = [x_time, y_vbat, line_vbat, ax_flat[4], fig]
    graphs["TEMP"] = [x_time, y_temp, line_temp, ax_flat[5], fig]
    graphs["CHG"] = [x_time, y_chg, line_chg, ax_flat[6], fig]
    plt.show()
    plt.pause(0.1)
    return graphs

def update_graphs(data, graphs):
    # loop through all items in DATA and update the corresponding graph
    graphs["SENSA"][0].append(data["TIMESTAMP"])   # shared x data, once
    for graph_name, graph in graphs.items():
        # update graphs live, takes in a dictionary, x axis is timestamp for all graphs
        graph[1].append(data[graph_name]) # Y DATA
        
        # Update existing line
        graph[2].set_xdata(graph[0])
        graph[2].set_ydata(graph[1])

        # Rescale axes
        graph[3].relim()
        graph[3].autoscale_view()

    # Redraw entire figure
    graphs["SENSA"][4].canvas.draw_idle()

    # Process GUI events
    plt.pause(0.001)


if __name__ == "__main__":
    selected_port = get_serial_port()
    print("Defaulting to 115200 baud")
    baud = 115200
    graphs = init_graphs()
    monitor_serial(selected_port, graphs, baud)
    plt.ioff()
    plt.show()