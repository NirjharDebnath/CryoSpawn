import os
import sqlite3
import tempfile
import unittest
from unittest.mock import MagicMock, patch

test_db = tempfile.mktemp(suffix=".db")
test_rootfs = tempfile.mktemp(suffix=".ext4")
os.environ["CRYOSPAWN_DB_PATH"] = test_db
os.environ["CRYOSPAWN_ROOTFS_PATH"] = test_rootfs
os.environ["CRYOSPAWN_ADMIN_USER"] = "hostadmin"
os.environ["CRYOSPAWN_ADMIN_PASSWORD"] = "supersecret-host-pw"
os.environ["CRYOSPAWN_ADMIN_SECRET_KEY"] = "admin-test-secret"

from werkzeug.security import generate_password_hash
import admin.app as admin_app


class AdminPanelTestCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._prev_db = os.environ.get("CRYOSPAWN_DB_PATH")
        cls._prev_rf = os.environ.get("CRYOSPAWN_ROOTFS_PATH")
        os.environ["CRYOSPAWN_DB_PATH"] = test_db
        os.environ["CRYOSPAWN_ROOTFS_PATH"] = test_rootfs
        with open(test_rootfs, "wb") as f:
            f.write(b"GOLDEN_ROOTFS_DATA")
        conn = sqlite3.connect(test_db)
        c = conn.cursor()
        c.execute("CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT UNIQUE NOT NULL, password_hash TEXT NOT NULL, role TEXT DEFAULT 'user')")
        c.execute("INSERT INTO users (username, password_hash, role) VALUES (?, ?, 'user')", ("regular", generate_password_hash("userpass123")))
        c.execute("INSERT INTO users (username, password_hash, role) VALUES (?, ?, 'admin')", ("dbadmin", generate_password_hash("dbadminpass")))
        conn.commit()
        conn.close()

    @classmethod
    def tearDownClass(cls):
        if cls._prev_db is not None:
            os.environ["CRYOSPAWN_DB_PATH"] = cls._prev_db
        if cls._prev_rf is not None:
            os.environ["CRYOSPAWN_ROOTFS_PATH"] = cls._prev_rf
        for p in [test_db, test_db + "-wal", test_db + "-shm", test_rootfs, test_rootfs + ".staging.ext4", test_rootfs + ".bak"]:
            if os.path.exists(p):
                os.remove(p)

    def test_01_unauthenticated_blocked(self):
        client = admin_app.app.test_client()
        for page in ["/", "/vms", "/rootfs", "/database"]:
            self.assertEqual(client.get(page, follow_redirects=False).status_code, 302)
        for ep in ["/api/admin/me", "/api/admin/telemetry", "/api/admin/daemon/status", "/api/admin/sqlite/tables", "/api/admin/rootfs/info", "/api/admin/rootfs/list", "/ws/admin/console/vm-1234"]:
            self.assertEqual(client.get(ep).status_code, 401)

    def test_02_non_admin_user_rejected_and_admin_accepted(self):
        client = admin_app.app.test_client()
        r_user = client.post("/api/admin/login", json={"username": "regular", "password": "userpass123"})
        self.assertEqual(r_user.status_code, 401)

        r_bad = client.post("/api/admin/login", json={"username": "hostadmin", "password": "wrong"})
        self.assertEqual(r_bad.status_code, 401)

        r_ok = client.post("/api/admin/login", json={"username": "hostadmin", "password": "supersecret-host-pw"})
        self.assertEqual(r_ok.status_code, 200)
        for page in ["/", "/vms", "/rootfs", "/database"]:
            with client.get(page) as p:
                self.assertEqual(p.status_code, 200)

    def test_03_sqlite_inspector_redacts_hashes_and_manages_users(self):
        client = admin_app.app.test_client()
        client.post("/api/admin/login", json={"username": "dbadmin", "password": "dbadminpass"})

        res = client.get("/api/admin/sqlite/tables")
        self.assertEqual(res.status_code, 200)
        users = res.json["tables"]["users"]
        self.assertTrue(all(u["password_hash"] == "***redacted***" for u in users))

        c_res = client.post("/api/admin/users", json={"username": "friend1", "password": "friendpass123", "role": "user"})
        self.assertEqual(c_res.status_code, 201)

        # Custom SQL SELECT, UPDATE, and syntax error handling
        q_sel = client.post("/api/admin/sqlite/query", json={"sql": "SELECT username, role FROM users WHERE username = 'friend1';"})
        self.assertEqual(q_sel.status_code, 200)
        self.assertEqual(q_sel.json["mode"], "select")
        self.assertEqual(q_sel.json["rows"], [["friend1", "user"]])

        q_upd = client.post("/api/admin/sqlite/query", json={"sql": "UPDATE users SET role = 'admin' WHERE username = 'friend1';"})
        self.assertEqual(q_upd.status_code, 200)
        self.assertEqual(q_upd.json["rows_affected"], 1)

        q_err = client.post("/api/admin/sqlite/query", json={"sql": "SELECT * FROM nonexistent_table;"})
        self.assertEqual(q_err.status_code, 400)

    def test_04_daemon_proxy_injects_admin_and_all_true(self):
        client = admin_app.app.test_client()
        client.post("/api/admin/login", json={"username": "hostadmin", "password": "supersecret-host-pw"})

        with patch("admin.daemon_routes.requests.request") as mock_req:
            mock_resp = MagicMock()
            mock_resp.status_code = 200
            mock_resp.content = b"[]"
            mock_resp.headers = {}
            mock_req.return_value = mock_resp

            res = client.get("/api/admin/daemon-api/vms")
            self.assertEqual(res.status_code, 200)
            self.assertIn("all=true", mock_req.call_args[1]["url"])
            self.assertEqual(mock_req.call_args[1]["headers"]["X-Role"], "admin")

    def test_05_rootfs_stage_promote_rollback(self):
        client = admin_app.app.test_client()
        client.post("/api/admin/login", json={"username": "hostadmin", "password": "supersecret-host-pw"})

        self.assertEqual(client.post("/api/admin/rootfs/stage").status_code, 200)
        with open(test_rootfs + ".staging.ext4", "wb") as f:
            f.write(b"UPDATED_STAGING")
        self.assertEqual(client.post("/api/admin/rootfs/promote").status_code, 200)
        with open(test_rootfs, "rb") as f:
            self.assertEqual(f.read(), b"UPDATED_STAGING")
        self.assertEqual(client.post("/api/admin/rootfs/rollback").status_code, 200)
        with open(test_rootfs, "rb") as f:
            self.assertEqual(f.read(), b"GOLDEN_ROOTFS_DATA")

    def test_06_multi_rootfs_list_clone_select_delete(self):
        client = admin_app.app.test_client()
        client.post("/api/admin/login", json={"username": "hostadmin", "password": "supersecret-host-pw"})

        base_name = os.path.basename(test_rootfs)
        # Cannot delete active rootfs
        self.assertEqual(client.post("/api/admin/rootfs/delete", json={"name": base_name}).status_code, 400)
        # Path traversal blocked
        self.assertEqual(client.post("/api/admin/rootfs/clone", json={"source": base_name, "target": "../evil.ext4"}).status_code, 400)

        # Clone into variant
        c_res = client.post("/api/admin/rootfs/clone", json={"source": base_name, "target": "ubuntu-java"})
        self.assertEqual(c_res.status_code, 201)
        self.assertEqual(c_res.json["name"], "ubuntu-java.ext4")

        l_res = client.get("/api/admin/rootfs/list")
        self.assertEqual(l_res.status_code, 200)
        names = [i["name"] for i in l_res.json["images"]]
        self.assertIn("ubuntu-java.ext4", names)

        # Delete cloned variant
        self.assertEqual(client.post("/api/admin/rootfs/delete", json={"name": "ubuntu-java.ext4"}).status_code, 200)


if __name__ == "__main__":
    unittest.main()
