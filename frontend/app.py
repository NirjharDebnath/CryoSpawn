from flask import Flask, send_from_directory, request, Response, session, redirect, jsonify
from werkzeug.security import generate_password_hash, check_password_hash
import sqlite3
import requests
import os

app = Flask(__name__, static_folder='../internal/ui')
app.secret_key = os.environ.get('CRYOSPAWN_SECRET_KEY', 'super-secret-cryo-key-replace-in-prod')
BACKEND_PORT = os.environ.get('CRYOSPAWN_BACKEND_PORT', '9090')
BACKEND_URL = os.environ.get('CRYOSPAWN_BACKEND_URL', f'http://127.0.0.1:{BACKEND_PORT}')
DB_PATH = os.environ.get('CRYOSPAWN_DB_PATH', os.path.abspath(os.path.join(os.path.dirname(__file__), '../cryospawn.db')))

def init_db():
    conn = sqlite3.connect(DB_PATH, timeout=10)
    c = conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS users (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    username TEXT UNIQUE NOT NULL,
                    password_hash TEXT NOT NULL,
                    role TEXT DEFAULT 'user'
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
        p = DB_PATH + ext
        if os.path.exists(p):
            try:
                os.chmod(p, 0o666)
            except Exception:
                pass

init_db()

# --- AUTH ROUTES ---
@app.route('/api/auth/login', methods=['POST'])
def login():
    data = request.json or {}
    username = data.get('username')
    password = data.get('password')
    
    conn = sqlite3.connect(DB_PATH, timeout=10)
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
    data = request.json or {}
    username = data.get('username', '').strip()
    password = data.get('password', '')
    if not username or not password:
        return jsonify({"error": "Username and password are required"}), 400
    if len(password) < 6:
        return jsonify({"error": "Password must be at least 6 characters"}), 400

    conn = sqlite3.connect(DB_PATH, timeout=10)
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
    data = request.json or {}
    curr_pw = data.get('current_password', '')
    new_pw = data.get('new_password', '')
    if not curr_pw or not new_pw:
        return jsonify({"error": "Current and new password are required"}), 400
    if len(new_pw) < 6:
        return jsonify({"error": "New password must be at least 6 characters"}), 400

    conn = sqlite3.connect(DB_PATH, timeout=10)
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

# --- UI ROUTES ---
@app.route('/')
def index():
    return redirect('/dashboard')

@app.route('/login')
def serve_login():
    return send_from_directory(app.static_folder, 'login.html')

@app.route('/dashboard')
@app.route('/volumes')
@app.route('/networks')
@app.route('/settings')
def serve_protected_pages():
    if 'user_id' not in session:
        return redirect('/login')
    page = request.path.strip('/') + '.html'
    return send_from_directory(app.static_folder, page)

@app.route('/<path:filename>')
def serve_static(filename):
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
        
    url = f"{BACKEND_URL}/api/{path}"
    if request.query_string:
        url = f"{url}?{request.query_string.decode('utf-8')}"

    excluded_headers = ['content-encoding', 'content-length', 'transfer-encoding', 'connection', 'host', 'x-user-id', 'x-role']
    headers = {name: value for (name, value) in request.headers if name.lower() not in excluded_headers}
    
    # INJECT TRUSTED HEADERS
    headers['X-User-ID'] = str(session['user_id'])
    headers['X-Role'] = session['role']

    try:
        resp = requests.request(
            method=request.method,
            url=url,
            headers=headers,
            data=request.get_data(),
            cookies=request.cookies,
            allow_redirects=False,
            timeout=60
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
