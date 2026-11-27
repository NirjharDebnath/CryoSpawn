"""SQLite database inspector, custom SQL console, and user management routes."""

import os
import sqlite3

from flask import Blueprint, jsonify, request
from werkzeug.security import generate_password_hash

from admin.config import SQLITE_TABLE_RE, admin_required, fix_db_perms, get_db_path

sqlite_bp = Blueprint("admin_sqlite", __name__)


@sqlite_bp.route("/api/admin/sqlite/tables", methods=["GET"])
@admin_required
def sqlite_tables():
    db_path = get_db_path()
    if not os.path.exists(db_path):
        return jsonify({"tables": {}})
    conn = sqlite3.connect(db_path, timeout=10)
    conn.row_factory = sqlite3.Row
    try:
        c = conn.cursor()
        c.execute("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")
        table_names = [r[0] for r in c.fetchall() if SQLITE_TABLE_RE.match(r[0])]
        result = {}
        for t in table_names:
            c.execute(f"SELECT * FROM {t} LIMIT 200")
            rows = []
            for row in c.fetchall():
                d = dict(row)
                if "password_hash" in d:
                    d["password_hash"] = "***redacted***"
                rows.append(d)
            result[t] = rows
        return jsonify({"tables": result, "db_path": db_path})
    finally:
        conn.close()


@sqlite_bp.route("/api/admin/sqlite/query", methods=["POST"])
@admin_required
def sqlite_query():
    data = request.get_json(silent=True) or {}
    sql = data.get("sql")
    if not isinstance(sql, str) or not sql.strip():
        return jsonify({"error": "SQL query is required"}), 400

    db_path = get_db_path()
    if not os.path.exists(db_path):
        return jsonify({"error": "Database not found"}), 404

    cleaned = sql.strip()
    conn = sqlite3.connect(db_path, timeout=10)
    try:
        c = conn.cursor()
        body_no_trail = cleaned.rstrip(";").strip()
        if ";" in body_no_trail:
            conn.executescript(cleaned)
            conn.commit()
            fix_db_perms(db_path)
            return jsonify({"mode": "script", "status": "ok", "message": "SQL script executed."})

        c.execute(cleaned)
        if c.description is not None:
            columns = [d[0] for d in c.description]
            rows = [list(r) for r in c.fetchmany(500)]
            return jsonify({"mode": "select", "columns": columns, "rows": rows, "row_count": len(rows)})
        conn.commit()
        fix_db_perms(db_path)
        return jsonify({"mode": "write", "status": "ok", "rows_affected": c.rowcount})
    except sqlite3.Error as e:
        return jsonify({"error": str(e)}), 400
    finally:
        conn.close()


@sqlite_bp.route("/api/admin/users", methods=["POST"])
@admin_required
def admin_create_user():
    data = request.get_json(silent=True) or {}
    username = (data.get("username") or "").strip()
    password = data.get("password") or ""
    role = data.get("role") if data.get("role") in ("user", "admin") else "user"
    if not username or len(password) < 6:
        return jsonify({"error": "Username and password (>=6 chars) required"}), 400

    db_path = get_db_path()
    conn = sqlite3.connect(db_path, timeout=10)
    try:
        c = conn.cursor()
        c.execute(
            "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT UNIQUE NOT NULL, password_hash TEXT NOT NULL, role TEXT DEFAULT 'user')"
        )
        c.execute(
            "INSERT INTO users (username, password_hash, role) VALUES (?, ?, ?)",
            (username, generate_password_hash(password), role),
        )
        conn.commit()
        fix_db_perms(db_path)
        return jsonify({"status": "created", "username": username, "role": role}), 201
    except sqlite3.IntegrityError:
        return jsonify({"error": "Username already exists"}), 409
    finally:
        conn.close()


@sqlite_bp.route("/api/admin/users/<int:user_id>", methods=["DELETE"])
@admin_required
def admin_delete_user(user_id):
    db_path = get_db_path()
    if not os.path.exists(db_path):
        return jsonify({"error": "Database not found"}), 404
    conn = sqlite3.connect(db_path, timeout=10)
    try:
        c = conn.cursor()
        c.execute("DELETE FROM users WHERE id = ?", (user_id,))
        conn.commit()
        if c.rowcount == 0:
            return jsonify({"error": "User not found"}), 404
        return jsonify({"status": "deleted"})
    finally:
        conn.close()
