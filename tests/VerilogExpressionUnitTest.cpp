#include <autopr/graph/parse.h>
#include <Parse.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::string typeOf(fcngraph::Parse& parse, int node) { return parse.getNodeType(node); }
std::string typeOf(iFCN_Lab::Parse& parse, int node) { return parse.getNodeTypeString(node); }

struct TemporarySources {
    std::filesystem::path directory;
    TemporarySources() {
        directory = std::filesystem::temp_directory_path() /
            ("ifcn-verilog-semantics-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
    }
    ~TemporarySources() { std::filesystem::remove_all(directory); }
    std::string write(const std::string& name, const std::string& source) {
        const auto path = directory / (name + ".v");
        std::ofstream file(path); file << source;
        assert(file.good());
        return path.string();
    }
};

using Truth = std::function<bool(bool, bool, bool)>;

template<class Parser>
void assertTruth(Parser& parse, const std::string& output, const Truth& expected,
                 const std::array<std::string, 3>& inputNames = {"a", "b", "c"})
{
    for (int id = 0; id < parse.getm_numVertices(); ++id) {
        const auto type = typeOf(parse, id);
        const auto fanin = parse.getFaninsIndex(id).size();
        if (type == "input") assert(fanin == 0);
        else if (type == "maj") assert(fanin == 3);
        else if (type == "and" || type == "or") assert(fanin == 2);
        else assert(fanin == 1);
    }
    for (int mask = 0; mask < 8; ++mask) {
        std::map<int, bool> values;
        for (int i = 0; i < 3; ++i) {
            const int node = parse.getVertexIndex(inputNames[i]);
            if (node >= 0) values[node] = (mask >> i) & 1;
        }
        std::map<int, bool> visiting;
        std::function<bool(int)> evaluate = [&](int node) {
            assert(node >= 0);
            if (values.count(node)) return values[node];
            assert(!visiting[node]); visiting[node] = true;
            const auto type = typeOf(parse, node);
            std::vector<bool> inputs;
            for (auto fanin : parse.getFaninsIndex(node)) inputs.push_back(evaluate(fanin));
            bool value;
            if (type == "and") value = inputs.at(0) && inputs.at(1);
            else if (type == "or") value = inputs.at(0) || inputs.at(1);
            else if (type == "maj") value = (inputs.at(0) + inputs.at(1) + inputs.at(2)) >= 2;
            else if (type == "not") value = !inputs.at(0);
            else value = inputs.at(0);
            visiting[node] = false;
            values[node] = value;
            return value;
        };
        const bool actual = evaluate(parse.getVertexIndex(output));
        const bool wanted = expected(mask & 1, mask & 2, mask & 4);
        if (actual != wanted) {
            std::cerr << "Truth mismatch for " << output << ", vector " << mask << '\n';
            assert(actual == wanted);
        }
    }
}

template<class Parser>
void runSuite(const std::string& backend)
{
    TemporarySources sources;
    const auto expression = [&](const std::string& name, const std::string& rhs, const Truth& expected,
                                int majorityCount = 0) {
        Parser parse;
        parse.parseVerilog(sources.write(name,
            "module top(a,b,c,y);\ninput a,b,c;\noutput y;\nwire spare;\nassign y=" + rhs + ";\nendmodule\n"));
        assertTruth(parse, "y", expected);
        assert(parse.get_majorityGateNum_num() == majorityCount);
        return parse.get_notGateNum_num();
    };
    expression("parenthesized_and", "(a & b)", [](bool a,bool b,bool){return a&&b;});
    expression("parenthesized_or", "(a) | (b) | (c)", [](bool a,bool b,bool c){return a||b||c;});
    expression("mixed_precedence", "a | b & c", [](bool a,bool b,bool c){return a||(b&&c);});
    expression("nested", "(a | b) & ~(b | c)", [](bool a,bool b,bool c){return (a||b)&&!(b||c);});
    expression("not_majority", "(a & b) | (a & c) | (b & ~c)",
               [](bool a,bool b,bool c){return (a&&b)||(a&&c)||(b&&!c);});
    expression("xor", "a ^ b", [](bool a,bool b,bool){return a!=b;});
    expression("xor_nested", "a ^ (b | c)", [](bool a,bool b,bool c){return a!=(b||c);});
    expression("xor_precedence", "a | b ^ c & a", [](bool a,bool b,bool c){return a||(b!=(c&&a));});
    expression("repeated_xor_input", "a ^ a", [](bool,bool,bool){return false;});
    expression("double_not", "~~(a | b)", [](bool a,bool b,bool){return a||b;});
    assert(expression("shared_not", "(~a & b) | (~a & c)",
               [](bool a,bool b,bool c){return (!a&&b)||(!a&&c);}) == 1);

    // All eight literal-polarity combinations and all six pair orderings.
    for (int signs = 0; signs < 8; ++signs) {
        std::array<std::string,3> literal{"a","b","c"};
        for (int i=0;i<3;++i) if ((signs>>i)&1) literal[i]="~"+literal[i];
        std::array<std::string,3> pairs{
            "("+literal[0]+"&"+literal[1]+")", "("+literal[0]+"&"+literal[2]+")",
            "("+literal[1]+"&"+literal[2]+")"};
        std::array<int,3> order{0,1,2};
        do {
            const auto rhs=pairs[order[0]]+"|"+pairs[order[1]]+"|"+pairs[order[2]];
            const int inversions=expression("majority_"+std::to_string(signs)+std::to_string(order[0])+std::to_string(order[1]), rhs,
                [=](bool a,bool b,bool c){ return ((a!=bool(signs&1))+(b!=bool(signs&2))+(c!=bool(signs&4)))>=2; },1);
            assert(inversions == ((signs&1)!=0)+((signs&2)!=0)+((signs&4)!=0));
            const std::array<std::string,3> productNames{"p","q","r"};
            Parser chained;
            chained.parseVerilog(sources.write("signed_chain",
                "module top(input a,b,c,output y); wire p,q,r,s,alias; "
                "assign y=alias|"+productNames[order[2]]+"; assign alias=s; "
                "assign s="+productNames[order[0]]+"|"+productNames[order[1]]+"; "
                "assign p="+pairs[0]+"; assign q="+pairs[1]+"; assign r="+pairs[2]+"; endmodule"));
            assertTruth(chained,"y",[=](bool a,bool b,bool c){
                return ((a!=bool(signs&1))+(b!=bool(signs&2))+(c!=bool(signs&4)))>=2;
            });
            assert(chained.get_majorityGateNum_num()==1);
            assert(chained.get_andGateNum_num()==0 && chained.get_orGateNum_num()==0);
            assert(chained.get_notGateNum_num()==inversions);
        } while (std::next_permutation(order.begin(),order.end()));
    }
    Parser rawMajority;
    rawMajority.parseVerilog(std::string(IFCN_TEST_SOURCE_DIR)+"/tests/benchmarks_f/MAJ/1bitAdderMaj.v");
    assertTruth(rawMajority,"y0",[](bool a,bool b,bool c){return (a&&b)||(a&&!c)||(b&&!c);},{"x0","x1","x2"});
    assert(rawMajority.get_majorityGateNum_num()==1);
    assert(rawMajority.get_andGateNum_num()==0);
    assert(rawMajority.get_notGateNum_num()==1);
    const auto majority=rawMajority.getVertexIndex("n4");
    std::vector<std::string> majorityInputs;
    for (auto node:rawMajority.getFaninsIndex(majority)) majorityInputs.push_back(rawMajority.getNodeName(node));
    std::sort(majorityInputs.begin(),majorityInputs.end());
    assert((majorityInputs==std::vector<std::string>{"x0","x1","~x2"}));
    Parser chainedMajorities;
    chainedMajorities.parseVerilog(std::string(IFCN_TEST_SOURCE_DIR)+"/tests/benchmarks_f/TOY/1bitAdderMaj.v");
    assertTruth(chainedMajorities,"M3",[](bool a,bool b,bool c){
        const bool m1=(a&&b)||(a&&c)||(b&&c);
        const bool m2=(!m1&&a)||(!m1&&b)||(a&&b);
        return (!c&&m2)||(!c&&m1)||(m2&&m1);
    },{"A","B","Cin"});
    assert(chainedMajorities.get_majorityGateNum_num()==3);
    assert(chainedMajorities.get_andGateNum_num()==0);
    assert(chainedMajorities.get_orGateNum_num()==0);
    assert(chainedMajorities.get_notGateNum_num()==2);
    assert(chainedMajorities.getm_numVertices()==8);
    for (const auto& name : {"M11","M12","M13","M14","M21","M22","M23","M31","M32","M33","M34"})
        assert(chainedMajorities.getVertexIndex(name)<0);

    // Product and OR assignments may be shared, ordered arbitrarily, and feed
    // separately observed outputs. Folding must preserve each live identity.
    Parser majorityTap;
    majorityTap.parseVerilog(sources.write("majority_tap",
        "module top(input a,b,c,output y,z,tap,forward); wire ab,ac,bc,partial; "
        "assign y=partial|bc; assign z=y; assign tap=ab; assign forward=tap&c; "
        "assign partial=ab|ac; assign bc=b&c; assign ac=a&c; assign ab=a&b; endmodule"));
    assertTruth(majorityTap,"y",[](bool a,bool b,bool c){return (a&&b)||(a&&c)||(b&&c);});
    assertTruth(majorityTap,"z",[](bool a,bool b,bool c){return (a&&b)||(a&&c)||(b&&c);});
    assertTruth(majorityTap,"tap",[](bool a,bool b,bool){return a&&b;});
    assertTruth(majorityTap,"forward",[](bool a,bool b,bool c){return a&&b&&c;});
    assert(majorityTap.get_majorityGateNum_num()==1);
    assert(majorityTap.getOutputNodesIndex().size()==4);
    assert(majorityTap.getVertexIndex("y")!=majorityTap.getVertexIndex("z"));
    assert(typeOf(majorityTap,majorityTap.getVertexIndex("ab"))=="and");
    assert(majorityTap.getVertexIndex("ac")<0 && majorityTap.getVertexIndex("bc")<0);
    assert(majorityTap.getVertexIndex("partial")<0);
    assert(majorityTap.getFanoutsIndex(majorityTap.getVertexIndex("tap")).size()==1);
    majorityTap.optimizeAIOG_DRC(2,2,2,2,2,2);
    majorityTap.optimizeBufferNode();
    assertTruth(majorityTap,"tap",[](bool a,bool b,bool){return a&&b;});
    assertTruth(majorityTap,"forward",[](bool a,bool b,bool c){return a&&b&&c;});
    assert(majorityTap.getOutputNodesIndex().size()==4);

    Parser gateLiterals;
    gateLiterals.parseVerilog(sources.write("majority_gate_literals",
        "module top(input a,b,c,output y,tap); wire n,alias,inverted,p,q,r,s; "
        "assign n=a|b; assign alias=n; assign inverted=~alias; "
        "assign p=inverted&b; assign q=~n&c; assign r=b&c; assign s=p|q; "
        "assign y=s|r; assign tap=n; endmodule"));
    assertTruth(gateLiterals,"y",[](bool a,bool b,bool c){return (!(a||b)&&b)||(!(a||b)&&c)||(b&&c);});
    assertTruth(gateLiterals,"tap",[](bool a,bool b,bool){return a||b;});
    assert(gateLiterals.get_majorityGateNum_num()==1);
    assert(gateLiterals.get_orGateNum_num()==1);
    assert(gateLiterals.get_notGateNum_num()==1);
    assert(gateLiterals.get_andGateNum_num()==0);
    assert(gateLiterals.getVertexIndex("n")>=0);
    assert(gateLiterals.getVertexIndex("inverted")<0);

    Parser nonMajorityChain;
    nonMajorityChain.parseVerilog(sources.write("non_majority_chain",
        "module top(input a,b,c,output y); wire ab,ac,bc,partial; "
        "assign ab=a&b; assign ac=a&c; assign bc=b&~c; "
        "assign partial=ab|ac; assign y=partial|bc; endmodule"));
    assertTruth(nonMajorityChain,"y",[](bool a,bool b,bool c){return (a&&b)||(a&&c)||(b&&!c);});
    assert(nonMajorityChain.get_majorityGateNum_num()==0);
    assert(nonMajorityChain.get_andGateNum_num()==3);
    assert(nonMajorityChain.get_orGateNum_num()==2);
    Parser ansiXor;
    ansiXor.parseVerilog(std::string(IFCN_TEST_SOURCE_DIR)+"/tests/benchmarks_f/TOY/xor2_demo.v");
    assertTruth(ansiXor,"y",[](bool a,bool b,bool){return a!=b;});

    Parser unordered;
    unordered.parseVerilog(sources.write("unordered",
        "/* circuit */ module top(input a, b, c, output y); assign y=n; // forward ref\n"
        "wire n; assign n=(a & b) | c; endmodule"));
    assertTruth(unordered,"y",[](bool a,bool b,bool c){return (a&&b)||c;});
    Parser outputTap;
    outputTap.parseVerilog(sources.write("output_tap",
        "module top(input a,b,c,output tap,y); assign tap=a; assign y=tap&b; endmodule"));
    outputTap.optimizeAIOG_DRC(2,2,2,2,2,2);
    outputTap.optimizeBufferNode();
    assertTruth(outputTap,"tap",[](bool a,bool,bool){return a;});
    assertTruth(outputTap,"y",[](bool a,bool b,bool){return a&&b;});
    assert(outputTap.getOutputNodesIndex().size()==2);
    assert(outputTap.getFaninsIndex(outputTap.getVertexIndex("tap")).size()==1);
    Parser outputNot;
    outputNot.parseVerilog(sources.write("output_not",
        "module top(input a,b,c,output y,z); assign y=~a; assign z=~a; endmodule"));
    assertTruth(outputNot,"y",[](bool a,bool,bool){return !a;});
    assertTruth(outputNot,"z",[](bool a,bool,bool){return !a;});
    assert(outputNot.getOutputNodesIndex().size()==2);
    assert(outputNot.getVertexIndex("y")!=outputNot.getVertexIndex("z"));
    assert(typeOf(outputNot,outputNot.getVertexIndex("y"))=="not");
    assert(typeOf(outputNot,outputNot.getVertexIndex("z"))=="not");
    assert(outputNot.getm_numVertices()==5);
    assert(outputNot.get_redundancyGateNum_num()==0);
    Parser observedNot;
    observedNot.parseVerilog(sources.write("observed_not",
        "module top(input a,b,c,output tap,y); assign tap=~a; assign y=tap&b; endmodule"));
    observedNot.optimizeAIOG_DRC(2,2,2,2,2,2);
    observedNot.optimizeBufferNode();
    assertTruth(observedNot,"tap",[](bool a,bool,bool){return !a;});
    assertTruth(observedNot,"y",[](bool a,bool b,bool){return !a&&b;});
    assert(typeOf(observedNot,observedNot.getVertexIndex("tap"))=="not");
    assert(observedNot.getFanoutsIndex(observedNot.getVertexIndex("tap")).size()==1);
    Parser rawXnor;
    rawXnor.parseVerilog(std::string(IFCN_TEST_SOURCE_DIR)+"/tests/benchmarks_f/TOY/xnor2.v");
    assertTruth(rawXnor,"out",[](bool a,bool b,bool){return a==b;});
    rawXnor.optimizeAIOG_DRC(2,2,2,2,2,2);
    rawXnor.optimizeBufferNode();
    std::size_t drawableNodes=0;
    for (const auto& layer:rawXnor.getlayerNodeDivVec()) drawableNodes+=layer.size();
    assert(drawableNodes==8);
    assert(rawXnor.getEffectiveEdges().size()==9);
    assert(typeOf(rawXnor,rawXnor.getVertexIndex("out"))=="not");
    for (const auto& rhs : {"a &", "(a|b", "a + b", "a ? b : c", "unknown"}) {
        Parser invalid;
        bool rejected=false;
        try { invalid.parseVerilog(sources.write("invalid",
            "module top(input a,b,c,output y); assign y="+std::string(rhs)+"; endmodule")); }
        catch (const std::runtime_error& error) { rejected=std::string(error.what()).size()>10; }
        assert(rejected);
    }
    Parser cyclic;
    bool cycleRejected=false;
    try { cyclic.parseVerilog(sources.write("cycle",
        "module top(input a,output y); wire n; assign n=y&a; assign y=n; endmodule")); }
    catch (const std::runtime_error&) {cycleRejected=true;}
    assert(cycleRejected);
    Parser deadCycle;
    bool deadCycleRejected=false;
    try { deadCycle.parseVerilog(sources.write("dead_cycle",
        "module top(input a,output y); wire n,m; assign n=m&a; assign m=n; assign y=a; endmodule")); }
    catch (const std::runtime_error&) {deadCycleRejected=true;}
    assert(deadCycleRejected); // Dead-cone cleanup never hides invalid source.
    for (const auto& suffix : {"", "endmodule; endmodule", "endmodule; wire extra;"}) {
        Parser truncated;
        bool rejected=false;
        try { truncated.parseVerilog(sources.write("module_boundary",
            "module top(input a,output y); assign y=a; "+std::string(suffix))); }
        catch (const std::runtime_error&) {rejected=true;}
        assert(rejected);
    }
    std::cout<<backend<<": scalar precedence, 48 signed/permuted majority cases, cross-assignment majority folding, live output taps, raw majority/XOR, shared NOT, and invalid/cyclic syntax passed.\n";
}

void runSequentialBoundarySuite()
{
    const auto source=std::string(IFCN_TEST_SOURCE_DIR)+"/tests/benchmarks_f/SEQUENTIAL/rtl_v/toggle_ff_cut.v";
    fcngraph::Parse combinational;
    combinational.parseVerilog(source);
    assert(combinational.getm_numVertices()==2);
    assert(typeOf(combinational,combinational.getVertexIndex("d"))=="not");

    fcngraph::Parse sequential;
    sequential.parseVerilog(source,ifcn::verilog::OutputBoundaryMode::PreserveSequential);
    sequential.optimizeAIOG_DRC(2,2,2,2,2,2);
    sequential.optimizeBufferNode(); // Output event remains observable even during buffer cleanup.
    assertTruth(sequential,"d",[](bool q,bool,bool){return !q;},{"q","unused1","unused2"});
    const int q=sequential.getVertexIndex("q"),d=sequential.getVertexIndex("d");
    const int inverter=sequential.getVertexIndex("~q");
    assert(sequential.getm_numVertices()==3);
    assert(inverter>=0 && inverter!=d && inverter!=q);
    assert(typeOf(sequential,inverter)=="not");
    assert(typeOf(sequential,d)=="output");
    assert(sequential.getOutputNodesIndex().size()==1 && sequential.getOutputNodesIndex().count(d));
    const auto edgeList=sequential.getEffectiveEdges();
    const std::set<std::pair<int,int>> cutEdges(edgeList.begin(),edgeList.end());
    assert((cutEdges==std::set<std::pair<int,int>>{{q,inverter},{inverter,d}}));
    std::set<std::pair<int,int>> feedbackEdges;
    for (const auto& edge:cutEdges) feedbackEdges.emplace(edge.first==q?d:edge.first,edge.second);
    assert((feedbackEdges==std::set<std::pair<int,int>>{{d,inverter},{inverter,d}}));
    for (const auto& edge:feedbackEdges) assert(edge.first!=edge.second);

    TemporarySources sources;
    fcngraph::Parse outputTaps;
    outputTaps.parseVerilog(sources.write("sequential_output_taps",
        "module top(input q,output d,monitor); assign d=~q; assign monitor=d; endmodule"),
        ifcn::verilog::OutputBoundaryMode::PreserveSequential);
    assertTruth(outputTaps,"d",[](bool q,bool,bool){return !q;},{"q","unused1","unused2"});
    assertTruth(outputTaps,"monitor",[](bool q,bool,bool){return !q;},{"q","unused1","unused2"});
    assert(outputTaps.getOutputNodesIndex().size()==2);
    assert(outputTaps.getVertexIndex("d")!=outputTaps.getVertexIndex("monitor"));
    std::cout<<"native Parse: sequential output NOT boundary preserves D/Q identity and a two-node feedback cycle; combinational fusion remains enabled.\n";
}

void runTwoInputBasisSuite()
{
    TemporarySources sources;
    for (int sign = 0; sign < 8; ++sign) {
        const auto a = std::string(sign & 1 ? "~a" : "a");
        const auto b = std::string(sign & 2 ? "~b" : "b");
        const auto c = std::string(sign & 4 ? "~c" : "c");
        const auto source = sources.write("binary_basis_" + std::to_string(sign),
            "module top(input a,b,c,output y); assign y=(" + a + "&" + b + ")|(" +
            a + "&" + c + ")|(" + b + "&" + c + "); endmodule");
        const Truth expected = [sign](bool av, bool bv, bool cv) {
            return (int(av != bool(sign & 1)) + int(bv != bool(sign & 2)) +
                    int(cv != bool(sign & 4))) >= 2;
        };
        fcngraph::Parse native;
        native.parseVerilog(source, ifcn::verilog::OutputBoundaryMode::Combinational, false);
        iFCN_Lab::Parse binding;
        binding.parseVerilog(source, false);
        assertTruth(native, "y", expected);
        assertTruth(binding, "y", expected);
        for (int node = 0; node < native.getm_numVertices(); ++node)
            assert(native.getFaninsIndex(node).size() <= 2);
        for (int node = 0; node < binding.getm_numVertices(); ++node)
            assert(binding.getFaninsIndex(node).size() <= 2);
    }
    std::cout << "regular clock gate basis: signed majority identities retain truth with at most two gate fanins.\n";
}
}
int main() {
    runSuite<fcngraph::Parse>("native Parse");
    runSuite<iFCN_Lab::Parse>("layout binding Parse");
    runSequentialBoundarySuite();
    runTwoInputBasisSuite();
}
