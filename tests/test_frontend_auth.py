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

    def test_11_unauthenticated_project_routes_and_apis(self):
        client = app.test_client()
        for path in ['/projects', '/projects/', '/projects/alpha', '/projects/default/', '/project.html']:
            res = client.get(path, follow_redirects=False)
            self.assertEqual(res.status_code, 302, f"Unauthenticated {path} should redirect to login")
            self.assertIn('/login', res.headers.get('Location', ''))

        self.assertEqual(client.get('/api/projects').status_code, 401)
        self.assertEqual(client.post('/api/projects', json={'name': 'alpha'}).status_code, 401)
        self.assertEqual(client.get('/api/projects/alpha').status_code, 401)
        self.assertEqual(client.delete('/api/projects/alpha').status_code, 401)

    def test_12_empty_project_persistence_and_validation(self):
        client = app.test_client()
        client.post('/api/auth/login', json={'username': 'alice', 'password': 'newpassword123'})

        # Invalid project names rejected
        for bad_name in ['', '   ', '../escape', 'bad/name', 'bad\\name', 'a' * 65]:
            bad_res = client.post('/api/projects', json={'name': bad_name})
            self.assertEqual(bad_res.status_code, 400, f"Expected 400 for invalid name {bad_name!r}")

        # Create explicit empty project
        res = client.post('/api/projects', json={'name': 'nordic-alpha', 'description': 'Fjord cluster'})
        self.assertEqual(res.status_code, 201)
        proj = res.json['project']
        self.assertEqual(proj['name'], 'nordic-alpha')
        self.assertEqual(proj['description'], 'Fjord cluster')
        self.assertEqual(proj['vm_count'], 0)
        self.assertTrue(proj['persisted'])

        # Duplicate creation for same user fails with 409
        dup_res = client.post('/api/projects', json={'name': 'nordic-alpha'})
        self.assertEqual(dup_res.status_code, 409)

        # Persists in GET /api/projects and GET /api/projects/<name>
        list_res = client.get('/api/projects')
        self.assertEqual(list_res.status_code, 200)
        names = [p['name'] for p in list_res.json]
        self.assertIn('nordic-alpha', names)

        detail_res = client.get('/api/projects/nordic-alpha')
        self.assertEqual(detail_res.status_code, 200)
        self.assertEqual(detail_res.json['vm_count'], 0)

    def test_13_project_multitenant_isolation_and_dynamic_discovery(self):
        # Register second user bob
        bob_client = app.test_client()
        reg_res = bob_client.post('/api/auth/register', json={'username': 'bob', 'password': 'bobpassword123'})
        self.assertEqual(reg_res.status_code, 201)
        bob_id = bob_client.get('/api/auth/me').json['user_id']

        alice_client = app.test_client()
        alice_client.post('/api/auth/login', json={'username': 'alice', 'password': 'newpassword123'})
        alice_id = alice_client.get('/api/auth/me').json['user_id']

        # Bob can independently create a project with the same name 'nordic-alpha' and a private 'bob-vault'
        self.assertEqual(bob_client.post('/api/projects', json={'name': 'nordic-alpha'}).status_code, 201)
        self.assertEqual(bob_client.post('/api/projects', json={'name': 'bob-vault'}).status_code, 201)

        # Alice creates 'alice-vault'
        self.assertEqual(alice_client.post('/api/projects', json={'name': 'alice-vault'}).status_code, 201)

        # Verify strict isolation even with spoofed headers
        alice_projs = [p['name'] for p in alice_client.get('/api/projects', headers={'X-User-ID': str(bob_id), 'X-Role': 'admin'}).json]
        bob_projs = [p['name'] for p in bob_client.get('/api/projects', headers={'X-User-ID': str(alice_id), 'X-Role': 'admin'}).json]

        self.assertIn('alice-vault', alice_projs)
        self.assertNotIn('bob-vault', alice_projs)
        self.assertIn('bob-vault', bob_projs)
        self.assertNotIn('alice-vault', bob_projs)

        # Bob cannot view or delete Alice's project
        self.assertEqual(bob_client.get('/api/projects/alice-vault').status_code, 404)
        self.assertEqual(bob_client.delete('/api/projects/alice-vault').status_code, 404)

        # Verify Alice's project still exists
        self.assertEqual(alice_client.get('/api/projects/alice-vault').status_code, 200)

        # Dynamic discovery from vms table (strictly scoped by owner_id)
        conn = sqlite3.connect(test_db)
        c = conn.cursor()
        c.execute('''CREATE TABLE IF NOT EXISTS vms (
                        slot_id INTEGER PRIMARY KEY,
                        owner_id INTEGER DEFAULT 1,
                        pid INTEGER,
                        status TEXT NOT NULL,
                        vcpus INTEGER NOT NULL,
                        mem_mib INTEGER NOT NULL,
                        guest_ip TEXT NOT NULL,
                        host_ip TEXT NOT NULL,
                        tap_name TEXT NOT NULL,
                        ssh_exposed INTEGER NOT NULL,
                        host_port INTEGER,
                        project_name TEXT NOT NULL
                     )''')
        c.execute(
            "INSERT INTO vms (slot_id, owner_id, pid, status, vcpus, mem_mib, guest_ip, host_ip, tap_name, ssh_exposed, host_port, project_name) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (0, alice_id, 1001, 'running', 1, 512, '172.16.0.2', '172.16.0.1', 'cryo-a0', 0, None, 'alice-discovered')
        )
        c.execute(
            "INSERT INTO vms (slot_id, owner_id, pid, status, vcpus, mem_mib, guest_ip, host_ip, tap_name, ssh_exposed, host_port, project_name) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (1, bob_id, 1002, 'hibernated', 1, 512, '172.16.0.6', '172.16.0.5', 'cryo-b1', 0, None, 'bob-discovered')
        )
        conn.commit()
        conn.close()

        alice_list = alice_client.get('/api/projects').json
        alice_map = {p['name']: p for p in alice_list}
        self.assertIn('alice-discovered', alice_map)
        self.assertEqual(alice_map['alice-discovered']['vm_count'], 1)
        self.assertEqual(alice_map['alice-discovered']['running_count'], 1)
        self.assertNotIn('bob-discovered', alice_map)

        # Cannot delete a project with active MicroVMs
        del_busy = alice_client.delete('/api/projects/alice-discovered')
        self.assertEqual(del_busy.status_code, 400)

        # Can delete an empty project
        del_empty = alice_client.delete('/api/projects/alice-vault')
        self.assertEqual(del_empty.status_code, 200)
        self.assertEqual(alice_client.get('/api/projects/alice-vault').status_code, 404)

    def test_14_authenticated_ui_pages_and_assets(self):
        client = app.test_client()
        client.post('/api/auth/login', json={'username': 'alice', 'password': 'newpassword123'})

        for route in ['/dashboard', '/projects/nordic-alpha', '/volumes', '/networks', '/settings']:
            with client.get(route) as res:
                self.assertEqual(res.status_code, 200, f"Authenticated route {route} should return 200")
                html = res.get_data(as_text=True)
                self.assertIn('alpine-daylight', html)
                self.assertIn('alpine-weather-layer', html)
                self.assertIn('href="/style.css"', html)
                self.assertIn('src="/app.js"', html)

        # Static assets resolve cleanly
        with client.get('/style.css') as css_res:
            self.assertEqual(css_res.status_code, 200)
        with client.get('/app.js') as js_res:
            self.assertEqual(js_res.status_code, 200)

    def test_15_edge_cases_malformed_json_index_redirect_and_spawn_project_validation(self):
        from unittest.mock import patch, MagicMock
        unauth = app.test_client()
        res_unauth_idx = unauth.get('/index.html', follow_redirects=False)
        self.assertEqual(res_unauth_idx.status_code, 302)
        self.assertIn('/login', res_unauth_idx.headers.get('Location', ''))

        # Non-dict JSON payloads must never raise 500
        self.assertEqual(unauth.post('/api/auth/login', json=['bad']).status_code, 401)
        self.assertEqual(unauth.post('/api/auth/register', json=['bad']).status_code, 400)

        client = app.test_client()
        client.post('/api/auth/login', json={'username': 'alice', 'password': 'newpassword123'})
        alice_id = client.get('/api/auth/me').json['user_id']

        res_auth_idx = client.get('/index.html', follow_redirects=False)
        self.assertEqual(res_auth_idx.status_code, 302)
        self.assertIn('/dashboard', res_auth_idx.headers.get('Location', ''))

        self.assertEqual(client.post('/api/projects', json=['not', 'a', 'dict']).status_code, 400)
        self.assertEqual(client.post('/api/auth/change-password', json=['bad']).status_code, 400)

        # Space-containing project name URL encoding & detail check
        self.assertEqual(client.post('/api/projects', json={'name': 'nordic alpha'}).status_code, 201)
        self.assertEqual(client.get('/api/projects/nordic%20alpha').status_code, 200)
        with client.get('/projects/nordic%20alpha') as page_res:
            self.assertEqual(page_res.status_code, 200)

        # POST /api/vms rejects invalid project names before proxying
        with patch('frontend.app.requests.request') as mock_req:
            bad_spawn = client.post('/api/vms', json={'name': 'vm1', 'project': '../escape'})
            self.assertEqual(bad_spawn.status_code, 400)
            self.assertFalse(mock_req.called)

            mock_resp = MagicMock()
            mock_resp.status_code = 200
            mock_resp.content = b'{"status":"booting"}'
            mock_resp.headers = {}
            mock_req.return_value = mock_resp
            ok_spawn = client.post('/api/vms', json={'name': 'vm1', 'project': '  nordic alpha  '})
            self.assertEqual(ok_spawn.status_code, 200)
            self.assertTrue(mock_req.called)
            import json as _json
            forwarded = _json.loads(mock_req.call_args[1]['data'].decode('utf-8'))
            self.assertEqual(forwarded['project'], 'nordic alpha')

        # Persisted 'default' cannot be deleted if a VM has whitespace project_name mapped to 'default'
        self.assertEqual(client.post('/api/projects', json={'name': 'default'}).status_code, 201)
        conn = sqlite3.connect(test_db)
        c = conn.cursor()
        c.execute(
            "INSERT INTO vms (slot_id, owner_id, pid, status, vcpus, mem_mib, guest_ip, host_ip, tap_name, ssh_exposed, host_port, project_name) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (2, alice_id, 1003, 'stopped', 1, 512, '172.16.0.10', '172.16.0.9', 'cryo-a2', 0, None, '   ')
        )
        conn.commit()
        conn.close()
        self.assertEqual(client.delete('/api/projects/default').status_code, 400)

    def test_16_null_project_normalization_and_template_banners(self):
        from unittest.mock import patch, MagicMock
        import json as _json

        client = app.test_client()
        client.post('/api/auth/login', json={'username': 'alice', 'password': 'newpassword123'})

        with patch('frontend.app.requests.request') as mock_req:
            mock_resp = MagicMock()
            mock_resp.status_code = 200
            mock_resp.content = b'{"status":"booting"}'
            mock_resp.headers = {}
            mock_req.return_value = mock_resp

            # JSON null project normalizes to 'default' instead of crashing C++ nlohmann::json
            null_spawn = client.post('/api/vms', json={'name': 'vm-null', 'project': None})
            self.assertEqual(null_spawn.status_code, 200)
            forwarded = _json.loads(mock_req.call_args[1]['data'].decode('utf-8'))
            self.assertEqual(forwarded['project'], 'default')

            # Trailing slash /api/vms/ also validates project
            bad_trailing = client.post('/api/vms/', json={'name': 'vm-bad', 'project': '../bad'})
            self.assertEqual(bad_trailing.status_code, 400)

        # Volumes and Networks templates include daemon-status-banner; project.html starts workspace hidden
        with client.get('/volumes') as r_vol:
            vol_html = r_vol.get_data(as_text=True)
        with client.get('/networks') as r_net:
            net_html = r_net.get_data(as_text=True)
        with client.get('/projects/nordic-alpha') as r_proj:
            proj_html = r_proj.get_data(as_text=True)
        self.assertIn('id="daemon-status-banner"', vol_html)
        self.assertIn('id="daemon-status-banner"', net_html)
        self.assertIn('id="project-workspace" class="hidden', proj_html)

    def test_17_text_plain_post_vms_validation_and_trailing_slashes(self):
        from unittest.mock import patch, MagicMock
        import json as _json

        unauth = app.test_client()
        for path in ['/dashboard/', '/volumes/', '/networks/', '/settings/']:
            res = unauth.get(path, follow_redirects=False)
            self.assertEqual(res.status_code, 302, f"Unauthenticated {path} should redirect to login")

        client = app.test_client()
        client.post('/api/auth/login', json={'username': 'alice', 'password': 'newpassword123'})

        for path in ['/dashboard/', '/volumes/', '/networks/', '/settings/']:
            with client.get(path) as res:
                self.assertEqual(res.status_code, 200, f"Authenticated {path} should return 200")

        with patch('frontend.app.requests.request') as mock_req:
            bad_plain = client.post(
                '/api/vms',
                data='{"name":"vm1","project":"../escape"}',
                content_type='text/plain'
            )
            self.assertEqual(bad_plain.status_code, 400)
            self.assertFalse(mock_req.called)

            mock_resp = MagicMock()
            mock_resp.status_code = 200
            mock_resp.content = b'{"status":"booting"}'
            mock_resp.headers = {}
            mock_req.return_value = mock_resp

            ok_plain = client.post(
                '/api/vms',
                data='{"name":"vm1","project":"  nordic alpha  "}',
                content_type='text/plain'
            )
            self.assertEqual(ok_plain.status_code, 200)
            forwarded = _json.loads(mock_req.call_args[1]['data'].decode('utf-8'))
            self.assertEqual(forwarded['project'], 'nordic alpha')

    def test_18_login_trailing_slash_and_prototype_project_names(self):
        client = app.test_client()
        with client.get('/login') as r1:
            self.assertEqual(r1.status_code, 200)
        with client.get('/login/') as r2:
            self.assertEqual(r2.status_code, 200)

        client.post('/api/auth/login', json={'username': 'alice', 'password': 'newpassword123'})
        res = client.post('/api/projects', json={'name': 'constructor', 'description': 'Reserved JS key test'})
        self.assertEqual(res.status_code, 201)
        self.assertEqual(client.get('/api/projects/constructor').status_code, 200)
        with client.get('/projects/constructor') as r3:
            self.assertEqual(r3.status_code, 200)
        self.assertEqual(client.delete('/api/projects/constructor').status_code, 200)

    def test_19_console_auth_uuid_validation_and_ws_accept(self):
        from unittest.mock import patch, MagicMock
        from frontend.ws_console import build_ws_accept

        # RFC 6455 test vector
        self.assertEqual(build_ws_accept("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")

        anon = app.test_client()
        self.assertEqual(anon.get('/console/vm-da05a06c', follow_redirects=False).status_code, 302)
        self.assertEqual(anon.get('/ws/console/vm-da05a06c').status_code, 401)

        client = app.test_client()
        client.post('/api/auth/login', json={'username': 'alice', 'password': 'newpassword123'})

        # Malicious / invalid VM UUIDs blocked
        self.assertEqual(client.get('/console/vm-bad;id').status_code, 400)
        self.assertEqual(client.get('/ws/console/vm-bad;id').status_code, 400)

        with patch('frontend.app.requests.request') as mock_req:
            # Cross-tenant 403 from C++ backend
            r403 = MagicMock()
            r403.status_code = 403
            r403.json.return_value = {'error': 'Forbidden'}
            mock_req.return_value = r403
            self.assertEqual(client.get('/ws/console/vm-da05a06c').status_code, 403)

            # SSH disabled -> 400
            r_no_ssh = MagicMock()
            r_no_ssh.status_code = 200
            r_no_ssh.json.return_value = {'uuid': 'vm-da05a06c', 'ssh_exposed': False}
            mock_req.return_value = r_no_ssh
            self.assertEqual(client.get('/ws/console/vm-da05a06c').status_code, 400)

            # SSH enabled but non-WebSocket HTTP request -> 426 Upgrade Required
            r_ok = MagicMock()
            r_ok.status_code = 200
            r_ok.json.return_value = {'uuid': 'vm-da05a06c', 'ssh_exposed': True}
            mock_req.return_value = r_ok
            self.assertEqual(client.get('/ws/console/vm-da05a06c').status_code, 426)

        with client.get('/console/vm-da05a06c') as page:
            self.assertEqual(page.status_code, 200)

        # /api/config requires auth and reflects CRYOSPAWN_PUBLIC_DOMAIN
        self.assertEqual(anon.get('/api/config').status_code, 401)
        os.environ['CRYOSPAWN_PUBLIC_DOMAIN'] = 'zibai.space'
        try:
            cfg = client.get('/api/config')
            self.assertEqual(cfg.status_code, 200)
            self.assertEqual(cfg.json.get('public_domain'), 'zibai.space')
        finally:
            os.environ.pop('CRYOSPAWN_PUBLIC_DOMAIN', None)

if __name__ == '__main__':
    unittest.main()
