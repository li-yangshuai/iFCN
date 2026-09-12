#pragma once

#include "scalarParseOptions.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <istream>
#include <iterator>
#include <regex>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ifcn::verilog {
struct LogicExpression {
    enum class Kind { Signal, Not, And, Or, Xor, Majority };
    Kind kind;
    std::string signal;
    std::vector<std::shared_ptr<LogicExpression>> operands;
};
using Expression = std::shared_ptr<LogicExpression>;
using ExpressionKind = LogicExpression::Kind;

inline Expression expressionNode(ExpressionKind kind, std::vector<Expression> operands = {},
                          std::string signal = {})
{
    return std::make_shared<LogicExpression>(LogicExpression{kind, std::move(signal), std::move(operands)});
}

// Scalar bitwise Verilog precedence: parentheses/unary ~, &, ^, then |.
// Unsupported operators fail instead of silently changing the circuit.
class LogicExpressionParser {
public:
    explicit LogicExpressionParser(const std::string& source) : source_(source) {}
    Expression parse() {
        auto result = parseOr();
        skipSpace();
        if (offset_ != source_.size()) fail("unexpected token");
        return result;
    }
private:
    const std::string& source_;
    std::size_t offset_ = 0;
    [[noreturn]] void fail(const char* reason) const {
        throw std::runtime_error(std::string("Invalid scalar Verilog expression '") + source_ +
                                 "' at offset " + std::to_string(offset_) + ": " + reason);
    }
    void skipSpace() {
        while (offset_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[offset_]))) ++offset_;
    }
    bool consume(char token) {
        skipSpace();
        if (offset_ == source_.size() || source_[offset_] != token) return false;
        ++offset_;
        return true;
    }
    Expression parseOr() {
        auto left = parseXor();
        while (consume('|')) left = expressionNode(ExpressionKind::Or, {left, parseXor()});
        return left;
    }
    Expression parseXor() {
        auto left = parseAnd();
        while (consume('^')) left = expressionNode(ExpressionKind::Xor, {left, parseAnd()});
        return left;
    }
    Expression parseAnd() {
        auto left = parseUnary();
        while (consume('&')) left = expressionNode(ExpressionKind::And, {left, parseUnary()});
        return left;
    }
    Expression parseUnary() {
        if (consume('~')) return expressionNode(ExpressionKind::Not, {parseUnary()});
        if (consume('(')) {
            auto nested = parseOr();
            if (!consume(')')) fail("missing closing parenthesis");
            return nested;
        }
        skipSpace();
        const auto start = offset_;
        if (offset_ == source_.size() ||
            !(std::isalpha(static_cast<unsigned char>(source_[offset_])) || source_[offset_] == '_'))
            fail("expected a scalar signal name");
        ++offset_;
        while (offset_ < source_.size()) {
            const unsigned char c = source_[offset_];
            if (!std::isalnum(c) && c != '_' && c != '$') break;
            ++offset_;
        }
        return expressionNode(ExpressionKind::Signal, {}, source_.substr(start, offset_ - start));
    }
};

inline std::string expressionKey(const Expression& expression)
{
    if (expression->kind == ExpressionKind::Signal) return expression->signal;
    std::vector<std::string> children;
    for (const auto& child : expression->operands) children.push_back(expressionKey(child));
    if (expression->kind != ExpressionKind::Not) std::sort(children.begin(), children.end());
    std::string key = std::to_string(static_cast<int>(expression->kind)) + "(";
    for (const auto& child : children) key += child + ",";
    return key + ")";
}

inline void collectOperands(const Expression& expression, ExpressionKind kind, std::vector<Expression>& terms)
{
    if (expression->kind == kind) {
        for (const auto& child : expression->operands) collectOperands(child, kind, terms);
    } else terms.push_back(expression);
}

inline bool isLiteral(const Expression& expression)
{
    return expression->kind == ExpressionKind::Signal ||
           (expression->kind == ExpressionKind::Not &&
            expression->operands[0]->kind == ExpressionKind::Signal);
}

inline Expression recognizeMajority(const Expression& expression)
{
    if (expression->kind != ExpressionKind::Or) return expression;
    std::vector<Expression> products;
    collectOperands(expression, ExpressionKind::Or, products);
    if (products.size() != 3) return expression;
    std::map<std::string, Expression> literals;
    std::set<std::pair<std::string, std::string>> pairs;
    for (const auto& product : products) {
        std::vector<Expression> factors;
        collectOperands(product, ExpressionKind::And, factors);
        if (factors.size() != 2 || !isLiteral(factors[0]) || !isLiteral(factors[1])) return expression;
        auto a = expressionKey(factors[0]), b = expressionKey(factors[1]);
        if (a == b) return expression;
        literals[a] = factors[0]; literals[b] = factors[1];
        if (b < a) std::swap(a, b);
        pairs.insert({a, b});
    }
    // Exactly the three distinct pairwise products of three signed literals.
    // Merely having parentheses or three OR terms does not imply majority.
    if (literals.size() != 3 || pairs.size() != 3) return expression;
    std::vector<Expression> inputs;
    for (const auto& literal : literals) inputs.push_back(literal.second);
    return expressionNode(ExpressionKind::Majority, std::move(inputs));
}

inline Expression normalizeExpression(const Expression& expression)
{
    for (auto& child : expression->operands) child = normalizeExpression(child);
    if (expression->kind == ExpressionKind::Not && expression->operands[0]->kind == ExpressionKind::Not)
        return expression->operands[0]->operands[0];
    if ((expression->kind == ExpressionKind::And || expression->kind == ExpressionKind::Or) &&
        expressionKey(expression->operands[0]) == expressionKey(expression->operands[1]))
        return expression->operands[0];
    if (expression->kind == ExpressionKind::Xor) {
        const auto& a = expression->operands[0]; const auto& b = expression->operands[1];
        // XOR = (a | b) & ~(a & b). Shared operands stay shared in the DAG.
        return normalizeExpression(expressionNode(ExpressionKind::And, {
            expressionNode(ExpressionKind::Or, {a, b}),
            expressionNode(ExpressionKind::Not, {expressionNode(ExpressionKind::And, {a, b})})}));
    }
    return recognizeMajority(expression);
}
struct ScalarModule {
    std::string name;
    std::set<std::string> inputs, outputs, wires;
    std::vector<std::pair<std::string, std::string>> assignments;
};

inline std::string scalarExpressionText(const Expression& expression)
{
    if (expression->kind == ExpressionKind::Signal) return expression->signal;
    if (expression->kind == ExpressionKind::Not)
        return "~(" + scalarExpressionText(expression->operands[0]) + ")";
    if (expression->kind == ExpressionKind::Majority) {
        const auto a = scalarExpressionText(expression->operands[0]);
        const auto b = scalarExpressionText(expression->operands[1]);
        const auto c = scalarExpressionText(expression->operands[2]);
        // Keep the reader/lowerer interface as ordinary scalar Verilog. The
        // existing exact SOP recognizer emits the single majority node.
        return "((" + a + "&" + b + ")|(" + a + "&" + c + ")|(" + b + "&" + c + "))";
    }
    const char* token = expression->kind == ExpressionKind::And ? "&" :
                        expression->kind == ExpressionKind::Or ? "|" : "^";
    std::string result = "(";
    for (const auto& operand : expression->operands) {
        if (result.size() > 1) result += token;
        result += scalarExpressionText(operand);
    }
    return result + ")";
}

// A structural fold, not Boolean expansion: follow OR/alias assignments to
// three products, then follow each product to one two-literal AND. A factor
// naming another gate remains an atom, so MAJ(M1, M2, ~Cin) keeps both gates.
// Call only after the original module has passed dependency/cycle validation.
inline void foldScalarMajorityAssignments(ScalarModule& module)
{
    std::map<std::string, Expression> drivers;
    for (const auto& assignment : module.assignments)
        drivers.emplace(assignment.first, LogicExpressionParser(assignment.second).parse());
    const auto resolveAlias = [&](Expression expression) {
        while (expression->kind == ExpressionKind::Signal) {
            const auto driver = drivers.find(expression->signal);
            if (driver == drivers.end()) break;
            expression = driver->second;
        }
        return expression;
    };
    std::function<Expression(const Expression&)> resolveLiteral;
    resolveLiteral = [&](const Expression& expression) -> Expression {
        if (expression->kind == ExpressionKind::Signal) {
            const auto driver = drivers.find(expression->signal);
            if (driver == drivers.end() ||
                (driver->second->kind != ExpressionKind::Signal && driver->second->kind != ExpressionKind::Not))
                return expression;
            const auto literal = resolveLiteral(driver->second);
            return literal ? literal : expression;
        }
        if (expression->kind == ExpressionKind::Not) {
            const auto literal = resolveLiteral(expression->operands[0]);
            if (!literal) return {};
            if (literal->kind == ExpressionKind::Not) return literal->operands[0];
            return expressionNode(ExpressionKind::Not, {literal});
        }
        return {};
    };
    std::function<void(const Expression&, std::vector<Expression>&)> sumTerms;
    sumTerms = [&](const Expression& expression, std::vector<Expression>& terms) {
        if (terms.size() > 3) return; // Large OR cones cannot match this pattern.
        const auto resolved = resolveAlias(expression);
        if (resolved->kind == ExpressionKind::Or)
            for (const auto& child : resolved->operands) sumTerms(child, terms);
        else terms.push_back(resolved);
    };
    std::function<Expression(const Expression&)> fold;
    fold = [&](const Expression& expression) -> Expression {
        for (auto& child : expression->operands) child = fold(child);
        if (expression->kind != ExpressionKind::Or) return expression;
        std::vector<Expression> terms;
        sumTerms(expression, terms);
        if (terms.size() != 3) return expression;
        std::vector<Expression> products;
        for (const auto& term : terms) {
            std::vector<Expression> factors;
            collectOperands(term, ExpressionKind::And, factors);
            if (factors.size() != 2) return expression;
            auto a = resolveLiteral(factors[0]), b = resolveLiteral(factors[1]);
            if (!a || !b) return expression;
            products.push_back(expressionNode(ExpressionKind::And, {a, b}));
        }
        const auto sop = expressionNode(ExpressionKind::Or, std::move(products));
        const auto recognized = recognizeMajority(sop);
        return recognized->kind == ExpressionKind::Majority ? recognized : expression;
    };
    // The reader has topologically ordered assignments. Fold producers before
    // consumers, retaining original product drivers until every match is done.
    for (const auto& assignment : module.assignments)
        drivers[assignment.first] = fold(drivers.at(assignment.first));

    // Keep every observed output, including equal signals and output taps used
    // by later gates. Remove only assignments outside all output dependency cones.
    std::set<std::string> live;
    std::function<void(const Expression&)> visitExpression;
    std::function<void(const std::string&)> visitSignal;
    visitSignal = [&](const std::string& name) {
        const auto driver = drivers.find(name);
        if (driver != drivers.end() && live.insert(name).second) visitExpression(driver->second);
    };
    visitExpression = [&](const Expression& expression) {
        if (expression->kind == ExpressionKind::Signal) visitSignal(expression->signal);
        else for (const auto& operand : expression->operands) visitExpression(operand);
    };
    for (const auto& output : module.outputs) visitSignal(output);
    std::vector<std::pair<std::string, std::string>> retained;
    for (const auto& assignment : module.assignments)
        if (live.count(assignment.first))
            retained.emplace_back(assignment.first, scalarExpressionText(drivers.at(assignment.first)));
    module.assignments = std::move(retained);
}

inline std::string trimScalarText(const std::string& value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? std::string() :
           value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

inline ScalarModule readScalarModule(std::istream& stream)
{
    const std::string raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    std::string source;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '/' && i + 1 < raw.size() && raw[i + 1] == '/') {
            i = raw.find('\n', i + 2);
            if (i == std::string::npos) break;
            source += ' ';
        } else if (raw[i] == '/' && i + 1 < raw.size() && raw[i + 1] == '*') {
            const auto end = raw.find("*/", i + 2);
            if (end == std::string::npos) throw std::runtime_error("Unterminated Verilog block comment");
            i = end + 1;
            source += ' ';
        } else source += std::isspace(static_cast<unsigned char>(raw[i])) ? ' ' : raw[i];
    }
    ScalarModule module;
    const std::string identifier = "[A-Za-z_][A-Za-z0-9_$]*";
    const std::regex namePattern("^" + identifier + "$");
    const std::regex modulePattern("^module\\s+(" + identifier + ")\\s*\\((.*)\\)$");
    const std::regex declarationPattern("^(input|output|wire)\\s+(.+)$");
    const std::regex assignmentPattern("^assign\\s+(" + identifier + ")\\s*=\\s*(.+)$");
    const auto commaList = [](const std::string& list) {
        std::vector<std::string> fields;
        std::size_t start = 0;
        do {
            const auto comma = list.find(',', start);
            fields.push_back(trimScalarText(list.substr(start, comma == std::string::npos ? comma : comma - start)));
            if (comma == std::string::npos) break;
            start = comma + 1;
        } while (true);
        return fields;
    };
    const auto addDeclaration = [&](const std::string& direction, std::string name) {
        name = trimScalarText(name);
        if (name.rfind("wire ", 0) == 0) name = trimScalarText(name.substr(5));
        if (!std::regex_match(name, namePattern))
            throw std::runtime_error("Unsupported scalar Verilog declaration: " + name);
        auto& names = direction == "input" ? module.inputs : direction == "output" ? module.outputs : module.wires;
        names.insert(name);
    };
    bool moduleEnded = false;
    std::size_t start = 0;
    while (true) {
        const auto end = source.find(';', start);
        if (end == std::string::npos) break;
        const auto statement = trimScalarText(source.substr(start, end - start));
        start = end + 1;
        if (statement.empty()) continue;
        if (moduleEnded) throw std::runtime_error("Unexpected statement after endmodule");
        std::smatch match;
        if (std::regex_match(statement, match, modulePattern)) {
            if (!module.name.empty()) throw std::runtime_error("Expected one scalar Verilog module");
            module.name = match[1].str();
            std::string direction;
            for (const auto& field : commaList(match[2].str())) {
                std::smatch declaration;
                if (std::regex_match(field, declaration, declarationPattern)) {
                    direction = declaration[1].str();
                    if (direction == "wire") throw std::runtime_error("Module port requires input/output direction");
                    addDeclaration(direction, declaration[2].str());
                } else if (!direction.empty()) addDeclaration(direction, field);
                else if (!std::regex_match(field, namePattern))
                    throw std::runtime_error("Invalid scalar Verilog module port: " + field);
            }
        } else if (std::regex_match(statement, match, declarationPattern)) {
            if (module.name.empty()) throw std::runtime_error("Declaration before module header");
            for (const auto& name : commaList(match[2].str())) addDeclaration(match[1].str(), name);
        } else if (std::regex_match(statement, match, assignmentPattern)) {
            if (module.name.empty()) throw std::runtime_error("Assignment before module header");
            module.assignments.push_back({match[1].str(), match[2].str()});
        } else if (statement == "endmodule") {
            moduleEnded = true;
        } else {
            throw std::runtime_error("Unsupported scalar Verilog statement: " + statement);
        }
    }
    const auto trailing = trimScalarText(source.substr(start));
    if (trailing == "endmodule") {
        if (moduleEnded) throw std::runtime_error("Repeated endmodule");
        moduleEnded = true;
    } else if (!trailing.empty())
        throw std::runtime_error("Missing semicolon or unsupported Verilog statement: " + trailing);
    if (!moduleEnded) throw std::runtime_error("Missing endmodule");
    if (module.name.empty() || module.inputs.empty() || module.outputs.empty())
        throw std::runtime_error("Scalar Verilog module requires a name, primary inputs, and primary outputs");
    for (const auto& input : module.inputs)
        if (module.outputs.count(input)) throw std::runtime_error("Scalar port cannot be both input and output: " + input);
    std::map<std::string, std::string> drivers;
    for (const auto& assignment : module.assignments) {
        if ((!module.wires.count(assignment.first) && !module.outputs.count(assignment.first)) ||
            module.inputs.count(assignment.first))
            throw std::runtime_error("Assignment target is not a declared wire/output: " + assignment.first);
        if (!drivers.emplace(assignment).second)
            throw std::runtime_error("Multiple assignments to scalar signal: " + assignment.first);
    }
    // Verilog continuous assignments are unordered. Resolve dependencies before
    // constructing either graph, and report combinational cycles explicitly.
    std::map<std::string, int> states;
    std::vector<std::pair<std::string, std::string>> ordered;
    std::function<void(const std::string&)> visit;
    visit = [&](const std::string& signal) {
        if (module.inputs.count(signal) || states[signal] == 2) return;
        if (states[signal] == 1) throw std::runtime_error("Cyclic scalar Verilog assignment: " + signal);
        const auto driver = drivers.find(signal);
        if (driver == drivers.end()) throw std::runtime_error("Undriven scalar Verilog signal: " + signal);
        states[signal] = 1;
        std::function<void(const Expression&)> dependencies = [&](const Expression& expression) {
            if (expression->kind == ExpressionKind::Signal) visit(expression->signal);
            else for (const auto& operand : expression->operands) dependencies(operand);
        };
        dependencies(LogicExpressionParser(driver->second).parse());
        states[signal] = 2;
        ordered.push_back(*driver);
    };
    for (const auto& assignment : module.assignments) visit(assignment.first);
    for (const auto& output : module.outputs) visit(output);
    module.assignments = std::move(ordered);
    foldScalarMajorityAssignments(module);
    return module;
}

struct ScalarDagAdapter {
    bool allowMajority = true;
    std::function<bool(const std::string&)> nodeExists;
    std::function<bool(const std::string&)> nameReserved;
    std::function<bool(const std::string&)> isPrimaryOutput;
    std::function<void(const std::string&, const std::string&)> addNode;
    std::function<void(const std::string&, const std::string&)> addEdge;
};

inline void lowerScalarAssignment(const std::string& nodeName, const std::string& lineString,
                                  const ScalarDagAdapter& adapter,
                                  OutputBoundaryMode outputBoundaryMode = OutputBoundaryMode::Combinational)
{
    auto expression = normalizeExpression(LogicExpressionParser(lineString).parse());
    std::map<std::string, std::string> emitted;
    unsigned int sequence = 0;
    const auto temporaryName = [&]() {
        std::string name;
        do { name = nodeName + "_expr_" + std::to_string(sequence++); }
        while (adapter.nameReserved(name));
        return name;
    };
    const auto connectAlias = [&](const std::string& source, const std::string& target) {
        if (source == target) throw std::runtime_error("Self-referencing Verilog assignment: " + target);
        adapter.addNode(target, "wire");
        adapter.addEdge(source, target);
    };
    std::function<std::string(const Expression&, const std::string&)> emit;
    emit = [&](const Expression& term, const std::string& preferred) -> std::string {
        if (term->kind == ExpressionKind::Signal) {
            if (!adapter.nodeExists(term->signal))
                throw std::runtime_error("Undefined or not-yet-driven scalar signal: " + term->signal);
            if (preferred.empty()) return term->signal;
            connectAlias(term->signal, preferred);
            return preferred;
        }
        const auto key = expressionKey(term);
        const auto found = emitted.find(key);
        if (found != emitted.end()) {
            if (preferred.empty()) return found->second;
            connectAlias(found->second, preferred);
            return preferred;
        }
        std::vector<std::string> inputs;
        for (const auto& child : term->operands) inputs.push_back(emit(child, {}));
        if (term->kind == ExpressionKind::Not) {
            if (outputBoundaryMode == OutputBoundaryMode::Combinational &&
                !preferred.empty() && adapter.isPrimaryOutput(preferred)) {
                // The named output itself can be the inverter. Keep distinct
                // output identities, including outputs used by later gates.
                adapter.addNode(preferred, "not");
                adapter.addEdge(inputs[0], preferred);
                emitted[key] = preferred;
                return preferred;
            }
            // Stable literal inversion names share one physical NOT across
            // internal assignments without adding another inverter.
            const auto name = "~" + inputs[0];
            adapter.addNode(name, "not");
            adapter.addEdge(inputs[0], name);
            emitted[key] = name;
            if (preferred.empty()) return name;
            connectAlias(name, preferred);
            return preferred;
        }
        const auto name = preferred.empty() ? temporaryName() : preferred;
        if (term->kind == ExpressionKind::Majority && !adapter.allowMajority) {
            // A right/down clock template has only two incoming gate ports.
            // MAJ(a,b,c) = (a & b) | (c & (a | b)); keep the source function
            // while selecting a two-input gate basis for these layout flows.
            const auto ab = temporaryName();
            adapter.addNode(ab, "and");
            adapter.addEdge(inputs[0], ab); adapter.addEdge(inputs[1], ab);
            const auto aOrB = temporaryName();
            adapter.addNode(aOrB, "or");
            adapter.addEdge(inputs[0], aOrB); adapter.addEdge(inputs[1], aOrB);
            const auto carry = temporaryName();
            adapter.addNode(carry, "and");
            adapter.addEdge(inputs[2], carry); adapter.addEdge(aOrB, carry);
            adapter.addNode(name, "or");
            adapter.addEdge(ab, name); adapter.addEdge(carry, name);
            emitted[key] = name;
            return name;
        }
        if (term->kind == ExpressionKind::And) adapter.addNode(name, "and");
        else if (term->kind == ExpressionKind::Or) adapter.addNode(name, "or");
        else if (term->kind == ExpressionKind::Majority) adapter.addNode(name, "maj");
        else throw std::runtime_error("Unsupported lowered scalar expression");
        for (const auto& input : inputs) adapter.addEdge(input, name);
        emitted[key] = name;
        return name;
    };
    emit(expression, nodeName);
}

} // namespace ifcn::verilog
