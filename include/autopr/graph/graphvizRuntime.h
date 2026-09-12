#pragma once

#include <graphviz/gvc.h>

#include <mutex>
#include <stdexcept>

namespace fcngraph::detail {

// Graphviz's Pango plugin caches font data across layouts. Destroying and
// recreating a GVC_t can leave that cache referring to the previous context.
// Both layout entry points therefore share one context until process exit,
// and serialize every Graphviz operation, including graph/layout destruction.
struct GraphvizRuntime
{
    std::mutex mutex;
    GVC_t *context = nullptr;

    ~GraphvizRuntime()
    {
        if (context != nullptr) {
            gvFreeContext(context);
        }
    }
};

inline GraphvizRuntime &graphvizRuntime()
{
    static GraphvizRuntime runtime;
    return runtime;
}

class GraphvizSession
{
public:
    GraphvizSession() : runtime_(graphvizRuntime()), lock_(runtime_.mutex)
    {
        if (runtime_.context == nullptr) {
            runtime_.context = gvContext();
        }
        if (runtime_.context == nullptr) {
            throw std::runtime_error("Graphviz could not create a layout context.");
        }
    }

    GVC_t *context() const { return runtime_.context; }

private:
    GraphvizRuntime &runtime_;
    std::unique_lock<std::mutex> lock_;
};

// Declare this after its session so all cleanup runs before the lock releases.
// gvFreeLayout also cleans a partially initialized, failed layout attempt.
struct GraphvizGraph
{
    explicit GraphvizGraph(const GraphvizSession &session) : context(session.context()) {}
    GraphvizGraph(const GraphvizGraph &) = delete;
    GraphvizGraph &operator=(const GraphvizGraph &) = delete;

    GVC_t *context;
    Agraph_t *graph = nullptr;
    bool layoutAttempted = false;

    int layout(const char *engine)
    {
        layoutAttempted = true;
        return gvLayout(context, graph, engine);
    }

    ~GraphvizGraph()
    {
        if (graph != nullptr) {
            if (layoutAttempted) {
                gvFreeLayout(context, graph);
            }
            agclose(graph);
        }
    }
};

} // namespace fcngraph::detail
