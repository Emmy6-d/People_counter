from datetime import datetime, timedelta, timezone
from functools import wraps
from flask import Flask, jsonify, render_template, request, abort
from supabase import create_client
import config

app = Flask(__name__)
app.config["SECRET_KEY"] = config.FLASK_SECRET_KEY

supabase = create_client(
    config.SUPABASE_URL,
    config.SUPABASE_SERVICE_ROLE_KEY
)


def device_auth_required(fn):
    @wraps(fn)
    def wrapper(*args, **kwargs):
        supplied = request.headers.get("X-Device-Key", "")
        if not supplied or supplied != config.DEVICE_API_KEY:
            return jsonify({"ok": False, "error": "Unauthorized device"}), 401
        return fn(*args, **kwargs)
    return wrapper


def utc_now_iso():
    return datetime.now(timezone.utc).isoformat()


@app.get("/")
def dashboard():
    return render_template("dashboard.html")


@app.get("/reports")
def reports():
    return render_template("reports.html")


@app.get("/events")
def events():
    return render_template("events.html")


@app.get("/api/health")
def health():
    return jsonify({"ok": True, "service": "people-counter-api"})


@app.post("/api/device/event")
@device_auth_required
def receive_event():
    data = request.get_json(silent=True) or {}

    device_id = str(data.get("device_id", "")).strip()
    event_type = str(data.get("event_type", "valid_s1_to_s2")).strip()
    sensor_sequence = str(data.get("sensor_sequence", "S1->S2")).strip()
    count_delta = int(data.get("count_delta", 1))

    if not device_id:
        return jsonify({"ok": False, "error": "device_id is required"}), 400

    if count_delta != 1:
        return jsonify({"ok": False, "error": "count_delta must be 1"}), 400

    if event_type != "valid_s1_to_s2":
        return jsonify({"ok": False, "error": "unsupported event_type"}), 400

    # Server time is authoritative. This avoids trusting the ESP32 clock.
    row = {
        "device_id": device_id,
        "event_type": event_type,
        "sensor_sequence": sensor_sequence,
        "count_delta": count_delta,
        "event_time": utc_now_iso(),
    }

    result = supabase.table("person_events").insert(row).execute()

    if not result.data:
        return jsonify({"ok": False, "error": "Database insert failed"}), 500

    return jsonify({
        "ok": True,
        "event_id": result.data[0]["id"],
        "event_time": result.data[0]["event_time"],
    }), 201


@app.get("/api/summary")
def api_summary():
    # Supabase SQL aggregation is exposed through RPC functions created by schema.sql.
    today = supabase.rpc("get_counter_summary", {}).execute()
    if not today.data:
        return jsonify({
            "today": 0,
            "week": 0,
            "month": 0,
            "all_time": 0
        })

    return jsonify(today.data[0])


@app.get("/api/daily")
def api_daily():
    days = min(max(int(request.args.get("days", 30)), 1), 366)
    result = supabase.rpc("get_daily_report", {"p_days": days}).execute()
    return jsonify(result.data or [])


@app.get("/api/weekly")
def api_weekly():
    weeks = min(max(int(request.args.get("weeks", 12)), 1), 104)
    result = supabase.rpc("get_weekly_report", {"p_weeks": weeks}).execute()
    return jsonify(result.data or [])


@app.get("/api/monthly")
def api_monthly():
    months = min(max(int(request.args.get("months", 12)), 1), 60)
    result = supabase.rpc("get_monthly_report", {"p_months": months}).execute()
    return jsonify(result.data or [])


@app.get("/api/recent-events")
def api_recent_events():
    limit = min(max(int(request.args.get("limit", 50)), 1), 200)
    result = (
        supabase.table("person_events")
        .select("id,device_id,event_type,sensor_sequence,count_delta,event_time")
        .order("event_time", desc=True)
        .limit(limit)
        .execute()
    )
    return jsonify(result.data or [])


@app.errorhandler(400)
def bad_request(e):
    return jsonify({"ok": False, "error": str(e)}), 400


@app.errorhandler(404)
def not_found(e):
    return jsonify({"ok": False, "error": "Not found"}), 404


if __name__ == "__main__":
    app.run(
        host="0.0.0.0",
        port=config.PORT,
        debug=config.FLASK_DEBUG
    )
