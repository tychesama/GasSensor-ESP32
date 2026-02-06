import serial
import threading
import csv
import os
from datetime import datetime
from flask import Flask, jsonify, send_from_directory

# Open Python side of virtual COM
ser = serial.Serial('COM1', 2400, timeout=1)

data = {"temp": 0, "hum": 0, "gas": 0}

CSV_FILE = "sensor_data.csv"

# Create CSV file with header if it doesn't exist
if not os.path.exists(CSV_FILE):
    with open(CSV_FILE, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["timestamp", "temp", "hum", "gas"])

def save_to_csv(d):
    with open(CSV_FILE, "a", newline="") as f:
        writer = csv.writer(f)
        writer.writerow([
            datetime.now().isoformat(),
            d["temp"],
            d["hum"],
            d["gas"]
        ])


def read_serial():
    while True:
        try:
            line = ser.readline().decode().strip()
            if line:
                print("Raw serial:", line)  
            if line.startswith("T"):
                parts = line.split(",")
                data["temp"] = float(parts[1])
                data["hum"]  = int(parts[3])
                data["gas"]  = int(parts[5])
                print("Parsed data:", data)  
                save_to_csv(data)
        except Exception as e:
            print("Serial error:", e)

threading.Thread(target=read_serial, daemon=True).start()

app = Flask(__name__)

@app.route("/data")
def get_data():
    try:
        with open(CSV_FILE, "r") as f:
            rows = list(csv.DictReader(f))
            if rows:
                last = rows[-1]
                return jsonify({
                    "temp": float(last["temp"]),
                    "hum": int(last["hum"]),
                    "gas": int(last["gas"])
                })
    except Exception as e:
        print("CSV read error:", e)

    return jsonify(data)

@app.route("/")
def index():
    return send_from_directory(".", "sensor.html")

@app.route("/history")
def history():
    rows = []
    with open(CSV_FILE, "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append({
                "x": row["timestamp"],
                "temp": float(row["temp"]),
                "hum": int(row["hum"]),
                "gas": int(row["gas"])
            })
    return jsonify(rows)


app.run(port=5000)
