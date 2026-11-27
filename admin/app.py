"""CryoSpawn God-Mode Host Admin Panel (0.0.0.0:9091) — Modular Entrypoint."""

import hmac
import os
import secrets
import sqlite3
import sys

from flask import Flask, jsonify, redirect, request, send_from_directory, session
from werkzeug.security import check_password_hash

if __package__ is None or __package__ == "":
    sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from admin.config import get_db_path, get_fallback_password
from admin.daemon_routes import daemon_bp
from admin.rootfs_routes import rootfs_bp
from admin.sqlite_routes import sqlite_bp

app = Flask(__name__, static_folder="ui")
app.secret_key = os.environ.get("CRYOSPAWN_ADMIN_SECRET_KEY") or secrets.token_hex(32)
app.config["SESSION_COOKIE_NAME"] = "cryospawn_admin_session"
app.config["SESSION_COOKIE_HTTPONLY"] = True
app.config["SESSION_COOKIE_SAMESITE"] = "Lax"

app.register_blueprint(daemon_bp)
app.register_blueprint(rootfs_bp)
app.register_blueprint(sqlite_bp)


@app.route("/api/admin/login", methods=["POST"])
def admin_login():
    data = request.get_json(silent=True)
    if not isinstance(data, dict):
        data = {}
    username = data.get("username")
    password = data.get("password")
    if not isinstance(username, str) or not isinstance(password, str) or not username or not password:
        return jsonify({"error": "Invalid credentials"}), 401

    expected_user = os.environ.get("CRYOSPAWN_ADMIN_USER", "hostadmin")
    expected_pw = get_fallback_password()
    if hmac.compare_digest(username, expected_user) and hmac.compare_digest(password, expected_pw):
        session["admin_authenticated"] = True
        session["admin_username"] = username
        return jsonify({"status": "success", "username": username}), 200

    db_path = get_db_path()
    if os.path.exists(db_path):
        conn = sqlite3.connect(db_path, timeout=10)
        try:
            c = conn.cursor()
            c.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name='users'")
            if c.fetchone():
                c.execute("SELECT password_hash, role FROM users WHERE username = ?", (username,))
                row = c.fetchone()
                if row and row[1] == "admin" and check_password_hash(row[0], password):
                    session["admin_authenticated"] = True
                    session["admin_username"] = username
                    return jsonify({"status": "success", "username": username}), 200
        finally:
            conn.close()

    return jsonify({"error": "Invalid admin credentials"}), 401


@app.route("/api/admin/logout", methods=["POST"])
def admin_logout():
    session.clear()
    return jsonify({"status": "success"})


@app.route("/api/admin/me", methods=["GET"])
def admin_me():
    if not session.get("admin_authenticated"):
        return jsonify({"error": "Unauthorized"}), 401
    return jsonify({
        "username": session.get("admin_username", "hostadmin"),
        "is_root": os.geteuid() == 0,
    })


# --- MULTI-PAGE UI ROUTES ---
@app.route("/")
def admin_index():
    if not session.get("admin_authenticated"):
        return redirect("/login")
    return send_from_directory(app.static_folder, "dashboard.html")


@app.route("/vms")
def admin_vms_page():
    if not session.get("admin_authenticated"):
        return redirect("/login")
    return send_from_directory(app.static_folder, "vms.html")


@app.route("/rootfs")
def admin_rootfs_page():
    if not session.get("admin_authenticated"):
        return redirect("/login")
    return send_from_directory(app.static_folder, "rootfs.html")


@app.route("/database")
def admin_database_page():
    if not session.get("admin_authenticated"):
        return redirect("/login")
    return send_from_directory(app.static_folder, "database.html")


@app.route("/login")
def admin_login_page():
    return send_from_directory(app.static_folder, "login.html")


@app.route("/<path:filename>")
def admin_static(filename):
    if filename.endswith(".html") and filename != "login.html":
        if not session.get("admin_authenticated"):
            return redirect("/login")
    return send_from_directory(app.static_folder, filename)


if __name__ == "__main__":
    port = int(os.environ.get("CRYOSPAWN_ADMIN_PORT", "9091"))
    pw = get_fallback_password()
    print(f"[*] CryoSpawn God-Mode Admin Panel running on http://0.0.0.0:{port}")
    print(f"[*] Admin User: {os.environ.get('CRYOSPAWN_ADMIN_USER', 'hostadmin')}")
    if not os.environ.get("CRYOSPAWN_ADMIN_PASSWORD"):
        print(f"[*] Generated One-Time Admin Password: {pw}")
    print(f"[*] Running as UID {os.geteuid()} ({'ROOT - full daemon & rootfs control' if os.geteuid() == 0 else 'NON-ROOT - start with sudo for full control'})")
    app.run(host="0.0.0.0", port=port, threaded=True)
