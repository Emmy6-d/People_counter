import os
from dotenv import load_dotenv

load_dotenv()

SUPABASE_URL = os.getenv("SUPABASE_URL", "").strip()
SUPABASE_SERVICE_ROLE_KEY = os.getenv("SUPABASE_SERVICE_ROLE_KEY", "").strip()
DEVICE_API_KEY = os.getenv("DEVICE_API_KEY", "").strip()
FLASK_SECRET_KEY = os.getenv("FLASK_SECRET_KEY", "").strip()

FLASK_DEBUG = os.getenv("FLASK_DEBUG", "0") == "1"
PORT = int(os.getenv("PORT", "5000"))

if not SUPABASE_URL:
    raise RuntimeError("SUPABASE_URL is missing from .env")

if not SUPABASE_SERVICE_ROLE_KEY:
    raise RuntimeError("SUPABASE_SERVICE_ROLE_KEY is missing from .env")

if SUPABASE_SERVICE_ROLE_KEY.startswith("sb_publishable_"):
    raise RuntimeError(
        "SUPABASE_SERVICE_ROLE_KEY contains a publishable key. "
        "Use the Supabase secret/service_role key in .env."
    )

if not DEVICE_API_KEY:
    raise RuntimeError("DEVICE_API_KEY is missing from .env")

if not FLASK_SECRET_KEY:
    raise RuntimeError("FLASK_SECRET_KEY is missing from .env")
