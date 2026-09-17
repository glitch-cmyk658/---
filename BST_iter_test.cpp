// ============================================================
//  BST_iter_test.cpp —— 针对 namespace BST（非递归版）的独立测试
//
//  接口见 BST.hpp（已泛型化，这里是 BinarySearch<int>）。
//
//  【编译】本文件是当前目录里**唯一**带 main 的文件（见下方"唯一入口"）
//    g++ -std=c++17 -Wall -Wextra BST.cpp BST_iter_test.cpp -o BST_iter_test
//
//  【运行】崩了后面的用例就丢了，所以支持按用例单独跑
//    outDebug.exe            跑非递归版全部（本文件）
//    outDebug.exe A          只跑 A 节
//    outDebug.exe D4 D5      只跑 D4 / D5 两个用例
//    outDebug.exe noI        不调用 inOrder（见下方 noI 安全模式）
//    outDebug.exe recur      跑递归版全部（转交 BST_test.cpp 里的 runRecurTests）
//    outDebug.exe recur A B  跑递归版的 A / B 节
//
//  【用例分布】
//    A. insert（7）  B. find（6）  C. update（17）  D. erase（26）
//    E. 遍历（12）   F. 综合（4）   I. 规模/深度（3）
//    G. 内存（5，追踪被释放的具体节点 —— 删错节点/漏删节点都躲不过）
//    H. 键集合校验（6，不依赖遍历，遍历没实现时照样有效）
//    J. 泛型/比较器（9，换 T 与换 Compare：string/Order/Money/greater，含递归版）
//  （E8/E9/E11/E12/E13 只在显式点名时才跑，见下）
//
//  【update 语义】这版是"拒绝式"，不合并：
//      newVal 已存在且与 oldVal 不等价 → false 且不动树（否则旧键白丢、新键插不进去）
//      oldVal 与 newVal 等价           → true  且不动树（幂等；这一句必须排在
//                                        find(newVal) 之前，否则 update(k,k) 会被误拒）
//      其余                            → 删旧插新，true
//    C4 测不等价的拒绝，C8 测等价的幂等。
//
//  【遍历接口】BST 里的后序有两个版本，测试分别覆盖：
//    posOrderTwoStack()  → capture(t,'O')   E1~E4 E6 E7 E10 E12
//    posOrderOndStack()  → capture(t,'S')   E8 E9 E11 E13   （用户已把 Onde 改成 Ond）
//    ⚠ E8 / E9 / E11 / E13 走单栈版，**有死循环风险**：它们只在显式点名时才执行
//      （跑 ./outDebug.exe E 不会带上它们）。真卡住时看输出在疯狂重复，就是循环没往前走。
//
//  【noI 安全模式】一旦 inOrder 写坏成死循环，测试会在第一句就卡死。加 noI 参数绕开它：
//    形状改用「前序输出 + 排序」代替中序。对一棵合法 BST，排序后的遍历结果就等于中序，
//    所以键集合 / 节点数 / 有没丢值这些断言仍然有效；代价是「中序严格递增」退化成恒真，
//    少一层校验。E2 / E5 / E7 / E12b 会记 SKIP。
//
//  【前提】本测试用「中序遍历」看树的形状，所以 namespace BST 的
//  preOrder / inOrder / posOrderTwoStack 必须是真实现（空函数体会全部 FAIL），
//  update 也不能是空函数体（它的返回值会被断言）。
//
//  【判读方法】
//    正常结束 → 会打印 "---- N passed / M failed ----"
//    进程中途消失 → 该用例**没有**输出 PASS/FAIL 行，说明是崩溃不是断言失败
//    进程卡住不返回 → 死循环（遍历没往前走）
// ============================================================

#include "BST.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

using Tree = BST::BinarySearch<int>;

// ============================================================
//  G 节专用：TreeNode 级分配追踪
//  目的：验证 erase 到底把**哪个**节点还给了系统。
//
//  做法：分配时按大小认出 TreeNode 的块，把地址登记进 gLive；
//        释放时先查表确认这个指针确实是我们登记过的节点，是就记一笔，
//        最后原样 std::free(p) —— 全程不修改指针的值。
//
//  【为什么不用"给指针加 16 字节头、free 时减回去"】
//    那套方案在 Windows/MinGW 下会崩（实测 c0000374 堆损坏）：
//    动态库里另有自己的分配路径，同一个程序里并非所有指针都出自本文件
//    重载的 operator new，于是 p-16 就成了非法地址。
//    查表方案不改变任何指针语义，对同程序的其它代码完全透明。
// ============================================================
static long       gNodeNew = 0;
static long       gNodeDel = 0;
static const void* gFreed[512];
static int        gFreedN = 0;

static const void* gLive[4096];     // 登记在册的存活节点地址
static int         gLiveN = 0;

static void freedReset() { gFreedN = 0; }
static bool wasFreed(const void* p) {
    for (int i = 0; i < gFreedN; ++i) if (gFreed[i] == p) return true;
    return false;
}

static bool forgetNode(const void* p) {
    for (int i = 0; i < gLiveN; ++i) {
        if (gLive[i] == p) { gLive[i] = gLive[--gLiveN]; return true; }
    }
    return false;
}

// 登记逻辑直接内联在这里：包一层 rememberNode() 会让 GCC 在 -O0 下
// 误报 'p' may be used uninitialized（把 p 传给 const void* 参数时）。
void* operator new(std::size_t n) {
    void* p = std::malloc(n);
    if (p == nullptr) throw std::bad_alloc();
    if (n == sizeof(BST::TreeNode<int>)) {
        ++gNodeNew;
        if (gLiveN < 4096) gLive[gLiveN++] = p;
    }
    return p;
}
void* operator new[](std::size_t n) { return operator new(n); }

static void noteDelete(void* p) {
    if (!p) return;
    if (forgetNode(p)) {                       // 只有登记过的节点才计数
        ++gNodeDel;
        if (gFreedN < 512) gFreed[gFreedN++] = p;
    }
    std::free(p);
}
void operator delete(void* p) noexcept { noteDelete(p); }
void operator delete[](void* p) noexcept { noteDelete(p); }
void operator delete(void* p, std::size_t) noexcept { noteDelete(p); }
void operator delete[](void* p, std::size_t) noexcept { noteDelete(p); }

static char gMsg[256];
static void msgf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(gMsg, sizeof(gMsg), fmt, ap);
    va_end(ap);
}

// ---------------- 用例统计 ----------------
static int  gPass = 0;
static int  gFail = 0;
static int  gSkip = 0;
static bool gTravEmpty   = false;   // 最近一次 capture 是否一个字都没输出
static bool gTravMissing = false;   // 三个遍历是否都没实现（由 probeTraversal 判定）

static void check(const char* id, const char* what, bool ok) {
    // 遍历没实现时，形状断言失败往往只是"读不到内容"，不是树错了 → 记 SKIP
    if (!ok && gTravMissing && gTravEmpty) {
        std::printf("  [SKIP] %-5s %s   ← 遍历无输出，形状无法判定\n", id, what);
        ++gSkip;
        gTravEmpty = false;
        return;
    }
    gTravEmpty = false;
    std::printf("  [%s] %-5s %s\n", ok ? "PASS" : "FAIL", id, what);
    if (ok) ++gPass; else ++gFail;
}

// ---------------- 用例筛选 ----------------
static int    gArgc = 0;
static char** gArgv = nullptr;

static bool want(const char* id) {
    if (gArgc < 2) return true;
    for (int i = 1; i < gArgc; ++i) {
        if (std::strcmp(gArgv[i], id) == 0) return true;                    // 精确命中用例，如 D4
        if (std::strlen(gArgv[i]) == 1 && gArgv[i][0] == id[0]) return true; // 命中整节，如 D
    }
    return false;
}

static bool wantSec(char sec) {
    if (gArgc < 2) return true;
    for (int i = 1; i < gArgc; ++i)
        if (gArgv[i][0] == sec) return true;                                // "D" 和 "D4" 都算 D 节
    return false;
}

// 只认精确用例名，不靠节名命中；不写参数时**不跑**。
// 给"有卡死风险"的用例用：跑 ./BST_iter_test E 不会误踩，必须显式点名 E8 才执行。
static bool wantExact(const char* id) {
    if (gArgc < 2) return false;
    for (int i = 1; i < gArgc; ++i)
        if (std::strcmp(gArgv[i], id) == 0) return true;
    return false;
}

// ---------------- 工具 ----------------
// 把任意输出里的整数抽成 "3,5,8" 这种序列，这样不管遍历用什么分隔符都不会误判
static std::string intsOf(const std::string& s) {
    std::string out;
    const char* p = s.c_str();
    char* end = nullptr;
    while (*p) {
        long v = std::strtol(p, &end, 10);
        if (end == p) { ++p; continue; }
        if (!out.empty()) out += ',';
        out += std::to_string(v);
        p = end;
    }
    return out;
}

static std::string capture(Tree& t, char which) {
    std::ostringstream oss;
    std::streambuf* old = std::cout.rdbuf(oss.rdbuf());
    if      (which == 'P') t.preOrder();
    else if (which == 'I') t.inOrder();
    else if (which == 'O') t.posOrderTwoStack();    // 双栈版后序
    else if (which == 'S') t.posOrderOndStack();    // 单栈版后序（用户改名：Onde → Ond）
    std::cout.rdbuf(old);
    std::string r = intsOf(oss.str());
    gTravEmpty = r.empty();
    return r;
}

// 想单独测 inOrder 时用它：noI 模式下直接返回空，让断言降级成 SKIP
static bool gSafeNoIn = false;   // noI：绕开会死循环的 inOrder
static std::string captureI(Tree& t) {
    if (gSafeNoIn) { gTravEmpty = true; return std::string(); }
    return capture(t, 'I');
}

// ---------------- 能力探测：遍历到底实现没有 ----------------
// 三个遍历如果一个数字都不输出，就认为「还没实现」。
// 这时依赖中序序列的形状断言一律记 SKIP —— 否则你会看到一片假失败。
static void probeTraversal() {
    Tree t;
    t.insert(10); t.insert(5); t.insert(15);
    const bool anyOut = !capture(t, 'P').empty()
                     || (gSafeNoIn ? false : !capture(t, 'I').empty())
                     || !capture(t, 'O').empty();
    gTravMissing = !anyOut;
    gTravEmpty = false;
}

static std::vector<int> csv(const std::string& s) {
    std::vector<int> v;
    const char* p = s.c_str();
    char* end = nullptr;
    while (*p) {
        long x = std::strtol(p, &end, 10);
        if (end == p) { ++p; continue; }
        v.push_back(static_cast<int>(x));
        p = end;
    }
    return v;
}

static bool strictInc(const std::vector<int>& v) {
    for (std::size_t i = 1; i < v.size(); ++i)
        if (v[i - 1] >= v[i]) return false;
    return true;
}

static int countOf(const std::vector<int>& v, int x) {
    int n = 0;
    for (int e : v) if (e == x) ++n;
    return n;
}

// 用中序序列代表「整棵树的内容」：BST 的中序必须是严格递增的
//
// 【noI 安全模式】如果 inOrder 写坏了（死循环），整个测试会在第一句就卡死，
// 这时加命令行参数 noI 绕开它：改用「前序输出 + 排序」来代表整棵树的内容。
// 对一棵合法 BST，排序后的遍历结果就等于中序序列，所以键集合 / 节点数 / 有没丢值
// 这些断言依然有效；但「中序严格递增」这条不变式会退化成恒真，会少一层校验。
static std::string snapshot(Tree& t) {
    if (!gSafeNoIn) return capture(t, 'I');
    std::vector<int> v = csv(capture(t, 'P'));
    std::sort(v.begin(), v.end());
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) out += ',';
        out += std::to_string(v[i]);
    }
    return out;
}

static void build(Tree& t, std::initializer_list<int> xs) {
    for (int x : xs) t.insert(x);
}

// 不依赖遍历的键集合检查：这些键必须都能找到 / 必须都找不到
static bool hasAll(Tree& t, std::initializer_list<int> mustHave) {
    for (int x : mustHave) if (!t.find(x)) return false;
    return true;
}
static bool hasNone(Tree& t, std::initializer_list<int> mustGone) {
    for (int x : mustGone) if (t.find(x)) return false;
    return true;
}

// 参考树故意做成不规则形状 —— 规则形状的小树会掩盖遍历/删除类 bug
static const int REF[] = {10, 5, 15, 3, 8, 12, 18};
static const int REFN = 7;
static const char* REF_IN = "3,5,8,10,12,15,18";

// ============================================================
//  A. insert —— 建树
// ============================================================
static void secA() {
    if (!wantSec('A')) return;
    std::printf("A. insert —— 建树\n");

    if (want("A1")) {
        Tree t;
        t.insert(5);
        check("A1", "空树 insert(5) 后 find(5) 非空", t.find(5) != nullptr);
    }
    if (want("A2")) {
        Tree t;
        build(t, {5, 3, 8});
        check("A2", "插 5/3/8 后三个都找得到",
              t.find(5) && t.find(3) && t.find(8));
    }
    if (want("A3")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        check("A3", "7 个乱序键 → 中序 = 3,5,8,10,12,15,18",
              snapshot(t) == "3,5,8,10,12,15,18");
    }
    if (want("A4")) {
        Tree t;
        build(t, {30, 30});
        check("A4", "重复插 30 不产生第二个 30", countOf(csv(snapshot(t)), 30) == 1);
    }
    if (want("A5")) {
        Tree t;
        for (int i = 1; i <= 10; ++i) t.insert(i);
        std::vector<int> v = csv(snapshot(t));
        check("A5", "递增序列 1..10 → 节点数 10 且中序严格递增",
              v.size() == 10 && strictInc(v));
    }
    if (want("A6")) {
        Tree t;
        for (int i = 10; i >= 1; --i) t.insert(i);
        std::vector<int> v = csv(snapshot(t));
        check("A6", "递减序列 10..1 → 节点数 10 且中序严格递增",
              v.size() == 10 && strictInc(v));
    }
    if (want("A7")) {
        Tree t;
        check("A7", "空树的中序为空",
              snapshot(t).empty() && (!gTravMissing || gSafeNoIn));
    }
}

// ============================================================
//  B. find —— 查找
// ============================================================
static void secB() {
    if (!wantSec('B')) return;
    std::printf("B. find —— 查找\n");

    if (want("B1")) {
        Tree t;
        check("B1", "空树 find(1) == nullptr", t.find(1) == nullptr);
    }
    if (want("B2")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        bool ok = true;
        for (int i = 0; i < REFN; ++i) {
            BST::TreeNode<int>* p = t.find(REF[i]);
            if (!p || p->val != REF[i]) ok = false;
        }
        check("B2", "参考树 7 个键全找得到，且返回节点的 val 正确", ok);
    }
    if (want("B3")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        check("B3", "find(999) == nullptr", t.find(999) == nullptr);
    }
    if (want("B4")) {
        Tree t;
        build(t, {10, 5});
        BST::TreeNode<int>* p = t.find(5);
        check("B4", "只挂在左子树上的 5 找得到", p && p->val == 5);
    }
    if (want("B5")) {
        Tree t;
        build(t, {10, 15});
        BST::TreeNode<int>* p = t.find(15);
        check("B5", "只挂在右子树上的 15 找得到", p && p->val == 15);
    }
    if (want("B6")) {
        Tree t;
        check("B6", "空树 find(INT_MIN) 不崩、返回 nullptr", t.find(-2147483647 - 1) == nullptr);
    }
}

// ============================================================
//  C. update —— 改键
// ============================================================
static void secC() {
    if (!wantSec('C')) return;
    std::printf("C. update —— 改键\n");

    if (want("C1")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        bool ret = t.update(8, 9);
        check("C1a", "update(8,9) 返回 true", ret);
        check("C1b", "9 进来了、8 走了", t.find(9) != nullptr && t.find(8) == nullptr);
        check("C1c", "节点数不变（7）且中序 = 3,5,9,10,12,15,18",
              csv(snapshot(t)).size() == 7 && snapshot(t) == "3,5,9,10,12,15,18");
    }
    if (want("C2")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        t.update(3, 20);
        check("C2", "把最小值 3 改成 20 → 20 成为最大值，中序 5,8,10,12,15,18,20",
              snapshot(t) == "5,8,10,12,15,18,20");
    }
    if (want("C3")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        bool ret = t.update(999, 1);
        check("C3a", "update 不存在的键返回 false", ret == false);
        check("C3b", "树一点没动（中序不变）", snapshot(t) == REF_IN);
    }
    if (want("C4")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        bool ret = t.update(8, 10);          // 目标键 10 已被别的键占着 → 应拒绝
        check("C4a", "update(8,10) 返回 false（拒绝更新）", ret == false);
        check("C4b", "树一点没动：8 和 10 都在，中序 = 3,5,8,10,12,15,18",
              t.find(8) != nullptr && snapshot(t) == REF_IN);
        check("C4c", "不产生第二个 10，节点数仍是 7", countOf(csv(snapshot(t)), 10) == 1);
    }
    if (want("C5")) {
        Tree t;
        bool ret = t.update(1, 2);
        check("C5", "空树 update 返回 false 且仍为空", ret == false && snapshot(t).empty());
    }
    if (want("C6")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        bool ret = t.update(10, 11);         // 改的正好是根，走 erase 双子节点分支
        check("C6a", "改根返回 true", ret);
        check("C6b", "中序 = 3,5,8,11,12,15,18",
              snapshot(t) == "3,5,8,11,12,15,18");
        check("C6c", "节点数仍是 7", csv(snapshot(t)).size() == 7);
    }
    if (want("C7")) {
        Tree t;
        build(t, {50, 30, 70, 20, 40, 60, 80});
        t.update(30, 35);
        t.update(35, 33);
        t.update(70, 65);
        std::vector<int> v = csv(snapshot(t));
        check("C7", "连续改 3 次后仍严格递增且节点数不变（7）",
              strictInc(v) && v.size() == 7);
    }
    if (want("C8")) {
        // oldVal 和 newVal 是同一个键：既不是"旧键不存在"，也不该被当成
        // "newVal 已被占用"而拒绝。它必须走等价分支 → 成功且不动树。
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        bool ret = t.update(8, 8);
        check("C8a", "update(8,8) 返回 true（同一个键，无需改动）", ret == true);
        check("C8b", "树一点没动，中序 = 3,5,8,10,12,15,18", snapshot(t) == REF_IN);
        check("C8c", "没有多出节点，仍是 7 个", csv(snapshot(t)).size() == 7);
    }
}

// ============================================================
//  D. erase —— 删除   <<< 崩溃高发区，建议一个一个跑
// ============================================================
static void secD() {
    if (!wantSec('D')) return;
    std::printf("D. erase —— 删除  <<< 这个函数现在会崩，建议 BST_iter_test D1 / D2 / ... 分开跑\n");

    if (want("D1")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        std::printf("  >> D1 删叶子 3\n");
        bool ret = t.erase(3);
        check("D1a", "删叶子 3 返回 true", ret);
        check("D1b", "find(3) == nullptr", t.find(3) == nullptr);
        check("D1c", "其余 6 个键都还在，中序 = 5,8,10,12,15,18",
              snapshot(t) == "5,8,10,12,15,18");
    }
    if (want("D2")) {
        Tree t;
        build(t, {10, 5, 3});                // 5 只有左孩子 3
        std::printf("  >> D2 删只有左孩子的 5\n");
        bool ret = t.erase(5);
        check("D2a", "删只有左孩子的 5 返回 true", ret);
        check("D2b", "5 走了、3 顶上来了", t.find(5) == nullptr && t.find(3) != nullptr);
        check("D2c", "中序 = 3,10", snapshot(t) == "3,10");
    }
    if (want("D3")) {
        Tree t;
        build(t, {10, 5, 8});                // 5 只有右孩子 8
        std::printf("  >> D3 删只有右孩子的 5\n");
        bool ret = t.erase(5);
        check("D3a", "删只有右孩子的 5 返回 true", ret);
        check("D3b", "5 走了、8 顶上来了", t.find(5) == nullptr && t.find(8) != nullptr);
        check("D3c", "中序 = 8,10", snapshot(t) == "8,10");
    }
    if (want("D4")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        std::printf("  >> D4 删双子节点 5（左右是 3 和 8）\n");
        bool ret = t.erase(5);
        check("D4a", "删双子节点 5 返回 true", ret);
        check("D4b", "中序 = 3,8,10,12,15,18", snapshot(t) == "3,8,10,12,15,18");
        check("D4c", "节点数 7 → 6", csv(snapshot(t)).size() == 6);
    }
    if (want("D5")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        std::printf("  >> D5 删根 10（双子）\n");
        bool ret = t.erase(10);
        check("D5a", "删根 10 返回 true", ret);
        check("D5b", "中序 = 3,5,8,12,15,18", snapshot(t) == "3,5,8,12,15,18");
        check("D5c", "原来的根 10 不在了", t.find(10) == nullptr);
        check("D5d", "整棵树没丢，15 还在", t.find(15) != nullptr);
    }
    if (want("D6")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        std::printf("  >> D6 删不存在的 999\n");
        bool ret = t.erase(999);
        check("D6a", "删不存在的键返回 false", ret == false);
        check("D6b", "树一点没动", snapshot(t) == REF_IN);
    }
    if (want("D7")) {
        Tree t;
        t.insert(42);
        std::printf("  >> D7 删掉唯一的根\n");
        bool ret = t.erase(42);
        check("D7a", "删唯一的根返回 true", ret);
        check("D7b", "删到空树：find(42)==nullptr 且中序为空",
              t.find(42) == nullptr && snapshot(t).empty());
    }
    if (want("D8")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        std::printf("  >> D8 把 7 个键依次全删掉\n");
        int ok = 0;
        for (int i = 0; i < REFN; ++i) if (t.erase(REF[i])) ++ok;
        check("D8a", "7 次 erase 全部返回 true", ok == 7);
        check("D8b", "删到空树：中序为空", snapshot(t).empty());
    }
    if (want("D9")) {
        // 这个形状专门打「找后继时把右子树左脊柱写坏」的 bug
        Tree t;
        build(t, {10, 5, 15, 12});
        std::printf("  >> D9 10/(5, 15->12) 删 10，后继是 12\n");
        bool ret = t.erase(10);
        check("D9a", "删 10 返回 true", ret);
        check("D9b", "15 不能被丢掉（中序应为 5,12,15）", snapshot(t) == "5,12,15");
        check("D9c", "find(15) 仍能找到", t.find(15) != nullptr);
    }
    if (want("D10")) {
        // 单链形状：全是右孩子
        Tree t;
        build(t, {1, 2, 3, 4, 5});
        std::printf("  >> D10 右链 1-2-3-4-5 依次删光\n");
        bool ok = true;
        for (int i = 1; i <= 5; ++i) if (!t.erase(i)) ok = false;
        check("D10", "右链 5 个节点全部删掉且树空", ok && snapshot(t).empty());
    }
}

// ============================================================
//  E. 三种遍历的顺序
// ============================================================
static void secE() {
    if (!wantSec('E')) return;
    std::printf("E. 遍历顺序\n");

    if (want("E1")) {
        Tree t;
        build(t, {10, 5, 15});
        check("E1", "小树 preOrder = 10,5,15", capture(t, 'P') == "10,5,15");
    }
    if (want("E2")) {
        Tree t;
        build(t, {10, 5, 15});
        check("E2", "小树 inOrder = 5,10,15", captureI(t) == "5,10,15");
    }
    if (want("E3")) {
        Tree t;
        build(t, {10, 5, 15});
        check("E3", "小树 双栈后序 = 5,15,10", capture(t, 'O') == "5,15,10");
    }
    if (want("E4")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        check("E4", "参考树 preOrder = 10,5,3,8,15,12,18",
              capture(t, 'P') == "10,5,3,8,15,12,18");
    }
    if (want("E5")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        check("E5", "参考树 inOrder = 3,5,8,10,12,15,18",
              captureI(t) == "3,5,8,10,12,15,18");
    }
    if (want("E6")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        check("E6", "参考树 双栈后序 = 3,8,5,12,18,15,10",
              capture(t, 'O') == "3,8,5,12,18,15,10");
    }
    if (want("E7")) {
        Tree t;
        check("E7", "空树 preOrder / inOrder / 双栈后序 都不输出",
              capture(t, 'P').empty() && captureI(t).empty()
              && capture(t, 'O').empty() && (!gTravMissing || gSafeNoIn));
    }
    if (wantExact("E8")) {
        // ⚠ 单栈版后序，有死循环风险 → 只在显式点名时跑：outDebug.exe E8
        Tree t;
        build(t, {10, 5, 15});
        check("E8", "小树 单栈后序 = 5,15,10", capture(t, 'S') == "5,15,10");
    }
    if (wantExact("E9")) {
        // ⚠ 同上。两个后序必须给出完全一样的序列，不一致就说明其中一个错了
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        std::string two = capture(t, 'O');
        std::string one = capture(t, 'S');
        check("E9a", "参考树 单栈后序 = 3,8,5,12,18,15,10",
              one == "3,8,5,12,18,15,10");
        check("E9b", "单栈与双栈结果一致", one == two);
    }
    if (want("E10")) {
        Tree t;
        check("E10", "空树 双栈后序 不输出也不崩", capture(t, 'O').empty());
    }
    if (wantExact("E11")) {
        // ⚠ 空树 + 单栈版：根就是 nullptr，若实现里第一步无条件 stack.push(root) 会解引用空指针
        Tree t;
        check("E11", "空树 单栈后序 不输出也不崩", capture(t, 'S').empty());
    }
    if (wantExact("E12")) {
        // ⚠ 双栈后序在"有叶子被压栈"的树上会崩（当前实现里推了 right 而不是 left），
        //   所以只在显式点名时跑：outDebug.exe E12
        Tree t;
        build(t, {1, 2, 3, 4, 5});
        check("E12a", "右链 前序 = 1,2,3,4,5", capture(t, 'P') == "1,2,3,4,5");
        check("E12b", "右链 中序 = 1,2,3,4,5", captureI(t) == "1,2,3,4,5");
        check("E12c", "右链 双栈后序 = 5,4,3,2,1", capture(t, 'O') == "5,4,3,2,1");
    }
    if (wantExact("E13")) {
        // ⚠ 单栈版
        Tree t;
        build(t, {1, 2, 3, 4, 5});
        check("E13", "右链 单栈后序 = 5,4,3,2,1", capture(t, 'S') == "5,4,3,2,1");
    }
}

// ============================================================
//  F. 综合 / 规模
// ============================================================
static void secF() {
    if (!wantSec('F')) return;
    std::printf("F. 综合 / 规模\n");

    if (want("F1")) {
        Tree t;
        std::vector<int> xs;
        unsigned s = 12345u;
        for (int i = 0; i < 300; ++i) {
            s = s * 1103515245u + 12345u;
            xs.push_back(static_cast<int>((s >> 16) % 10000));
        }
        for (int x : xs) t.insert(x);
        std::vector<int> want_sorted = xs;
        std::sort(want_sorted.begin(), want_sorted.end());
        want_sorted.erase(std::unique(want_sorted.begin(), want_sorted.end()), want_sorted.end());
        std::vector<int> got = csv(snapshot(t));
        check("F1a", "300 个伪随机键插入后中序严格递增", strictInc(got));
        check("F1b", "中序 == 输入去重排序后的结果", csv(snapshot(t)) == want_sorted);
    }
    if (want("F2")) {
        Tree t;
        for (int i = 0; i < 60; ++i) t.insert(i * 7 % 101);
        std::printf("  >> F2 从 60 个键里删掉一半\n");
        int expect = static_cast<int>(csv(snapshot(t)).size());
        for (int i = 0; i < 60; i += 2) t.erase(i * 7 % 101);
        std::vector<int> v = csv(snapshot(t));
        check("F2a", "删掉一半后中序仍严格递增", strictInc(v));
        check("F2b", "节点数确实减少了",
              static_cast<int>(csv(snapshot(t)).size()) < expect);
    }
}

// ============================================================
//  H. 键集合校验 —— 不依赖遍历，遍历没实现时这几项照样有效
// ============================================================
static void secH() {
    if (!wantSec('H')) return;
    std::printf("H. 键集合校验（不需要遍历）\n");

    if (want("H1")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        t.erase(3);
        check("H1", "删叶子 3：3 没了，其余 6 个键都在",
              hasNone(t, {3}) && hasAll(t, {5, 8, 10, 12, 15, 18}));
    }
    if (want("H2")) {
        Tree t;
        build(t, {10, 5, 3});                 // 5 只有左孩子
        t.erase(5);
        check("H2", "删只有左孩子的 5：5 没了，3 和 10 都在（孩子不能被误删）",
              hasNone(t, {5}) && hasAll(t, {3, 10}));
    }
    if (want("H3")) {
        Tree t;
        build(t, {10, 5, 8});                 // 5 只有右孩子
        t.erase(5);
        check("H3", "删只有右孩子的 5：5 没了，8 和 10 都在（孩子不能被误删）",
              hasNone(t, {5}) && hasAll(t, {8, 10}));
    }
    if (want("H4")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        t.erase(5);
        check("H4", "删双子节点 5：5 没了，3/8/10/12/15/18 都在",
              hasNone(t, {5}) && hasAll(t, {3, 8, 10, 12, 15, 18}));
    }
    if (want("H5")) {
        Tree t;
        build(t, {10, 5, 15, 12});            // 后继 12 藏在下一层，专打那个循环
        t.erase(10);
        check("H5", "删根 10（后继 12 在下一层）：10 没了，5/12/15 都在，15 不能被丢掉",
              hasNone(t, {10}) && hasAll(t, {5, 12, 15}));
    }
    if (want("H6")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        int order[] = {10, 5, 15, 3, 8, 12, 18};
        int n = 0;
        for (int i = 0; i < 7; ++i) if (t.erase(order[i])) ++n;
        check("H6", "7 个键依次删光：erase 全返回 true 且一个都找不到",
              n == 7 && hasNone(t, {10, 5, 15, 3, 8, 12, 18}));
    }
}

// ============================================================
//  I. 规模 / 深度 —— 专门验证「非递归」这个设计目标本身
//  递增序列会退化成一条右链，深度就等于节点数。
//  insert / find / 遍历如果还藏着递归，或者析构是递归的，这种形状下就会爆栈。
// ============================================================
static void secI() {
    if (!wantSec('I')) return;
    std::printf("I. 规模 / 深度（右链退化树，深度 = 节点数）\n");

    if (want("I1")) {
        const int N = 50000;
        std::printf("  >> 建 %d 个递增键，树退化成一条右链\n", N);
        {
            Tree t;
            for (int i = 0; i < N; ++i) t.insert(i);
            check("I1", "最深处 find(49999) 得到", t.find(N - 1) != nullptr);
            check("I2", "前序输出节点数 = 50000（遍历没漏没重）",
                  csv(capture(t, 'P')).size() == static_cast<std::size_t>(N));
            std::printf("  >> 离开作用域，开始析构\n");
        }
        // 这一行能打印出来，说明析构没爆栈 —— 递归析构在这个深度必挂
        check("I3", "析构后进程还活着（递归析构撑不住这个深度）", true);
    }
}

// ============================================================
//  J. 泛型 / 比较器 —— 换 T 与换 Compare，同一套代码怎么变
//
//  T 决定"存什么"，Compare 决定"谁该排在前面"。两者都从模板参数进来，
//  类里一行 if 都不用改。三种给自定义类型定义比较的办法各验一遍：
//    路1 成员 operator<        → BinarySearch<Order>          (J4)
//    路2 非成员 operator<      → BinarySearch<Money>          (J7)
//    路3 外部比较器（仿函数）  → BinarySearch<Order, ByPrice> (J5/J6)
//
//  最硬的是 J2/J3/J8/J9：把 Compare 换成 std::greater<int>，整棵树必须降序、
//  连形状都镜像。只要实现里还残留硬编码的 < / >，这里立刻 FAIL ——
//  这条就是用来守"Compare 变成死参数"的（BSTRE 里真出过一次，
//  比较写的是 node->val > val，Compare 传进去完全没用）。
//
//  两套实现都在 BST.hpp 里，所以本节把递归版（J8）也一起验了。
// ============================================================

// —— 路1：成员 operator<（类型是自己写的，首选）——
struct Order {
    int    id;
    double price;
    bool operator<(const Order& o) const { return id < o.id; }   // const 不能省
};
// 打印成 1/20 这样，id 和 price 一起看，顺序对不对一眼能看出来
static std::ostream& operator<<(std::ostream& os, const Order& o) {
    return os << o.id << '/' << o.price;
}

// —— 路2：非成员 operator< ——
struct Money { long cents; };
static bool operator<(const Money& a, const Money& b) { return a.cents < b.cents; }
static std::ostream& operator<<(std::ostream& os, const Money& m) { return os << m.cents; }

// —— 路3：外部比较器。改不了 Order，或同一个类型想按另一个字段排 ——
// 它跟 std::less<T> 是同一类东西：空类，只提供一个能调用的 operator()
struct ByPrice {
    bool operator()(const Order& a, const Order& b) const { return a.price < b.price; }
};

// 把任意一棵树的某次遍历输出抓成 "a,b,c"。
// 用 lambda 传遍历动作，而不是在这里写 if/else 调 preOrder/inOrder/...：
// 模板函数体是整体实例化的，若写死 t.posOrderTwoStack() 这类调用，
// BSTRE 那套没有这个方法的类型一进来就编译不过。
template <typename Fn>
static std::string capFn(Fn fn) {
    std::ostringstream oss;
    std::streambuf* old = std::cout.rdbuf(oss.rdbuf());
    fn();
    std::cout.rdbuf(old);
    std::istringstream iss(oss.str());
    std::string w, out;
    while (iss >> w) { if (!out.empty()) out += ','; out += w; }
    gTravEmpty = out.empty();           // 接上 check() 的「遍历无输出记 SKIP」逻辑
    return out;
}

// "a,b,c" → 3（数逗号，比再解析一遍省事）
static std::size_t wordsOf(const std::string& s) {
    if (s.empty()) return 0;
    std::size_t n = 1;
    for (std::size_t i = 0; i < s.size(); ++i) if (s[i] == ',') ++n;
    return n;
}

static void secJ() {
    if (!wantSec('J')) return;
    std::printf("J. 泛型 / 比较器（换 T、换 Compare）\n");

    // —— J1 换 T：std::string。重复插 "apple" 只留一个 ——
    //  判等靠的是「两个方向都不小于」，不是 ==（std::less 里根本没有 ==）
    if (want("J1")) {
        BST::BinarySearch<std::string> t;
        t.insert("banana"); t.insert("apple"); t.insert("cherry"); t.insert("apple");
        const std::string in  = capFn([&]{ t.inOrder(); });
        const std::string pre = capFn([&]{ t.preOrder(); });
        msgf("BinarySearch<string> 中序 = %s，节点数 = %zu（apple 只留一个）",
             in.c_str(), wordsOf(pre));
        check("J1", gMsg, in == "apple,banana,cherry" && wordsOf(pre) == 3);
    }

    // —— J2/J3 换 Compare：greater<int> → 中序降序、形状镜像 ——
    if (want("J2")) {
        BST::BinarySearch<int, std::greater<int>> t;
        t.insert(10); t.insert(5); t.insert(15); t.insert(3); t.insert(12);
        const std::string in = capFn([&]{ t.inOrder(); });
        msgf("BinarySearch<int, greater<int>> 中序 = %s", in.c_str());
        check("J2", gMsg, in == "15,12,10,5,3");        // 降序 = Compare 确实生效了
    }
    if (want("J3")) {
        BST::BinarySearch<int, std::greater<int>> t;
        t.insert(5); t.insert(3); t.insert(8);
        const std::string pre = capFn([&]{ t.preOrder(); });
        msgf("同样插 5/3/8，greater 下前序 = %s（less 时是 5,3,8）", pre.c_str());
        check("J3", gMsg, pre == "5,8,3");              // 连形状都反了
    }

    // —— J4 路1：成员 operator< 按 id 排 ——
    if (want("J4")) {
        BST::BinarySearch<Order> t;
        t.insert(Order{2, 30}); t.insert(Order{3, 10}); t.insert(Order{1, 20});
        const std::string in = capFn([&]{ t.inOrder(); });
        msgf("BinarySearch<Order>（成员 operator< 按 id）中序 = %s", in.c_str());
        check("J4", gMsg, in == "1/20,2/30,3/10");
    }

    // —— J5 路3：同一批数据、同一个类型，只换比较器 → 另一种顺序 ——
    if (want("J5")) {
        BST::BinarySearch<Order, ByPrice> t;
        t.insert(Order{2, 30}); t.insert(Order{3, 10}); t.insert(Order{1, 20});
        const std::string in = capFn([&]{ t.inOrder(); });
        msgf("BinarySearch<Order, ByPrice>（按 price）中序 = %s", in.c_str());
        check("J5", gMsg, in == "3/10,1/20,2/30");
    }

    // —— J6 find 也必须走同一个比较器，不能偷偷用 == ——
    if (want("J6")) {
        BST::BinarySearch<Order, ByPrice> t;
        t.insert(Order{2, 30}); t.insert(Order{3, 10}); t.insert(Order{1, 20});
        const bool hit  = (t.find(Order{1, 20}) != nullptr);
        const bool miss = (t.find(Order{1, 99}) == nullptr);   // price 99 不在树里
        msgf("ByPrice 下 find(1/20) 找到 = %s，find(1/99) 找不到 = %s",
             hit ? "是" : "否", miss ? "是" : "否");
        check("J6", gMsg, hit && miss);
    }

    // —— J7 路2：非成员 operator< ——
    if (want("J7")) {
        BST::BinarySearch<Money> t;
        t.insert(Money{250}); t.insert(Money{50}); t.insert(Money{999});
        const std::string in = capFn([&]{ t.inOrder(); });
        msgf("BinarySearch<Money>（非成员 operator<）中序 = %s", in.c_str());
        check("J7", gMsg, in == "50,250,999");
    }

    // —— J8 递归版 BSTRE 也认自定义比较器（守上一轮修的死参数）——
    if (want("J8")) {
        BSTRE::BinarySearch<int, std::greater<int>> t;
        t.insert(10); t.insert(5); t.insert(15);
        const std::string in = capFn([&]{ t.inOrder(); });
        msgf("BSTRE::BinarySearch<int, greater<int>> 中序 = %s", in.c_str());
        check("J8", gMsg, in == "15,10,5");
    }

    // —— J9 自定义比较器下 erase / find 也得同规则 ——
    if (want("J9")) {
        BST::BinarySearch<int, std::greater<int>> t;
        t.insert(10); t.insert(5); t.insert(15); t.insert(3);
        const bool erased = t.erase(5);
        const std::string in = capFn([&]{ t.inOrder(); });
        msgf("greater 下 erase(5) 返回 %s，中序 = %s",
             erased ? "true" : "false", in.c_str());
        check("J9", gMsg, erased && in == "15,10,3");
    }
}

// ============================================================
//  G. 内存 —— erase 到底把哪个节点还给了系统
// ============================================================
static void secG() {
    if (!wantSec('G')) return;
    std::printf("G. 内存（追踪被释放的具体节点）\n");

    if (want("G1")) {
        // 7 个键按「每次都是叶子」的顺序删，理论上 7 个节点逐个归还
        Tree t;
        int xs[] = {10, 5, 15, 3, 8, 12, 18};
        for (int i = 0; i < 7; ++i) t.insert(xs[i]);
        BST::TreeNode<int>* p[7];
        for (int i = 0; i < 7; ++i) p[i] = t.find(xs[i]);

        freedReset();
        int order[] = {3, 18, 8, 12, 5, 15, 10};   // 每次都保证删的是叶子
        bool allTrue = true;
        for (int i = 0; i < 7; ++i) if (!t.erase(order[i])) allTrue = false;

        int freed = 0;
        for (int i = 0; i < 7; ++i) if (wasFreed(p[i])) ++freed;
        msgf("7 个节点都是当叶子删的：erase 全返回 true = %s，真正被释放 %d/7 个",
             allTrue ? "是" : "否", freed);
        check("G1", gMsg, allTrue && freed == 7);
    }

    if (want("G2")) {
        Tree t;
        build(t, {10, 5, 15});
        BST::TreeNode<int>* leaf = t.find(5);
        freedReset();
        const long before = gNodeDel;
        t.erase(5);
        const long d = gNodeDel - before;
        msgf("删叶子 5：本次释放了 %ld 个节点，被释放的正是 5 吗 = %s",
             d, wasFreed(leaf) ? "是" : "不是");
        check("G2", gMsg, d == 1 && wasFreed(leaf));
    }

    if (want("G3")) {
        Tree t;
        build(t, {10, 5, 3});                 // 5 只有左孩子 3
        BST::TreeNode<int>* victim = t.find(5);
        BST::TreeNode<int>* child  = t.find(3);
        freedReset();
        t.erase(5);
        msgf("删只有左孩子的 5：释放的 5 本身 = %s ；误删了孩子 3 = %s",
             wasFreed(victim) ? "是" : "不是", wasFreed(child) ? "是" : "不是");
        check("G3", gMsg, wasFreed(victim) && !wasFreed(child));
    }

    if (want("G4")) {
        Tree t;
        build(t, {10, 5, 15, 3, 8, 12, 18});
        BST::TreeNode<int>* victim = t.find(10);   // 双子节点只搬值，节点本身留着
        BST::TreeNode<int>* succ   = t.find(12);   // 后继，应该被删掉
        freedReset();
        t.erase(10);
        msgf("删双子根 10：后继 12 被释放 = %s ；10 自身被释放 = %s（只搬值，应为否）",
             wasFreed(succ) ? "是" : "不是", wasFreed(victim) ? "是" : "不是");
        check("G4", gMsg, wasFreed(succ) && !wasFreed(victim));
    }

    if (want("G5")) {
        freedReset();
        const long before = gNodeDel;
        {
            Tree t;
            build(t, {10, 5, 15, 3, 8, 12, 18});
        }                                     // ← 出了作用域，析构函数该把整棵树还回去
        const long d = gNodeDel - before;
        msgf("对象析构：7 个节点里有 %ld 个被释放", d);
        check("G5", gMsg, d == 7);
    }
}

// ============================================================
//  唯一入口
//
//  为什么只能有一个 main：run.ps1 把当前目录下所有 *.cpp 一起链接成
//  outDebug.exe，两个 main 就会报
//      multiple definition of `main'; first defined here
//  所以递归版那套测试（BST_test.cpp）把自己的 main 改名成了
//  runRecurTests()，由这里在需要时调用。
//
//  除 runRecurTests 外，两个测试文件里的辅助函数/变量都是 static
//  （内部链接），不会互相冲突。
// ============================================================
int runRecurTests(int argc, char** argv);   // 定义在 BST_test.cpp

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // 关缓冲：崩了/卡住前已打印的内容也能看到
    // 把开关词 "recur" 从参数里摘掉再转交：
    // 摘干净后只剩程序名时，runRecurTests 里的 want() 看到 argc<2 就当作"跑全部"。
    char* rest[64];
    int restN = 1;
    rest[0] = argv[0];
    bool wantRecur = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "recur") == 0) {
            wantRecur = true;
            continue;
        }
        if (std::strcmp(argv[i], "noI") == 0) {      // 绕开会死循环的 inOrder
            gSafeNoIn = true;
            continue;
        }
        if (restN < 63) rest[restN++] = argv[i];
    }
    rest[restN] = nullptr;

    if (wantRecur) {
        return runRecurTests(restN, rest);      // 递归版那一套
    }

    gArgc = restN; gArgv = rest;

    // 标题必须打在 probeTraversal() 之前：
    // probeTraversal() 里会建一棵临时树并在离开作用域时跑析构 ——
    // 析构写坏（比如漏了 stack.pop()）会在那里直接堆损坏崩溃，
    // 而它跑在整程序的第一条输出之前，于是你会看到「编译完成 + 一片空白」。
    std::printf("=== namespace BST（非递归版）测试 ===\n");

    probeTraversal();
    if (gSafeNoIn) {
        gTravMissing = true;    // 必须放在 probeTraversal 之后：否则会被它覆盖
    }

    if (gSafeNoIn)
        std::printf("!! 已启用 noI：不调用 inOrder，形状用「前序+排序」替代（E2/E5/E7/E12b 记 SKIP）\n");
    else if (gTravMissing)
        std::printf("!! 三个遍历都没有任何输出：依赖中序序列的形状断言记 SKIP，不计入失败\n");

    // J 放最最后：它用的是 TreeNode<Order> 这类别的类型，
    // 排在 G（按 sizeof(TreeNode<int>) 认节点、做分配计数）之后跑，互不干扰。
    secA(); secB(); secC(); secE(); secF(); secG(); secH(); secD(); secI(); secJ();  // D 容易崩，放倒数第二

    std::printf("\n---- %d passed / %d failed / %d skipped ----\n", gPass, gFail, gSkip);
    if (gFail == 0)
        std::printf("（提示：没有输出 PASS/FAIL 行的用例 = 进程崩在那里了，不是断言失败）\n");
    return gFail ? 1 : 0;
}
