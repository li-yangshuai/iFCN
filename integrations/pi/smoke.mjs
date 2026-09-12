import assert from "node:assert/strict";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const web = resolve(process.argv[2] || "../pi-web-ui/amedac-pi-web");
const sdk = join(web, "node_modules/@earendil-works/pi-coding-agent/dist");
const { default: ts } = await import(pathToFileURL(join(web, "node_modules/typescript/lib/typescript.js")));
const program = ts.createProgram([join(root, "integrations/pi/extension.ts")], {
  noEmit: true, strict: true, skipLibCheck: true, target: ts.ScriptTarget.ES2022,
  module: ts.ModuleKind.ESNext, moduleResolution: ts.ModuleResolutionKind.Bundler,
  types: ["node"], typeRoots: [join(web, "node_modules/@types")], baseUrl: web,
  paths: { "@earendil-works/*": ["node_modules/@earendil-works/*"] },
});
const diagnostics = ts.getPreEmitDiagnostics(program);
if (diagnostics.length) {
  console.error(ts.formatDiagnosticsWithColorAndContext(diagnostics, {
    getCurrentDirectory: () => root, getCanonicalFileName: (name) => name, getNewLine: () => "\n",
  }));
  process.exit(1);
}
console.log("Extension TypeScript check passed.");
if (process.argv.includes("--typecheck-only")) process.exit(0);
const { loadExtensions } = await import(pathToFileURL(join(sdk, "core/extensions/loader.js")));
const { parseFrontmatter } = await import(pathToFileURL(join(sdk, "index.js")));
const loaded = await loadExtensions([join(root, ".pi/extensions/ifcn/index.ts")], root);
assert.deepEqual(loaded.errors, []);
assert.equal(loaded.extensions.length, 1);
const definitions = loaded.extensions[0].tools;
const expected = ["ifcn_capabilities", "ifcn_run", "ifcn_status", "ifcn_artifact"];
assert.deepEqual([...definitions.keys()], expected);
const agent = parseFrontmatter(readFileSync(join(root, ".pi/agents/ifcn.md"), "utf8"));
assert.equal(agent.frontmatter.name, "ifcn");
for (const name of expected) assert.ok(agent.frontmatter.tools.includes(name));
const invoke = async (name, params) => {
  const response = await definitions.get(name).definition.execute("smoke", params, new AbortController().signal, () => {}, { cwd: root });
  return JSON.parse(response.content[0].text);
};
const capabilities = await invoke("ifcn_capabilities", { deep: true });
assert.equal(capabilities.ready, true);
console.log("Pi SDK loaded the main-agent definition and four executable tools.");
const started = await invoke("ifcn_run", { action: "start", input: join(root, "integrations/pi/examples/xor2.v"),
  until: "energy", model: "both", energy_profile: "standard", timeout_s: 180 });
let state = started;
console.log(`Submitted real XOR run: ${started.run_id}`);
try {
  for (let attempt = 0; attempt < 24 && state.active; attempt++) {
    state = await invoke("ifcn_status", { run_id: started.run_id, wait_seconds: 20 });
    console.log(`Stage ${state.current_stage}: ${state.status}`);
  }
  assert.equal(state.active, false);
  assert.equal(state.status, "completed", state.error || JSON.stringify(state.metrics));
  for (const step of ["parse", "pnr", "map", "simulate", "energy"]) assert.equal(state.stages[step].status, "completed");
  const results = {};
  for (const name of ["dag.json", "routed_dag.json", "pnr.json", "simulation.json", "energy.json"]) {
    const artifact = await invoke("ifcn_artifact", { run_id: started.run_id, artifact: name });
    assert.equal(artifact.truncated, false);
    results[name] = JSON.parse(artifact.content);
  }
  assert.equal(results["pnr.json"].layout_legal, true);
  assert.equal(results["simulation.json"].comparisons.length, 2);
  for (const comparison of results["simulation.json"].comparisons) {
    assert.equal(comparison.accuracy.max_absolute_error, 0);
    assert.ok(comparison.accuracy.output_samples > 0);
  }
  assert.equal(results["energy.json"].numerical_status, "nonnegative_bath_energy");
  assert.ok(results["energy.json"].average_dissipated_power_W >= 0);
  const destination = join(root, "output/pi-validation");
  mkdirSync(destination, { recursive: true });
  const summary = { validated_at: new Date().toISOString(), pi_sdk: "0.84.2", tools: expected,
    run_id: state.run_id, run_directory: state.run_directory, metrics: state.metrics, validation: state.validation,
    artifacts: state.artifacts };
  writeFileSync(join(destination, "sdk-smoke.json"), JSON.stringify(summary, null, 2) + "\n");
  console.log(JSON.stringify(summary, null, 2));
} finally {
  if (state.active) await invoke("ifcn_run", { action: "cancel", run_id: started.run_id });
}
