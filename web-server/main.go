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

const insertReading = `
INSERT INTO readings (ts, uptime_ms, temperature, humidity, mic_peak, ir_cmd, ac_power, ac_mode, ac_setpoint, ac_fan)
VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)
ON CONFLICT DO NOTHING`

const insertPrediction = `INSERT INTO predictions (payload) VALUES ($1)`

func main() {
	ctx := context.Background()

	db, err := pgxpool.New(ctx, os.Getenv("DATABASE_URL"))
	if err != nil {
		log.Fatal(err)
	}

	if _, err := db.Exec(ctx, createTable); err != nil {
		log.Fatal(err)
	}

	server := mqtt.New(&mqtt.Options{InlineClient: true})

	if err := server.AddHook(new(auth.AllowHook), nil); err != nil {
		log.Fatal(err)
	}

	if err := server.AddListener(listeners.NewTCP(listeners.Config{ID: "tcp", Address: ":4092"})); err != nil {
		log.Fatal(err)
	}

	err = server.Subscribe("talos/readings", 1, func(cl *mqtt.Client, sub packets.Subscription, pk packets.Packet) {
		var r Reading

		if err := json.Unmarshal(pk.Payload, &r); err != nil {
			log.Printf("bad payload %q: %v", pk.Payload, err)
			return
		}

		var ts *time.Time

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

	err = server.Subscribe("talos/status", 2, func(cl *mqtt.Client, sub packets.Subscription, pk packets.Packet) {
		log.Printf("board is %s", pk.Payload)
	})

	if err != nil {
		log.Fatal(err)
	}

	if err := server.Serve(); err != nil {
		log.Fatal(err)
	}

	select {}
}
