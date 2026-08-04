#include <autopr/algorithms/mapping.h>

#include <algorithm>
#include <iostream>
#include <vector>

namespace {

using fcngraph::Mapping;
using fcngraph::position;

bool isBoundedStraightCrossover(const std::vector<position>& cells)
{
    if (cells.size() != 5) {
        return false;
    }

    const bool horizontal = std::all_of(cells.begin(), cells.end(), [&](const position& cell) {
        return cell.second == cells.front().second;
    });
    const bool vertical = std::all_of(cells.begin(), cells.end(), [&](const position& cell) {
        return cell.first == cells.front().first;
    });
    if (horizontal == vertical) {
        return false;
    }

    const unsigned int tileX = cells.front().first / 5;
    const unsigned int tileY = cells.front().second / 5;
    for (std::size_t index = 0; index < cells.size(); ++index) {
        if (cells[index].first / 5 != tileX || cells[index].second / 5 != tileY) {
            return false;
        }
        if (index == 0) {
            continue;
        }
        const auto dx = cells[index].first > cells[index - 1].first
                            ? cells[index].first - cells[index - 1].first
                            : cells[index - 1].first - cells[index].first;
        const auto dy = cells[index].second > cells[index - 1].second
                            ? cells[index].second - cells[index - 1].second
                            : cells[index - 1].second - cells[index].second;
        if (dx + dy != 1) {
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    // Seed-1 legal 2DDWave routing for TOY/par_gen after rejecting parallel
    // inter-net overlap. Same-source fanout trunks remain shared; the five
    // remaining inter-net intersections are orthogonal and local.
    std::vector<std::vector<position>> routes{
        {{0, 0}, {0, 1}, {1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5}, {1, 6},
         {1, 7}, {1, 8}, {2, 8}, {3, 8}, {4, 8}, {5, 8}, {6, 8}, {7, 8}},
        {{0, 0}, {0, 1}, {0, 2}},
        {{2, 0}, {2, 1}, {2, 2}},
        {{2, 0}, {2, 1}, {3, 1}, {3, 2}, {3, 3}, {3, 4}, {4, 4}},
        {{4, 0}, {4, 1}, {5, 1}, {5, 2}, {5, 3}, {5, 4}, {6, 4}},
        {{4, 0}, {4, 1}, {4, 2}},
        {{2, 2}, {2, 3}, {3, 3}, {4, 3}, {5, 3}, {6, 3}, {6, 4}},
        {{6, 4}, {6, 5}},
        {{4, 2}, {4, 3}, {4, 4}},
        {{4, 4}, {4, 5}, {5, 5}, {6, 5}},
        {{6, 5}, {6, 6}, {7, 6}, {7, 7}},
        {{6, 5}, {6, 6}, {6, 7}},
        {{7, 7}, {7, 8}},
        {{7, 8}, {7, 9}},
        {{0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7}, {1, 7}, {2, 7},
         {3, 7}, {4, 7}, {5, 7}, {6, 7}},
        {{6, 7}, {6, 8}, {6, 9}, {7, 9}},
    };

    Mapping mapping;
    mapping.mapping_line(routes);

    std::string validationError;
    if (!mapping.validate_crossovers(&validationError)) {
        std::cerr << "legal par_gen crossovers were rejected: "
                  << validationError << '\n';
        return 1;
    }

    std::size_t crossoverCount = 0;
    for (const auto& routeEntry : mapping.crossline_list) {
        for (const auto& segment : routeEntry.second) {
            ++crossoverCount;
            if (!isBoundedStraightCrossover(segment)) {
                std::cerr << "invalid crossover segment for route ("
                          << routeEntry.first.first.first << ','
                          << routeEntry.first.first.second << ")->("
                          << routeEntry.first.second.first << ','
                          << routeEntry.first.second.second << "), cells="
                          << segment.size() << '\n';
                return 2;
            }
        }
    }

    if (crossoverCount != 5) {
        std::cerr << "expected five local par_gen crossovers, got "
                  << crossoverCount << '\n';
        return 3;
    }

    Mapping legacyLocalTurn;
    legacyLocalTurn.crossline_list[{{1, 0}, {3, 4}}].push_back(
        {{7, 10}, {8, 10}, {9, 10}, {9, 11}, {9, 12}, {9, 13}, {9, 14}});
    if (!legacyLocalTurn.validate_crossovers(&validationError)) {
        std::cerr << "local legacy crossover turn was rejected: "
                  << validationError << '\n';
        return 4;
    }

    Mapping invalidLiftedCorridor;
    invalidLiftedCorridor.crossline_list[{{1, 0}, {3, 4}}].push_back(
        {{7, 5}, {7, 6}, {7, 7}, {7, 8}, {7, 9}, {7, 10},
         {8, 10}, {9, 10}, {9, 11}, {9, 12}, {9, 13}, {9, 14}});
    if (invalidLiftedCorridor.validate_crossovers(&validationError)) {
        std::cerr << "cross-tile lifted corridor was not rejected\n";
        return 5;
    }
    return 0;
}
