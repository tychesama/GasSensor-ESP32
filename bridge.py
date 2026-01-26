import serial
import threading
from flask import Flask, jsonify, send_from_directory

# Open Python side of virtual COM
ser = serial.Serial('COM1', 2400, timeout=1)

data = {"temp": 0, "hum": 0, "gas": 0}

def read_serial():
    while True:
        try:
            line = ser.readline().decode().strip()
            if line:
                print("Raw serial:", line)   # <-- debug
            if line.startswith("T"):
                parts = line.split(",")
                data["temp"] = float(parts[1])
                data["hum"]  = int(parts[3])
                data["gas"]  = int(parts[5])
                print("Parsed data:", data)  # <-- debug
        except Exception as e:
            print("Serial error:", e)

threading.Thread(target=read_serial, daemon=True).start()

app = Flask(__name__)

@app.route("/data")
def get_data():
    return jsonify(data)

@app.route("/")
def index():
    return send_from_directory(".", "sensor.html")


app.run(port=5000)
