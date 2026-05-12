/**
 * mcp-cst-core
 * Copyright (c) 2026 Kenji Igarashi
 * LinkedIn https://www.linkedin.com/in/kenjiigarashi
 * Licensed under the MIT License.
 * 
 * SDKを使わず、物理（10スレッド）でC++20で解析する。
 * セマフォ使用し、10スレッドが暴れる。
 * Tree-sitterで構文解析して、シンボルの座標だけをメモリに持ち
 * CSTそのものはキャッシュファイルにすることで、メモリを最小限に抑える。
 * 解析の基盤の為のモジュールである
 */

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <semaphore> // C++20：スレッドの管理
#include <thread>    // jthread：終了を自動で待つ、賢いスレッド
#include <mutex>     // 共有資源の保護
#include <unistd.h>  // 低レイヤーシステムコール
#include <nlohmann/json.hpp> // AI（MCP）との対話に使う、ライブラリ
#include <tree_sitter/api.h> // 構文解析の心臓部

using json = nlohmann::json;
namespace fs = std::filesystem;

// --- 外部のパーサーを呼び出す ---
extern "C" {
    TSLanguage *tree_sitter_cpp();
    TSLanguage *tree_sitter_java();
    TSLanguage *tree_sitter_kotlin();
    TSLanguage *tree_sitter_typescript();
}

// 言語ごとの「特性と、何を切り出すか）」をまとめる
struct LangTrait {
    TSLanguage* (*get_lang)(); // 言語エンジンの取得関数
    std::string query;         // S式クエリ：何を探すか（関数やクラスなど）
};

class CSTAnalyzer {
    // 【重要】file_cache：詳細を求められた時だけ、ここから「赤身肉」を切り出す
    std::unordered_map<std::string, std::string> file_cache;

    // 【核心】NodeInfo：贅肉を削ぎ落とした「3つの座標」
    // 実体（コード）を持たず、開始・終了・行数・パスという「数値」だけで5.8GBを支配する
    struct NodeInfo { 
        uint32_t s_byte; // 開始位置（バイト）：ここから
        uint32_t e_byte; // 終了位置（バイト）：ここまで
        std::string file_path; // どのファイルにあるか
        uint32_t row;    // 人間（エディタ）が迷わないための行番号
    };

    // シンボル名（例: MyClass::myMethod）から位置情報を一瞬で引くための「巨大な地図」
    std::unordered_map<std::string, NodeInfo> symbol_index;
    
    std::mutex mtx; // 10スレッドが同時に地図を書き換えて壊さないための「交通整理」
    int max_threads; // 「10スレッド」という、ハードウェアのスイートスポット

public:
    // コンストラクタ：初期化（学生らしく、キリよく10スレッドから）
    explicit CSTAnalyzer(int j) : max_threads(j) {}
    // デストラクタ：後片付けは「来た時よりも美しく」
    ~CSTAnalyzer() { clear(); }

    // メモリの「大掃除」：RES（物理メモリ）を0にするための儀式
    void clear() {
        std::lock_guard<std::mutex> lock(mtx);
        file_cache.clear(); symbol_index.clear();
    }

    // --- マルチスレッドで処理を行う ---
    bool scan_parallel(const std::string& root, const std::unordered_map<std::string, LangTrait>& lmap) {
        std::cerr << "🚀 SCAN INITIATED" << std::endl;
        clear();
        if (!fs::exists(root)) {
            return false;
        }
        std::vector<std::jthread> workers;

        // 開始時間
        auto startms = std::chrono::steady_clock::now();

        // 【セマフォ】：12GBの檻を壊さないための門番。同時に暴れるスレッドを「10」に制限。
        std::counting_semaphore<256> sem(max_threads); 

        // プロジェクト内の全ファイルを「再帰的」に走査
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            // ファイル以外、または対応言語（.cpp/.java等）以外は、見向きもしない
            if (!entry.is_regular_file() || !lmap.count(entry.path().extension().string())) continue;

            std::string path = entry.path().string();
            const auto& trait = lmap.at(entry.path().extension().string());

            // 門番（セマフォ）に許可を求める。10個のスロットが埋まっていれば、ここで待機。
            sem.acquire();
            
            // 筋肉質な「並列ワークユニット」を生成
            workers.emplace_back([this, &sem, path, trait]() {
                std::cerr << "🔍 Analysis start: " << path << std::endl;
                
                // ファイルを開く（贅肉のないバイナリ読み込み）
                std::ifstream ifs(path, std::ios::binary);
                if (!ifs) {
                    std::cerr << "❌ Failed to open: " << path << " (errno: " << errno << ")" << std::endl;
                    sem.release();
                    return;
                }

                // ソース全体をメモリに展開
                std::string src((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                if (src.empty()) {
                    std::cerr << "⚠️ Empty file: " << path << std::endl;
                }

                // 【 Tree-sitter の起動 】
                TSParser* lp = ts_parser_new(); // パーサーを生成
                ts_parser_set_language(lp, trait.get_lang()); // 言語をセット
                // パース実行（ここで「木」が生まれる）
                TSTree* lt = ts_parser_parse_string(lp, nullptr, src.c_str(), src.size());
                
                std::vector<std::pair<std::string, NodeInfo>> local_indices;

                if (!lt) {
                    std::cerr << "❌ Parse failed: " << path << std::endl;
                } else {
                    // 木（CST）から「行・開始・終了」だけを抜き出して、ローカル地図を作る
                    build_local_index(path, lt, trait, src, local_indices);

                     // 【重要】：用が済んだら、巨大な「木」は即座に破棄。メモリ（RES）を守る。
                    ts_tree_delete(lt);

                }

                ts_parser_delete(lp); // パーサーも使い捨て。贅肉は残さない。

                // 全体のCSTに、解析したCSTを統合する
                {
                    // スレッドセーフの為、書き込み中は他からをロックする。
                    std::lock_guard<std::mutex> lock(mtx);
                    file_cache[path] = std::move(src); // ソース実体も詳細用に保管
                    for (auto& item : local_indices) symbol_index[std::move(item.first)] = item.second;
                }
                sem.release(); // 門番に「スロットが空いたぞ」と告げる。次のファイルへ。
            });
        }
        // ここで全スレッドが終了するのを待つ（jthreadが自動でやってくれる）
        std::cerr << "✅ Analysis complete. All cores resting." << std::endl;
        auto endms = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endms - startms).count(); 
        
        std::cerr << "✅ Processing time: " << elapsed << " ms" << std::endl;
        return true;
    }

    // --- AIへの回答（2秒の全体把握） ---
    json get_summary() {
        std::lock_guard<std::mutex> lock(mtx);
        json res = json::array();
        // 地図（シンボル一覧）から、名前とファイル、行数だけをAIに差し出す
        for (auto const& [name, info] : symbol_index) {
            res.push_back({{"symbol", name}, {"file", info.file_path}, {"line", info.row}});
        }
        return res; // これを受け取ったAIは、ハルシネーション（幻覚）から解放される
    }

    // --- 特定箇所の切り出し（ミリ秒の詳細取得） ---
    json get_detail(const std::vector<std::string>& symbol_paths) {
        std::lock_guard<std::mutex> lock(mtx);
        json res = json::array();
        for (const auto& sp : symbol_paths) {
            if (symbol_index.count(sp)) {
                auto& info = symbol_index[sp];
                // 地図にある「座標」に基づき、file_cache（肉）から該当部分だけをスライス
                res.push_back({{"symbol", sp}, {"code", file_cache[info.file_path].substr(info.s_byte, info.e_byte - info.s_byte)}});
            }
        }
        return res; // AIが「ここを見たい！」と言った瞬間に、ピンポイントで差し出す
    }

private:
    // CST（木）から座標を抜き出す、職人の仕事
    void build_local_index(const std::string& fpath, TSTree* tree, const LangTrait& trait, const std::string& src, std::vector<std::pair<std::string, NodeInfo>>& out) {
        uint32_t err_o; TSQueryError err_t;
        // S式クエリ：特定の構造（関数等）を探すための「型紙」を生成
        TSQuery* q = ts_query_new(trait.get_lang(), trait.query.c_str(), trait.query.size(), &err_o, &err_t);

        if (!q) {
            std::cerr << "[Warning] Query creation failed for: " << fpath << std::endl;
            return; 
        }

        TSQueryCursor* c = ts_query_cursor_new(); // 木の上を歩くための「カーソル」
        ts_query_cursor_exec(c, q, ts_tree_root_node(tree)); // 探索開始！
        
        TSQueryMatch m;
        while (ts_query_cursor_next_match(c, &m)) { // 見つかる限りループ
            for (int i = 0; i < m.capture_count; i++) {
                TSNode n = m.captures[i].node;
                // 見つかったノードの「パス」「開始」「終了」「ファイル名」「行数」を記録
                out.push_back({build_path(n, src), {ts_node_start_byte(n), ts_node_end_byte(n), fpath, ts_node_start_point(n).row + 1}});
            }
        }
        ts_query_cursor_delete(c); ts_query_delete(q); // 「型紙」と「カーソル」を破棄（後片付け）
    }

    // ノードから「名前::名前」のようなパスを組み立てるロジック
    std::string build_path(TSNode n, const std::string& s) {
        std::vector<std::string> p;
        // 親を辿りながら、名前（nameフィールド）をスタックに積んでいく
        for (TSNode curr = n; !ts_node_is_null(curr); curr = ts_node_parent(curr)) {
            TSNode name = ts_node_child_by_field_name(curr, "name", 4);
            if (!ts_node_is_null(name)) {
                p.push_back(s.substr(ts_node_start_byte(name), ts_node_end_byte(name) - ts_node_start_byte(name)));
            }
        }
        std::string res;
        // スタックを逆に辿って、C++らしい「::」繋ぎのパスを完成させる
        for (auto it = p.rbegin(); it != p.rend(); ++it) { if(!res.empty()) res += "::"; res += *it; }
        return res;
    }
};

// --- ここからが「入り口（Main）」 ---
int main(int argc, char* argv[]) {
    int thread_count = 10; // デフォルトは、10スレッド
    int opt;
    // 「-j 256」のようにスレッド数指定
    while ((opt = getopt(argc, argv, "j:")) != -1) {
        if (opt == 'j') thread_count = std::stoi(optarg);
    }

    // 高速化：標準入出力バッファリングさせてをOSと同期しない
    std::ios::sync_with_stdio(false);
    CSTAnalyzer analyzer(thread_count);
    
    // 言語とクエリ（S式）の定義。ここが抜き出しの肝となり、巨大ソースを解析
    std::unordered_map<std::string, LangTrait> lmap = {
        {".cpp", {tree_sitter_cpp, "(function_definition) @s (class_specifier) @s"}},
        {".java", {tree_sitter_java, "(method_declaration) @s (class_declaration) @s"}},
        {".kt", {tree_sitter_kotlin, "(function_declaration) @s (class_declaration) @s"}},
        {".ts", {tree_sitter_typescript, "(method_declaration) @s (class_declaration) @s"}}
    };

    std::string line;
    // メインループ：AIからのリクエスト待ち
    while (std::getline(std::cin, line)) {
        try {
            auto req = json::parse(line); // 届いたJSONを解釈
            json res = {{"jsonrpc", "2.0"}};
            if (req.contains("id")) res["id"] = req["id"];

            // --- MCP（AIプロトコル）との対話 ---
            if (req["method"] == "initialize") {
                res["result"] =  {
                    {"protocolVersion", "2024-11-05"},
                    {"capabilities", {
                        {"tools", {
                            {"listChanged", true}
                        }},
                        {"resources", {
                            {"subscribe", true},
                            {"listChanged", true}
                        }},
                        {"prompts", {
                            {"listChanged", true}
                        }}
                    }},
                    {"serverInfo", {
                        {"name", "mcp_engine"},
                        {"version", "1.0.0"}
                    }}
                };
            } else if (req["method"] == "tools/list") {
                // 自分が「できること（ツール）」をAIに自慢する。
                res["result"] = {
                    {"tools", {
                        {
                            {"name", "CreateCST"},
                            {"description", "1.1GB/55sのC++エンジンでソースを解析し、物理座標（NodeInfo）を抽出する"},
                            {"inputSchema", {{"type", "object"}, {"arguments", {{"path", {{"type", "string"}}}}}}}
                        },
                        {
                            {"name", "getProductSummary"},
                            {"description", "解析済みの全シンボル（関数・クラス）のインデックスを取得する"},
                            {"inputSchema", {{"type", "object"}, {"arguments", {}}}}
                        },
                        {
                            {"name", "getMethodDetail"},
                            {"description", "NodeInfo（座標）に基づき、特定のシンボルのソースコード詳細を爆速で切り出す"},
                            {"inputSchema", {{"type", "object"}, {"arguments", {{"symbol", {{"type", "string"}}}}}}}
                        }
                    }}
                };
            } else if (req["method"] == "tools/call") {
                // 処理実行
                std::string tn = req["params"]["name"]; auto args = req["params"]["arguments"];
                if (tn == "CreateCST") {
                    if(analyzer.scan_parallel(args["path"], lmap)) {
                        res["result"] = {{"content", {{{"text", "OK", "type", "text"}}}}};
                    } else {
                        res["result"] = {{"content", {{{"text", "NG", "type", "text"}}}}};
                    }
                } else if (tn == "getProductSummary") {
                    res["result"] = {{"content", {{{"text", analyzer.get_summary().dump(), "type", "text"}}}}};
                } else if (tn == "getMethodDetail") {
                    res["result"] = {{"content", {{{"text", analyzer.get_detail(args["symbolPaths"]).dump(), "type", "text"}}}}};
                }
            }
            // 標準出力へ回答（MCPからAIへの回答）
            std::cout << res.dump() << std::endl;
        } catch (...) { /* 例外の場合は何もしない */ }
    }
    return 0; // 処理終了
}