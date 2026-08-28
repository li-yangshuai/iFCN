#!/usr/bin/env python3
"""Check the geometry-first/random-clock toggle fixture using only stdlib."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent


def load(name: str) -> dict:
    with (ROOT / name).open("r", encoding="utf-8") as stream:
        return json.load(stream)


def point(value: list[int]) -> tuple[int, int]:
    assert len(value) == 2
    return int(value[0]), int(value[1])


def route_map(geometry: dict) -> dict[str, dict]:
    return {str(route["id"]): route for route in geometry["routes"]}


def validate_geometry(geometry: dict) -> int:
    assert geometry["phase_status"] == "unassigned"
    edge_count = 0
    for route in geometry["routes"]:
        points = [point(raw) for raw in route["points"]]
        assert len(points) >= 2
        assert len(set(points)) == len(points)
        for source, sink in zip(points, points[1:]):
            assert abs(source[0] - sink[0]) + abs(source[1] - sink[1]) == 1
            edge_count += 1
    return edge_count


def max_feedback_advance(problem: dict, geometry: dict) -> int:
    routes = route_map(geometry)
    loop = problem["feedback_loop"]
    route_capacity = sum(
        len(routes[route_id]["points"]) - 1 for route_id in loop["route_ids"]
    )
    macro_latency = sum(
        int(macro["latency_epochs"])
        for macro in problem["macros"]
        if macro["id"] in loop["macro_ids"]
    )
    temporal_latency = sum(
        int(arc["latency_epochs"])
        for arc in problem["temporal_arcs"]
        if arc["id"] in loop["temporal_arc_ids"]
    )
    return route_capacity + macro_latency + temporal_latency


def max_equal_run(values: list[int]) -> int:
    result = 1
    current = 1
    for previous, current_value in zip(values, values[1:]):
        if current_value == previous:
            current += 1
            result = max(result, current)
        else:
            current = 1
    return result


def validate_solution(problem: dict, geometry: dict, solution: dict) -> None:
    phase_count = int(problem["phase_count"])
    ii = int(solution["ii_epochs"])
    assert ii == int(problem["required_ii_epochs"])
    assert ii > 0 and ii % phase_count == 0
    assert int(solution["phase_count"]) == phase_count

    phases = {
        point(item["point"]): int(item["phase"])
        for item in solution["cell_phases"]
    }
    assert all(0 <= phase < phase_count for phase in phases.values())

    events = {str(name): int(epoch) for name, epoch in solution["event_epochs"].items()}
    arrivals_by_route = solution["route_arrival_epochs"]
    routes = route_map(geometry)
    route_advances = 0
    observed_max_run = 1

    for route_id, route in routes.items():
        points = [point(raw) for raw in route["points"]]
        arrivals = [int(value) for value in arrivals_by_route[route_id]]
        assert len(points) == len(arrivals)
        assert arrivals[0] == events[route["source_event"]]
        expected_sink = (
            events[route["sink_event"]]
            + int(route.get("iteration_distance", 0)) * ii
        )
        assert arrivals[-1] == expected_sink

        route_phases = []
        for coordinate, epoch in zip(points, arrivals):
            assert coordinate in phases
            assert phases[coordinate] == epoch % phase_count
            route_phases.append(phases[coordinate])

        for previous, current in zip(arrivals, arrivals[1:]):
            delta = current - previous
            assert delta in (0, 1)
            route_advances += delta
        observed_max_run = max(observed_max_run, max_equal_run(route_phases))

    macro_latency = 0
    for macro in problem["macros"]:
        actual = (
            events[macro["output_event"]]
            + int(macro.get("iteration_distance", 0)) * ii
            - events[macro["input_event"]]
        )
        expected = int(macro["latency_epochs"])
        assert actual == expected
        macro_latency += expected

    temporal_latency = 0
    total_iteration_distance = 0
    for arc in problem["temporal_arcs"]:
        actual = (
            events[arc["sink_event"]]
            + int(arc["iteration_distance"]) * ii
            - events[arc["source_event"]]
        )
        expected = int(arc["latency_epochs"])
        assert actual == expected
        temporal_latency += expected
        total_iteration_distance += int(arc["iteration_distance"])

    loop_advance = route_advances + macro_latency + temporal_latency
    assert total_iteration_distance > 0
    assert loop_advance == ii * total_iteration_distance
    assert loop_advance >= phase_count
    assert loop_advance % phase_count == 0
    assert loop_advance == int(solution["loop_advance_epochs"])

    assert observed_max_run <= int(problem["max_same_phase_run"])
    assert observed_max_run == int(solution["max_same_phase_run"])

    trace = [int(problem["initial_state_for_test_only"])]
    for _ in range(len(problem["expected_trace"]) - 1):
        trace.append(1 - trace[-1])
    assert trace == [int(value) for value in problem["expected_trace"]]


def main() -> None:
    problem = load("problem.json")
    short_geometry = load("short_geometry.json")
    repaired_geometry = load("repaired_geometry.json")
    solution = load("phase_solution.json")

    short_edges = validate_geometry(short_geometry)
    repaired_edges = validate_geometry(repaired_geometry)
    assert short_edges == 2
    assert repaired_edges == 4

    required_ii = int(problem["required_ii_epochs"])
    short_capacity = max_feedback_advance(problem, short_geometry)
    repaired_capacity = max_feedback_advance(problem, repaired_geometry)
    assert short_capacity == 3 and short_capacity < required_ii
    assert repaired_capacity == 5 and repaired_capacity >= required_ii

    validate_solution(problem, repaired_geometry, solution)
    print(
        "toggle1 random-clock fixture passed: "
        f"short max advance={short_capacity}<II={required_ii}; "
        f"dogleg capacity={repaired_capacity}, solved loop advance=4"
    )


if __name__ == "__main__":
    main()
