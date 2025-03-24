#pragma once
#include<iostream>
#include<string>

namespace fcngraph{

class AbstractNode{
public:
    AbstractNode(){}
    virtual std::string getNodeName()const = 0;
    virtual ~AbstractNode() = 0;

};

class InputNode : public AbstractNode{
public:
    InputNode(std::string _name){
        node_name = _name;
    }

    std::string getNodeName() const{
        return node_name;
    }

    ~InputNode(){}
private:
    std::string node_name;
};

class OutputNode : public AbstractNode{
public:
    OutputNode(std::string _name){
        node_name = _name;
    }

     std::string getNodeName()const{
        return node_name;
    }

    ~OutputNode(){}
private:
    std::string node_name;
};

class RedundancyNode : public AbstractNode{
public:
    RedundancyNode(std::string _name){
        node_name = _name;
    }

     std::string getNodeName()const{
        return node_name;
    }

    ~RedundancyNode(){}
private:
    std::string node_name;
};


class MajNode : public AbstractNode{
public:
    MajNode(std::string _name){
        node_name = _name;
    }

     std::string getNodeName()const{
        return node_name;
    }

    ~MajNode(){}
private:
    std::string node_name;
};

class AndNode : public AbstractNode{
public:
    AndNode(std::string _name){
        node_name = _name;
    }

     std::string getNodeName()const{
        return node_name;
    }

    ~AndNode(){}
private:
    std::string node_name;
};

class OrNode : public AbstractNode{
public:
    OrNode(std::string _name){
        node_name = _name;
    }

     std::string getNodeName()const{
        return node_name;
    }

    ~OrNode(){}
private:
    std::string node_name;
};

class NotNode : public AbstractNode{
public:
    NotNode(std::string _name){
        node_name = _name;
    }

     std::string getNodeName()const{
        return node_name;
    }

    ~NotNode(){}
private:
    std::string node_name;
};


class FanoutNode : public AbstractNode{
public:
    FanoutNode(std::string _name){
        node_name = _name;
    }

     std::string getNodeName()const{
        return node_name;
    }

    ~FanoutNode(){}
private:
    std::string node_name;
};

//buffernode可以消除，消除后，将buffernode类型修改为Eliminated_node
class Eliminated_node  : public AbstractNode{
public:
    Eliminated_node(std::string _name){
        node_name = _name;
    }

     std::string getNodeName()const{
        return node_name;
    }

    ~Eliminated_node(){}
private:
    std::string node_name;
};




};