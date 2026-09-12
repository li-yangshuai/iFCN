#pragma once
#include <string>

namespace iFCN_Lab {

enum class NodeType {
    Input,
    Output,
    Maj,
    And,
    Or,
    Not,
    Redundancy,
    Fanout
};

class AbstractNode {
public:
    AbstractNode(const std::string& name, NodeType type)
        : node_name(name), node_type(type) {}

    virtual ~AbstractNode() = default;

    std::string getNodeName() const { return node_name; }
    NodeType getNodeType() const { return node_type; }
    bool isInputNode() const { return node_type == NodeType::Input; }
    bool isOutputNode() const { return node_type == NodeType::Output; }
    // 可扩展更多 isXxxNode() 方法

protected:
    std::string node_name;
    NodeType node_type;
};

// 派生类采用统一写法
#define NODE_CLASS(NAME, TYPE_ENUM) \
class NAME##Node : public AbstractNode { \
public: \
    NAME##Node(const std::string& name) : AbstractNode(name, NodeType::TYPE_ENUM) {} \
    virtual ~NAME##Node() = default; \
};

NODE_CLASS(Input, Input)
NODE_CLASS(Output, Output)
NODE_CLASS(Maj, Maj)
NODE_CLASS(And, And)
NODE_CLASS(Or, Or)
NODE_CLASS(Not, Not)
NODE_CLASS(Redundancy, Redundancy)
NODE_CLASS(Fanout, Fanout)

#undef NODE_CLASS

} // namespace iFCN_Lab
