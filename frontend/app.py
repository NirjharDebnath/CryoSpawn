from flask import Flask, send_from_directory, request, Response, session, redirect, jsonify
from werkzeug.security import generate_password_hash, check_password_hash
import sqlite3
import requests
import os
import re
import json
import secrets

app = Flask(__name__, static_folder='../internal/ui')
app.secret_key = os.environ.get('CRYOSPAWN_SECRET_KEY') or secrets.token_hex(32)
BACKEND_PORT = os.environ.get('CRYOSPAWN_BACKEND_PORT', '9090')
BACKEND_URL = os.environ.get('CRYOSPAWN_BACKEND_URL', f'http://127.0.0.1:{BACKEND_PORT}')
INTERNAL_SECRET = os.environ.get('CRYOSPAWN_INTERNAL_SECRET', '')
DB_PATH = os.environ.get('CRYOSPAWN_DB_PATH', os.path.abspath(os.path.join(os.path.dirname(__file__), '../cryospawn.db')))

PROJECT_NAME_RE = re.compile(r'^[A-Za-z0-9][A-Za-z0-9._ -]{0,63}\Z')


def get_db_path():
    return os.environ.get('CRYOSPAWN_DB_PATH', DB_PATH)


def is_valid_project_name(name):
    if not isinstance(name, str):
        return False
    stripped = name.strip()
    if not stripped or '..' in stripped or '/' in stripped or '\\' in stripped:
        return False
    return bool(PROJECT_NAME_RE.match(stripped))


def table_exists(conn, table_name):
    c = conn.cursor()
    c.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", (table_name,))
    return c.fetchone() is not None


def init_db():
    db_path = get_db_path()
    conn = sqlite3.connect(db_path, timeout=10)
    c = conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS users (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    username TEXT UNIQUE NOT NULL,
                    password_hash TEXT NOT NULL,
                    role TEXT DEFAULT 'user'
                 )''')
    c.execute('''CREATE TABLE IF NOT EXISTS projects (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    owner_id INTEGER NOT NULL,
                    name TEXT NOT NULL,
                    description TEXT DEFAULT '',
                    created_at TEXT DEFAULT CURRENT_TIMESTAMP,
                    UNIQUE(owner_id, name)
                 )''')
    # Auto-create super admin if no users exist
    c.execute("SELECT COUNT(*) FROM users")
    if c.fetchone()[0] == 0:
        c.execute("INSERT INTO users (username, password_hash, role) VALUES (?, ?, ?)",
                  ('admin', generate_password_hash('admin123'), 'admin'))
    conn.commit()
    conn.close()

    # Ensure permissions allow both user and root processes to write to DB
    for ext in ['', '-wal', '-shm']:
        p = db_path + ext
        if os.path.exists(p):
            try:
                os.chmod(p, 0o666)
            except Exception:
                pass

init_db()

# --- AUTH ROUTES ---
@app.route('/api/auth/login', methods=['POST'])
def login():
    raw_data = request.get_json(silent=True)
    data = raw_data if isinstance(raw_data, dict) else {}
    username = data.get('username')
    password = data.get('password')
    if not isinstance(username, str) or not isinstance(password, str):
        return jsonify({"error": "Invalid credentials"}), 401
    
    conn = sqlite3.connect(get_db_path(), timeout=10)
    c = conn.cursor()
    c.execute("SELECT id, password_hash, role FROM users WHERE username = ?", (username,))
    user = c.fetchone()
    conn.close()
    
    if user and check_password_hash(user[1], password):
        session['user_id'] = user[0]
        session['role'] = user[2]
        session['username'] = username
        return jsonify({"status": "success", "role": user[2]})
    
    return jsonify({"error": "Invalid credentials"}), 401

@app.route('/api/auth/register', methods=['POST'])
def register():
    raw_data = request.get_json(silent=True)
    data = raw_data if isinstance(raw_data, dict) else {}
    username = data.get('username', '').strip() if isinstance(data.get('username'), str) else ''
    password = data.get('password', '') if isinstance(data.get('password'), str) else ''
    if not username or not password:
        return jsonify({"error": "Username and password are required"}), 400
    if len(password) < 6:
        return jsonify({"error": "Password must be at least 6 characters"}), 400

    conn = sqlite3.connect(get_db_path(), timeout=10)
    c = conn.cursor()
    try:
        c.execute("INSERT INTO users (username, password_hash, role) VALUES (?, ?, 'user')",
                  (username, generate_password_hash(password)))
        conn.commit()
        user_id = c.lastrowid
        conn.close()
        session['user_id'] = user_id
        session['username'] = username
        session['role'] = 'user'
        return jsonify({"status": "success", "role": "user"}), 201
    except sqlite3.IntegrityError:
        conn.close()
        return jsonify({"error": "Username already exists"}), 400

@app.route('/api/auth/change-password', methods=['POST'])
def change_password():
    if 'user_id' not in session:
        return jsonify({"error": "Unauthorized"}), 401
    raw_data = request.get_json(silent=True)
    data = raw_data if isinstance(raw_data, dict) else {}
    curr_pw = data.get('current_password', '') if isinstance(data.get('current_password'), str) else ''
    new_pw = data.get('new_password', '') if isinstance(data.get('new_password'), str) else ''
    if not curr_pw or not new_pw:
        return jsonify({"error": "Current and new password are required"}), 400
    if len(new_pw) < 6:
        return jsonify({"error": "New password must be at least 6 characters"}), 400

    conn = sqlite3.connect(get_db_path(), timeout=10)
    c = conn.cursor()
    c.execute("SELECT password_hash FROM users WHERE id = ?", (session['user_id'],))
    row = c.fetchone()
    if not row or not check_password_hash(row[0], curr_pw):
        conn.close()
        return jsonify({"error": "Current password incorrect"}), 400

    c.execute("UPDATE users SET password_hash = ? WHERE id = ?",
              (generate_password_hash(new_pw), session['user_id']))
    conn.commit()
    conn.close()
    return jsonify({"status": "success", "message": "Password updated successfully"})

@app.route('/api/auth/logout', methods=['POST'])
def logout():
    session.clear()
    return jsonify({"status": "success"})

@app.route('/api/auth/me', methods=['GET'])
def get_me():
    if 'user_id' not in session:
        return jsonify({"error": "Unauthorized"}), 401
    return jsonify({"user_id": session['user_id'], "username": session['username'], "role": session['role']})

# --- PROJECT ROUTES (Multi-tenant scoped by session['user_id']) ---
def _get_user_projects_map(conn, user_id):
    c = conn.cursor()
    projects_map = {}
    if table_exists(conn, 'projects'):
        c.execute(
            "SELECT name, description, created_at FROM projects WHERE owner_id = ? ORDER BY name COLLATE NOCASE ASC",
            (user_id,)
        )
        for name, description, created_at in c.fetchall():
            projects_map[name] = {
                "name": name,
                "description": description or "",
                "created_at": created_at,
                "persisted": True,
                "vm_count": 0,
                "running_count": 0,
                "hibernated_count": 0,
                "stopped_count": 0,
                "booting_count": 0,
            }

    if table_exists(conn, 'vms'):
        c.execute("SELECT project_name, status FROM vms WHERE owner_id = ?", (user_id,))
        for proj_name, status in c.fetchall():
            pname = (proj_name or '').strip() or 'default'
            if pname not in projects_map:
                projects_map[pname] = {
                    "name": pname,
                    "description": "",
                    "created_at": None,
                    "persisted": False,
                    "vm_count": 0,
                    "running_count": 0,
                    "hibernated_count": 0,
                    "stopped_count": 0,
                    "booting_count": 0,
                }
            entry = projects_map[pname]
            entry["vm_count"] += 1
            if status == "running":
                entry["running_count"] += 1
            elif status == "hibernated":
                entry["hibernated_count"] += 1
            elif status == "stopped":
                entry["stopped_count"] += 1
            else:
                entry["booting_count"] += 1

    return projects_map


@app.route('/api/projects', methods=['GET'])
def list_projects():
    if 'user_id' not in session:
        return jsonify({"error": "Unauthorized"}), 401
    conn = sqlite3.connect(get_db_path(), timeout=10)
    try:
        projects_map = _get_user_projects_map(conn, session['user_id'])
        ordered = sorted(
            projects_map.values(),
            key=lambda p: (0 if p["name"] == "default" else 1, p["name"].lower())
        )
        return jsonify(ordered), 200
    finally:
        conn.close()


@app.route('/api/projects', methods=['POST'])
def create_project():
    if 'user_id' not in session:
        return jsonify({"error": "Unauthorized"}), 401
    raw_data = request.get_json(silent=True)
    data = raw_data if isinstance(raw_data, dict) else {}
    name = (data.get('name') or '').strip() if isinstance(data.get('name'), str) else ''
    raw_desc = data.get('description', '')
    description = raw_desc.strip()[:200] if isinstance(raw_desc, str) else ''

    if not is_valid_project_name(name):
        return jsonify({
            "error": "Invalid project name. Use 1-64 characters: letters, numbers, spaces, '.', '_', or '-'."
        }), 400

    conn = sqlite3.connect(get_db_path(), timeout=10)
    try:
        c = conn.cursor()
        c.execute('''CREATE TABLE IF NOT EXISTS projects (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        owner_id INTEGER NOT NULL,
                        name TEXT NOT NULL,
                        description TEXT DEFAULT '',
                        created_at TEXT DEFAULT CURRENT_TIMESTAMP,
                        UNIQUE(owner_id, name)
                     )''')
        try:
            c.execute(
                "INSERT INTO projects (owner_id, name, description) VALUES (?, ?, ?)",
                (session['user_id'], name, description)
            )
            conn.commit()
        except sqlite3.IntegrityError:
            return jsonify({"error": f"Project '{name}' already exists"}), 409

        projects_map = _get_user_projects_map(conn, session['user_id'])
        return jsonify({"status": "created", "project": projects_map[name]}), 201
    finally:
        conn.close()


@app.route('/api/projects/<project_name>', methods=['GET'])
def get_project_detail(project_name):
    if 'user_id' not in session:
        return jsonify({"error": "Unauthorized"}), 401
    pname = project_name.strip()
    if not pname:
        return jsonify({"error": "Invalid project name"}), 400

    conn = sqlite3.connect(get_db_path(), timeout=10)
    try:
        projects_map = _get_user_projects_map(conn, session['user_id'])
        if pname not in projects_map:
            return jsonify({"error": "Project not found"}), 404
        return jsonify(projects_map[pname]), 200
    finally:
        conn.close()


@app.route('/api/projects/<project_name>', methods=['DELETE'])
def delete_project(project_name):
    if 'user_id' not in session:
        return jsonify({"error": "Unauthorized"}), 401
    pname = project_name.strip()
    if not pname:
        return jsonify({"error": "Invalid project name"}), 400

    conn = sqlite3.connect(get_db_path(), timeout=10)
    try:
        c = conn.cursor()
        if table_exists(conn, 'vms'):
            if pname == 'default':
                c.execute(
                    "SELECT COUNT(*) FROM vms WHERE owner_id = ? AND (TRIM(COALESCE(project_name, '')) = '' OR TRIM(project_name) = 'default')",
                    (session['user_id'],)
                )
            else:
                c.execute(
                    "SELECT COUNT(*) FROM vms WHERE owner_id = ? AND TRIM(COALESCE(project_name, '')) = ?",
                    (session['user_id'], pname)
                )
            if c.fetchone()[0] > 0:
                return jsonify({
                    "error": "Cannot delete a project with active MicroVMs. Delete its MicroVMs first."
                }), 400

        if not table_exists(conn, 'projects'):
            return jsonify({"error": "Project not found"}), 404

        c.execute(
            "DELETE FROM projects WHERE owner_id = ? AND name = ?",
            (session['user_id'], pname)
        )
        conn.commit()
        if c.rowcount == 0:
            return jsonify({"error": "Project not found"}), 404
        return jsonify({"status": "deleted", "name": pname}), 200
    finally:
        conn.close()


# --- UI ROUTES ---
@app.route('/')
def index():
    return redirect('/dashboard')

@app.route('/login')
@app.route('/login/')
def serve_login():
    return send_from_directory(app.static_folder, 'login.html')

@app.route('/dashboard')
@app.route('/dashboard/')
@app.route('/volumes')
@app.route('/volumes/')
@app.route('/networks')
@app.route('/networks/')
@app.route('/settings')
@app.route('/settings/')
def serve_protected_pages():
    if 'user_id' not in session:
        return redirect('/login')
    page = request.path.strip('/') + '.html'
    return send_from_directory(app.static_folder, page)

@app.route('/projects')
@app.route('/projects/')
def redirect_projects_index():
    if 'user_id' not in session:
        return redirect('/login')
    return redirect('/dashboard')

@app.route('/projects/<project_name>')
@app.route('/projects/<project_name>/')
def serve_project_page(project_name):
    if 'user_id' not in session:
        return redirect('/login')
    return send_from_directory(app.static_folder, 'project.html')

@app.route('/<path:filename>')
def serve_static(filename):
    if filename == 'index.html':
        if 'user_id' not in session:
            return redirect('/login')
        return redirect('/dashboard')
    if filename.endswith('.html'):
        if filename == 'login.html':
            return send_from_directory(app.static_folder, 'login.html')
        if 'user_id' not in session:
            return redirect('/login')
    return send_from_directory(app.static_folder, filename)

@app.route('/api/<path:path>', methods=['GET', 'POST', 'PUT', 'DELETE', 'PATCH', 'OPTIONS'])
def proxy_api(path):
    if request.method == 'OPTIONS':
        return Response('', status=200, headers={
            'Access-Control-Allow-Origin': '*',
            'Access-Control-Allow-Methods': 'GET, POST, PUT, DELETE, PATCH, OPTIONS',
            'Access-Control-Allow-Headers': 'Content-Type, X-User-ID, X-Role'
        })

    if 'user_id' not in session:
        return jsonify({"error": "Unauthorized API access"}), 401

    raw_body = request.get_data()
    if request.method == 'POST' and path.rstrip('/') == 'vms' and raw_body:
        payload = request.get_json(force=True, silent=True)
        if isinstance(payload, dict) and 'project' in payload:
            raw_proj = payload['project']
            if raw_proj is None:
                payload['project'] = 'default'
                raw_body = json.dumps(payload).encode('utf-8')
            else:
                if not isinstance(raw_proj, str):
                    return jsonify({"error": "Invalid project name."}), 400
                stripped_proj = raw_proj.strip()
                if stripped_proj and not is_valid_project_name(stripped_proj):
                    return jsonify({
                        "error": "Invalid project name. Use 1-64 characters: letters, numbers, spaces, '.', '_', or '-'."
                    }), 400
                payload['project'] = stripped_proj or 'default'
                raw_body = json.dumps(payload).encode('utf-8')
        
    url = f"{BACKEND_URL}/api/{path}"
    if request.query_string:
        url = f"{url}?{request.query_string.decode('utf-8', errors='replace')}"

    excluded_headers = ['content-encoding', 'content-length', 'transfer-encoding', 'connection', 'host', 'x-user-id', 'x-role']
    headers = {name: value for (name, value) in request.headers if name.lower() not in excluded_headers}
    
    # INJECT TRUSTED HEADERS
    headers['X-User-ID'] = str(session['user_id'])
    headers['X-Role'] = session['role']
    if INTERNAL_SECRET:
        headers['X-Internal-Secret'] = INTERNAL_SECRET

    try:
        resp = requests.request(
            method=request.method,
            url=url,
            headers=headers,
            data=raw_body,
            allow_redirects=False,
            timeout=15
        )
        return Response(
            resp.content,
            status=resp.status_code,
            headers=[(name, value) for (name, value) in resp.headers.items() if name.lower() not in excluded_headers]
        )
    except requests.exceptions.RequestException as e:
        return jsonify({"error": "C++ backend unavailable", "details": str(e)}), 502

if __name__ == '__main__':
    port = int(os.environ.get('CRYOSPAWN_FRONTEND_PORT', 8080))
    print(f"[*] CryoSpawn Flask Dashboard running on http://0.0.0.0:{port}")
    print(f"[*] Proxying API calls to C++ Daemon at {BACKEND_URL}")
    app.run(host='0.0.0.0', port=port, threaded=True)
