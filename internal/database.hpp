#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <sys/stat.h>
#include <algorithm>
#include <mutex>
#include "config.hpp"

class Database {
private:
    sqlite3* db = nullptr;
    mutable std::recursive_mutex db_mtx;

    inline std::string col_text(sqlite3_stmt* stmt, int col) {
        const unsigned char* txt = sqlite3_column_text(stmt, col);
        return txt ? reinterpret_cast<const char*>(txt) : "";
    }

    void execute_query(const std::string& sql) {
        std::lock_guard<std::recursive_mutex> lock(db_mtx);
        char* err_msg = nullptr;
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
            std::string err = err_msg ? err_msg : "Unknown SQLite error";
            sqlite3_free(err_msg);
            throw std::runtime_error("DB Error: " + err);
        }
    }

public:
    Database(const std::string& path = config::get_db_path()) {
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
            throw std::runtime_error("Cannot open database: " + std::string(sqlite3_errmsg(db)));
        }

        // Enable Write-Ahead Logging (WAL) and 5000ms busy timeout for safe concurrent access
        sqlite3_busy_timeout(db, 5000);
        execute_query("PRAGMA journal_mode = WAL;");
        execute_query("PRAGMA synchronous = NORMAL;");

        // Ensure database files are accessible across root daemon and non-root Python frontend
        chmod(path.c_str(), 0666);
        chmod((path + "-wal").c_str(), 0666);
        chmod((path + "-shm").c_str(), 0666);
        
        std::string ddl = R"(
            CREATE TABLE IF NOT EXISTS vms (
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
                project_name TEXT NOT NULL,
                http_exposed INTEGER DEFAULT 0,
                https_exposed INTEGER DEFAULT 0,
                rootfs_gb INTEGER DEFAULT 3,
                uuid TEXT,
                name TEXT,
                allow_interproject_routing INTEGER DEFAULT 0,
                mac TEXT
            );
            CREATE TABLE IF NOT EXISTS network_links (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                owner_id INTEGER DEFAULT 1,
                project_name TEXT NOT NULL,
                vm1_id INTEGER NOT NULL,
                vm2_id INTEGER NOT NULL,
                UNIQUE(vm1_id, vm2_id)
            );
            CREATE TABLE IF NOT EXISTS volumes (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                owner_id INTEGER DEFAULT 1,
                uuid TEXT UNIQUE,
                name TEXT NOT NULL,
                size_gb INTEGER NOT NULL,
                attached_vm_id INTEGER DEFAULT -1
            );
        )";
        execute_query(ddl);

        // Safe alterations to add new columns if they don't exist
        try { execute_query("ALTER TABLE vms ADD COLUMN owner_id INTEGER DEFAULT 1;"); } catch(...) {}
        try { execute_query("ALTER TABLE network_links ADD COLUMN owner_id INTEGER DEFAULT 1;"); } catch(...) {}
        try { execute_query("ALTER TABLE volumes ADD COLUMN owner_id INTEGER DEFAULT 1;"); } catch(...) {}
        try { execute_query("ALTER TABLE vms ADD COLUMN http_exposed INTEGER DEFAULT 0;"); } catch(...) {}
        try { execute_query("ALTER TABLE vms ADD COLUMN https_exposed INTEGER DEFAULT 0;"); } catch(...) {}
        try { execute_query("ALTER TABLE vms ADD COLUMN rootfs_gb INTEGER DEFAULT 3;"); } catch(...) {}
        try { execute_query("ALTER TABLE vms ADD COLUMN uuid TEXT;"); } catch(...) {}
        try { execute_query("ALTER TABLE vms ADD COLUMN name TEXT;"); } catch(...) {}
        try { execute_query("ALTER TABLE vms ADD COLUMN allow_interproject_routing INTEGER DEFAULT 0;"); } catch(...) {}
        try { execute_query("ALTER TABLE vms ADD COLUMN mac TEXT;"); } catch(...) {}
    }

    ~Database() { if (db) sqlite3_close(db); }

    struct DBRow {
        int owner_id;
        int slot; pid_t pid; std::string status;
        int vcpus; int mem_mib; std::string guest_ip; std::string host_ip;
        std::string tap_name; bool ssh_exposed; int host_port;
        bool http_exposed; bool https_exposed;
        std::string socket_path; std::string project_name; int rootfs_gb;
        std::string uuid; std::string name;
        bool allow_interproject_routing = false;
        std::string mac = "";
    };

    struct LinkRow { int vm1_id; int vm2_id; };

    std::vector<DBRow> get_all_vms(int user_id = 1, bool is_admin = true) {
        std::lock_guard<std::recursive_mutex> lock(db_mtx);
        std::vector<DBRow> rows;
        const char* sql = is_admin
            ? "SELECT slot_id, pid, status, vcpus, mem_mib, guest_ip, host_ip, tap_name, ssh_exposed, host_port, project_name, http_exposed, https_exposed, rootfs_gb, uuid, name, owner_id, allow_interproject_routing, mac FROM vms;"
            : "SELECT slot_id, pid, status, vcpus, mem_mib, guest_ip, host_ip, tap_name, ssh_exposed, host_port, project_name, http_exposed, https_exposed, rootfs_gb, uuid, name, owner_id, allow_interproject_routing, mac FROM vms WHERE owner_id = ?;";
        sqlite3_stmt* stmt;
        
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (!is_admin) {
                sqlite3_bind_int(stmt, 1, user_id);
            }
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                DBRow r;
                r.slot = sqlite3_column_int(stmt, 0);
                r.pid = sqlite3_column_int(stmt, 1);
                r.status = col_text(stmt, 2);
                r.vcpus = sqlite3_column_int(stmt, 3);
                r.mem_mib = sqlite3_column_int(stmt, 4);
                r.guest_ip = col_text(stmt, 5);
                r.host_ip = col_text(stmt, 6);
                r.tap_name = col_text(stmt, 7);
                r.ssh_exposed = sqlite3_column_int(stmt, 8);
                r.host_port = sqlite3_column_int(stmt, 9);
                r.project_name = col_text(stmt, 10);
                r.http_exposed = sqlite3_column_int(stmt, 11);
                r.https_exposed = sqlite3_column_int(stmt, 12);
                r.rootfs_gb = sqlite3_column_count(stmt) > 13 ? sqlite3_column_int(stmt, 13) : 3;
                r.uuid = sqlite3_column_count(stmt) > 14 ? col_text(stmt, 14) : "";
                r.name = sqlite3_column_count(stmt) > 15 ? col_text(stmt, 15) : "";
                r.owner_id = sqlite3_column_count(stmt) > 16 ? sqlite3_column_int(stmt, 16) : 1;
                r.allow_interproject_routing = sqlite3_column_count(stmt) > 17 ? (sqlite3_column_int(stmt, 17) != 0) : false;
                r.mac = sqlite3_column_count(stmt) > 18 ? col_text(stmt, 18) : "";
                std::string hex = (r.uuid.length() > 3 && r.uuid.substr(0, 3) == "vm-") ? r.uuid.substr(3) : r.uuid;
                r.socket_path = "/tmp/cryo-" + hex + ".socket";
                rows.push_back(r);
            }
            sqlite3_finalize(stmt);
        }
        return rows;
    }

    int get_next_free_slot() {
        // Correctly finds slot 0 if available, then any gap, up to 16383
        const char* sql = "SELECT COALESCE("
                          "  (SELECT 0 WHERE NOT EXISTS (SELECT 1 FROM vms WHERE slot_id = 0)),"
                          "  (SELECT MIN(slot_id + 1) FROM vms WHERE (slot_id + 1) NOT IN (SELECT slot_id FROM vms) AND (slot_id + 1) <= 16383)"
                          ");";
        sqlite3_stmt* stmt; int slot = 0;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) slot = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
        }
        return slot;
    }

    void insert_vm(int slot, pid_t pid, const std::string& status, int vcpus, int mem,
                   const std::string& g_ip, const std::string& h_ip, const std::string& tap,
                   bool ssh, int port, const std::string& project, bool http_exposed, bool https_exposed, int rootfs_gb, const std::string& uuid, const std::string& name, int owner_id = 1, bool allow_interproject_routing = false, const std::string& mac = "") {
        const char* sql = "INSERT INTO vms (slot_id, pid, status, vcpus, mem_mib, guest_ip, host_ip, tap_name, ssh_exposed, host_port, project_name, http_exposed, https_exposed, rootfs_gb, uuid, name, owner_id, allow_interproject_routing, mac) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, slot);
            sqlite3_bind_int(stmt, 2, pid);
            sqlite3_bind_text(stmt, 3, status.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 4, vcpus);
            sqlite3_bind_int(stmt, 5, mem);
            sqlite3_bind_text(stmt, 6, g_ip.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 7, h_ip.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 8, tap.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 9, ssh ? 1 : 0);
            if (ssh && port > 0) sqlite3_bind_int(stmt, 10, port); else sqlite3_bind_null(stmt, 10);
            sqlite3_bind_text(stmt, 11, project.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 12, http_exposed ? 1 : 0);
            sqlite3_bind_int(stmt, 13, https_exposed ? 1 : 0);
            sqlite3_bind_int(stmt, 14, rootfs_gb);
            sqlite3_bind_text(stmt, 15, uuid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 16, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 17, owner_id);
            sqlite3_bind_int(stmt, 18, allow_interproject_routing ? 1 : 0);
            sqlite3_bind_text(stmt, 19, mac.c_str(), -1, SQLITE_TRANSIENT);

            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    void update_mac(int slot, const std::string& mac) {
        const char* sql = "UPDATE vms SET mac = ? WHERE slot_id = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, mac.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, slot);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    void update_vm_routing(int slot, bool allow_interproject_routing) {
        const char* sql = "UPDATE vms SET allow_interproject_routing = ? WHERE slot_id = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, allow_interproject_routing ? 1 : 0);
            sqlite3_bind_int(stmt, 2, slot);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    void update_volume_uuid(int id, const std::string& uuid) {
        const char* sql = "UPDATE volumes SET uuid = ? WHERE id = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    
    void update_uuid_and_name(int slot, const std::string& uuid, const std::string& name) {
        const char* sql = "UPDATE vms SET uuid = ?, name = ? WHERE slot_id = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 3, slot);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    void update_status(int slot, const std::string& status) {
        const char* sql = "UPDATE vms SET status = ? WHERE slot_id = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, slot);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    void update_vm_ports(int slot, bool ssh, bool http, bool https, int host_port = 0) {
        const char* sql = (host_port > 0)
            ? "UPDATE vms SET ssh_exposed = ?, http_exposed = ?, https_exposed = ?, host_port = ? WHERE slot_id = ?;"
            : "UPDATE vms SET ssh_exposed = ?, http_exposed = ?, https_exposed = ? WHERE slot_id = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, ssh ? 1 : 0);
            sqlite3_bind_int(stmt, 2, http ? 1 : 0);
            sqlite3_bind_int(stmt, 3, https ? 1 : 0);
            if (host_port > 0) {
                sqlite3_bind_int(stmt, 4, host_port);
                sqlite3_bind_int(stmt, 5, slot);
            } else {
                sqlite3_bind_int(stmt, 4, slot);
            }
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    void remove_vm(int slot) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "DELETE FROM vms WHERE slot_id = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, slot);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        if (sqlite3_prepare_v2(db, "DELETE FROM network_links WHERE vm1_id = ? OR vm2_id = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, slot);
            sqlite3_bind_int(stmt, 2, slot);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        if (sqlite3_prepare_v2(db, "UPDATE volumes SET attached_vm_id = -1 WHERE attached_vm_id = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, slot);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    bool add_link(const std::string& project, int vm1, int vm2, int owner_id = 1) {
        int first = std::min(vm1, vm2); int second = std::max(vm1, vm2);
        const char* sql = "INSERT OR IGNORE INTO network_links (project_name, vm1_id, vm2_id, owner_id) VALUES (?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        bool inserted = false;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, project.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, first);
            sqlite3_bind_int(stmt, 3, second);
            sqlite3_bind_int(stmt, 4, owner_id);
            if (sqlite3_step(stmt) == SQLITE_DONE) {
                inserted = (sqlite3_changes(db) > 0);
            }
            sqlite3_finalize(stmt);
        }
        return inserted;
    }

    void remove_link(int vm1, int vm2) {
        int first = std::min(vm1, vm2); int second = std::max(vm1, vm2);
        const char* sql = "DELETE FROM network_links WHERE vm1_id = ? AND vm2_id = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, first);
            sqlite3_bind_int(stmt, 2, second);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    std::vector<LinkRow> get_all_links(int user_id = 1, bool is_admin = true) {
        std::lock_guard<std::recursive_mutex> lock(db_mtx);
        std::vector<LinkRow> links; sqlite3_stmt* stmt;
        const char* sql = is_admin
            ? "SELECT vm1_id, vm2_id FROM network_links;"
            : "SELECT vm1_id, vm2_id FROM network_links WHERE owner_id = ?;";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (!is_admin) {
                sqlite3_bind_int(stmt, 1, user_id);
            }
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                links.push_back({sqlite3_column_int(stmt, 0), sqlite3_column_int(stmt, 1)});
            }
            sqlite3_finalize(stmt);
        }
        return links;
    }

    void cleanup_orphaned_links() {
        const char* sql = "DELETE FROM network_links WHERE vm1_id NOT IN (SELECT slot_id FROM vms) OR vm2_id NOT IN (SELECT slot_id FROM vms);";
        execute_query(sql);
    }

    // --- Volumes ---
    int insert_volume(const std::string& name, int size_gb, int owner_id = 1) {
        std::lock_guard<std::recursive_mutex> lock(db_mtx);
        const char* sql = "INSERT INTO volumes (name, size_gb, owner_id) VALUES (?, ?, ?);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, size_gb);
            sqlite3_bind_int(stmt, 3, owner_id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        return sqlite3_last_insert_rowid(db);
    }

    void remove_volume(int id) {
        std::lock_guard<std::recursive_mutex> lock(db_mtx);
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "DELETE FROM volumes WHERE id = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    bool remove_volume_if_unattached(int id) {
        std::lock_guard<std::recursive_mutex> lock(db_mtx);
        sqlite3_stmt* stmt;
        bool deleted = false;
        if (sqlite3_prepare_v2(db, "DELETE FROM volumes WHERE id = ? AND attached_vm_id = -1;", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, id);
            if (sqlite3_step(stmt) == SQLITE_DONE) {
                deleted = (sqlite3_changes(db) > 0);
            }
            sqlite3_finalize(stmt);
        }
        return deleted;
    }

    void attach_volume(int vol_id, int vm_id) {
        std::lock_guard<std::recursive_mutex> lock(db_mtx);
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "UPDATE volumes SET attached_vm_id = ? WHERE id = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, vm_id);
            sqlite3_bind_int(stmt, 2, vol_id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    void detach_volume(int vol_id) {
        std::lock_guard<std::recursive_mutex> lock(db_mtx);
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "UPDATE volumes SET attached_vm_id = -1 WHERE id = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, vol_id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    void detach_all_volumes_from_vm(int vm_id) {
        std::lock_guard<std::recursive_mutex> lock(db_mtx);
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "UPDATE volumes SET attached_vm_id = -1 WHERE attached_vm_id = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, vm_id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    void cleanup_orphaned_volumes() {
        const char* sql = "UPDATE volumes SET attached_vm_id = -1 WHERE attached_vm_id != -1 AND attached_vm_id NOT IN (SELECT slot_id FROM vms);";
        execute_query(sql);
    }

    struct VolumeRow { int id; std::string name; int size_gb; int attached_vm_id; int owner_id; };

    bool get_volume(int vol_id, VolumeRow& vol) {
        std::lock_guard<std::recursive_mutex> lock(db_mtx);
        const char* sql = "SELECT id, name, size_gb, attached_vm_id, owner_id FROM volumes WHERE id = ?;";
        sqlite3_stmt* stmt;
        bool found = false;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, vol_id);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                vol.id = sqlite3_column_int(stmt, 0);
                vol.name = col_text(stmt, 1);
                vol.size_gb = sqlite3_column_int(stmt, 2);
                vol.attached_vm_id = sqlite3_column_int(stmt, 3);
                vol.owner_id = sqlite3_column_int(stmt, 4);
                found = true;
            }
            sqlite3_finalize(stmt);
        }
        return found;
    }

    bool user_owns_volume(int vol_id, int user_id) {
        VolumeRow vol;
        if (!get_volume(vol_id, vol)) return false;
        return vol.owner_id == user_id;
    }

    std::vector<VolumeRow> get_volumes(int user_id = 1, bool is_admin = true) {
        std::lock_guard<std::recursive_mutex> lock(db_mtx);
        std::vector<VolumeRow> vols; sqlite3_stmt* stmt;
        const char* sql = is_admin
            ? "SELECT id, name, size_gb, attached_vm_id, owner_id FROM volumes;"
            : "SELECT id, name, size_gb, attached_vm_id, owner_id FROM volumes WHERE owner_id = ?;";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (!is_admin) {
                sqlite3_bind_int(stmt, 1, user_id);
            }
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                vols.push_back({
                    sqlite3_column_int(stmt, 0),
                    col_text(stmt, 1),
                    sqlite3_column_int(stmt, 2),
                    sqlite3_column_int(stmt, 3),
                    sqlite3_column_count(stmt) > 4 ? sqlite3_column_int(stmt, 4) : 1
                });
            }
            sqlite3_finalize(stmt);
        }
        return vols;
    }
};