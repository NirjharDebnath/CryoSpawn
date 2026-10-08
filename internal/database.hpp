#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <iostream>

class Database {
private:
    sqlite3* db = nullptr;

    void execute_query(const std::string& sql) {
        char* err_msg = nullptr;
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
            std::string err = err_msg ? err_msg : "Unknown SQLite error";
            sqlite3_free(err_msg);
            throw std::runtime_error("DB Error: " + err);
        }
    }

public:
    Database(const std::string& path = "cryospawn.db") {
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
            throw std::runtime_error("Cannot open database: " + std::string(sqlite3_errmsg(db)));
        }
        
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
                project_name TEXT NOT NULL
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
        // SQLite Migrations (Ignore errors if column already exists)
        try { execute_query("ALTER TABLE vms ADD COLUMN owner_id INTEGER DEFAULT 1;"); } catch(...) {}
        try { execute_query("ALTER TABLE network_links ADD COLUMN owner_id INTEGER DEFAULT 1;"); } catch(...) {}
        try { execute_query("ALTER TABLE volumes ADD COLUMN owner_id INTEGER DEFAULT 1;"); } catch(...) {}


        // Safe alterations to add new columns if they don't exist
        sqlite3_exec(db, "ALTER TABLE vms ADD COLUMN http_exposed INTEGER DEFAULT 0;", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "ALTER TABLE vms ADD COLUMN https_exposed INTEGER DEFAULT 0;", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "ALTER TABLE vms ADD COLUMN rootfs_gb INTEGER DEFAULT 1;", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "ALTER TABLE vms ADD COLUMN uuid TEXT;", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "ALTER TABLE vms ADD COLUMN name TEXT;", nullptr, nullptr, nullptr);
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
    };

    struct LinkRow { int vm1_id; int vm2_id; };

    std::vector<DBRow> get_all_vms(int user_id = 1, bool is_admin = true) {
        std::vector<DBRow> rows;
        // The Vault checks identity: Admins see all, Users see only their own.
        std::string sql = "SELECT slot_id, pid, status, vcpus, mem_mib, guest_ip, host_ip, tap_name, ssh_exposed, host_port, project_name, http_exposed, https_exposed, rootfs_gb, uuid, name, owner_id FROM vms WHERE owner_id = ? OR ? = 1;";
        sqlite3_stmt* stmt;
        
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, user_id);
            sqlite3_bind_int(stmt, 2, is_admin ? 1 : 0);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                DBRow r;
                r.slot = sqlite3_column_int(stmt, 0);
                r.pid = sqlite3_column_int(stmt, 1);
                r.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                r.vcpus = sqlite3_column_int(stmt, 3);
                r.mem_mib = sqlite3_column_int(stmt, 4);
                r.guest_ip = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                r.host_ip = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
                r.tap_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
                r.ssh_exposed = sqlite3_column_int(stmt, 8);
                r.host_port = sqlite3_column_int(stmt, 9);
                r.project_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
                r.http_exposed = sqlite3_column_int(stmt, 11);
                r.https_exposed = sqlite3_column_int(stmt, 12);
                r.rootfs_gb = sqlite3_column_count(stmt) > 13 ? sqlite3_column_int(stmt, 13) : 1;
                r.uuid = sqlite3_column_count(stmt) > 14 && sqlite3_column_text(stmt, 14) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 14)) : "";
                r.name = sqlite3_column_count(stmt) > 15 && sqlite3_column_text(stmt, 15) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 15)) : "";
                r.owner_id = sqlite3_column_count(stmt) > 16 ? sqlite3_column_int(stmt, 16) : 1;
                r.socket_path = "/tmp/cryo_" + r.uuid + ".socket";
                rows.push_back(r);
            }
            sqlite3_finalize(stmt);
        }
        return rows;
    }

    int get_next_free_slot() {
        const char* sql = "SELECT coalesce(min(slot_id) + 1, 0) FROM vms WHERE (slot_id + 1) NOT IN (SELECT slot_id FROM vms) AND (slot_id + 1) <= 16383;";
        sqlite3_stmt* stmt; int slot = 0;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) slot = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
        }
        return slot;
    }

    void insert_vm(int slot, pid_t pid, const std::string& status, int vcpus, int mem,
                   const std::string& g_ip, const std::string& h_ip, const std::string& tap,
                   bool ssh, int port, const std::string& project, bool http_exposed, bool https_exposed, int rootfs_gb, const std::string& uuid, const std::string& name, int owner_id = 1) {
        const char* sql = "INSERT INTO vms (slot_id, pid, status, vcpus, mem_mib, guest_ip, host_ip, tap_name, ssh_exposed, host_port, project_name, http_exposed, https_exposed, rootfs_gb, uuid, name, owner_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        
        sqlite3_bind_int(stmt, 1, slot); sqlite3_bind_int(stmt, 2, pid);
        sqlite3_bind_text(stmt, 3, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, vcpus); sqlite3_bind_int(stmt, 5, mem);
        sqlite3_bind_text(stmt, 6, g_ip.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, h_ip.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, tap.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 9, ssh ? 1 : 0);
        if (ssh) sqlite3_bind_int(stmt, 10, port); else sqlite3_bind_null(stmt, 10);
        sqlite3_bind_text(stmt, 11, project.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 12, http_exposed ? 1 : 0);
        sqlite3_bind_int(stmt, 13, https_exposed ? 1 : 0);
        sqlite3_bind_int(stmt, 14, rootfs_gb);
        sqlite3_bind_text(stmt, 15, uuid.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 16, name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 17, owner_id);

        sqlite3_step(stmt); sqlite3_finalize(stmt);
    }

            void update_volume_uuid(int id, const std::string& uuid) {
        execute_query("UPDATE volumes SET uuid = '" + uuid + "' WHERE id = " + std::to_string(id) + ";");
    }
    
    void update_uuid_and_name(int slot, const std::string& uuid, const std::string& name) {
        execute_query("UPDATE vms SET uuid = '" + uuid + "', name = '" + name + "' WHERE slot_id = " + std::to_string(slot) + ";");
    }

    void update_status(int slot, const std::string& status) {
        execute_query("UPDATE vms SET status = '" + status + "' WHERE slot_id = " + std::to_string(slot) + ";");
    }

    void update_vm_ports(int slot, bool ssh, bool http, bool https) {
        std::string sql = "UPDATE vms SET ssh_exposed = " + std::to_string(ssh ? 1 : 0) + 
                          ", http_exposed = " + std::to_string(http ? 1 : 0) + 
                          ", https_exposed = " + std::to_string(https ? 1 : 0) + 
                          " WHERE slot_id = " + std::to_string(slot) + ";";
        execute_query(sql);
    }

    void remove_vm(int slot) {
        execute_query("DELETE FROM vms WHERE slot_id = " + std::to_string(slot) + ";");
        execute_query("DELETE FROM network_links WHERE vm1_id = " + std::to_string(slot) + " OR vm2_id = " + std::to_string(slot) + ";");
    }

    void add_link(const std::string& project, int vm1, int vm2) {
        int first = std::min(vm1, vm2); int second = std::max(vm1, vm2);
        std::string sql = "INSERT OR IGNORE INTO network_links (project_name, vm1_id, vm2_id) VALUES ('" + project + "', " + std::to_string(first) + ", " + std::to_string(second) + ");";
        execute_query(sql);
    }

    void remove_link(int vm1, int vm2) {
        int first = std::min(vm1, vm2); int second = std::max(vm1, vm2);
        execute_query("DELETE FROM network_links WHERE vm1_id = " + std::to_string(first) + " AND vm2_id = " + std::to_string(second) + ";");
    }

    std::vector<LinkRow> get_all_links() {
        std::vector<LinkRow> links; sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT vm1_id, vm2_id FROM network_links;", -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) links.push_back({sqlite3_column_int(stmt, 0), sqlite3_column_int(stmt, 1)});
            sqlite3_finalize(stmt);
        }
        return links;
    }

    // --- Volumes ---
    int insert_volume(const std::string& name, int size_gb, int owner_id = 1) {
        std::string sql = "INSERT INTO volumes (name, size_gb, owner_id) VALUES ('" + name + "', " + std::to_string(size_gb) + ", " + std::to_string(owner_id) + ");";
        execute_query(sql);
        return sqlite3_last_insert_rowid(db);
    }

    void remove_volume(int id) {
        execute_query("DELETE FROM volumes WHERE id = " + std::to_string(id) + ";");
    }

    void attach_volume(int vol_id, int vm_id) {
        execute_query("UPDATE volumes SET attached_vm_id = " + std::to_string(vm_id) + " WHERE id = " + std::to_string(vol_id) + ";");
    }

    void detach_volume(int vol_id) {
        execute_query("UPDATE volumes SET attached_vm_id = -1 WHERE id = " + std::to_string(vol_id) + ";");
    }

    struct VolumeRow { int id; std::string name; int size_gb; int attached_vm_id; int owner_id; };

    std::vector<VolumeRow> get_volumes(int user_id = 1, bool is_admin = true) {
        std::vector<VolumeRow> vols; sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT id, name, size_gb, attached_vm_id, owner_id FROM volumes WHERE owner_id = ? OR ? = 1;", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, user_id);
            sqlite3_bind_int(stmt, 2, is_admin ? 1 : 0);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                vols.push_back({
                    sqlite3_column_int(stmt, 0),
                    reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
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