#include <bits/stdc++.h>
using namespace std;

// File-backed KV database.
// We persist all mutations to a single append-only log file. We do not
// maintain global in-memory indices. For find, we scan the log and build
// the values for the specific key only, then output sorted unique values.
// This keeps memory usage proportional to a single key's values.
//
// Log format per line supports two variants for compatibility with past runs:
// 1) Legacy:  I <len_key> <key> <value>
//             D <len_key> <key> <value>
// 2) Current: I <key> <value>
//             D <key> <value>
// We will parse both formats.

static const char* LOG_FILE = "kvstore.log";

static inline bool is_number(const string &s){
    if (s.empty()) return false;
    for (char c : s) if (!isdigit((unsigned char)c)) return false;
    return true;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    int n;
    if (!(cin >> n)) return 0;

    ofstream fout(LOG_FILE, ios::app);

    for (int i = 0; i < n; ++i) {
        string cmd;
        if (!(cin >> cmd)) break;
        if (cmd == "insert") {
            string idx; int v;
            cin >> idx >> v;
            fout << 'I' << ' ' << idx << ' ' << v << '\n';
        } else if (cmd == "delete") {
            string idx; int v;
            cin >> idx >> v;
            fout << 'D' << ' ' << idx << ' ' << v << '\n';
        } else if (cmd == "find") {
            string idx; cin >> idx;
            vector<int> vals;
            vals.reserve(256);
            ifstream fin(LOG_FILE);
            if (fin.good()) {
                char op;
                while (fin >> op) {
                    string t1; fin >> t1; // either len or key
                    string key;
                    int val;
                    if (is_number(t1)) {
                        fin >> key >> val;
                    } else {
                        key = t1;
                        fin >> val;
                    }
                    if (key != idx) continue;
                    auto it = lower_bound(vals.begin(), vals.end(), val);
                    if (op == 'I') {
                        if (it == vals.end() || *it != val) vals.insert(it, val);
                    } else if (op == 'D') {
                        if (it != vals.end() && *it == val) vals.erase(it);
                    }
                }
            }
            if (vals.empty()) {
                cout << "null\n";
            } else {
                for (size_t i2 = 0; i2 < vals.size(); ++i2) {
                    if (i2) cout << ' ';
                    cout << vals[i2];
                }
                cout << '\n';
            }
        } else {
            string line; getline(cin, line);
        }
    }

    return 0;
}
