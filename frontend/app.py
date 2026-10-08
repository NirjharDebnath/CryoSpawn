from flask import Flask, send_from_directory, request, Response, session, redirect, jsonify
from werkzeug.security import generate_password_hash, check_password_hash
import sqlite3
import requests
import os

app = Flask(__name__, static_folder='../internal/ui')
app.secret_key = 'super-secret-cryo-key-replace-in-prod' # Needed for secure sessions
BACKEND_URL = 'http://127.0.0.1:9090'
DB_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), '../cryospawn.db'))

def init_db():
    conn = sqlite3.connect(DB_PATH)
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

init_db()



# --- AUTH ROUTES ---
@app.route('/api/auth/login', methods=['POST'])
def login():
    data = request.json
    username = data.get('username')
    password = data.get('password')
    
    conn = sqlite3.connect(DB_PATH)
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
    # Because we are still using a SPA-like structure for now, or splitting HTMLs,
    # we return the specific HTML file based on the path.
    page = request.path.strip('/') + '.html'
    return send_from_directory(app.static_folder, page)

@app.route('/<path:filename>')
def serve_static(filename):
    return send_from_directory(app.static_folder, filename)

@app.route('/api/<path:path>', methods=['GET', 'POST', 'PUT', 'DELETE', 'PATCH', 'OPTIONS'])
def proxy_api(path):
    if 'user_id' not in session:
        return jsonify({"error": "Unauthorized API access"}), 401
        
    url = f"{BACKEND_URL}/api/{path}"
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
        return {"error": "C++ backend unavailable", "details": str(e)}, 502

if __name__ == '__main__':
    print("[*] CryoSpawn Flask Dashboard running on http://0.0.0.0:8080")
    print("[*] Proxying API calls to C++ Daemon on 127.0.0.1:9090")
    app.run(host='0.0.0.0', port=8080, threaded=True)
