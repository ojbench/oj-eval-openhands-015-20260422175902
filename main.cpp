#include <bits/stdc++.h>
using namespace std;

// File-backed KV database with bucketed storage.
// - We split the whole key space into a fixed number of bucket files.
// - Each bucket file stores many lines, each line: "key v1 v2 v3" with ascending values.
// - For each operation, we load only the target bucket into memory, modify/read,
//   and then write the bucket back. This keeps memory bounded and avoids scanning
//   a global log per find.
// - To ensure compatibility with older runs that used kvstore.log, on startup we
//   rebuild bucket files from kvstore.log if no bucket files exist yet.

static const char* LOG_FILE = "kvstore.log";
static const int NUM_BUCKETS = 16;

static inline int bucket_id(const string &key) {
    uint64_t h = 1469598103934665603ull; // FNV-1a 64-bit
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return int(h % NUM_BUCKETS);
}

static inline string bucket_path(int id) {
    char buf[64];
    snprintf(buf, sizeof(buf), "bucket_%02d.db", id);
    return string(buf);
}

static void load_bucket(int id, unordered_map<string, vector<int>> &data) {
    data.clear();
    ifstream fin(bucket_path(id));
    if (!fin.good()) return;
    string line;
    while (getline(fin, line)) {
        if (line.empty()) continue;
        istringstream iss(line);
        string key; if (!(iss >> key)) continue;
        vector<int> vals; int x;
        while (iss >> x) vals.push_back(x);
        if (!vals.empty()) data.emplace(move(key), move(vals));
    }
}

static void write_bucket(int id, const unordered_map<string, vector<int>> &data) {
    ofstream fout(bucket_path(id), ios::trunc);
    for (const auto &kv : data) {
        if (kv.second.empty()) continue;
        fout << kv.first;
        for (int v : kv.second) fout << ' ' << v;
        fout << '\n';
    }
}

static bool scan_find(int id, const string &key, vector<int> &vals) {
    ifstream fin(bucket_path(id));
    if (!fin.good()) return false;
    string line;
    while (getline(fin, line)) {
        if (line.empty()) continue;
        istringstream iss(line);
        string k; if (!(iss >> k)) continue;
        if (k == key) {
            int x; while (iss >> x) vals.push_back(x);
            return true;
        }
    }
    return false;
}

static void apply_update(int id, const string &key, int val, bool is_insert) {
    string path = bucket_path(id);
    string tmp = path + ".tmp";
    ifstream fin(path);
    ofstream fout(tmp, ios::trunc);
    bool found = false;
    string line;
    if (fin.good()) while (getline(fin, line)) {
        if (line.empty()) continue;
        istringstream iss(line);
        string k; if (!(iss >> k)) { fout << line << '\n'; continue; }
        if (k == key) {
            vector<int> vec; int x; while (iss >> x) vec.push_back(x);
            auto it = lower_bound(vec.begin(), vec.end(), val);
            if (is_insert) {
                if (it == vec.end() || *it != val) vec.insert(it, val);
            } else {
                if (it != vec.end() && *it == val) vec.erase(it);
            }
            if (!vec.empty()) {
                fout << k;
                for (int v : vec) fout << ' ' << v;
                fout << '\n';
            }
            found = true;
        } else {
            fout << line << '\n';
        }
    }
    if (!found && is_insert) {
        fout << key << ' ' << val << '\n';
    }
    fout.close();
    fin.close();
    // Replace original file
    rename(tmp.c_str(), path.c_str());
}


static void rebuild_from_log_if_needed() {
    // If any bucket exists, assume already rebuilt
    for (int i = 0; i < NUM_BUCKETS; ++i) {
        ifstream chk(bucket_path(i));
        if (chk.good()) return;
    }
    ifstream flog(LOG_FILE);
    if (!flog.good()) return;
    // Process each bucket sequentially to limit file count and memory
    for (int b = 0; b < NUM_BUCKETS; ++b) {
        // Create a temporary per-bucket log by scanning the global log
        string tmp = string("bucket_tmp_") + to_string(b) + ".log";
        {
            flog.clear();
            flog.seekg(0);
            ofstream out(tmp, ios::trunc);
            string line;
            while (getline(flog, line)) {
                if (line.empty()) continue;
                // Quick parse to get key
                vector<string> tok; tok.reserve(4);
                string cur;
                for (char c : line) {
                    if (isspace((unsigned char)c)) { if (!cur.empty()) { tok.push_back(cur); cur.clear(); } }
                    else cur.push_back(c);
                }
                if (!cur.empty()) tok.push_back(cur);
                if (tok.size() < 3) continue;
                string key = (tok.size() == 4) ? tok[2] : tok[1];
                if (bucket_id(key) == b) out << line << '\n';
            }
        }
        // Build bucket data from its temporary log
        unordered_map<string, vector<int>> data; data.reserve(2048);
        ifstream tin(tmp);
        if (tin.good()) {
            string line;
            while (getline(tin, line)) {
                if (line.empty()) continue;
                vector<string> tok; tok.reserve(4);
                string cur;
                for (char c : line) {
                    if (isspace((unsigned char)c)) { if (!cur.empty()) { tok.push_back(cur); cur.clear(); } }
                    else cur.push_back(c);
                }
                if (!cur.empty()) tok.push_back(cur);
                if (tok.size() < 3) continue;
                char op = tok[0].empty() ? '?' : tok[0][0];
                string key; int val = 0;
                if (tok.size() == 4) { key = tok[2]; val = stoi(tok[3]); }
                else { key = tok[1]; val = stoi(tok[2]); }
                auto &vec = data[key];
                auto it = lower_bound(vec.begin(), vec.end(), val);
                if (op == 'I') {
                    if (it == vec.end() || *it != val) vec.insert(it, val);
                } else if (op == 'D') {
                    if (it != vec.end() && *it == val) vec.erase(it);
                }
            }
            tin.close();
        }
        write_bucket(b, data);
        // Remove temporary log
        remove(tmp.c_str());
    }
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    rebuild_from_log_if_needed();

    int n;
    if (!(cin >> n)) return 0;

    for (int i = 0; i < n; ++i) {
        string cmd;
        if (!(cin >> cmd)) break;
        if (cmd == "insert") {
            string idx; int v; cin >> idx >> v;
            int bid = bucket_id(idx);
            apply_update(bid, idx, v, true);
        } else if (cmd == "delete") {
            string idx; int v; cin >> idx >> v;
            int bid = bucket_id(idx);
            apply_update(bid, idx, v, false);
        } else if (cmd == "find") {
            string idx; cin >> idx;
            int bid = bucket_id(idx);
            vector<int> vec; vec.reserve(16);
            bool ok = scan_find(bid, idx, vec);
            if (!ok || vec.empty()) {
                cout << "null\n";
            } else {
                for (size_t j = 0; j < vec.size(); ++j) {
                    if (j) cout << ' ';
                    cout << vec[j];
                }
                cout << '\n';
            }
        } else {
            string line; getline(cin, line);
        }
    }

    return 0;
}
