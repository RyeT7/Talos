import json
import os
from collections import deque
from pathlib import Path

import joblib
import numpy as np
import pandas as pd
import paho.mqtt.client as mqtt

# the broker is the one inside the Go server, readings come in and predictions go back out to the board
HERE = Path(__file__).parent
MQTT_HOST = os.getenv("MQTT_HOST", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "4092"))
READINGS_TOPIC = "talos/readings"
PREDICTIONS_TOPIC = "talos/predictions"
# the board sends UTC, the models were trained on Jakarta time
UTC_OFFSET = pd.Timedelta(hours=7)
# keeps the last 40 minutes of rows (1200 x 2 seconds), enough for the 30 minute features
HISTORY_ROWS = 1200
# same occupancy as 00_preprocessing, a spike over double the 5 minute median and it stays occupied for 15 minutes
ACTIVITY_RATIO = 2.0
ACTIVITY_HOLD_ROWS = 450

# the three models from the notebooks
forecast = joblib.load(HERE / "temp_forecast_model.pkl")
anomaly = joblib.load(HERE / "anomaly_model.pkl")
recommender = joblib.load(HERE / "ac_recommender_model.pkl")

# the recent rows, the minute we're collecting, and how many anomalies came in during it
rows = deque(maxlen=HISTORY_ROWS)
state = {"minute": None, "anomalies": 0}


# checks one row against the previous one with the anomaly model, same features as 02_anomaly_detection
def is_anomaly(row, prev):
    x = pd.DataFrame([{
        "temp_c": row["temp_c"],
        "humidity_pct": row["humidity_pct"],
        "mic_log": np.log10(max(row["mic_peak"], 1)),
        "temp_jump": abs(row["temp_c"] - prev["temp_c"]) if prev else 0.0,
        "hum_jump": abs(row["humidity_pct"] - prev["humidity_pct"]) if prev else 0.0,
    }])
    return anomaly["model"].predict(x[anomaly["features"]])[0] == -1


# builds the same per minute table as 00_preprocessing out of the rows we have
def minutes():
    df = pd.DataFrame(rows)
    df["mic_peak"] = df["mic_peak"].clip(upper=5_000_000)
    df["temp_c"] = df["temp_c"].rolling(5, center=True, min_periods=1).median()
    df["humidity_pct"] = df["humidity_pct"].rolling(5, center=True, min_periods=1).median()
    df["mic_median_5m"] = df["mic_peak"].rolling(150, min_periods=150).median()
    df["activity"] = (df["mic_peak"] >= ACTIVITY_RATIO * df["mic_median_5m"]).astype(int)
    df["occupied"] = df["activity"].rolling(ACTIVITY_HOLD_ROWS, min_periods=1).max().astype(int)
    df["hour"] = df["pc_time"].dt.hour
    df["weekday"] = df["pc_time"].dt.weekday
    df = df.dropna(subset=["mic_median_5m"])

    m = df.set_index("pc_time").resample("1min").agg({
        "temp_c": "mean",
        "humidity_pct": "mean",
        "mic_peak": "median",
        "mic_median_5m": "last",
        "occupied": "last",
        "ac_on": "last",
        "setpoint": "last",
        "hour": "last",
        "weekday": "last",
    }).dropna()

    # shift by time instead of rows, so a gap doesn't make it look at the wrong minute
    m["temp_change_5m"] = m["temp_c"] - m["temp_c"].shift(freq="5min")
    m["temp_30m_ago"] = m["temp_c"].shift(freq="30min")
    m["hour_sin"] = np.sin(2 * np.pi * m["hour"] / 24)
    m["hour_cos"] = np.cos(2 * np.pi * m["hour"] / 24)
    return m


# runs the forecast and the recommender for one minute, either one stays None if it doesn't have enough history yet
def predict(minute):
    m = minutes()

    if minute not in m.index:
        return None

    now = m.loc[[minute]]
    out = {
        "minute": minute.strftime("%Y-%m-%d %H:%M"),
        "temp_now": round(float(now["temp_c"].iloc[0]), 2),
        "temp_in_10m": None,
        "ac_recommendation": None,
        "confidence": None,
        "anomalies": state["anomalies"],
    }

    x = now[forecast["features"]]
    if not x.isna().any(axis=None):
        out["temp_in_10m"] = round(float(forecast["model"].predict(x)[0]), 2)

    x = now[recommender["features"]]
    if not x.isna().any(axis=None):
        p = recommender["model"].predict_proba(x)[0]
        out["ac_recommendation"] = str(recommender["model"].classes_[p.argmax()])
        out["confidence"] = round(float(p.max()), 2)

    return out


# called on every reading from the board
def on_message(client, userdata, msg):
    try:
        r = json.loads(msg.payload)
    except ValueError:
        return

    # skip rows without a time or a DHT reading
    if r.get("ts") is None or r.get("temperature") is None or r.get("humidity") is None:
        return

    row = {
        "pc_time": pd.to_datetime(r["ts"], unit="ms") + UTC_OFFSET,
        "temp_c": float(r["temperature"]),
        "humidity_pct": float(r["humidity"]),
        "mic_peak": int(r["mic_peak"]),
        "ac_on": int(bool(r.get("ac_power"))),
        "setpoint": float(r.get("ac_setpoint") or 21),
    }

    # throw out values the DHT22 can't actually give, same as the notebook
    if not (10 <= row["temp_c"] <= 45 and 0 <= row["humidity_pct"] <= 100):
        return

    minute = row["pc_time"].floor("1min")

    # a new minute started, so predict the one that just finished and send it
    if state["minute"] is not None and minute > state["minute"]:
        out = predict(state["minute"])

        if out:
            payload = json.dumps(out, separators=(",", ":"))
            client.publish(PREDICTIONS_TOPIC, payload, qos=1)
            print(payload, flush=True)

        state["anomalies"] = 0

    # count the anomalies within the minute, then keep the row
    if is_anomaly(row, rows[-1] if rows else None):
        state["anomalies"] += 1

    rows.append(row)
    state["minute"] = minute


# subscribe again on every connect
def on_connect(client, userdata, flags, reason_code, properties):
    print(f"mqtt: connected ({reason_code})", flush=True)
    client.subscribe(READINGS_TOPIC, qos=1)


# keeps retrying until the broker is up
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="talos-models")
client.on_connect = on_connect
client.on_message = on_message
client.connect_async(MQTT_HOST, MQTT_PORT)
client.loop_forever(retry_first_connection=True)
