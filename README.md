# mcp_cst_core

**LLVM-scale codebase mapping in <1 min, <910MB RAM.**  
**LLVM級の巨大コードベースを1分・910MB以下で完全把握。**

This is an ultra-minimal, high-performance infrastructure designed for AI-driven large-scale code analysis. It provides the "raw power" to navigate massive repositories that conventional tools struggle to handle.  
AIによる大規模コード解析のために設計された、超軽量・極限性能の解析基盤です。既存のツールでは扱いきれない巨大なリポジトリを瞬時に掌握する「生のパワー」を提供します。

---

## 🚀 Real-world Proof / 性能の実証

### Verified Performance (LLVM Project)
![LLVM Analysis Result](result.png)  
*LLVM full index: **57,444 ms** / **901 MB** RAM usage.*

### 🎬 Demo Video / デモ動画
[**Watch the Monster Performance (Video)**](running.mp4)  
*See how 10 physical threads devour the LLVM source in real-time.*

---

## ✨ Features / 特徴

- **Monster Performance / 圧倒的性能**: 
  - Indexes the entire LLVM project in **under 1 minute**.
  - LLVM級の巨大ソースを**1分以内**に完全インデックス化。
- **Memory Efficiency / 極限の省メモリ**: 
  - Keeps full symbol maps in **<910MB RAM** (RES).
  - 座標管理の徹底により、メモリ消費を**910MB以下**に抑制。
- **Hyper-Scalable / 高度な並列処理**: 
  - Pure C++20 multi-threaded analysis (Scalable up to **`-j 256`**).
  - C++20セマフォによる並列解析。物理コアの性能を極限まで引き出します。
- **Minimalist Implementation / ミニマリズム**: 
  - Core logic is condensed into **<400 lines** of pure C++20. No heavy SDKs.
  - 本体は**400行以下**の研ぎ澄まされたシングルソース。ライブラリ依存を排除。
- **AI-Optimized Two-Stage Analysis / AI最適化**: 
  - **1. Global Overview**: Memory-mapped coordinates for instant symbol discovery.
  - **2. Detailed Insight**: On-demand CST extraction from cache for AI context.
  - 全体把握（座標マップ）と詳細取得（オンデマンド抽出）の2段構え。

---

## 💻 Benchmark Environment / 検証環境
Tested on a compact mini-PC to prove extreme efficiency.  
実用的なミニPC環境で「物理コアの暴力」を実証済み。

- **Device**: GMKTEC M7 (Mini PC)
- **CPU**: AMD Ryzen 7 PRO 6850H (using `-j 10` for standard runs)
- **RAM**: 16GB (Approx. **12GB available** for App)
- **Storage**: 512GB SSD

---

## 🛠 Implementation Detail / 実装の核心
```cpp
// Customize extraction targets per language via S-expression
// 言語ごとの抽出ターゲットをS式で自在に定義可能
{".cpp", {tree_sitter_cpp, "(function_definition) @s (class_specifier) @s"}},
{".ts",  {tree_sitter_typescript, "(method_declaration) @s (class_specifier) @s"}}
```

---

## 📦 Requirements / 依存関係
- **C++20** compatible compiler (GCC 11+, Clang 13+)
- **Tree-Sitter**
- **JSON for Modern C++** (nlohmann/json)

---

## 📄 License
**MIT License**
Copyright (c) 2026 Kenji Igarashi

