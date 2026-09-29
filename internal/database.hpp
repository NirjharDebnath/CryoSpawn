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
                project_name TEXT NOT NULL,
                vm1_id INTEGER NOT NULL,
                vm2_id INTEGER NOT NULL,
                UNIQUE(vm1_id, vm2_id)
            );
        )";
        execute_query(ddl);

        // Safe alterations to add new columns if they don't exist
        sqlite3_exec(db, "ALTER TABLE vms ADD COLUMN http_exposed INTEGER DEFAULT 0;", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "ALTER TABLE vms ADD COLUMN https_exposed INTEGER DEFAULT 0;", nullptr, nullptr, nullptr);
    }

    ~Database() { if (db) sqlite3_close(db); }

    struct DBRow {
        int slot; pid_t pid; std::string status;
        int vcpus; int mem_mib; std::string guest_ip; std::string host_ip;
        std::string tap_name; bool ssh_exposed; int host_port;
        bool http_exposed; bool https_exposed;
        std::string socket_path; std::string project_name;
    };

    struct LinkRow { int vm1_id; int vm2_id; };

    std::vector<DBRow> get_all_vms() {
        std::vector<DBRow> rows;
        const char* sql = "SELECT slot_id, pid, status, vcpus, mem_mib, guest_ip, host_ip, tap_name, ssh_exposed, host_port, project_name, http_exposed, https_exposed FROM vms;";
        sqlite3_stmt* stmt;
        
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
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
                r.socket_path = "/tmp/cryo_" + std::to_string(r.slot) + ".socket";
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
                   bool ssh, int port, const std::string& project, bool http_exposed, bool https_exposed) {
        const char* sql = "INSERT INTO vms (slot_id, pid, status, vcpus, mem_mib, guest_ip, host_ip, tap_name, ssh_exposed, host_port, project_name, http_exposed, https_exposed) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
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

        sqlite3_step(stmt); sqlite3_finalize(stmt);
    }

    void update_status(int slot, const std::string& status) {
        execute_query("UPDATE vms SET status = '" + status + "' WHERE slot_id = " + std::to_string(slot) + ";");
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
};