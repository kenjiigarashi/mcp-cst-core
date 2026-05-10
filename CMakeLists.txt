cmake_minimum_required(VERSION 3.15)

# --- プロジェクト基本情報 ---
set(PROJECT_NAME "mcp_cpp_project")
project(${PROJECT_NAME} C CXX)

# --- ビルドモードの切り替え ---
# "EXEC", "SHARED", "STATIC"
set(BUILD_TYPE_MODE "EXEC") 
set(CREATE_PRODUCT "mcp_engine")

# --- 言語規格の設定 ---
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- ソースファイルの収集 (GLOB_RECURSE) ---
file(GLOB_RECURSE SOURCE_LIST
    "main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/tree-sitter/lib/src/lib.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/tree-sitter-*/src/parser.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/tree-sitter-*/src/scanner.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/tree-sitter-*/src/scanner.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/tree-sitter-typescript/typescript/src/parser.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/tree-sitter-typescript/typescript/src/scanner.c"
)

# 必要なコードのみに削ぎ落とす
if(SOURCE_LIST)
    list(FILTER SOURCE_LIST EXCLUDE REGEX "lib/json/tools")
    list(FILTER SOURCE_LIST EXCLUDE REGEX "/tests?/")
    list(FILTER SOURCE_LIST EXCLUDE REGEX "/module_cpp20/")
    # list(FILTER SOURCE_LIST EXCLUDE REGEX "doctest")
    # 全てをC++としてビルド
    # set_source_files_properties(${SOURCE_LIST} PROPERTIES LANGUAGE CXX)
endif()

# --- インクルードパスの解決 ---
# 重要: CMakeの include_directories はワイルドカード(*)が効きません。
# そのため、存在するディレクトリを明示、またはGLOBで取得します。
file(GLOB TS_SRC_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/lib/tree-sitter-*/src")

set(INCLUDEPATH_LIST
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/json/single_include" # nlohmann/jsonが1ファイル版ならここ
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/json/include"        # 通常版ならここ
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/tree-sitter/lib/include"
    ${TS_SRC_DIRS} # GLOBで取得した各言語のsrcディレクトリ
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/tree-sitter-typescript/typescript/src"
)

# --- リンクライブラリ ---
find_package(Threads REQUIRED)
set(LINK_LIB_LIST Threads::Threads) # pthread直書きより安全

# --- 自動分岐処理 (ビルド実行) ---
if(${BUILD_TYPE_MODE} STREQUAL "EXEC")
    add_executable(${CREATE_PRODUCT} ${SOURCE_LIST})
elseif(${BUILD_TYPE_MODE} STREQUAL "SHARED")
    add_library(${CREATE_PRODUCT} SHARED ${SOURCE_LIST})
elseif(${BUILD_TYPE_MODE} STREQUAL "STATIC")
    add_library(${CREATE_PRODUCT} STATIC ${SOURCE_LIST})
endif()

# --- 共通設定の適用 ---
target_include_directories(${CREATE_PRODUCT} PRIVATE ${INCLUDEPATH_LIST})
target_link_libraries(${CREATE_PRODUCT} PRIVATE ${LINK_LIB_LIST})

# 最適化フラグ (10スレッドを活かす筋肉)
if(NOT MSVC)
    target_compile_options(${CREATE_PRODUCT} PRIVATE -O3 -march=native -pthread -w)
endif()