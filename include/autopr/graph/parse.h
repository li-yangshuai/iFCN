/*
verilog解析结果注意点：
    1. node的总数 <= 输入、输出、与门、或门等等的总和，因为有的节点既是逻辑门又是输出
    2.禁止使用  assign n5 = x1 | x2 | ~n4; 需要使用  assign n5 = (x1) | (x2) | (~n4),否则无法识别出择多门;
    3. max_layer 是从第1层开始计算的
*/


#pragma once
#include<iostream>
#include<string>
#include<vector>
#include<fstream>
#include<regex>
#include"crossgraph.hpp"
#include"node.h"
#include<set>
#include<cassert>
#include<boost/algorithm/string.hpp>
#include <memory>

namespace fcngraph{

    class Parse{
    public:

        Parse():graphLink(std::make_shared<CrossGraphLink<AbstractNode>>()) {}
        ~Parse(){}

        void parseVerilog(std::string _fileName);

        //对外接口，获取电路基本信息
        inline std::string get_moduleName()         {return moduleName;}
        inline uint32_t get_input_num()             {return vec_input.size();}
        inline uint32_t get_output_num()            {return vec_output.size();}
        inline uint32_t get_andGateNum_num()        {return vec_andGate.size();}
        inline uint32_t get_orGateNum_num()         {return vec_orGate.size();}
        inline uint32_t get_notGateNum_num()        {return vec_notGate.size();}
        inline uint32_t get_majorityGateNum_num()   {return vec_majorityGate.size();}
        inline uint32_t get_redundancyGateNum_num() {return vec_redundancyNode.size();}
        inline uint32_t get_fanout_num()            {return vec_fanoutNode.size();}

        const auto& getVertexNode(int index) const  {return graphLink->m_verticesArray[index];}
        //获取有向图的基本信息
        const auto& getOutDegreeIndex()const         {return graphLink->outDegreeIndex;}
        //输入node id ，返回node类型的string
        std::string getNodeType(int index)           {return graphLink->getNodeType(index);}
        //输入node id，返回node的名字
        std::string getNodeName(int index)          {return getVertexNode(index).data.get()->getNodeName();}

        const std::vector<uint64_t>& getFanoutsIndex(unsigned int _nodeIndex) const {
            auto it = graphLink->outDegreeIndex.find(_nodeIndex);  // 执行查找一次
            if (it != graphLink->outDegreeIndex.end()) {
                return it->second;  // 如果找到了，返回找到的vector
            } else {
                static const std::vector<uint64_t> emptyVector;  // 静态空vector，避免多次创建
                return emptyVector;  // 如果没有找到，返回空的vector
            }
        }

        const auto& getInDegreeIndex() const                    {return graphLink->inDegreeIndex;}

        const std::vector<uint64_t>& getFaninsIndex(unsigned int _nodeIndex)const{
            auto it = graphLink->inDegreeIndex.find(_nodeIndex);  // 执行查找一次
            if (it != graphLink->inDegreeIndex.end()) {
                return it->second;  // 如果找到了，返回找到的vector
            } else {
                static const std::vector<uint64_t> emptyVector;  // 静态空vector，避免多次创建
                return emptyVector;  // 如果没有找到，返回空的vector
            }
        }

        const auto& getlayerNodeDivVec()const                   {return graphLink->layerNodeDivVec;}
        const auto find_continuous_path()                      {return graphLink->find_continuous_path();}
        const std::string getVertexName(const int index)const   {return graphLink->getVertexName(index);}
        const int getVertexIndex(const std::string &name)const  {return graphLink->getVertexIndex(name);}
        const auto getm_numVertices() const                     {return graphLink->m_numVertices;}
        const auto getm_numEdges() const                        {return graphLink->m_numEdges;}
        const auto getmax_layer() const                         {return graphLink->max_layer;}
        const auto getVertexLayer(const int _index)const        {return graphLink->getVertexLayer(_index);}
        const auto& getm_vertexNameIndex(){return graphLink->m_vertexNameIndex;}
        const auto& getm_vertexType(const int index){return typeid(*(graphLink->m_verticesArray[index].data));}
        const auto& getOutputNodesIndex() {return outputNodesIndex;}

        //获取同一层的node index pair
        void caculateSameLayerNodeRoutePair()                   {graphLink->getSameLayerNodeIndex();}
        const auto getSameLayerNodeRoutePair()                  {return graphLink->sameIndegreeLayerNode;}
        const auto getDifferLayerNodeRoutePair()                {return graphLink->differIndegreeLayerNodePair;}
        
        //获取各种类型节点的名字
        const auto& getVec_inputNodeName() {return vec_input;}
        const auto& getVec_wireNodeName() {return vec_wire;}
        const auto& getVec_fanoutNodeName() {return vec_fanoutNode;}
        const auto& getvec_andGateNodeName() {return vec_andGate;}
        const auto& getvec_orGateNodeName() {return vec_orGate;}
        const auto& getvec_notGateNodeName() {return vec_notGate;}
        const auto& getvec_majorityGateNodeName() {return vec_majorityGate;}
        const auto& getvec_redundancyNodeNodeName() {return vec_redundancyNode;}

        //对构建好的有向图进行处理的算法：
        //1. 分层算法  2. AIOG的DRC优化  3.buffer的优化(redundancyNode)消除 4. not的优化
        void layerDivision() { graphLink->layerDivision(vec_input, vec_output);}
        void optimizeAIOG_DRC(int _limit_input, int _limit_and, int _limit_or, int _limit_not, 
                              int _limit_majority, int _limit_redundancy);
        void optimizeBufferNode();
        void optimizeNOTNode();
        const auto& getHide_node_place_pair() {return hide_not_place_pair;}
        //5 增加层级冗余节点
        void addLayerRedundancyNode();
        
        //获取图的有效边
        std::vector<std::pair<int,int>> getEffectiveEdges();
        const auto& getEffectiveNodes(){return effectiveNodes;}

        //获取隐藏的节点
        std::vector<unsigned int> hideBufferNodeIndex;
        std::vector<unsigned int> hideNotNodeIndex;
        std::multimap<unsigned int, std::pair<unsigned int, unsigned int>> hide_not_place_pair;
        

    private:
        //parse verilog file
        bool is_parseModuleName (const std::string &_lineString);
        bool is_parseInputNode  (const std::string &_lineString);
        bool is_parseOutputNode (const std::string &_lineString);
        bool is_parseWireGate   (const std::string &_lineString);
        void is_parseLogicNode  (const std::string &_lineString);
        void parse_logicNode_string(std::string &nodeName, std::string & lineString, int &_seqNo);
        
        // create logic node
        inline void check_not_node(const std::string &_string);
        inline void check_and_or_node(const std::string &_nodeName, const std::string &signalName);
        inline void insert_input_node       (const std::string &_string);
        inline void insert_output_node      (const std::string &_string);
        inline void insert_or_node          (const std::string &_string);
        inline void insert_majority_node    (const std::string &_string);
        inline void insert_and_node         (const std::string &_string);
        inline void insert_not_node         (const std::string &_string);
        inline void insert_redundancyNode   (const std::string &_string);
        inline void insert_fanoutNode       (const std::string &_string);

        //build graph
        inline void insertEdge(const std::string _node1, const std::string _node2);
        inline void insertEdge(const uint64_t index1,  const uint64_t index2);
        bool removeEdge(const uint64_t index1,  const uint64_t index2){ return graphLink->removeEdge(index1, index2);}
   
        ///AIOG的DRC优化    
        void optimizeAIOG_DRC_node(std::set<std::string> & _circuitNodeType, const int _limit_fanout_num);
        void optimiz_one_node_fanout(int index1, std::deque<int> &out_deq, const int _limit_fanout_num);
     
    private:
        std::shared_ptr<CrossGraphLink<AbstractNode>>graphLink;
        std::vector<std::string> verilogParseContainer; 
        std::string moduleName;
        std::set<std::string> vec_input;
        std::set<std::string> vec_output; 
        std::set<std::string> vec_andGate;
        std::set<std::string> vec_wire;
        std::set<std::string> vec_orGate;
        std::set<std::string> vec_notGate;
        std::set<std::string> vec_majorityGate;
        std::set<std::string> vec_redundancyNode;  //buffer
        std::set<std::string> vec_fanoutNode;
        std::set<unsigned int> outputNodesIndex;
        std::set<unsigned int> effectiveNodes;        
    };

};