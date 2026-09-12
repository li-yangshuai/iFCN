import { spawn } from "node:child_process";
import { readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { Type, StringEnum } from "@earendil-works/pi-ai";
import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";

const stages = ["parse", "pnr", "map", "simulate", "energy"] as const;

export default function (pi: ExtensionAPI) {
  const local = dirname(fileURLToPath(import.meta.url));
  let configuredRoot: string | undefined;
  try {
    configuredRoot = JSON.parse(readFileSync(join(local, "ifcn-config.json"), "utf8")).root;
  } catch { /* Source checkout can be loaded directly with pi -e. */ }
  const root = resolve(process.env.IFCN_ROOT || configuredRoot || join(local, "../.."));
  const runner = join(root, "scripts/ifcn_agent.py");

  async function invoke(args: string[], signal?: AbortSignal): Promise<Record<string, unknown>> {
    if (signal?.aborted) throw new Error("iFCN tool call cancelled.");
    return new Promise((resolveResult, reject) => {
      const child = spawn("python3", [runner, ...args], {
        cwd: root, env: process.env, stdio: ["ignore", "pipe", "pipe"], signal,
      });
      let stdout = "";
      let stderr = "";
      const timer = setTimeout(() => child.kill("SIGTERM"), 70000);
      child.stdout.on("data", (chunk) => { stdout += chunk; });
      child.stderr.on("data", (chunk) => { stderr = (stderr + chunk).slice(-3000); });
      child.on("error", (error) => { clearTimeout(timer); reject(error); });
      child.on("close", (code) => {
        clearTimeout(timer);
        try {
          const value = JSON.parse(stdout);
          if (code !== 0) reject(new Error(value.error || stderr || `iFCN exit ${code}`));
          else resolveResult(value);
        } catch (error) {
          reject(new Error(`Cannot read iFCN response: ${String(error)} ${stderr}`));
        }
      });
    });
  }

  function result(value: Record<string, unknown>) {
    return { content: [{ type: "text" as const, text: JSON.stringify(value, null, 2) }], details: value };
  }

  function compact(value: Record<string, unknown>) {
    const { configuration: _configuration, ...rest } = value;
    return rest;
  }

  pi.registerTool({
    name: "ifcn_capabilities", label: "iFCN 环境与能力",
    description: "Inspect the local iFCN installation and supported Verilog-to-DAG-to-QCA-to-simulation/energy flow. Use before starting work. Sequential support is reserved, not implemented. deep=true verifies Python/native imports.",
    parameters: Type.Object({ deep: Type.Optional(Type.Boolean()) }),
    async execute(_id, params, signal) {
      return result(await invoke(["doctor", ...(params.deep ? ["--deep"] : [])], signal));
    },
  });

  pi.registerTool({
    name: "ifcn_run", label: "iFCN 流程执行",
    description: "Start/resume/cancel a durable iFCN combinational circuit job. start takes an absolute Verilog path and runs dependencies through until (default energy). P&R candidates require bounded source/DAG logic validation and matching QCA I/O terminal counts; auto records failures and tries alternatives. Returns a run_id immediately; use ifcn_status to wait. Cancelling this tool call does NOT cancel a submitted job: use action=cancel. Inputs and artifacts are snapshotted. Physical simulation compares numerical engines and is NOT device waveform signoff; preview energy is exploratory.",
    parameters: Type.Object({
      action: StringEnum(["start", "resume", "cancel"] as const),
      input: Type.Optional(Type.String({ description: "Absolute path of the scalar combinational Verilog file (start)." })),
      run_id: Type.Optional(Type.String({ description: "Run id from a previous submission (resume/cancel)." })),
      until: Type.Optional(StringEnum(stages)),
      algorithm: Type.Optional(StringEnum(["normal_2ddwave", "compact", "june_random", "auto"] as const,
        { description: "P&R backend. auto tries available backends within a shared timeout and stops at the first validated layout; it does not optimize across algorithms. Check capabilities for availability and clock schemes." })),
      model: Type.Optional(StringEnum(["bistable", "coherence", "both"] as const)),
      energy_profile: Type.Optional(StringEnum(["preview", "standard"] as const)),
      timeout_s: Type.Optional(Type.Integer({ minimum: 1, maximum: 86400 })),
      samples: Type.Optional(Type.Integer({ minimum: 1, maximum: 86400 })),
      seed: Type.Optional(Type.Integer({ minimum: 0, maximum: 4294967295 })),
      vectors: Type.Optional(Type.String({ description: "Optional absolute .vt stimulus path. Inputs are snapshotted." })),
    }),
    async execute(_id, params, signal) {
      const args: string[] = [params.action];
      if (params.action === "start") {
        if (!params.input?.startsWith("/")) throw new Error("start requires an absolute WSL/Linux Verilog input path.");
        args.push(params.input);
        for (const [key, flag] of [["algorithm", "--algorithm"], ["model", "--model"], ["energy_profile", "--energy-profile"],
          ["timeout_s", "--timeout"], ["samples", "--samples"], ["seed", "--seed"], ["vectors", "--vectors"]] as const) {
          if (params[key] !== undefined) args.push(flag, String(params[key]));
        }
      } else {
        if (!params.run_id) throw new Error("resume/cancel requires run_id.");
        args.push(params.run_id);
        for (const key of ["input", "algorithm", "model", "energy_profile", "timeout_s", "samples", "seed", "vectors"] as const) {
          if (params[key] !== undefined) throw new Error(`${key} is fixed for an existing run; start a new run to change it.`);
        }
      }
      if (params.until && params.action !== "cancel") args.push("--until", params.until);
      return result(compact(await invoke(args, signal)));
    },
  });

  pi.registerTool({
    name: "ifcn_status", label: "iFCN 进度与结果",
    description: "Read a run's stage status, parameters, validated artifact list, metrics, and log tail. Set wait_seconds=20 when waiting for a background job to avoid busy polling. completed means requested stages executed, not functional signoff. Artifact paths are relative to run_directory.",
    parameters: Type.Object({ run_id: Type.String(), wait_seconds: Type.Optional(Type.Integer({ minimum: 0, maximum: 20 })) }),
    async execute(_id, params, signal, onUpdate) {
      const end = Date.now() + (params.wait_seconds ?? 0) * 1000;
      let value = await invoke(["status", params.run_id], signal);
      while (value.active && Date.now() < end) {
        onUpdate?.(result({ run_id: params.run_id, status: value.status, stage: value.current_stage }));
        await new Promise((done) => setTimeout(done, Math.min(1000, end - Date.now())));
        value = await invoke(["status", params.run_id], signal);
      }
      return result(compact(value));
    },
  });

  pi.registerTool({
    name: "ifcn_artifact", label: "iFCN 读取产物",
    description: "Read a named artifact from an iFCN run. result.json is the source-linked final report; pnr_attempts.json records algorithm choices and failures. logic.json records source/DAG equivalence, interface.json QCA terminal counts. Also dag.json, routed_dag.json, pnr.json, simulation.json, energy.json. Uses a manifest allow-list and verifies its hash. Large text is truncated to 24 KB; use the returned absolute file path for full inspection.",
    parameters: Type.Object({ run_id: Type.String(), artifact: Type.String() }),
    async execute(_id, params, signal) {
      return result(await invoke(["read", params.run_id, params.artifact], signal));
    },
  });
}
