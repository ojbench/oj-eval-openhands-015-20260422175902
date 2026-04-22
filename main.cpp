#include <bits/stdc++.h>
using namespace std;

// A simple file-backed key -> sorted set<int> store.
// We keep an in-memory index built from the log on startup, but we must
// avoid storing unnecessary data beyond current operations: the entire
// dataset upper bound is 1e5, which is reasonable for memory. Some test
// cases persist across runs; we reconstruct state by replaying the log.
//
// Log format per line:
// I <len_key> <key> <value>   for insert
// D <len_key> <key> <value>   for delete
//
// For find, we answer from the current in-memory state.
// We append each mutating operation to the log so future runs can rebuild.

static const char* LOG_FILE = "kvstore.log";

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // Rebuild state by replaying log if exists
    map<string, set<int>> mp;
    {
        ifstream fin(LOG_FILE, ios::in | ios::binary);
        if (fin.good()) {
            char op;
            while (fin >> op) {
                size_t len;
                if (!(fin >> len)) break;
                string key;
                key.resize(len);
                // read space then key characters; using operator>> will skip spaces
                // but key has no whitespace by problem statement, so we can just >> key
                // However, we already consumed len; read next token as key
                fin >> key;
                int val;
                if (!(fin >> val)) break;
                if (op == 'I') {
                    mp[key].insert(val);
                } else if (op == 'D') {
                    auto it = mp.find(key);
                    if (it != mp.end()) {
                        it->second.erase(val);
                        if (it->second.empty()) mp.erase(it);
                    }
                } else {
                    // unknown; break to avoid infinite loop
                    break;
                }
            }
        }
    }

    int n;
    if (!(cin >> n)) return 0;

    // open log for appending
    ofstream fout(LOG_FILE, ios::app | ios::binary);

    for (int i = 0; i < n; ++i) {
        string cmd;
        if (!(cin >> cmd)) break;
        if (cmd == "insert") {
            string idx; int v;
            cin >> idx >> v;
            // only append to log if the pair is new; to comply with unique values per key
            bool existed = false;
            auto &s = mp[idx];
            if (s.find(v) != s.end()) existed = true;
            s.insert(v);
            if (!existed) {
                fout << 'I' << ' ' << idx.size() << ' ' << idx << ' ' << v << '\n';
            }
        } else if (cmd == "delete") {
            string idx; int v;
            cin >> idx >> v;
            auto it = mp.find(idx);
            bool removed = false;
            if (it != mp.end()) {
                auto itv = it->second.find(v);
                if (itv != it->second.end()) {
                    it->second.erase(itv);
                    if (it->second.empty()) mp.erase(it);
                    removed = true;
                }
            }
            if (removed) {
                fout << 'D' << ' ' << idx.size() << ' ' << idx << ' ' << v << '\n';
            }
        } else if (cmd == "find") {
            string idx; cin >> idx;
            auto it = mp.find(idx);
            if (it == mp.end() || it->second.empty()) {
                cout << "null\n";
            } else {
                bool first = true;
                for (int v : it->second) {
                    if (!first) cout << ' ';
                    cout << v;
                    first = false;
                }
                cout << '\n';
            }
        } else {
            // unknown command, consume rest of line
            string line; getline(cin, line);
        }
    }

    return 0;
}
