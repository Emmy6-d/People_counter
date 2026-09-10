# ESP32 + Flask + Supabase People Counter

A complete starter system for a two-sensor directional counter.

## Architecture

ESP32 sensors -> Wi-Fi -> Flask API -> Supabase PostgreSQL
                                      -> Flask dashboard
                                      -> daily/weekly/monthly reports

The ESP32 only sends an event when the existing S1 -> S2 rule produces a valid count.
The server stores one immutable event row per counted crossing. Reports are calculated from
the event timestamps, so historical daily/weekly/monthly reports remain available.

## Important limitation

This is a directional crossing counter, not a human-recognition system. It assumes one person
passes the sensing zone per valid S1 -> S2 event. If two people overlap in the sensing zone,
the hardware logic may count them as one.

## Project tree

people_counter_system/
  app.py
  config.py
  requirements.txt
  .env.example
  .gitignore
  database/
    schema.sql
  templates/
    base.html
    dashboard.html
    reports.html
    events.html
  static/
    css/style.css
    js/dashboard.js
  esp32/
    people_counter_esp32.ino

## Quick start

1. Create a Supabase project.
2. Run database/schema.sql in Supabase SQL Editor.
3. Copy .env.example to .env and fill in:
   - SUPABASE_URL
   - SUPABASE_SERVICE_ROLE_KEY
   - DEVICE_API_KEY
   - FLASK_SECRET_KEY
4. Create a Python virtual environment.
5. Install requirements.txt.
6. Run `python app.py`.
7. Open http://127.0.0.1:5000
8. Put your computer and ESP32 on the same network.
9. Configure Wi-Fi and FLASK_API_URL in the ESP32 sketch.
10. Upload the ESP32 sketch.
11. Test S1 -> S2. A valid crossing should create one row in `person_events`.

## Deploy Flask on Render

1. Push this repository to GitHub, including `render.yaml`.
2. In Render, choose **New + -> Blueprint** and select the repository.
3. Create the service from `render.yaml`.
4. In the Render service environment settings, enter:
  - `SUPABASE_URL`
  - `SUPABASE_SERVICE_ROLE_KEY`
  - `DEVICE_API_KEY`
  - `FLASK_SECRET_KEY`
5. Deploy and open `https://YOUR-RENDER-SERVICE.onrender.com/api/health`.
6. Confirm the response is `{"ok":true,"service":"people-counter-api"}`.
7. Replace `YOUR-RENDER-SERVICE` in `esp32/people_counter_esp32.ino` with the actual Render service name.
8. Upload the sketch to the ESP32 and test a valid S1 -> S2 crossing.

The ESP32 uses HTTPS for Render. The `WiFiClientSecure` setup currently uses
`setInsecure()` for compatibility; install certificate validation before using
the device in an untrusted environment.

## Security

The Supabase service-role/secret key belongs ONLY in Flask's server-side .env.
Never put it in HTML, JavaScript, ESP32 firmware, or GitHub.

The ESP32 authenticates to Flask using DEVICE_API_KEY.
