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

    def test_08_authenticated_session_me(self):
        client = app.test_client()
        client.post('/api/auth/login', json={'username': 'alice', 'password': 'newpassword123'})
        res = client.get('/api/auth/me')
        self.assertEqual(res.status_code, 200)
        self.assertEqual(res.json.get('username'), 'alice')
        self.assertEqual(res.json.get('role'), 'user')
        self.assertIsInstance(res.json.get('user_id'), int)

    def test_09_proxy_header_injection_and_anti_spoofing(self):
        from unittest.mock import patch, MagicMock

        client = app.test_client()
        # Login alice (user role)
        client.post('/api/auth/login', json={'username': 'alice', 'password': 'newpassword123'})
        me_res = client.get('/api/auth/me')
        alice_id = me_res.json['user_id']

        # Mock requests.request to intercept outgoing call to C++ backend
        with patch('frontend.app.requests.request') as mock_req:
            mock_resp = MagicMock()
            mock_resp.status_code = 200
            mock_resp.content = b'[]'
            mock_resp.headers = {}
            mock_req.return_value = mock_resp

            # User attempts to spoof admin role and different user id
            client.get('/api/vms', headers={'X-User-ID': '999', 'X-Role': 'admin'})

            self.assertTrue(mock_req.called)
            called_headers = mock_req.call_args[1]['headers']

            # Must have alice's trusted credentials, NOT spoofed headers
            self.assertEqual(called_headers['X-User-ID'], str(alice_id))
            self.assertEqual(called_headers['X-Role'], 'user')

    def test_10_proxy_multitenant_routing_and_volumes_methods(self):
        from unittest.mock import patch, MagicMock

        client = app.test_client()
        client.post('/api/auth/login', json={'username': 'alice', 'password': 'newpassword123'})
        me_res = client.get('/api/auth/me')
        alice_id = str(me_res.json['user_id'])

        with patch('frontend.app.requests.request') as mock_req:
            mock_resp = MagicMock()
            mock_resp.status_code = 200
            mock_resp.content = b'{"status":"ok"}'
            mock_resp.headers = {}
            mock_req.return_value = mock_resp

            # POST /api/links
            client.post('/api/links', json={'vm1_id': 'vm-1', 'vm2_id': 'vm-2'}, headers={'X-User-ID': '1', 'X-Role': 'admin'})
            self.assertEqual(mock_req.call_args[1]['headers']['X-User-ID'], alice_id)
            self.assertEqual(mock_req.call_args[1]['headers']['X-Role'], 'user')

            # PATCH /api/vms/vm-1/routing
            client.patch('/api/vms/vm-1/routing', json={'allow_interproject_routing': True}, headers={'X-User-ID': '1', 'X-Role': 'admin'})
            self.assertEqual(mock_req.call_args[1]['headers']['X-User-ID'], alice_id)
            self.assertEqual(mock_req.call_args[1]['headers']['X-Role'], 'user')

            # DELETE /api/vms/vm-1
            client.delete('/api/vms/vm-1', headers={'X-User-ID': '1', 'X-Role': 'admin'})
            self.assertEqual(mock_req.call_args[1]['headers']['X-User-ID'], alice_id)
            self.assertEqual(mock_req.call_args[1]['headers']['X-Role'], 'user')

            # GET /api/volumes
            client.get('/api/volumes', headers={'X-User-ID': '1', 'X-Role': 'admin'})
            self.assertEqual(mock_req.call_args[1]['headers']['X-User-ID'], alice_id)
            self.assertEqual(mock_req.call_args[1]['headers']['X-Role'], 'user')

if __name__ == '__main__':
    unittest.main()
