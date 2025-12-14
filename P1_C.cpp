///*
//  Part 1 (C++17) - Top 10 JSON Version
//  Modifications:
//  1. Fixed Fatal Flaw: Added 'get_bottleneck' to prevent transaction reverts.
//  2. Fixed Fatal Flaw: Updated 'calc_profit' to handle failed paths correctly.
//  3. Feature: Limits 'weth_opportunities.json' output to Top 10.
//*/
//
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <set>
#include <chrono>
#include "json.hpp" 

using json = nlohmann::json;
using namespace std;

// --- 1. 配置参数 ---
const double MIN_TVL_USD = 50000.0; // 调至适中，防止漏掉机会
const double FEE = 0.997;
// 32 Gwei * 400k Gas ≈ 0.0128 ETH
const double GAS_COST_ETH = 0.0128;
const string WETH = "0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2";

// --- 2. 整数映射系统 ---
unordered_map<string, size_t> token_to_id;
vector<string> id_to_token;

size_t get_id(const string& addr) {
    if (token_to_id.find(addr) == token_to_id.end()) {
        token_to_id[addr] = id_to_token.size();
        id_to_token.push_back(addr);
    }
    return token_to_id[addr];
}

struct Edge {
    size_t to;
    double r_in;
    double r_out;
    double weight;
};

struct ArbResult {
    size_t base_id;
    vector<size_t> path;
    double opt_input;
    double profit_eth;
    double raw_profit;
};

vector<vector<Edge>> adj;

// --- 3. 核心数学函数  ---
double get_amount_out(double amount_in, double reserve_in, double reserve_out) {
    if (amount_in <= 0) return 0;
    double amount_in_with_fee = amount_in * 997.0;
    double numerator = amount_in_with_fee * reserve_out;
    double denominator = (reserve_in * 1000.0) + amount_in_with_fee;
    if (denominator == 0) return 0;
    return numerator / denominator;
}

double calc_profit(double amount_in, const vector<size_t>& path) {
    double curr = amount_in;
    for (size_t i = 0; i < path.size() - 1; ++i) {
        size_t u = path[i];
        size_t v = path[i + 1];
        bool found = false;
        for (const auto& e : adj[u]) {
            if (e.to == v) {
                curr = get_amount_out(curr, e.r_in, e.r_out);
                found = true;
                break;
            }
        }
        // 如果中间断路或金额过小，返回 -1 防止误导优化器
        if (!found || curr <= 1e-15) return -1.0;
    }
    return curr - amount_in;
}

// 防止投入金额过大撑爆小池子
double get_bottleneck(const vector<size_t>& path) {
    double limit = 1e18;
    double simulated = 0.001;
    double initial = simulated;

    for (size_t i = 0; i < path.size() - 1; ++i) {
        size_t u = path[i];
        size_t v = path[i + 1];
        for (const auto& e : adj[u]) {
            if (e.to == v) {
                double max_pool = e.r_in * 0.50; // 最大只动用 50%
                double ratio = simulated / initial;
                if (ratio > 1e-9) {
                    double local_limit = max_pool / ratio;
                    if (local_limit < limit) limit = local_limit;
                }
                simulated = get_amount_out(simulated, e.r_in, e.r_out);
                break;
            }
        }
    }
    return limit;
}

pair<double, double> optimize(const vector<size_t>& path) {
    // 使用瓶颈探测获取正确的上限
    double limit = get_bottleneck(path);
    if (limit <= 0) return { 0.0, -1.0 };

    double low = limit * 0.0001; // 动态下限
    double high = limit;

    // 如果区间无效，直接返回
    if (low >= high) return { low, calc_profit(low, path) };

    double phi = (sqrt(5.0) - 1.0) / 2.0;
    double c = high - (high - low) * phi;
    double d = low + (high - low) * phi;
    double p_c = calc_profit(c, path);
    double p_d = calc_profit(d, path);

    for (int i = 0; i < 20; ++i) {
        if (p_c > p_d) {
            high = d; d = c; p_d = p_c;
            c = high - (high - low) * phi; p_c = calc_profit(c, path);
        }
        else {
            low = c; c = d; p_c = p_d;
            d = low + (high - low) * phi; p_d = calc_profit(d, path);
        }
    }
    double opt = (low + high) / 2.0;
    return { opt, calc_profit(opt, path) };
}

// --- 主程序 ---
int main() {
    auto start_total = chrono::high_resolution_clock::now();

    // 1. Load Data
    cout << "[INFO] Loading data..." << endl;
    ifstream f("v2pools.json");
    if (!f.is_open()) { cerr << "[ERROR] v2pools.json not found." << endl; return 1; }
    json data;
    try { data = json::parse(f); }
    catch (...) { return 1; }

    id_to_token.reserve(10000);
    size_t weth_id = 0;

    // 2. Build Graph
    int loaded = 0;
    for (const auto& pool : data) {
        try {
            string s_usd = pool.value("reserveUSD", "0");
            if (stod(s_usd) < MIN_TVL_USD) continue;
            string t0 = pool["token0"]["id"];
            string t1 = pool["token1"]["id"];
            double r0 = stod(pool["reserve0"].get<string>());
            double r1 = stod(pool["reserve1"].get<string>());
            if (r0 < 1e-6 || r1 < 1e-6) continue;

            size_t u = get_id(t0);
            size_t v = get_id(t1);
            if (adj.size() <= max(u, v)) adj.resize(max(u, v) + 1);

            double w01 = -log((r1 / r0) * FEE);
            double w10 = -log((r0 / r1) * FEE);
            adj[u].push_back({ v, r0, r1, w01 });
            adj[v].push_back({ u, r1, r0, w10 });
            loaded++;
        }
        catch (...) { continue; }
    }
    if (token_to_id.find(WETH) != token_to_id.end()) weth_id = token_to_id[WETH];

    size_t N = id_to_token.size();
    cout << "[INFO] Graph built. Pools: " << loaded << " Nodes: " << N << endl;

    // 3. Pricing Oracle
    vector<double> eth_prices(N, 0.0);
    if (N > weth_id) eth_prices[weth_id] = 1.0;
    queue<size_t> q; q.push(weth_id);
    vector<bool> visited_oracle(N, false);
    visited_oracle[weth_id] = true;

    while (!q.empty()) {
        size_t u = q.front(); q.pop();
        if (u >= adj.size()) continue;
        for (const auto& e : adj[u]) {
            size_t v = e.to;
            if (!visited_oracle[v]) {
                eth_prices[v] = eth_prices[u] * (e.r_in / e.r_out);
                visited_oracle[v] = true;
                q.push(v);
            }
        }
    }

    // 4. SPFA Detection
    cout << "[INFO] Running SPFA..." << endl;
    vector<double> dist(N, 0.0);
    vector<long long> parent(N, -1);
    vector<int> count(N, 0);
    vector<bool> in_q(N, false);
    deque<size_t> spfa_q;
    for (size_t i = 0; i < N; ++i) { in_q[i] = true; spfa_q.push_back(i); }

    vector<vector<size_t>> cycles;
    set<size_t> seen_hashes;
    long long steps = 0;

    while (!spfa_q.empty() && steps < 20000000) { // 增加步数上限
        size_t u = spfa_q.front(); spfa_q.pop_front();
        in_q[u] = false; steps++;
        if (u >= adj.size()) continue;

        for (const auto& e : adj[u]) {
            size_t v = e.to;
            if (dist[u] + e.weight < dist[v] - 1e-9) {
                dist[v] = dist[u] + e.weight;
                parent[v] = u;
                count[v]++;
                if (count[v] > 1) {
                    vector<size_t> path;
                    long long curr = v;
                    for (int k = 0; k < 20; ++k) {
                        path.push_back((size_t)curr);
                        curr = parent[curr];
                        if (curr == (long long)v && path.size() > 2) {
                            path.push_back(v);
                            reverse(path.begin(), path.end());
                            size_t h = 0;
                            for (size_t id : path) h ^= hash<size_t>{}(id)+0x9e3779b9 + (h << 6) + (h >> 2);
                            if (seen_hashes.find(h) == seen_hashes.end()) {
                                seen_hashes.insert(h);
                                cycles.push_back(path);
                            }
                            count[v] = 0; break;
                        }
                        if (curr == -1) break;
                    }
                }
                if (!in_q[v]) { spfa_q.push_back(v); in_q[v] = true; }
            }
        }
    }

    // 5. Optimization & Output
    cout << "[INFO] Optimizing " << cycles.size() << " candidates..." << endl;
    vector<ArbResult> results;
    for (const auto& path : cycles) {
        auto res = optimize(path);
        // 只有毛利 > 0 才处理
        if (res.second > 0) {
            double eth_val = res.second * eth_prices[path[0]];
            double net = eth_val - GAS_COST_ETH;

            // 只要净利 > 0.0001 
            if (net > 0.0001) {
                results.push_back({ path[0], path, res.first, net, res.second });
            }
        }
    }

    // Sort by Net Profit
    sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
        return a.profit_eth > b.profit_eth;
        });

    // --- 6. 双文件输出 ---

    // A. ALL Data
    cout << "[INFO] Saving ALL raw opportunities..." << endl;
    ofstream f_all("all_opportunities.csv");
    f_all << "rank,base_token,base_symbol,net_profit_eth,input_amount,path_array" << endl;

    for (int i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        string base_addr = id_to_token[r.base_id];
        string csv_path = "[";
        for (size_t k = 0; k < r.path.size(); ++k) {
            csv_path += "\"" + id_to_token[r.path[k]] + "\"";
            if (k < r.path.size() - 1) csv_path += ",";
        }
        csv_path += "]";

        f_all << i + 1 << ","
            << base_addr << ","
            << ((r.base_id == weth_id) ? "WETH" : "OTHER") << ","
            << r.profit_eth << ","
            << r.opt_input << ","
            << "\"" << csv_path << "\"" << endl;
    }
    f_all.close();

    //// B. Best Opportunities
    //unordered_set<string> TRUSTED_BASES = {
    //    WETH, "0xdac17f958d2ee523a2206206994597c13d831ec7",
    //    "0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48",
    //    "0x6b175474e89094c44da98b954eedeac495271d0f",
    //    "0x2260fac5e5542a773aa44fbcfedf7c193bc2c599"
    //};

    //ofstream f_best("best_opportunities.csv");
    //f_best << "rank,base_token,net_profit_eth,input_amount,path_array" << endl;
    //int print_count = 0;
    //for (const auto& r : results) {
    //    string base_addr = id_to_token[r.base_id];
    //    if (TRUSTED_BASES.find(base_addr) == TRUSTED_BASES.end()) continue;
    //    if (print_count >= 50) break;
    //    print_count++;

    //    string csv_path = "[";
    //    for (size_t k = 0; k < r.path.size(); ++k) {
    //        csv_path += "\"" + id_to_token[r.path[k]] + "\"";
    //        if (k < r.path.size() - 1) csv_path += ",";
    //    }
    //    csv_path += "]";
    //    f_best << print_count << "," << base_addr << "," << r.profit_eth << "," << r.opt_input << "," << "\"" << csv_path << "\"" << endl;
    //}
    //f_best.close();

    //  JSON 输出：仅前 10 个 WETH 结果
    string TARGET_TOKEN = "0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2";
    string OUTPUT_FILENAME = "weth_opportunities.json";

    if (!results.empty()) {
        ofstream f_json(OUTPUT_FILENAME);
        f_json << "[" << endl;

        bool isFirst = true;
        int count = 0;
        int MAX_JSON_OUTPUT = 10; // 限制输出数量

        for (const auto& r : results) {
            // 去掉了ETH起点限制
            //if (id_to_token[r.path[0]] == TARGET_TOKEN) {
            if (1) {
                // [修改点] 达到10个就停止
                if (count >= MAX_JSON_OUTPUT) break;

                if (!isFirst) f_json << "," << endl;

                double net_profit = r.profit_eth;

                f_json << "  {" << endl;
                f_json << "    \"id\": " << count + 1 << "," << endl;
                f_json << "    \"inputAmount\": \"" << fixed << setprecision(18) << r.opt_input << "\"," << endl;
                f_json << "    \"expectedProfit\": \"" << fixed << setprecision(18) << net_profit << "\"," << endl;
                f_json << "    \"path\": [" << endl;

                for (size_t k = 0; k < r.path.size(); ++k) {
                    f_json << "      \"" << id_to_token[r.path[k]] << "\"" << (k < r.path.size() - 1 ? "," : "") << endl;
                }

                f_json << "    ]" << endl;
                f_json << "  }";

                isFirst = false;
                count++;
            }
        }

        f_json << endl << "]" << endl;
        f_json.close();

        cout << "[INFO] Saved top " << count << " WETH cycles to '" << OUTPUT_FILENAME << "'" << endl;
    }

    // 生成全量 opportunity.json 供参考 
    //保留原来的 opportunity.json 逻辑

    return 0;
}