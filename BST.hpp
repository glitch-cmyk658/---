#pragma once
#include <iostream>
#include <stack>
#include <functional>

namespace BSTRE {
    template<typename T>
    struct TreeNode {
        T val;
        TreeNode<T>* left, * right;
        explicit TreeNode(const T& val) : val(val), left(nullptr), right(nullptr) {}
    };
    template<typename T, typename Compare = std::less<T>>
    class BinarySearch {
    public:
        explicit BinarySearch(Compare c = Compare()) : cmp(c) {}
        BinarySearch(const BinarySearch&) = delete;
        BinarySearch& operator=(const BinarySearch&) = delete;
        void insert(const T& val) { root = insert(root, val); }
        void erase(const T& val) { root = erase(root, val); }
        bool update(const T& oldVal, const T& newVal);
        bool find(const T& val) { return (find(root, val) != nullptr); }
        void preOrder() { preOrder(root); }
        void inOrder() { inOrder(root); }
        void posOrder() { posOrder(root); }
        ~BinarySearch() { destroy(root); }
    private:
        TreeNode<T>* root = nullptr;
        Compare cmp;    // 和 BST 版一样，所有比较都走它
        TreeNode<T>* insert(TreeNode<T>* node, const T& val);
        TreeNode<T>* erase(TreeNode<T>* node, const T& val);
        TreeNode<T>* find(TreeNode<T>* node, const T& val);
        TreeNode<T>* findMin(TreeNode<T>* node);
        void preOrder(TreeNode<T>* node);
        void inOrder(TreeNode<T>* node);
        void posOrder(TreeNode<T>* node);
        void destroy(TreeNode<T>* node);
    };
    template<typename T, typename Compare>
    BSTRE::TreeNode<T>* BSTRE::BinarySearch<T, Compare>::insert(TreeNode<T>* node, const T& val) {
        if (!node) {
            return new TreeNode<T>(val);
        }
        if (cmp(val, node->val)) {
            node->left = insert(node->left, val);
        }
        else if (cmp(node->val, val)) {
            node->right = insert(node->right, val);
        }
        return node;
    }
    template<typename T, typename Compare>
    BSTRE::TreeNode<T>* BSTRE::BinarySearch<T, Compare>::erase(TreeNode<T>* node, const T& val) {
        if (!node) {
            return nullptr;
        }
        if (cmp(val, node->val)) {
            node->left = erase(node->left, val);
        }
        else if (cmp(node->val, val)) {
            node->right = erase(node->right, val);
        }
        else {
            if (!node->left) {
                TreeNode<T>* right = node->right;
                delete node;
                return right;
            }
            if (!node->right) {
                TreeNode<T>* left = node->left;
                delete node;
                return left;
            }
            TreeNode<T>* inherit = findMin(node->right);
            node->val = inherit->val;
            node->right = erase(node->right, inherit->val); // 必须这样，因为如果只是使用了delete，
            // 但是它的父节点还指向着它，父节点的left就成为野指针了。
        }
        return node;
    }
    template<typename T, typename Compare>
    BSTRE::TreeNode<T>* BSTRE::BinarySearch<T, Compare>::findMin(TreeNode<T>* node) {
        while (node->left != nullptr) {
            node = node->left;
        }
        return node;
    }
    template<typename T, typename Compare>
    BSTRE::TreeNode<T>* BSTRE::BinarySearch<T, Compare>::find(TreeNode<T>* node, const T& val) {
        if (!node) {
            return nullptr;
        }
        TreeNode<T>* curNode = node;
        if (cmp(val, node->val)) {
            curNode = find(curNode->left, val);
        }
        else if (cmp(node->val, val)) {
            curNode = find(curNode->right, val);
        }
        return curNode;
    }
    template<typename T, typename Compare>
    bool BSTRE::BinarySearch<T, Compare>::update(const T& oldVal, const T& newVal) {
        if(!find(oldVal)) {
            return false;
        }
        // 和 BST 版同一套语义：先判等价（同一个键 → 成功且不动），
        // 再拒绝"newVal 已被占用"，最后才真的删旧插新。
        if(!cmp(oldVal, newVal) && !cmp(newVal, oldVal)) {
            return true;
        }
        if(find(newVal)) {
            return false;
        }
        root = erase(root, oldVal);
        root = insert(root, newVal);
        return true;
    }
    template<typename T, typename Compare>
    void BSTRE::BinarySearch<T, Compare>::preOrder(TreeNode<T>* node) {
        if(!node) {
            return;
        }
        std::cout << node->val << " ";
        preOrder(node->left);
        preOrder(node->right);
    }
    template<typename T, typename Compare>
    void BSTRE::BinarySearch<T, Compare>::inOrder(TreeNode<T>* node) {
        if(!node) {
            return;
        }
        inOrder(node->left);
        std::cout << node->val << " ";
        inOrder(node->right);
    }
    template<typename T, typename Compare>
    void BSTRE::BinarySearch<T, Compare>::posOrder(TreeNode<T>* node) {
        if(!node) {
            return;
        }
        posOrder(node->left);
        posOrder(node->right);
        std::cout << node->val << " ";
    }
    template<typename T, typename Compare>
    void BSTRE::BinarySearch<T, Compare>::destroy(TreeNode<T>* node) {
        if (!node) {
            return;
        }
        destroy(node->left);
        destroy(node->right);
        delete node;
    }
}
// 没有递归的版本
namespace BST {
    template<typename T>
    struct TreeNode {
        T val;
        TreeNode* left, * right;
        explicit TreeNode(const T& val) : val(val), left(nullptr), right(nullptr) {}
    };
    // T       = 存进去的元素类型
    // Compare = 怎么比较两个 T；默认 std::less<T>，它就等价于 a < b
    template<typename T, typename Compare = std::less<T>>
    class BinarySearch {
    public:
        BinarySearch() = default;
        BinarySearch(const BinarySearch&) = delete;
        BinarySearch& operator=(const BinarySearch&) = delete;
        void insert(const T& val);
        bool erase(const T& val);
        TreeNode<T>* find(const T& val);
        bool update(const T& oldVal, const T& newVal);
        void preOrder();
        void inOrder();
        void posOrderTwoStack();
        void posOrderOndStack();
        ~BinarySearch();
    private:
        TreeNode<T>* root = nullptr;
        Compare cmp;    // 空对象，唯一作用是把"a 该排在 b 前面吗"包成一个能当参数传递的东西
    };

    // 模板的实现必须留在头文件里：编译器要为每一种 T 现场生成代码，
    // 看不到函数体就生成不出来，链接期会报 undefined reference。
    template<typename T, typename Compare>
    void BST::BinarySearch<T, Compare>::insert(const T& val) {
        TreeNode<T>** link = &root;
        while(*link) {
            if(cmp(val, (*link)->val)) {
                link = &(*link)->left;
            }
            else if(cmp((*link)->val, val)) {
                link = &(*link)->right;
            }
            else {
                return;
            }
        }
        *link =  new TreeNode<T>(val);
    }

    template<typename T, typename Compare>
    BST::TreeNode<T>* BST::BinarySearch<T, Compare>::find(const T& val) {
        TreeNode<T>* curNode = root;
        while(curNode) {
            if(cmp(val, curNode->val)) {
                curNode = curNode->left;
            }
            else if(cmp(curNode->val, val)) {
                curNode = curNode->right;
            }
            else {
                return curNode;
            }
        }
        return nullptr;
    }
    template<typename T, typename Compare>
    bool BST::BinarySearch<T, Compare>::erase(const T& val) {
        TreeNode<T>** curNode = &root;
        while(*curNode) {
            if(cmp(val, (*curNode)->val)) {
                curNode = &(*curNode)->left;
            }
            else if(cmp((*curNode)->val, val)) {
                curNode = &(*curNode)->right;
            }
            else {
                break;
            }
        }
        if(!*curNode) {
            return false;
        }
        TreeNode<T>* victim = *curNode;
        if(!(*curNode)->left) {
            *curNode = (*curNode)->right;
        }
        else if(!(*curNode)->right) {
            *curNode = (*curNode)->left;
        }
        else {
            TreeNode<T>** succLink = &(*curNode)->right;
            while((*succLink)->left) {
                succLink = &(*succLink)->left;
            }
            TreeNode<T>* succ = *succLink;
            (*curNode)->val = succ->val;
            (*succLink) = succ->right;
            delete succ;
            return true;
        }
        delete victim;
        return true;
    }
    template<typename T, typename Compare>
    bool BST::BinarySearch<T, Compare>::update(const T& oldVal, const T& newVal) {
        if(find(oldVal) == nullptr) {
            return false;
        }
        // 等价判断必须放在 find(newVal) 之前：否则 update(5,5) 会被当成
        // "newVal 已存在"而拒绝。cmp 两个方向都不小于 = 比较器认为它俩是同一个键。
        if(!cmp(oldVal, newVal) && !cmp(newVal, oldVal)) {
            return true;
        }
        // newVal 已经被别的键占着：erase 之后再 insert 会插不进去，
        // 结果是旧键白丢、新键也没多出来。宁可什么都不做。
        if(find(newVal) != nullptr) {
            return false;
        }
        erase(oldVal);
        insert(newVal);
        return true;
    }
    template<typename T, typename Compare>
    void BST::BinarySearch<T, Compare>::preOrder() {
        if(!root) {
            return;
        }
        std::stack<TreeNode<T>*> stack;
        TreeNode<T>* node = root;
        stack.push(root);
        while(!stack.empty()) {
            node = stack.top();
            stack.pop();
            std::cout << node->val << " ";
            if(node->right != nullptr) {
                stack.push(node->right);
            }
            if(node->left != nullptr) {
                stack.push(node->left);
            }
        }
        std::cout << std::endl;
    }
    template<typename T, typename Compare>
    void BST::BinarySearch<T, Compare>::inOrder() {
        if(root != nullptr) {
            std::stack<TreeNode<T>*> stack;
            TreeNode<T>* node = root;
            while(!stack.empty() || node != nullptr) {
                if(node != nullptr) {
                    stack.push(node);
                    node = node->left;
                }
                else {
                    node = stack.top(); stack.pop();
                    std::cout << node->val << " ";
                    node = node->right;
                }
            }
            std::cout << std::endl;
        }
    }
    template<typename T, typename Compare>
    void BST::BinarySearch<T, Compare>::posOrderTwoStack() {
        if(root != nullptr) {
            TreeNode<T>* node = root;
            std::stack<TreeNode<T>*> stack;
            std::stack<TreeNode<T>*> collect;
            stack.push(root);
            while(!stack.empty()) {
                node = stack.top(); stack.pop();
                collect.push(node);
                if(node->left) {
                    stack.push(node->left);
                }
                if(node->right) {
                    stack.push(node->right);
                }
            }
            TreeNode<T>* temp;
            while(!collect.empty()) {
                temp = collect.top(); collect.pop();
                std::cout << temp->val << " ";
            }
            std::cout << std::endl;
        }
    }
    template<typename T, typename Compare>
    void BST::BinarySearch<T, Compare>::posOrderOndStack() {
        if(!root) {
            return;
        }
        TreeNode<T>* node = root; // 含义是上次打印的节点
        std::stack<TreeNode<T>*> stack;
        stack.push(node);
        while(!stack.empty()) {
            TreeNode<T>* curNode = stack.top();
            if(curNode->left != nullptr && node != curNode->left 
                && node != curNode->right) {
                stack.push(curNode->left);
            }
            else if(curNode->right != nullptr && node != curNode->right) {
                stack.push(curNode->right);
            }
            else {
                std::cout << curNode->val << " ";
                node = stack.top(); stack.pop();
            }
        }
        std::cout << std::endl;
    }
    template<typename T, typename Compare>
    BST::BinarySearch<T, Compare>::~BinarySearch() {
        if(!root) {
            return;
        }
        std::stack<TreeNode<T>*> stack;
        stack.push(root);
        while(!stack.empty()) {
            TreeNode<T>* node = stack.top(); stack.pop();
            if(node->left != nullptr) {
                stack.push(node->left);
            }
            if(node->right != nullptr) {
                stack.push(node->right);
            }
            delete node;
        }
    }
}
