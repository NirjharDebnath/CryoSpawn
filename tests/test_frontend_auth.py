import unittest
import os
import tempfile
import sqlite3

# Set test environment
test_db = tempfile.mktemp(suffix=".db")
os.environ["CRYOSPAWN_DB_PATH"] = test_db
os.environ["CRYOSPAWN_SECRET_KEY"] = "test-secret-key-12345"

from frontend.app import app, init_db

class FrontendAuthTestCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        init_db()
        cls.client = app.test_client()

    @classmethod
    def tearDownClass(cls):
        for f in [test_db, test_db + "-wal", test_db + "-shm"]:
            if os.path.exists(f):
                os.remove(f)

    def test_01_default_admin_created(self):
        conn = sqlite3.connect(test_db)
        c = conn.cursor()
        c.execute("SELECT username, role FROM users WHERE username='admin'")
        row = c.fetchone()
        conn.close()
        self.assertIsNotNone(row)
        self.assertEqual(row[1], 'admin')

    def test_02_login_admin(self):
        res = self.client.post('/api/auth/login', json={'username': 'admin', 'password': 'admin123'})
        self.assertEqual(res.status_code, 200)
        self.assertEqual(res.json.get('role'), 'admin')

    def test_03_login_wrong_password(self):
        res = self.client.post('/api/auth/login', json={'username': 'admin', 'password': 'wrongpassword'})
        self.assertEqual(res.status_code, 401)

    def test_04_register_new_user(self):
        res = self.client.post('/api/auth/register', json={'username': 'alice', 'password': 'alicepassword'})
        self.assertEqual(res.status_code, 201)
        self.assertEqual(res.json.get('role'), 'user')

        # Duplicate register should fail
        res2 = self.client.post('/api/auth/register', json={'username': 'alice', 'password': 'alicepassword'})
        self.assertEqual(res2.status_code, 400)

    def test_05_unauthenticated_protected_pages(self):
        client = app.test_client() # fresh unauthenticated client
        for path in ['/dashboard', '/volumes', '/networks', '/settings', '/dashboard.html', '/volumes.html', '/networks.html', '/settings.html']:
            res = client.get(path, follow_redirects=False)
            self.assertIn(res.status_code, [302, 401], f"Path {path} should be protected")
            if res.status_code == 302:
                self.assertIn('/login', res.headers.get('Location', ''))

    def test_06_unauthenticated_api_access(self):
        client = app.test_client()
        res = client.get('/api/vms')
        self.assertEqual(res.status_code, 401)

    def test_07_change_password(self):
        client = app.test_client()
        # Login alice
        login_res = client.post('/api/auth/login', json={'username': 'alice', 'password': 'alicepassword'})
        self.assertEqual(login_res.status_code, 200)

        # Wrong current password
        res = client.post('/api/auth/change-password', json={'current_password': 'wrong', 'new_password': 'newpassword123'})
        self.assertEqual(res.status_code, 400)

        # Successful change
        res = client.post('/api/auth/change-password', json={'current_password': 'alicepassword', 'new_password': 'newpassword123'})
        self.assertEqual(res.status_code, 200)

        # Verify new password works
        client2 = app.test_client()
        res = client2.post('/api/auth/login', json={'username': 'alice', 'password': 'newpassword123'})
        self.assertEqual(res.status_code, 200)

if __name__ == '__main__':
    unittest.main()
