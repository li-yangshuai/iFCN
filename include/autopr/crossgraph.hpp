#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <map>
#include <set>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <typeinfo>
#include "node.h"

namespace fcngraph {

struct EdgeNode {
    EdgeNode(uint64_t _tailidx, uint64_t _headidx) : tailidx(_tailidx), headidx(_headidx), headlink(nullptr), taillink(nullptr) {}
    uint64_t tailidx; 
    uint64_t headidx;   
    EdgeNode* headlink;
    EdgeNode* taillink;
};

template<typename T>
struct VertexNode {
    VertexNode(const std::shared_ptr<T>& _tempNode) : data(_tempNode), firstin(nullptr), firstout(nullptr), outdegree(0), indegree(0), layer(-1) {}
    std::shared_ptr<T> data;
    uint64_t outdegree;
    uint64_t indegree;
    int layer;

    EdgeNode* firstin;
    EdgeNode* firstout;
};

class Parse;

template<typename T>
class CrossGraphLink {
public:
    CrossGraphLink() : m_numVertices(0), m_numEdges(0), max_layer(0){} 

    ~CrossGraphLink() {
        for (auto &vertex : m_verticesArray) {
            // 释放正向边的内存
            EdgeNode* current = vertex.firstout;
            while (current != nullptr) {
                EdgeNode* next = current->taillink;
                delete current;
                current = next;
            }
        }
    }

    std::string getNodeType(int index);
    void getSameLayerNodeIndex();
    std::vector<int> find_continuous_path();


private:
    // 顶点的设置、插入、检查、搜索
    void setVertexIndex(const T&_vetex);
    void insertVertex(const std::shared_ptr<T>& _tmpv);
    inline bool is_inserted(const std::string _string) const;
    const std::string getVertexName(const int index)const;
    int getVertexIndex(const std::string &_string) const;

    //插入边的两种方式
    void insertEdge(const std::string &_nodeName1, const std::string &_nodeName2);
    void insertEdge(const uint64_t index1,  const uint64_t index2);

    //删除边
    bool removeEdge(const uint64_t index1,  const uint64_t index2);
    //删除node, 实际上不是真正的删除node
    void deleteNode(int index);
    //修改node属性
    bool modifyNodeType(const std::string& nodeName, const std::string& newType);

    //分层算法：分层算法需要获取电路所有输入节点的index和输出节点的index
    void layerDivision(std::set<std::string> &_inputNode,std::set<std::string> &_outputNode);
    void caculateVertexDegree();
    void layerRecursive(std::set<int> &_vertex, std::vector<bool>& _is_traversed, int &_currentLevel);
    const int getVertexLayer(const int& _index)const;

    //DFS, 务必执行完layerDivision之后，再执行DFS
    // std::vector<uint64_t> BFSFromFirstLayer();
    // void DFSUtil(uint64_t vertexIndex, std::vector<bool> &visited, std::vector<uint64_t> &traversalOrder);




private:
    friend class Parse;
    std::vector<VertexNode<T>> m_verticesArray;
    std::map<std::string, uint64_t> m_vertexNameIndex;
    std::map<int, std::vector<uint64_t>> outDegreeIndex;
    std::map<int, std::vector<uint64_t>> inDegreeIndex;
    std::vector<std::set<int>> layerNodeDivVec;
    std::set<int> inputVertexIndexVec;
    std::set<int> outputVertexIndexVec;
    // std::vector<int> continuous_path;

    std::map<uint8_t, std::vector<std::pair<uint64_t,uint64_t>>> sameIndegreeLayerNode;
    std::vector<std::pair<unsigned int ,unsigned int>> differIndegreeLayerNodePair;

    uint64_t m_numVertices;   
    uint64_t m_numEdges;  
    uint64_t max_layer;    

    // uint64_t after_optimize_m_numVertices;
    // uint64_t after_optimize_m_numEdges;
};

template<typename T>
const std::string CrossGraphLink<T>::getVertexName(const int index) const {
    if (index < 0 || index >= m_verticesArray.size()) {
        throw std::out_of_range("Index out of range in getVertexName");
    }
    // 确保 data 是正确的类型
    return m_verticesArray.at(index).data->getNodeName();
}

template<typename T>
inline bool CrossGraphLink<T>::is_inserted(const std::string _string)const{
    if(getVertexIndex(_string) != -1){
        return true;
    }else{
        return false;
    }
}

template<typename T>
void CrossGraphLink<T>::insertVertex(const std::shared_ptr<T>& _tmpv) {
    auto _vertexName = _tmpv->getNodeName();
    if (getVertexIndex(_vertexName) == -1) {
        setVertexIndex(*_tmpv); // 注意这里使用 *_tmpv
        VertexNode<T> _node(_tmpv);
        m_verticesArray.push_back(_node);
        m_numVertices++;
    } 
}

template<typename T>
void CrossGraphLink<T>::insertEdge(const std::string &_nodeName1, const std::string &_nodeName2) {
    auto index1 = getVertexIndex(_nodeName1);
    auto index2 = getVertexIndex(_nodeName2);

    if (index1 == -1 || index2 == -1) {
        throw std::runtime_error("Invalid vertex name provided for edge creation.");
    }

    // 插入正向边
    if (m_verticesArray[index1].firstout == nullptr) {
        //为空，直接插入即可
        m_verticesArray[index1].firstout = new EdgeNode(index1, index2);
    } else {
        //找到第一个EdgeNode
        EdgeNode *p = m_verticesArray[index1].firstout;

        if(p->headidx == index2)
            return;
        while(p->taillink != nullptr){
            p = p->taillink;
            if(p->headidx == index2)
                return;
        }
        //一直找到最后一个taillink为空的Edgenode，新建我们的边，指向它。
        p->taillink = new EdgeNode(index1, index2);
    }

    //因为入度和出度共享一个EdgeNode，这个很关键
    //正向已经插入EdgeNode，通过index1索引以及headidx是否和index2匹配，可以找到这个EdgeNode
    auto newp = m_verticesArray[index1].firstout;
    while(newp->headidx != index2 && newp->taillink){
        newp = newp->taillink;
    }

    // 插入反向边
    if (!m_verticesArray[index2].firstin) {
        //找到后，因为firstin是空的，直接连接即可
        m_verticesArray[index2].firstin = newp;
    } else {
        //已经确定非空，则获取firstin指向的EdgeNode，然后一直找到headlink为空的EdgeNode，让它的headlink指向我们新生成的EdgeNode
        EdgeNode *p = m_verticesArray[index2].firstin;
        while(p->headlink){
            p = p->headlink;
        }

        p->headlink = newp;
    }

    m_numEdges++;
}

template<typename T>
void CrossGraphLink<T>::insertEdge(const uint64_t index1,  const uint64_t index2) {
    if (index1 >= m_verticesArray.size() || index2 >= m_verticesArray.size()) {
        throw std::out_of_range("Index out of range in getVertexName");
    }

    // 插入正向边
    if (m_verticesArray[index1].firstout == nullptr) {
        //为空，直接插入即可
        m_verticesArray[index1].firstout = new EdgeNode(index1, index2);
    } else {
        //找到第一个EdgeNode
        EdgeNode *p = m_verticesArray[index1].firstout;

        if(p->headidx == index2)
            return;
        while(p->taillink != nullptr){
            p = p->taillink;
            if(p->headidx == index2)
                return;
        }
        //一直找到最后一个taillink为空的Edgenode，新建我们的边，指向它。
        p->taillink = new EdgeNode(index1, index2);
    }

    //因为入度和出度共享一个EdgeNode，这个很关键
    //正向已经插入EdgeNode，通过index1索引以及headidx是否和index2匹配，可以找到这个EdgeNode
    auto newp = m_verticesArray[index1].firstout;
    while(newp->headidx != index2 && newp->taillink){
        newp = newp->taillink;
    }

    // 插入反向边
    if (!m_verticesArray[index2].firstin) {
        //找到后，因为firstin是空的，直接连接即可
        m_verticesArray[index2].firstin = newp;
    } else {
        //已经确定非空，则获取firstin指向的EdgeNode，然后一直找到headlink为空的EdgeNode，让它的headlink指向我们新生成的EdgeNode
        EdgeNode *p = m_verticesArray[index2].firstin;
        while(p->headlink){
            p = p->headlink;
        }

        p->headlink = newp;
    }

    m_numEdges++;
}

template<typename T>
bool CrossGraphLink<T>::removeEdge(const uint64_t index1, const uint64_t index2) {
    if (index1 >= m_verticesArray.size() || index2 >= m_verticesArray.size()) {
        return false;
    }

    bool removed = false;

    //EdgeNode被入度和出度共享，因此，有两根线，需要先定位，删除一根后，再删除一根，不可直接全部删除，
    EdgeNode* toDelete = nullptr;
    EdgeNode** currOutEdge = &(m_verticesArray[index1].firstout);
    
    while (*currOutEdge != nullptr) {
        if ((*currOutEdge)->headidx == index2) {
            //获取一整块
            toDelete = *currOutEdge;
            //截取剩下的快
            *currOutEdge = (*currOutEdge)->taillink;
            removed = true;
            break;
        }
        //把截取剩下的块再接上
        currOutEdge = &((*currOutEdge)->taillink);
    }

    if (removed == false) {
        return false;
    }

    //此时两根线已经拆除一根了，还有一根需要拆除
    EdgeNode** currentInEdge = &m_verticesArray[index2].firstin;
    while (*currentInEdge != nullptr) {
        //该EdgeNode是我们想要拆除的
        if (*currentInEdge == toDelete) {
            //firstin的指针直接指向后面的部分
            *currentInEdge = (*currentInEdge)->headlink;
            break;
        }
        currentInEdge = &((*currentInEdge)->headlink);
    }

    delete toDelete;

    m_numEdges--;
    return true;
}

template<typename T>
bool CrossGraphLink<T>::modifyNodeType(const std::string& nodeName, const std::string& newType) {
    int index = getVertexIndex(nodeName);
    if (index == -1) return false; 
    VertexNode<T>& vertex = m_verticesArray.at(index);

    if (newType == "or") {
        vertex.data = std::make_shared<OrNode>(nodeName);
    } else if (newType == "and") {
        vertex.data = std::make_shared<AndNode>(nodeName);
    } else if (newType == "not") {
        vertex.data = std::make_shared<NotNode>(nodeName);
    } else if (newType == "maj") {
        vertex.data = std::make_shared<MajNode>(nodeName);
    } else if (newType == "wire") {
        vertex.data = std::make_shared<RedundancyNode>(nodeName);
    } else {
        return false;
    }
    return true;
}


template<typename T>
void CrossGraphLink<T>::caculateVertexDegree(){
    outDegreeIndex.clear();
    inDegreeIndex.clear();
    layerNodeDivVec.clear();

    int index = 0;
    for(auto&v: m_verticesArray){
        std::vector<uint64_t> _outDegreeIndex;
        uint64_t _outdegree = 0;
        auto temptr_out = v.firstout;
        while(temptr_out != nullptr){
            _outDegreeIndex.push_back(temptr_out->headidx);
            _outdegree ++;
            temptr_out = temptr_out->taillink;
        }
        v.outdegree = _outdegree;
        outDegreeIndex.insert({index, _outDegreeIndex});


        std::vector<uint64_t> _inDegreeIndex;
        uint64_t _indegree = 0;
        auto temptr_in = v.firstin;
        while(temptr_in != nullptr){
            _inDegreeIndex.push_back(temptr_in->tailidx);
            _indegree++;
            temptr_in = temptr_in->headlink;
        }
        v.indegree = _indegree;
        inDegreeIndex.insert({index, _inDegreeIndex});

        index++;
    }
}

template<typename T>
void CrossGraphLink<T>::layerDivision(std::set<std::string> &_inputNode,std::set<std::string> &_outputNode){
    //首先获取电路所有输入、输出节点index
    for(auto &_string: _inputNode){
        auto _index = getVertexIndex(_string);
        inputVertexIndexVec.insert(_index);
    }
    for(auto &_string: _outputNode){
        auto _index = getVertexIndex(_string);
        outputVertexIndexVec.insert(_index);
    }

    // 首先计算所有node的出度和入度
    caculateVertexDegree();
    std::vector<bool> is_traversed(m_verticesArray.size(), false);
    int currentLevel = 0;
    // 递归, 第一次传入所有的输入节点
    layerRecursive(inputVertexIndexVec, is_traversed, currentLevel);

    // currentLevel ++;
    std::set<int> lastLayerIndex;
    for (int i = 0; i < is_traversed.size(); ++i) {
        if (!is_traversed[i] ) {
            //如果遍历为false的node，要么是优化后的buffer_node，没有出度和入度的,要么是电路的输出node，
            //所以，当从输出node容器中未检索到这个node，但是它是buffernode，那么就正常pass掉，
            if(outputVertexIndexVec.find(i) == outputVertexIndexVec.end()){
                if(getNodeType(i) == "wire" || getNodeType(i) == "not"){
                    continue;
                }else{
                    throw std::runtime_error("Exception: Vertex index " + std::to_string(i) + " not found in outputVertexIndexVec");
                    return;
                }
            }
            m_verticesArray[i].layer = currentLevel;
            lastLayerIndex.insert(i);
        }
    }
    if(!lastLayerIndex.empty()){
        layerNodeDivVec.push_back(lastLayerIndex);
    }
    max_layer = layerNodeDivVec.size();
}

template<typename T>
void CrossGraphLink<T>::layerRecursive(std::set<int> &_vertex, std::vector<bool>& _is_traversed, int &_currentLevel){

    // 获取每一层的节点，进行保存
    layerNodeDivVec.push_back(_vertex);

    // 该层所有节点层级赋值为, 该层节点的所有出度节点放入容器。
    std::set<int> nextLayerIndex;
    // nextLayerIndex.clear();
    for(auto &v : _vertex){
        _is_traversed[v] = true;
        m_verticesArray[v].layer = _currentLevel;
        if(outDegreeIndex.find(v) != outDegreeIndex.end()){
            auto vec = outDegreeIndex[v];
            //节点不一定会有出度
            if(!vec.empty()){
                for(auto &i : vec){
                    // _is_traversed[i] = true;
                    nextLayerIndex.insert(i);
                }
            }
        }
    }

    _currentLevel ++;

    //对该层节点的所有出度节点的入度进行检查， 如果有入度是上一个函数未曾访问过的，则放入errorIndex中，
    //如果该节点的所有入度都是上一个层级,则该节点层级+1.
    std::set<int> errorIndex;
    for(auto &i :nextLayerIndex){
        auto vec = inDegreeIndex[i];
        for(auto &v : vec){
            //这里格外注意v是当前节点的入读index， i才是当前节点，不要弄混淆了，有的电路的输出会有出度，所以这里还得加上出度为空的限制
            if(_is_traversed[v] == false || ((outputVertexIndexVec.find(i) != outputVertexIndexVec.end()) && outDegreeIndex.at(i).empty()))
            // if(_is_traversed[v] == false)
                errorIndex.insert(i);
        }
        m_verticesArray[i].layer = _currentLevel;
    }

    //纠正，不放入下一层节点容器中，设置为未访问节点。
    if(!errorIndex.empty()){
        for(auto & i: errorIndex){
            nextLayerIndex.erase(i);
            _is_traversed[i] = false;
        }
    }

    //递归终止条件：只要下一层节点不为空，说明出度还有节点，当该层节点的所有出度全部为0时，证明全部遍历完成
    if(!nextLayerIndex.empty()){
        layerRecursive(nextLayerIndex, _is_traversed, _currentLevel);
    }

    return;
}

template<typename T>
void CrossGraphLink<T>::setVertexIndex(const T& _vertex) {
    auto _string = _vertex.getNodeName();
    if(getVertexIndex(_string) == -1) {
        auto _size = m_vertexNameIndex.size();
        m_vertexNameIndex.insert({_string, _size});       
    }
}

template<typename T>
inline int CrossGraphLink<T>::getVertexIndex(const std::string &_string) const {
    auto iter = m_vertexNameIndex.find(_string);
    return iter != m_vertexNameIndex.end() ? iter->second : -1;
}


template<typename T>
const int CrossGraphLink<T>::getVertexLayer(const int& _index)const{
    return m_verticesArray.at(_index).layer;
}


template<typename T>
void CrossGraphLink<T>::getSameLayerNodeIndex(){

    sameIndegreeLayerNode.clear();
    differIndegreeLayerNodePair.clear();
    std::vector<std::pair<uint64_t,uint64_t>> _sameLayerNodeIndex;
    int layer = layerNodeDivVec.size();
    for(int i = 1; i < layer; i++){
        auto layer_nodes = layerNodeDivVec[i];
        for(auto &node : layer_nodes){
            
            for(auto &in_node : inDegreeIndex.at(node)){
                if(getVertexLayer(in_node) == (i - 1)){
                    //如果是同一层级保存
                    _sameLayerNodeIndex.push_back({in_node, node});
                }else{
                    //不同层级，也保存
                    differIndegreeLayerNodePair.push_back({in_node, node});
                }
            }
        }
        if(!_sameLayerNodeIndex.empty()){
            sameIndegreeLayerNode[i] = _sameLayerNodeIndex; // 使用下标操作符进行赋值
            _sameLayerNodeIndex.clear(); // 清除这个临时向量以供下一层使用
        }
    }
}

template<typename T>
std::vector<int> CrossGraphLink<T>::find_continuous_path() {
    // 尝试从最后一层的每个输出节点开始构建路径
    std::vector<int> continuous_path;
    for (auto candidate : layerNodeDivVec.back()) {
        unsigned int currentNode = candidate;  // 当前节点从候选节点开始
        // 从输出层开始逐层向上搜索
        for (int currentLayer = layerNodeDivVec.size() - 1; currentLayer != 0; --currentLayer) {
            continuous_path.push_back(currentNode);  // 将当前节点添加到路径
            // 在上一层中查找当前节点的入度节点
            bool found = false;
            for (auto& prevNode : layerNodeDivVec[currentLayer - 1]) {
                // 检查 prevNode 是否连接到 currentNode
                auto& successors = outDegreeIndex.at(prevNode);
                if (std::find(successors.begin(), successors.end(), currentNode) != successors.end()) {
                    if(currentLayer ==1){
                        continuous_path.push_back(prevNode);
                    }
                    currentNode = prevNode;  // 找到了上一层的节点连接到当前节点
                    found = true;
                    break;
                }
            }
            if (!found) {
                continuous_path.clear();
                break;  // 如果没有找到连接的节点，则尝试下一个候选节点
            }
        }

        if (!continuous_path.empty() && continuous_path.size() == layerNodeDivVec.size()) {
            break;
        }
    }
    return continuous_path;
}

template<typename T>
std::string CrossGraphLink<T>::getNodeType(int index){
    auto v = m_verticesArray.at(index);
    if(typeid(*(v.data)) == typeid(InputNode)){
        return "input";
    }

    if(typeid(*(v.data)) == typeid(OutputNode)){
        return "output";
    }

    if(typeid(*(v.data)) == typeid(MajNode)){
        return "maj";
    }

    if(typeid(*(v.data)) == typeid(AndNode)){
        return "and";
    }

    if(typeid(*(v.data)) == typeid(OrNode)){
        return "or";
    }

    if(typeid(*(v.data)) == typeid(NotNode)){
        return "not";
    }

    if(typeid(*(v.data)) == typeid(RedundancyNode)){
        return "wire";
    }

    if(typeid(*(v.data)) == typeid(FanoutNode)){
        return "fanout";
    }

    return "unknown";
}

//实际并没有删除这个node, 只是隐藏起来了
template<typename T>
void CrossGraphLink<T>::deleteNode(int index) {
    if (index >= m_verticesArray.size() || index < 0) {
        throw std::out_of_range("Index out of range in deleteNode");
    }

    auto fanins = inDegreeIndex[index];
    auto fanouts = outDegreeIndex[index];
    

    removeEdge(fanins[0], index);
    m_verticesArray[index].indegree--;
    
    for(auto &out: fanouts){
        removeEdge(index, out);
        insertEdge(fanins[0], out);
        m_verticesArray[index].outdegree--;
    }
    // 从顶点数组中删除最后一个节点
    // m_numVertices --;
}


// template<typename T>
// std::vector<uint64_t> CrossGraphLink<T>::BFSFromFirstLayer() {
//         std::vector<bool> visited(m_numVertices, false);
//         std::vector<uint64_t> traversalOrder;

//         // 按层顺序遍历节点
//         for (const auto& layer : layerNodeDivVec) {
//             for (const int vertexIndex : layer) {
//                 if (!visited[vertexIndex]) {
//                     // 如果节点未被访问，执行BFS
//                     visited[vertexIndex] = true;
//                     traversalOrder.push_back(vertexIndex);

//                     // 遍历所有相邻的顶点
//                     for (EdgeNode* edge = m_verticesArray[vertexIndex].firstout; edge != nullptr; edge = edge->taillink) {
//                         if (!visited[edge->headidx]) {
//                             visited[edge->headidx] = true;
//                             traversalOrder.push_back(edge->headidx);
//                         }
//                     }
//                 }
//             }
//         }

//         return traversalOrder;
// }

// template<typename T>
// void CrossGraphLink<T>::DFSUtil(uint64_t vertexIndex, std::vector<bool> &visited, std::vector<uint64_t> &traversalOrder) {
//     // 标记当前节点为已访问
//     visited[vertexIndex] = true;
//     traversalOrder.push_back(vertexIndex);

//     // 递归遍历所有未访问的相邻节点
//     for (EdgeNode* edge = m_verticesArray[vertexIndex].firstout; edge != nullptr; edge = edge->taillink) {
//         if (!visited[edge->headidx]) {
//             DFSUtil(edge->headidx, visited, traversalOrder);
//         }
//     }
// }



// template<typename T>
// void CrossGraphLink<T>::printInfo(std::string _fileName){
//     std::ofstream os;
//     os.open(_fileName);
//     // int index = 0;

//     os << "head" <<","<<"tails" << ","<<"name" << ","<< "type" << ","<< "layer" << "\n";

//     for(auto &_first : outDegreeIndex){
//         os << _first.first << ",";
//         if(!_first.second.empty()){
//             int i = 1; 
//             int total = _first.second.size();
//             for(auto & _second : _first.second){
//                 if( i == total){
//                     os << _second; 
//                 }else{
//                     os << _second << " "; 
//                 }
//                 i++;
//             }
//         }

//         auto v = m_verticesArray.at(_first.first);
//         if(typeid(*(v.data)) == typeid(InputNode)){
//            os << "," << v.data->getNodeName() << "," << "input" << "," <<  v.layer << "\n";
//            continue;
//         }

//         if(typeid(*(v.data)) == typeid(OutputNode)){
//            os << "," << v.data->getNodeName() << "," << "output" << "," <<  v.layer << "\n";
//            continue;
//         }

//         if(typeid(*(v.data)) == typeid(MajNode)){
//            os  << "," << v.data->getNodeName() << "," << "maj" << "," <<  v.layer << "\n";
//            continue;
//         }

//         if(typeid(*(v.data)) == typeid(AndNode)){
//            os << "," << v.data->getNodeName() << "," << "and" << "," <<  v.layer << "\n";
//            continue;
//         }

//         if(typeid(*(v.data)) == typeid(OrNode)){
//            os << "," << v.data->getNodeName() << "," << "or" << "," <<  v.layer << "\n";
//            continue;
//         }

//         if(typeid(*(v.data)) == typeid(NotNode)){
//            os  << "," << v.data->getNodeName() << "," << "not" << "," <<  v.layer << "\n";
//            continue;
//         }

//         if(typeid(*(v.data)) == typeid(RedundancyNode)){
//            os  << "," << v.data->getNodeName() << "," << "buffer" << "," <<  v.layer << "\n";
//            continue;
//         }

//     }
//     os.close();
// }














} // namespace fcngraph
