import csv, os
from datetime import datetime
from flask import Flask, jsonify, send_from_directory

app = Flask(__name__)

data = {"temp": 0, "hum": 0, "gas": 0}
CSV_FILE = "sensor_data.csv"

# create file if not exist
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

@app.route("/ingest", methods=["POST"])
def ingest():
    payload = request.get_json(force=True, silent=True) or {}
    try:
        data["temp"] = float(payload.get("temp", data["temp"]))
        data["hum"]  = int(payload.get("hum", data["hum"]))
        data["gas"]  = int(payload.get("gas", data["gas"]))
        save_to_csv(data)
        return jsonify({"ok": True, "data": data})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 400

@app.route("/data")
def get_data():
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

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=False)
