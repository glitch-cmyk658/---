// ============================================================
//  BST_test.cpp —— 针对 namespace BSTRE（递归版）的测试
//  说明：实现都在 BST.hpp（已泛型化）；本文件只做测试。
//
//  【重要】本文件**没有 main**
//    run.ps1 会把当前目录下所有 *.cpp 一起链接成 outDebug.exe，
//    一个目录只能有一个 main，否则报 "multiple definition of `main'"。
//    所以这里把原来的 main 改名成 runRecurTests()，由 BST_iter_test.cpp
//    的 main 在需要时调用。
//
//  【接口模式开关】
//    0 = 对外接口形态：insert(int) / erase(int) / find(int) / preOrder() / inOrder() / posOrder()
//        （递归版私有）—— 当前 BST.hpp 是这种形态
//    1 = 递归版(带 node 参数)是 public，测试直接传节点指针
//
//  【怎么跑】通过唯一的入口（BST_iter_test.cpp 的 main）：
//      outDebug.exe recur          跑本文件全部（= 递归版全量）
//      outDebug.exe recur A        只跑递归版的 A 节
//      outDebug.exe recur B C      跑 B 和 C
// ============================================================
#define BST_TEST_PUBLIC_API 0

#include "BST.hpp"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

static void check(const char* id, const char* desc, bool ok) {
    std::printf("  [%s] %-4s %s\n", ok ? "PASS" : "FAIL", id, desc);
    if (ok) ++g_pass;
    else     ++g_fail;
}

static bool want(char sec, int argc, char** argv) {
    if (argc < 2) return true;
    for (int i = 1; i < argc; ++i)
        if (argv[i][0] == sec) return true;
    return false;
}

#if BST_TEST_PUBLIC_API == 1
// 模式 1 专用：手动建节点（绕开 TreeNode 里的初始化细节）+ 结构检查
static BSTRE::TreeNode<int>* mk(int v, BSTRE::TreeNode<int>* l = nullptr, BSTRE::TreeNode<int>* r = nullptr) {
    BSTRE::TreeNode<int>* n = new BSTRE::TreeNode<int>(v);
    n->left = l;
    n->right = r;
    return n;
}

static int countVal(BSTRE::TreeNode<int>* n, int v) {
    if (!n) return 0;
    return (n->val == v ? 1 : 0) + countVal(n->left, v) + countVal(n->right, v);
}

static int size(BSTRE::TreeNode<int>* n) {
    return n ? 1 + size(n->left) + size(n->right) : 0;
}
#endif

#if BST_TEST_PUBLIC_API == 0
// 模式 0 专用：抓取遍历的 std::cout 输出，抽出整数序列来校验，
// 这样不管你用什么分隔符/换行都不会误报。
static std::vector<int> parseInts(const std::string& s) {
    std::vector<int> out;
    const char* p = s.c_str();
    char* end = nullptr;
    while (*p) {
        long v = std::strtol(p, &end, 10);
        if (end == p) { ++p; continue; }
        out.push_back((int)v);
        p = end;
    }
    return out;
}

static std::vector<int> traversalVec(BSTRE::BinarySearch<int>& t, char which) {
    std::ostringstream oss;
    std::streambuf* old = std::cout.rdbuf(oss.rdbuf());
    if (which == 'P') t.preOrder();
    else if (which == 'I') t.inOrder();
    else                   t.posOrder();
    std::cout.rdbuf(old);
    return parseInts(oss.str());
}

static std::string joinInts(const std::vector<int>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += ',';
        out += std::to_string(v[i]);
    }
    return out;
}

static std::string traversalSeq(BSTRE::BinarySearch<int>& t, char which) {
    return joinInts(traversalVec(t, which));
}

// 中序遍历是否严格递增 —— 这是 BST 最核心的不变式
static bool isSortedStrict(const std::vector<int>& v) {
    for (size_t i = 1; i < v.size(); ++i)
        if (v[i - 1] >= v[i]) return false;
    return true;
}

static int countIn(const std::vector<int>& v, int x) {
    int n = 0;
    for (int e : v) if (e == x) ++n;
    return n;
}

// 注意：BinarySearch 有析构函数却还能被拷贝（规则三件套缺了两个），
// 按值返回/拷贝对象会浅拷贝 root → double free。所以这里只用引用，不复制。
static void buildInto(BSTRE::BinarySearch<int>& t, const int* vals, int n) {
    for (int i = 0; i < n; ++i) t.insert(vals[i]);
}
#endif

// ------------------------------------------------------------
//  入口说明：这里不是 main。
//  名字带 run* 且非 static，是为了让 BST_iter_test.cpp 里的 main 能调用它。
//  其它所有辅助函数/变量都是 static，不会和另一个测试文件冲突。
// ------------------------------------------------------------
int runRecurTests(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // 关缓冲，崩溃前已打印的内容也能看到

#if BST_TEST_PUBLIC_API == 0
    // ==================== 模式 0：只走公开接口（功能级测试） ====================
    BSTRE::BinarySearch<int> t;

    if (want('A', argc, argv)) {
        std::printf("A. 建树 + 查找\n");
        t.insert(5);
        t.insert(3);
        t.insert(8);
        check("A1", "insert(5) 后 find(5) == true", t.find(5));
        check("A2", "find(3) == true", t.find(3));
        check("A3", "find(8) == true", t.find(8));
        check("A4", "find(999) == false", !t.find(999));
    }

    if (want('B', argc, argv)) {
        std::printf("B. 空树 / 边界\n");
        BSTRE::BinarySearch<int> empty;
        check("B1", "空树 find(1) == false", !empty.find(1));
        empty.erase(1);
        check("B2", "空树 erase(1) 不崩", true);
        check("B3", "空树 erase 后 find(1) 仍是 false", !empty.find(1));
    }

    if (want('C', argc, argv)) {
        std::printf("C. 重复插入\n");
        BSTRE::BinarySearch<int> u;
        u.insert(10);
        u.insert(10);
        u.insert(10);
        check("C1", "重复插入 10 后 find(10) == true", u.find(10));
        check("C2", "重复插入后 find(999) == false", !u.find(999));
    }

    if (want('D', argc, argv)) {
        std::printf("D. erase（含删根）\n");
        BSTRE::BinarySearch<int> u;
        u.insert(10); u.insert(5); u.insert(15); u.insert(3); u.insert(8);
        u.erase(3);
        check("D1", "删叶子 3 后 find(3) == false", !u.find(3));
        check("D2", "5/8/10/15 都还在",
            u.find(5) && u.find(8) && u.find(10) && u.find(15));
        u.erase(15);
        check("D3", "删叶子 15 后 find(15) == false", !u.find(15));
        u.erase(5);
        check("D4a", "删单孩子 5 后 find(5) == false", !u.find(5));
        check("D4b", "子节点 8 顶上来了，find(8) == true", u.find(8));
        u.erase(10);
        check("D5a", "删根 10 后 find(10) == false", !u.find(10));
        check("D5b", "剩下的 8 还在（整棵树没丢）", u.find(8));
        u.erase(999);
        check("D6", "删不存在的 999 不影响，find(8) == true", u.find(8));
        u.erase(8);
        check("D7", "删到空树后 find(8) == false", !u.find(8));
    }

    if (want('E', argc, argv)) {
        std::printf("E. findMin\n");
        std::printf("  [SKIP] E1   递归版已私有，改在 D 节里间接验证\n");
    }

    if (want('F', argc, argv)) {
        std::printf("F. 三种遍历（三个结果必须各不相同）\n");
        // 树 10 / 5 / 15
        BSTRE::BinarySearch<int> u;
        u.insert(10);
        u.insert(5);
        u.insert(15);
        check("F1", "preOrder 应为 10,5,15", traversalSeq(u, 'P') == "10,5,15");
        check("F2", "inOrder  应为 5,10,15", traversalSeq(u, 'I') == "5,10,15");
        check("F3", "posOrder 应为 5,15,10", traversalSeq(u, 'O') == "5,15,10");

        // 再补一棵不规则形状的树，防止碰巧对上
        BSTRE::BinarySearch<int> v;
        v.insert(10); v.insert(5); v.insert(15); v.insert(3); v.insert(8);
        check("F4", "preOrder 应为 10,5,3,8,15", traversalSeq(v, 'P') == "10,5,3,8,15");
        check("F5", "inOrder  应为 3,5,8,10,15", traversalSeq(v, 'I') == "3,5,8,10,15");
        check("F6", "posOrder 应为 3,8,5,15,10", traversalSeq(v, 'O') == "3,8,5,15,10");

        t.update(5, 999);
        std::printf("  [SKIP] F7   update 的行为改在 G 节专门测\n");
    }

    // ---------------- G. update（改键；newVal 已存在则拒绝，不删旧键） ----------------
    if (want('G', argc, argv)) {
        std::printf("G. update\n");
        const int T7[] = { 50, 30, 70, 20, 40, 60, 80 };
        const int T3[] = { 50, 30, 70 };

        // --- G1~G7 改一个普通节点：新值在、旧值没了、其余不动、仍然有序 ---
        BSTRE::BinarySearch<int> u;
        buildInto(u, T7, 7);
        check("G1", "初始 inOrder 严格递增", isSortedStrict(traversalVec(u, 'I')));
        check("G2", "update(40, 45) 返回 true", u.update(40, 45));
        check("G3", "find(45) == true（新键可用）", u.find(45));
        check("G4", "find(40) == false（旧键已移除）", !u.find(40));
        check("G5", "其余 6 个键都还在",
            u.find(20) && u.find(30) && u.find(50) && u.find(60) && u.find(70) && u.find(80));
        check("G6", "节点数不变（仍 7 个）", traversalVec(u, 'I').size() == 7);
        check("G7", "inOrder 仍严格递增（BST 不变式没破）", isSortedStrict(traversalVec(u, 'I')));

        // --- G8~G11 把键改到"另一头"，检查树被正确重排 ---
        BSTRE::BinarySearch<int> v;
        buildInto(v, T7, 7);
        check("G8", "update(20, 90) 返回 true", v.update(20, 90));
        check("G9", "find(90) == true", v.find(90));
        check("G10", "find(20) == false", !v.find(20));
        {
            std::vector<int> mid = traversalVec(v, 'I');
            check("G11", "90 成了最大值且 inOrder 有序",
                isSortedStrict(mid) && !mid.empty() && mid.back() == 90 && mid.size() == 7);
        }

        // --- G12~G14 改不存在的键：返回 false，树一点都不能动 ---
        BSTRE::BinarySearch<int> w;
        buildInto(w, T3, 3);
        std::vector<int> before = traversalVec(w, 'I');
        check("G12", "update(999, 1) 返回 false", !w.update(999, 1));
        check("G13", "树完全没变（连结构顺序都一样）", traversalVec(w, 'I') == before);
        check("G14", "也没有偷偷把 1 插进去", !w.find(1));

        // --- G15~G16 空树 ---
        BSTRE::BinarySearch<int> e;
        check("G15", "空树 update(1, 2) 返回 false", !e.update(1, 2));
        check("G16", "空树 update 之后仍是空", traversalVec(e, 'I').empty());

        // --- G17~G19 新键已经存在：应拒绝（否则旧键白丢、新键插不进去） ---
        BSTRE::BinarySearch<int> z;
        buildInto(z, T3, 3);
        check("G17", "update(30, 70)（70 已存在）返回 false", !z.update(30, 70));
        check("G18", "70 只出现一次（没造出重复键）", countIn(traversalVec(z, 'I'), 70) == 1);
        check("G19", "树一点没动：30 还在、70 还在、仍 3 个节点",
            z.find(30) && z.find(70) && traversalVec(z, 'I').size() == 3);

        // --- G20~G21 oldVal == newVal：应幂等 ---
        BSTRE::BinarySearch<int> y;
        buildInto(y, T3, 3);
        check("G20", "update(30, 30) 返回 true 且值还在", y.update(30, 30) && y.find(30));
        check("G21", "update(30, 30) 之后仍是 3 个节点", traversalVec(y, 'I').size() == 3);

        // --- G22~G24 连续多次 update ---
        BSTRE::BinarySearch<int> q;
        buildInto(q, T7, 7);
        q.update(20, 65);
        q.update(80, 25);
        q.update(50, 55);
        check("G22", "连续 3 次 update 后 inOrder 仍严格递增", isSortedStrict(traversalVec(q, 'I')));
        check("G23", "节点数仍是 7", traversalVec(q, 'I').size() == 7);
        check("G24", "65/25/55 在，20/80/50 不在",
            q.find(65) && q.find(25) && q.find(55) && !q.find(20) && !q.find(80) && !q.find(50));

        // --- G25~G27 改的正好是根节点（会走 erase 的"双子节点"分支） ---
        BSTRE::BinarySearch<int> r;
        buildInto(r, T3, 3);
        check("G25", "update(50, 45)（改的是根）返回 true", r.update(50, 45));
        check("G26", "find(45) == true 且 find(50) == false", r.find(45) && !r.find(50));
        check("G27", "inOrder 仍严格递增", isSortedStrict(traversalVec(r, 'I')));

        // --- G28~G30 改成一个中间值，树要重新找位置 ---
        BSTRE::BinarySearch<int> s;
        buildInto(s, T7, 7);
        check("G28", "update(30, 35) 返回 true", s.update(30, 35));
        {
            std::vector<int> mid = traversalVec(s, 'I');
            // 期望 20,35,40,50,60,70,80
            check("G29", "inOrder == 20,35,40,50,60,70,80", joinInts(mid) == "20,35,40,50,60,70,80");
            check("G30", "节点数 7 且有序", mid.size() == 7 && isSortedStrict(mid));
        }
    }

#else
    // ==================== 模式 1：递归版 public，可直接传节点 ====================
    BSTRE::BinarySearch<int> t;

    if (want('A', argc, argv)) {
        std::printf("A. insert / 建树\n");
        BSTRE::TreeNode<int>* ret = t.insert(nullptr, 5);
        check("A1", "insert(nullptr,5) 返回值非空", ret != nullptr);
        check("A2", "插入 5 之后 find(nullptr,5) 能找到", t.find(nullptr, 5) != nullptr);
        t.insert(nullptr, 3);
        t.insert(nullptr, 8);
        check("A3", "插入 5/3/8 之后三个都能找到",
            t.find(nullptr, 5) && t.find(nullptr, 3) && t.find(nullptr, 8));
    }

    if (want('B', argc, argv)) {
        std::printf("B. find(node, val)\n");
        BSTRE::TreeNode<int>* n5 = mk(5);
        BSTRE::TreeNode<int>* n15 = mk(15);
        BSTRE::TreeNode<int>* rootB = mk(10, n5, n15);
        check("B1", "find(子树根,15) 应返回 15 那个节点", t.find(rootB, 15) == n15);
        check("B2", "find(子树根,10) 应返回子树根本身", t.find(rootB, 10) == rootB);
        check("B3", "find(子树根,999) 应返回 nullptr", t.find(rootB, 999) == nullptr);
    }

    if (want('C', argc, argv)) {
        std::printf("C. insert(node, val)\n");
        BSTRE::TreeNode<int>* rootC = mk(10);
        t.insert(rootC, 5);
        check("C1", "insert(根,5) 后根的左孩子应是 5", rootC->left && rootC->left->val == 5);
        t.insert(rootC, 5);
        check("C2", "重复插入 5 不应产生第二个 5", countVal(rootC, 5) == 1);
    }

    if (want('E', argc, argv)) {
        std::printf("E. findMin\n");
        BSTRE::TreeNode<int>* rootE = mk(10, mk(5, mk(2), nullptr), mk(15));
        check("E1", "findMin(root) 应返回 2 那个节点", t.findMin(rootE) == rootE->left->left);
    }

    if (want('F', argc, argv)) {
        std::printf("F. update / 三种遍历\n");
        t.update(5, 999);
        std::printf("  [SKIP] F1   模式 1 下遍历要自备节点，顺序校验请用模式 0 的 F 节\n");
    }

    if (want('D', argc, argv)) {
        std::printf("D. erase\n");

        BSTRE::TreeNode<int>* rootD1 = mk(10, mk(5), mk(15));
        t.erase(rootD1, 5);
        check("D1", "删叶子 5 后 root->left == nullptr", rootD1->left == nullptr);

        BSTRE::TreeNode<int>* rootD2 = mk(10, mk(5, nullptr, mk(8)));
        t.erase(rootD2, 8);
        check("D2", "删单孩子 8 后 5->right == nullptr",
            rootD2->left && rootD2->left->right == nullptr);

        BSTRE::TreeNode<int>* rootD3 = mk(10, mk(5), mk(15));
        t.erase(rootD3, 999);
        check("D3", "删不存在的 999 不改变结构",
            rootD3->val == 10 && rootD3->left && rootD3->left->val == 5 &&
            rootD3->right && rootD3->right->val == 15 && size(rootD3) == 3);

        BSTRE::TreeNode<int>* rootD4 = mk(10, mk(5), mk(15));
        t.erase(rootD4, 10);
        check("D4a", "删双子节点 10 后 root->val 应为 15", rootD4->val == 15);
        check("D4b", "删完后 15 只应出现一次（successor 的副本要被删掉）", countVal(rootD4, 15) == 1);
        check("D4c", "删完后节点总数应从 3 变成 2", size(rootD4) == 2);
    }
#endif

    std::printf("\n===== 结果: %d passed, %d failed =====\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
