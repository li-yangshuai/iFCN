#include"parse.h"

namespace fcngraph{

void Parse::parseVerilog(std::string _fileName){
    std::ifstream file(_fileName);
    if(file.fail()){
        std::cout << "failed open! " <<"\n";
        return ;
    }
    std::string fileLineString;
    std::string fileSemicolonString;
    while(std::getline(file,fileLineString)){
        if(fileLineString.empty()){
            continue;
        }
        if(fileLineString.find_last_of(';') == std::string::npos){
            fileSemicolonString += fileLineString;
            continue;
        }
        fileSemicolonString += fileLineString; 
        size_t pos;
        while ((pos = fileSemicolonString.find('\r')) != std::string::npos) {
            fileSemicolonString.erase(pos, 1);
        }

        //remove " "
        fileSemicolonString.erase(std::remove(fileSemicolonString.begin(),fileSemicolonString.end(), ' '),fileSemicolonString.end());
        verilogParseContainer.push_back(fileSemicolonString);
        fileSemicolonString.clear();
    }

    if(verilogParseContainer.empty()) return;
    if(! is_parseModuleName(verilogParseContainer[0])) return;
    if(! is_parseInputNode (verilogParseContainer[1])) return;
    if(! is_parseOutputNode (verilogParseContainer[2])) return;
    if(! is_parseWireGate(verilogParseContainer[3])) return;
    if(verilogParseContainer.size() > 4){
        for(auto i = 4; i < verilogParseContainer.size(); i++){
            is_parseLogicNode(verilogParseContainer[i]);   
        }
    }

    layerDivision();
}

bool Parse::is_parseModuleName(const std::string &_lineString){
    auto index_1 = _lineString.find("module");
    if(index_1 == std::string::npos){
        return false;
    }
    auto index_2 = _lineString.find('(');
    moduleName = _lineString.substr(index_1 + 6, index_2 - index_1 - 6);
    return true;
}


bool Parse::is_parseInputNode(const std::string &_lineString){

    if( _lineString.find("input") == std::string::npos)
        return false;
    std::string lineString = _lineString;
    lineString.erase(0, 5);   // delete "input"
    lineString.pop_back();    // delete ";"
    auto index_2 = lineString.find_first_of(',');
    // only one item
    if(index_2 == std::string::npos){
        insert_input_node(lineString);
    }else{
        //split by ","
        std::set<std::string> result;
        boost::split(result, lineString, boost::is_any_of(","));
        for(const auto &s : result){
            insert_input_node(s);
        }
    }
    return true;
}

bool Parse::is_parseOutputNode(const std::string &_lineString){
    if( _lineString.find("output") == std::string::npos)
        return false;
    std::string lineString = _lineString;
    lineString.erase(0, 6);   // delete "input"
    lineString.pop_back();    // delete ","
    auto index_2 = lineString.find_first_of(',');
    // only one item
    if(index_2 == std::string::npos){   
        insert_output_node(lineString); 
    }else{
        //split by ","
        std::set<std::string> result;
        boost::split(result, lineString, boost::is_any_of(","));
        for(const auto &s : result){
            insert_output_node(s);
        }
    }
    return true;
}

bool Parse::is_parseWireGate (const std::string &_lineString){
    if( _lineString.find("wire") == std::string::npos)
        return false;
    std::string lineString = _lineString;
    lineString.erase(0, 4);   // delete "input"
    lineString.pop_back();    // delete ","
    auto index_2 = lineString.find_first_of(',');
    // only one item
    if(index_2 == std::string::npos){   
        vec_wire.insert(lineString);
    }else{
        //split by ","
        std::set<std::string> result;
        boost::split(result, lineString, boost::is_any_of(","));
        for(const auto &s : result){
            vec_wire.insert(s);
        }
    }
    return true;
}

void Parse::is_parseLogicNode (const std::string &_lineString){
    if(_lineString.find("assign") == std::string::npos)
        return;
    
    // get node name
    std::string lineString = _lineString;
    lineString.erase(0,6);
    auto index1 = lineString.find('=');
    auto nodeName = lineString.substr(0, index1);
    lineString.erase(0, index1 + 1);
    lineString.pop_back();

    // logic analysis
    int num = 0;
    parse_logicNode_string(nodeName, lineString, num);

}

void Parse::parse_logicNode_string(std::string &nodeName, std::string & lineString, int &_seqNo){
    if(lineString.find_first_of('(') != std::string::npos){
        //  assign n5 = (x1 & x2) | (x1 & ~n4) | (x2 & ~n4);
        insert_majority_node(nodeName);    //n5 is majority gate
        while( lineString.find_first_of('(') != std::string::npos ){
            auto index2 = lineString.find_first_of('(');
            auto index3 = lineString.find_first_of(')');
            auto len = index3 -index2 - 1;
            auto and_or_string = lineString.substr(index2 + 1 , len);
            
            auto redundance_node_name = nodeName + '_' + std::to_string(_seqNo);
            check_and_or_node(redundance_node_name, and_or_string);
            insertEdge(redundance_node_name, nodeName);
            lineString.erase(index2, len + 2);
            _seqNo++;
        }
        return;
    }else{
        check_and_or_node(nodeName, lineString);
        return;
    }
}

inline void Parse::check_and_or_node(const std::string &_nodeName, const std::string &signalName){
    bool is_only_not_gate = true;
    for(int i = 0; i < signalName.size(); i++){
        // ps : assign w6 = ~a & ~w5;
        if(signalName[i] == '&' || signalName[i] =='|'){
            auto leftNode = signalName.substr(0,i);
            auto rightNode = signalName.substr(i+1, signalName.size() - i);
            switch (signalName[i]){
                case '&':
                        check_not_node(leftNode);
                        check_not_node(rightNode);
                        insert_and_node(_nodeName);
                        insertEdge(leftNode, _nodeName);
                        insertEdge(rightNode, _nodeName);
                        is_only_not_gate = false;
                    break;
                case '|':
                        check_not_node(leftNode);
                        check_not_node(rightNode);
                        insert_or_node(_nodeName);
                        insertEdge(leftNode, _nodeName);
                        insertEdge(rightNode, _nodeName);
                        is_only_not_gate = false;
                    break;                 
                default:
                    break;
            }
        }
    }
    // ps: assign w1 = ~b;
    if(is_only_not_gate){
        check_not_node(signalName);
        // ~b -> w1
        insert_redundancyNode(_nodeName);
        insertEdge(signalName, _nodeName);
    }
    return;
}

inline void Parse::check_not_node(const std::string &_string){
    if(_string.find('~') == std::string::npos)
        return;
    if(_string.find('&') != std::string::npos )
        return;
    if(_string.find('|') != std::string::npos )
        return;
    auto index1 = _string.find('~');
    auto notNodeName = _string.substr(index1, _string.size());              // ~node_name
    auto originalNodeName = _string.substr(index1 + 1, _string.size()-1);  //  node_name
    insert_not_node(notNodeName);
    insertEdge(originalNodeName, notNodeName);
}


inline void Parse::insert_input_node(const std::string &_string) {
    vec_input.insert(_string);
    if (graphLink->is_inserted(_string))
        return;
    auto node = std::make_shared<InputNode>(_string);
    graphLink->insertVertex(node);
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
            if(graphLink->modifyNodeType(_string, "or"))
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
            if(graphLink->modifyNodeType(_string, "maj"))
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
            if(graphLink->modifyNodeType(_string, "and"))
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
            // if(graphLink->modifyNodeType(_string, "not"))
                outputNodesIndex.insert(graphLink->getVertexIndex(_string));
                // return;
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
        int faninIndex = graphLink->inDegreeIndex[index1][0];
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

std::vector<std::pair<int,int>> Parse::getEffectiveEdges(){
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