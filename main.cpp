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
static const int NUM_BUCKETS = 20;

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
    string first;
    if (!getline(fin, first)) return;
    if (first.rfind("INDEX ", 0) == 0) {
        // Indexed format
        size_t nidx = 0;
        {
            istringstream iss(first);
            string idxw; iss >> idxw >> nidx;
        }
        string line;
        for (size_t i = 0; i < nidx; ++i) getline(fin, line);
        getline(fin, line); // DATA
        while (getline(fin, line)) {
            if (line.empty()) continue;
            istringstream iss(line);
            string key; if (!(iss >> key)) continue;
            vector<int> vals; int x; while (iss >> x) vals.push_back(x);
            if (!vals.empty()) data.emplace(move(key), move(vals));
        }
    } else {
        // Simple format; include first line
        auto process = [&](const string &line){
            if (line.empty()) return;
            istringstream iss(line);
            string key; if (!(iss >> key)) return;
            vector<int> vals; int x; while (iss >> x) vals.push_back(x);
            if (!vals.empty()) data.emplace(key, move(vals));
        };
        process(first);
        string line;
        while (getline(fin, line)) process(line);
    }
}


static void build_indexed_from_data(const string &data_path, const string &final_path);

static void write_bucket(int id, const unordered_map<string, vector<int>> &data) {
    string temp = bucket_path(id) + ".data";
    {
        ofstream td(temp, ios::trunc);
        for (const auto &kv : data) {
            if (kv.second.empty()) continue;
            td << kv.first;
            for (int v : kv.second) td << ' ' << v;
            td << '\n';
        }
    }
    build_indexed_from_data(temp, bucket_path(id));
    remove(temp.c_str());
}

static void build_indexed_from_data(const string &data_path, const string &final_path) {
    // Pass 1: collect keys and line lengths
    vector<pair<string, long long>> keys; // key, length
    keys.reserve(1024);
    {
        ifstream fin(data_path);
        string line;
        while (getline(fin, line)) {
            if (line.empty()) continue;
            istringstream iss(line);
            string k; if (!(iss >> k)) continue;
            long long len = static_cast<long long>(line.size()) + 1; // include '\n'
            keys.emplace_back(k, len);
        }
    }
    // Compute offsets relative to start of data
    vector<tuple<string,long long,long long>> idx; // key, offset, len
    idx.reserve(keys.size());
    long long offset = 0;
    for (auto &kv : keys) {
        idx.emplace_back(kv.first, offset, kv.second);
        offset += kv.second;
    }
    // Sort index by key for binary search
    sort(idx.begin(), idx.end(), [](const auto &a, const auto &b){return get<0>(a) < get<0>(b);} );

    // Pass 2: write final file: INDEX, then DATA, then copy lines
    ofstream fout(final_path, ios::trunc | ios::binary);
    fout << "INDEX " << idx.size() << '\n';
    for (auto &t : idx) {
        fout << get<0>(t) << ' ' << get<1>(t) << ' ' << get<2>(t) << '\n';
    }
    fout << "DATA" << '\n';
    ifstream fin2(data_path, ios::binary);
    string line;
    while (getline(fin2, line)) {
        if (line.empty()) { fout << '\n'; continue; }
        fout << line << '\n';
    }
}


static bool scan_find(int id, const string &key, vector<int> &vals) {
    ifstream fin(bucket_path(id));
    if (!fin.good()) return false;
    string first;
    if (!getline(fin, first)) return false;
    if (first.rfind("INDEX ", 0) == 0) {
        // Indexed format
        size_t nidx = 0; {
            istringstream iss(first); string idxw; iss >> idxw >> nidx;
        }
        string line;
        long long target_off = -1;
        for (size_t i = 0; i < nidx; ++i) {
            if (!getline(fin, line)) return false;
            istringstream iss(line);
            string k; long long off, len; if (!(iss >> k >> off >> len)) return false;
            if (k == key) target_off = off;
        }
        getline(fin, line); // DATA
        streampos data_start = fin.tellg();
        if (target_off < 0) return false;
        fin.seekg(data_start + std::streamoff(target_off));
        if (!getline(fin, line)) return false;
        istringstream iss2(line);
        string k; if (!(iss2 >> k)) return false;
        int x; while (iss2 >> x) vals.push_back(x);
        return true;
    } else {
        // Simple format; include first line
        auto process = [&](const string &line){
            if (line.empty()) return false;
            istringstream iss(line);
            string k; if (!(iss >> k)) return false;
            if (k != key) return false;
            int x; while (iss >> x) vals.push_back(x);
            return true;
        };
        if (process(first)) return true;
        string line;
        while (getline(fin, line)) if (process(line)) return true;
        return false;
    }
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

    // One-pass split kvstore.log into per-bucket temp logs
    vector<unique_ptr<ofstream>> outs(NUM_BUCKETS);
    for (int b = 0; b < NUM_BUCKETS; ++b) {
        outs[b] = make_unique<ofstream>(string("bucket_tmp_") + to_string(b) + ".log", ios::trunc);
    }
    string line;
    while (getline(flog, line)) {
        if (line.empty()) continue;
        // parse key quickly
        vector<string> tok; tok.reserve(4);
        string cur;
        for (char c : line) {
            if (isspace((unsigned char)c)) { if (!cur.empty()) { tok.push_back(cur); cur.clear(); } }
            else cur.push_back(c);
        }
        if (!cur.empty()) tok.push_back(cur);
        if (tok.size() < 3) continue;
        string key = (tok.size() == 4) ? tok[2] : tok[1];
        int b = bucket_id(key);
        (*outs[b]) << line << '\n';
    }
    for (int b = 0; b < NUM_BUCKETS; ++b) outs[b]->close();

    // Build each bucket from its own temp log
    for (int b = 0; b < NUM_BUCKETS; ++b) {
        unordered_map<string, vector<int>> data; data.reserve(2048);
        ifstream tin(string("bucket_tmp_") + to_string(b) + ".log");
        if (tin.good()) {
            string l;
            while (getline(tin, l)) {
                if (l.empty()) continue;
                vector<string> tok; tok.reserve(4);
                string cur;
                for (char c : l) {
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
        remove((string("bucket_tmp_") + to_string(b) + ".log").c_str());
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
