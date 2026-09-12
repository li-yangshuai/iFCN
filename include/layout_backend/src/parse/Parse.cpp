#include"Parse.h"
#include <autopr/graph/scalarExpression.h>

namespace iFCN_Lab{

void Parse::parseVerilog(std::string fileName, bool allowMajority)
{
    std::ifstream file(fileName);
    if (!file) throw std::runtime_error("Cannot open scalar Verilog file: " + fileName);
    const auto module = ifcn::verilog::readScalarModule(file);
    moduleName = module.name;
    for (const auto& input : module.inputs) insert_input_node(input);
    for (const auto& output : module.outputs) insert_output_node(output);
    vec_wire = module.wires;
    for (const auto& assignment : module.assignments)
        parse_logicNode_string(assignment.first, assignment.second, allowMajority);
    // A direct alias (assign out = input) adds only an edge. It must still
    // remain an observable output even though no logic gate was inserted.
    for (const auto& output : module.outputs)
        outputNodesIndex.insert(graphLink->getVertexIndex(output));
    layerDivision();
}

void Parse::parse_logicNode_string(const std::string& nodeName, const std::string& lineString,
                                 bool allowMajority)
{
    ifcn::verilog::ScalarDagAdapter adapter;
    adapter.allowMajority = allowMajority;
    adapter.nodeExists = [&](const std::string& name) { return graphLink->is_inserted(name); };
    adapter.nameReserved = [&](const std::string& name) {
        return graphLink->is_inserted(name) || vec_wire.count(name) || vec_output.count(name);
    };
    adapter.isPrimaryOutput = [&](const std::string& name) { return vec_output.count(name) != 0; };
    adapter.addNode = [&](const std::string& name, const std::string& type) {
        if (type == "and") insert_and_node(name);
        else if (type == "or") insert_or_node(name);
        else if (type == "maj") insert_majority_node(name);
        else if (type == "not") insert_not_node(name);
        else insert_redundancyNode(name);
    };
    adapter.addEdge = [&](const std::string& source, const std::string& target) {
        insertEdge(source, target);
    };
    ifcn::verilog::lowerScalarAssignment(nodeName, lineString, adapter);
}

inline void Parse::insert_input_node(const std::string &_string) {
    vec_input.insert(_string);
    if (graphLink->is_inserted(_string)){
        inputNodesIndex.insert(graphLink->getVertexIndex(_string));
        return;
    }
    auto node = std::make_shared<InputNode>(_string);
    graphLink->insertVertex(node);
    inputNodesIndex.insert(graphLink->getVertexIndex(_string));
}

inline void Parse::insert_output_node(const std::string &_string){
    if(graphLink->is_inserted(_string)){
        std::string outputName = _string + "_o"; 
        vec_output.insert(outputName);

        if (graphLink->is_inserted(_string))
            return;
        auto node = std::make_shared<OutputNode>(_string);
        graphLink->insertVertex(node);
        graphLink->insertEdge(_string, outputName);
    }else{
        vec_output.insert(_string);
        if (graphLink->is_inserted(_string))
            return;

        auto node = std::make_shared<OutputNode>(_string);
        graphLink->insertVertex(node);
    }
}

inline void Parse::insert_or_node(const std::string &_string){
    vec_orGate.insert(_string);
    if (graphLink->is_inserted(_string)){
        //如果这个node是output node，那么修改这个noode的属性，变成对应逻辑门属性，输出node只是记录
        if(vec_output.find(_string) != vec_output.end()){
            if(graphLink->modifyNodeType(_string, NodeType::Or))
                outputNodesIndex.insert(graphLink->getVertexIndex(_string));
                return;
        }
        return;
    }
    auto node = std::make_shared<OrNode>(_string);
    graphLink->insertVertex(node);
}

inline void Parse::insert_majority_node(const std::string &_string){
    vec_majorityGate.insert(_string);
    if (graphLink->is_inserted(_string)){
        if(vec_output.find(_string) != vec_output.end()){
            if(graphLink->modifyNodeType(_string, NodeType::Maj))
                outputNodesIndex.insert(graphLink->getVertexIndex(_string));
                return;
        }
        return;
    }
    auto node = std::make_shared<MajNode>(_string);
    graphLink->insertVertex(node);
}

inline void Parse::insert_and_node(const std::string &_string){
    vec_andGate.insert(_string);
    if (graphLink->is_inserted(_string)){
        if(vec_output.find(_string) != vec_output.end()){
            if(graphLink->modifyNodeType(_string, NodeType::And))
                outputNodesIndex.insert(graphLink->getVertexIndex(_string));
                return;
        }
        return;
    }
    auto node = std::make_shared<AndNode>(_string);
    graphLink->insertVertex(node);
}

inline void Parse::insert_not_node(const std::string &_string){
    vec_notGate.insert(_string);
    if (graphLink->is_inserted(_string)){
        if(vec_output.find(_string) != vec_output.end()){
            if (graphLink->modifyNodeType(_string, NodeType::Not))
                outputNodesIndex.insert(graphLink->getVertexIndex(_string));
        }
        return;
    }
    auto node = std::make_shared<NotNode>(_string);
    graphLink->insertVertex(node);
}

inline void Parse::insert_redundancyNode(const std::string &_string){
    vec_redundancyNode.insert(_string);
    if (graphLink->is_inserted(_string)){
        if(vec_output.find(_string) != vec_output.end()){
            // if(graphLink->modifyNodeType(_string, "wire")){
                outputNodesIndex.insert(graphLink->getVertexIndex(_string));
                // return;
            }
        // }
        return;
    }
    auto node = std::make_shared<RedundancyNode>(_string);
    graphLink->insertVertex(node);
}

inline void Parse::insert_fanoutNode (const std::string &_string){
    vec_fanoutNode.insert(_string);
    if (graphLink->is_inserted(_string))
        return;
    auto node = std::make_shared<FanoutNode>(_string);
    graphLink->insertVertex(node);
}



inline void Parse::insertEdge(const std::string _nodeName1, const std::string _nodeName2){
    graphLink->insertEdge(_nodeName1, _nodeName2);
}

inline void Parse::insertEdge(const uint64_t index1,  const uint64_t index2) {
    graphLink->insertEdge(index1, index2);
}


void Parse::optimizeAIOG_DRC(int _limit_input, int _limit_and, int _limit_or, int _limit_not, 
                              int _limit_majority, int _limit_redundancy){
    originCircuitNodeNum = graphLink->m_numVertices;
    originCircuitEdgeNum = graphLink->m_numEdges;

    //需要先获取度容器
    graphLink->caculateVertexDegree();
    // optimizeBufferNode();

    if(graphLink->outDegreeIndex.empty())
        return;

    //graph: 2,1,1,2,1,3
    //ga :   2,2,2,2,2,2 
    //QCA中，不同类型的器件的扇出数量是不一样的
    optimizeAIOG_DRC_node(vec_input,          _limit_input);
    optimizeAIOG_DRC_node(vec_andGate,        _limit_and);
    optimizeAIOG_DRC_node(vec_orGate,         _limit_or);
    optimizeAIOG_DRC_node(vec_notGate,        _limit_not);
    optimizeAIOG_DRC_node(vec_majorityGate,   _limit_majority);
    optimizeAIOG_DRC_node(vec_redundancyNode, _limit_redundancy);
    //处理完之后调用分层算法
    layerDivision();

}

void Parse::optimizeAIOG_DRC_node(std::set<std::string> & _circuitNodeType, const int _limit_fanout_num){
    //获取该器件类型的节点
    for(auto &in_str : _circuitNodeType){
        //根据名字获取index
        int index1 = graphLink->m_vertexNameIndex.at(in_str);
        //寻找输入节点的出度
        if(graphLink->outDegreeIndex.find(index1) == graphLink->outDegreeIndex.end())
            return;
        //获取index1的所有出度的index，用deque存储，该容器相当于双端链表
        std::deque<int> out_deq(graphLink->outDegreeIndex.at(index1).begin(), graphLink->outDegreeIndex.at(index1).end());
        //具体的优化函数
        optimiz_one_node_fanout(index1, out_deq, _limit_fanout_num);
    }

}

/*
    算法思想：当一个节点A的出度，过于多，插入过多的冗余节点，那么冗余节点之间也要插入冗余节点来消除过多的扇出
    使用算法还是采用递归：
        1.假设根据当前的扇出数量是m，限制扇出数量为a，新建一个空的容器n，n存储扇出节点的index，当前大小为0；
        2.每次新建一个扇出节点fanout，都会放入n中，n大小+1， 该fanout节点解决的了两个扇出的问题，所以 m-2
            此时会遇到一个问题：假设扇出数量限制a为偶数， 每次新建扇出节点，正好处理2n个扇出，最后刚好清空为0；
            但是如果a为奇数，每次新建fancout，解决了2n个扇出后，还剩下1个扇出，不能再新建扇出了，否则程序每次固定扇出两条线，会出现没有多余的线可以删除，
            所以，当 m - 2*n == a时，就不再执行新建节点的操作了，把m中剩下的节点都放入到n中
        3. n容器此时会包括新建的fanout、上一次函数未处理的A节点的出度，n作为A节点的所有出度节点再执行函数，
        4.迭代，知道n容器的大小，也就是A节点的出度 <= a 时，才退出
*/
void Parse::optimiz_one_node_fanout(int index1, std::deque<int> &out_deq, const int _limit_fanout_num){

    std::deque<int> nextlayerBufferNodes;

    if(out_deq.size() > _limit_fanout_num){

        while(out_deq.size() >= _limit_fanout_num && out_deq.size()>=2){
            //增加一个buffer要删除两条边
            int edge1 = out_deq.back();
            out_deq.pop_back();
            int edge2 = out_deq.back();
            out_deq.pop_back();

            //删除原node和两个出度的边
            removeEdge(index1, edge1);
            removeEdge(index1, edge2);

            //插入新节点，按照有向图的设计必须指定一个string的名字，且命名具有唯一性，这里以两个出度的序号来命名
            std::string buffer_name = std::to_string(index1) +"_" +std::to_string(edge1) + "_" + std::to_string(edge2);
            //插入buffer
            insert_fanoutNode(buffer_name);
            int buffer_index = graphLink->m_vertexNameIndex.at(buffer_name);
            
            //建立初始节点和buffer的边
            insertEdge(index1, buffer_index);

            //建立buffer和两个出度节点的边
            insertEdge(buffer_index,  edge1);
            insertEdge(buffer_index,  edge2);

            nextlayerBufferNodes.push_front(buffer_index);

        }
        for(auto &i : out_deq){
            nextlayerBufferNodes.push_front(i);
        }
        optimiz_one_node_fanout(index1, nextlayerBufferNodes, _limit_fanout_num);
    }else{
        return;
    }

}

void Parse::optimizeBufferNode(){
    effectiveNodes.clear();
    for(int i = 0; i < getm_numVertices(); i++){
        effectiveNodes.insert(i);
    }
    for(auto &in_str : vec_redundancyNode){
        //根据名字获取index
        int index1 = graphLink->m_vertexNameIndex.at(in_str);
        // 获取 index1 的扇出和扇入
        // A primary output remains an observable port even when it also
        // drives another gate. Removing this alias silently drops that port.
        if (outputNodesIndex.count(index1)) continue;
        auto indegree_num = graphLink->m_verticesArray[index1].indegree;
        auto outdegree_num = graphLink->m_verticesArray[index1].outdegree;
        if (indegree_num == 1 && outdegree_num == 1) {
            graphLink->deleteNode(index1);
            hideBufferNodeIndex.push_back(index1);
            //隐藏fanout节点，首先断言，这个node有效
            assert(effectiveNodes.find(index1) != effectiveNodes.end());
            effectiveNodes.erase(index1);
        }   
    }
    layerDivision();
}

//非门优化：
/*
    如果 （非门的扇出数量 + 非门扇入node的扇出数量 - 1 <= 2），删除非门，
    记录（非门的扇入，非门的扇出）的边
*/
void Parse::optimizeNOTNode(){
    for(auto &in_str : vec_notGate){
        //根据名字获取index
        uint64_t index1 = graphLink->m_vertexNameIndex.at(in_str);
        //获取该node的扇入数量,
        int indegree_num = graphLink->m_verticesArray[index1].indegree;
        //断言not的扇入数量为1
        assert(indegree_num == 1);
        // 获取 index1 的扇入节点的索引
        uint64_t faninIndex = graphLink->inDegreeIndex[index1][0];
        //获取扇入节点的扇出数量
        auto faninIndex_fanout_num = graphLink->m_verticesArray[faninIndex].outdegree;
        //获取非门的扇出数量
        auto fanout_num = graphLink->m_verticesArray[index1].outdegree;
        
        if(faninIndex_fanout_num + fanout_num -1  <= 2){
            //删除非门
            graphLink->deleteNode(index1);
            hideNotNodeIndex.push_back(index1);
            //获取非门扇出的所有节点
            auto fanouts = graphLink->outDegreeIndex[index1];
            //记录非门的扇入和扇出的节点
            for(auto &fanout : fanouts){
                hide_not_place_pair.insert({index1,{faninIndex, fanout}});
            }
            //隐藏非门
            effectiveNodes.erase(index1);
        }
    }
    layerDivision();
}

std::vector<std::pair<int,int>> Parse::getEffectiveEdges() const{
    std::vector<std::pair<int,int>> vec;
    for(auto &node_fanouts : getOutDegreeIndex()){
        int node = node_fanouts.first;
        if((getVertexNode(node).indegree == 0 && getVertexNode(node).outdegree == 0)  ) {
            continue;
        }
        const auto& fanouts = node_fanouts.second;
        for(int fanout :fanouts){
            vec.push_back({node, fanout});
        }
    }
    return vec;
}

void Parse::addLayerRedundancyNode(){
    effectiveNodes.clear();
    for(int i = 0; i < getm_numVertices(); i++){
        effectiveNodes.insert(i);
    }

    auto fanouts = getOutDegreeIndex();
    for(auto node : fanouts){
        auto fanout = node.first;
        auto fanout_fanouts = node.second;
        for(auto fanout_fanout : fanout_fanouts){
            int layer_diff = getVertexLayer(fanout_fanout) - getVertexLayer(fanout) - 1;
            if(layer_diff != 0){
                removeEdge(fanout, fanout_fanout);
                //创建layer_diff个冗余节点, 是这些冗余节点之间连接，最后一个冗余节点连接fanout
                int curr_node = fanout;
                for(int i = 0; i < layer_diff; i++){
                    std::string buffer_name = std::to_string(curr_node) +"_" +std::to_string(fanout) + "_" + std::to_string(fanout_fanout) + "_" + std::to_string(i);
                    insert_redundancyNode(buffer_name);
                    int buffer_index = graphLink->m_vertexNameIndex.at(buffer_name);
                    effectiveNodes.insert(buffer_index);
                    insertEdge(curr_node, buffer_index);
                    curr_node = buffer_index;
                }
                insertEdge(curr_node, fanout_fanout);
            }
        }
    }
    layerDivision();

}


};
