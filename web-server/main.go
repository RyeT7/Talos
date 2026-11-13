package main

import (
	"context"
	"encoding/json"
	"log"
	"os"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"
	mqtt "github.com/mochi-mqtt/server/v2"
	"github.com/mochi-mqtt/server/v2/hooks/auth"
	"github.com/mochi-mqtt/server/v2/listeners"
	"github.com/mochi-mqtt/server/v2/packets"
)

// one row from the board, pointers for anything that can be null
type Reading struct {
	Ts          *int64   `json:"ts"`
	UptimeMs    int64    `json:"uptime_ms"`
	Temperature *float64 `json:"temperature"`
	Humidity    *float64 `json:"humidity"`
	MicPeak     int64    `json:"mic_peak"`
	IrCmd       *string  `json:"ir_cmd"`
	AcPower     *bool    `json:"ac_power"`
	AcMode      *string  `json:"ac_mode"`
	AcSetpoint  *int64   `json:"ac_setpoint"`
	AcFan       *string  `json:"ac_fan"`
}

// makes the tables if they're not there yet, the AC columns got added later so they're added separately
const createTable = `
CREATE TABLE IF NOT EXISTS readings (
    ts          TIMESTAMPTZ,
    uptime_ms   BIGINT NOT NULL,
    temperature REAL,
    humidity    REAL,
    mic_peak    INTEGER NOT NULL,
    ir_cmd      TEXT,
    received_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (ts, uptime_ms)
);
ALTER TABLE readings ADD COLUMN IF NOT EXISTS ac_power BOOLEAN;
ALTER TABLE readings ADD COLUMN IF NOT EXISTS ac_mode TEXT;
ALTER TABLE readings ADD COLUMN IF NOT EXISTS ac_setpoint SMALLINT;
ALTER TABLE readings ADD COLUMN IF NOT EXISTS ac_fan TEXT;
CREATE TABLE IF NOT EXISTS predictions (
    received_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    payload     JSONB NOT NULL
)`

// the board can resend a row after reconnecting, the unique (ts, uptime_ms) makes sure it only goes in once
const insertReading = `
INSERT INTO readings (ts, uptime_ms, temperature, humidity, mic_peak, ir_cmd, ac_power, ac_mode, ac_setpoint, ac_fan)
VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)
ON CONFLICT DO NOTHING`

// predictions are stored as they come, the payload is already JSON
const insertPrediction = `INSERT INTO predictions (payload) VALUES ($1)`

// runs the MQTT broker for the board and the model service, and writes everything it gets into PostgreSQL
func main() {
	ctx := context.Background()

	// connect to PostgreSQL and make the tables
	db, err := pgxpool.New(ctx, os.Getenv("DATABASE_URL"))
	if err != nil {
		log.Fatal(err)
	}

	if _, err := db.Exec(ctx, createTable); err != nil {
		log.Fatal(err)
	}

	// inline client so the server itself can subscribe to the topics
	server := mqtt.New(&mqtt.Options{InlineClient: true})

	// no auth, it's only on the LAN
	if err := server.AddHook(new(auth.AllowHook), nil); err != nil {
		log.Fatal(err)
	}

	if err := server.AddListener(listeners.NewTCP(listeners.Config{ID: "tcp", Address: ":4092"})); err != nil {
		log.Fatal(err)
	}

	// every reading from the board goes into readings
	err = server.Subscribe("talos/readings", 1, func(cl *mqtt.Client, sub packets.Subscription, pk packets.Packet) {
		var r Reading

		if err := json.Unmarshal(pk.Payload, &r); err != nil {
			log.Printf("bad payload %q: %v", pk.Payload, err)
			return
		}

		var ts *time.Time

		// no ts means the board's clock wasn't synced, so it stays null and received_at is the time
		if r.Ts != nil {
			t := time.UnixMilli(*r.Ts)
			ts = &t
		}

		if _, err := db.Exec(ctx, insertReading, ts, r.UptimeMs, r.Temperature, r.Humidity, r.MicPeak, r.IrCmd, r.AcPower, r.AcMode, r.AcSetpoint, r.AcFan); err != nil {
			log.Printf("insert: %v", err)
		}
	})

	if err != nil {
		log.Fatal(err)
	}

	// every prediction from the model service goes into predictions
	err = server.Subscribe("talos/predictions", 3, func(cl *mqtt.Client, sub packets.Subscription, pk packets.Packet) {
		if !json.Valid(pk.Payload) {
			log.Printf("bad prediction %q", pk.Payload)
			return
		}

		if _, err := db.Exec(ctx, insertPrediction, string(pk.Payload)); err != nil {
			log.Printf("insert prediction: %v", err)
		}
	})

	if err != nil {
		log.Fatal(err)
	}

	// the board's will, just log when it goes online or offline
	err = server.Subscribe("talos/status", 2, func(cl *mqtt.Client, sub packets.Subscription, pk packets.Packet) {
		log.Printf("board is %s", pk.Payload)
	})

	if err != nil {
		log.Fatal(err)
	}

	// start the broker and keep running
	if err := server.Serve(); err != nil {
		log.Fatal(err)
	}

	select {}
}
