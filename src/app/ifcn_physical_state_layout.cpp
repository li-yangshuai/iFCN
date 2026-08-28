#include <autopr/sequential/physicalStateMacro.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{

using fcngraph::position;
using namespace fcngraph::sequential;

std::string usage()
{
    return
        "usage: ifcn_physical_state_layout <output.tex> "
        "[--kind reset-toggle|t1-toggle|johnson2-reset|johnson4] "
        "[--origin <x>,<y>]\n";
}

position parseOrigin(const std::string &text)
{
    const auto separator = text.find(',');
    if (separator == std::string::npos || separator == 0 ||
        separator + 1 >= text.size() ||
        text.find(',', separator + 1) != std::string::npos)
    {
        throw std::runtime_error("--origin must have the form x,y");
    }
    const int x = std::stoi(text.substr(0, separator));
    const int y = std::stoi(text.substr(separator + 1));
    if (x < 0 || y < 0)
    {
        throw std::runtime_error("--origin coordinates cannot be negative");
    }
    return {static_cast<unsigned int>(x), static_cast<unsigned int>(y)};
}

} // namespace

int main(int argc, char **argv)
{
    try
    {
        if (argc < 2)
        {
            throw std::runtime_error(usage());
        }
        const std::filesystem::path outputPath(argv[1]);
        std::string kind = "reset-toggle";
        position origin{4, 4};
        for (int index = 2; index < argc; ++index)
        {
            const std::string option(argv[index]);
            const auto value = [&](const char *name) -> std::string {
                if (index + 1 >= argc)
                {
                    throw std::runtime_error(
                        std::string("missing value for ") + name);
                }
                return argv[++index];
            };
            if (option == "--kind")
            {
                kind = value("--kind");
                if (kind != "reset-toggle" && kind != "t1-toggle" &&
                    kind != "johnson2-reset" && kind != "johnson4")
                {
                    throw std::runtime_error("unsupported state macro: " + kind);
                }
            }
            else if (option == "--origin")
            {
                origin = parseOrigin(value("--origin"));
            }
            else
            {
                throw std::runtime_error(
                    "unknown option: " + option + "\n" + usage());
            }
        }

        PhysicalStateMacro macro;
        if (kind == "reset-toggle")
        {
            macro = makePhysicalResetToggleMacro(origin);
        }
        else if (kind == "t1-toggle")
        {
            macro = makePhysicalT1ToggleMacro(origin);
        }
        else if (kind == "johnson2-reset")
        {
            macro = makePhysicalResetJohnson2Macro(origin);
        }
        else
        {
            macro = makePhysicalJohnson4Macro(origin);
        }
        const PhysicalStateValidationResult validation =
            validatePhysicalStateMacro(macro);
        if (!validation.valid)
        {
            throw std::runtime_error(
                "physical state validation failed: " + validation.message);
        }
        const PhysicalStateMappingResult mapping =
            validatePhysicalStateMapping(macro);
        if (!mapping.valid)
        {
            throw std::runtime_error(
                "physical state Mapping failed: " + mapping.message);
        }

        if (!outputPath.parent_path().empty())
        {
            std::filesystem::create_directories(outputPath.parent_path());
        }
        std::string latexError;
        if (!writePhysicalStateMacroLatex(
                outputPath.string(), macro, &latexError))
        {
            throw std::runtime_error(latexError);
        }

        std::cout << "physical_sequential_layout=success"
                  << " kind=" << kind
                  << " nodes=" << macro.nodes.size()
                  << " routes=" << macro.nets.size()
                  << " II=" << macro.initiationInterval
                  << " mapped_qca_cells=" << mapping.uniqueQcaCells
                  << " tex=" << outputPath.string() << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "ifcn_physical_state_layout failed: "
                  << error.what() << '\n';
        return 1;
    }
}
