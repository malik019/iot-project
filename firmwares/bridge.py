import json
from influxdb import InfluxDBClient
import paho.mqtt.client as mqtt

# Configuration
MQTT_BROKER = "localhost"  
MQTT_PORT = 1883
MQTT_TOPIC = "robot/telemetry"

INFLUX_HOST = "localhost"
INFLUX_PORT = 8086
INFLUX_USER = "botuser"
INFLUX_PASS = "robot2026"
INFLUX_DB = "robot"

# Initialize InfluxDB Client
db_client = InfluxDBClient(
    host=INFLUX_HOST,
    port=INFLUX_PORT,
    username=INFLUX_USER,
    password=INFLUX_PASS,
    database=INFLUX_DB
)

# Callback when connecting to the MQTT broker
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"Connected to MQTT Broker! Subscribing to: {MQTT_TOPIC}")
        client.subscribe(MQTT_TOPIC)
    else:
        print(f"MQTT Connection failed with code {rc}")

# Callback when an MQTT message is received
def on_message(client, userdata, msg):
    try:
        # Decode JSON payload from MQTT
        payload = json.loads(msg.payload.decode('utf-8'))
        print(f"Received data: {payload}")
        
        # Build InfluxDB JSON structure
        json_body = [
            {
                "measurement": "battery_metrics",
                "tags": {
                    "device_id": payload.get("device_id", "unknown_device"),
                    "status": payload.get("status", "unknown")
                },
                "fields": {
                    "current_a": float(payload["current_a"]),
                    "remaining_mah": int(payload["remaining_mah"]),
                    "soc_percent": int(payload["soc_percent"]),
                    "soh_percent": float(payload["soh_percent"]),
                    "temperature_c": float(payload["temperature_c"]),
                    "voltage_v": float(payload["voltage_v"])
                }
            }
        ]
        
        # Write to InfluxDB
        db_client.write_points(json_body)
        print("Successfully written to InfluxDB.")
        
    except KeyError as e:
        print(f"Data missing expected field: {e}")
    except ValueError as e:
        print(f"Data type conversion error: {e}")
    except Exception as e:
        print(f"Error handling message: {e}")

if __name__ == "__main__":
    mqtt_client = mqtt.Client()
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message

    print("Starting MQTT to InfluxDB Bridge...")
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
        mqtt_client.loop_forever()
    except KeyboardInterrupt:
        print("\nDisconnecting bridge...")
        mqtt_client.disconnect()
