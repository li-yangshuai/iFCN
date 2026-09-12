import { readFileSync, writeFileSync, mkdirSync, existsSync, statSync } from "node:fs";
import { dirname, resolve, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const web = resolve(process.argv.slice(2).find(arg => !arg.startsWith("--")) || join(root, "../pi-web-ui/amedac-pi-web"));
const sdkPath = join(web, "node_modules/@earendil-works/pi-coding-agent/dist");
const sdk = await import(pathToFileURL(join(sdkPath, "index.js")));
const { loadExtensions } = await import(pathToFileURL(join(sdkPath, "core/extensions/loader.js")));
const stamp = new Date().toISOString().replace(/[:.]/g, "-");
const destination = join(root, "output/pi-validation", "llm-" + stamp);
mkdirSync(destination, {recursive:true});
const output = (name,value) => writeFileSync(join(destination,name), typeof value === "string" ? value : JSON.stringify(value,null,2)+"\n");
const redact = value => String(value).replace(/https?:\/\/[^\s"'<>]+/g, "[endpoint redacted]").replace(/(?:Bearer\s+|sk-)[A-Za-z0-9._-]+/g, "[credential redacted]");
const input = join(root,"integrations/pi/examples/xor2.v");
const toolNames = ["ifcn_capabilities","ifcn_run","ifcn_status","ifcn_artifact"];
const originalSettings = sdk.SettingsManager.create(resolve(web,".."));
const provider = originalSettings.getDefaultProvider();
const modelId = originalSettings.getDefaultModel();
const summary = { started_at:new Date().toISOString(), provider, model:modelId, protocol:"single real Pi SDK LLM run; not a statistical benchmark", scope:"xor2.v through map only", deadline_ms:120000, output_token_budget:5000, tool_call_limit:12, output_directory:destination };
output("summary.json",summary);
let session;
let runtime;
let state;
let deadline;
let callCount=0;
let requestCount=0;
let outputTokens=0;
const trace=[];
const assistants=[];
const runIds=new Set();
const totals={input:0,output:0,cacheRead:0,cacheWrite:0,totalTokens:0,cost:0};
let definitions;
let error;
let timer;
const mark = (event) => { trace.push({at:new Date().toISOString(), ...event}); output("trace.json",trace); };
try {
  runtime = await sdk.ModelRuntime.create({signal:AbortSignal.timeout(15000)});
  const model=provider && modelId && runtime.getModel(provider,modelId);
  const available=!!model && (await runtime.getAvailable(provider,{signal:AbortSignal.timeout(15000)})).some(m=>m.id===modelId);
  Object.assign(summary,{available,model_found:!!model});
  console.log(JSON.stringify({provider,model:modelId,available,model_found:!!model}));
  if(!provider || !modelId) throw new Error("No explicit default provider/model is configured; no fallback provider was selected.");
  if(!model) throw new Error("Configured default model is absent from the local Pi model registry.");
  if(!available) throw new Error("Configured default model has no available authentication; no credentials were changed.");
  const loaded=await loadExtensions([join(root,".pi/extensions/ifcn/index.ts")],root);
  if(loaded.errors.length) throw new Error("Installed iFCN extension did not load: "+loaded.errors.map(e=>redact(e.error)).join("; "));
  definitions=loaded.extensions[0].tools;
  if(toolNames.some(name=>!definitions.has(name))) throw new Error("An installed iFCN tool is missing.");
  const body=sdk.parseFrontmatter(readFileSync(join(root,"integrations/pi/agent.md"),"utf8")).body;
  if(typeof body!=="string") throw new Error("Agent frontmatter parser returned no body.");
  output("system-prompt.txt",body);
  let prompt="请使用 iFCN 处理这个已有组合电路文件："+input+"。运行到 QCA 器件版图映射阶段（until=map）即可，不运行仿真或能耗分析。请自主检查环境、提交运行、等待实际结束，读取关键产物核对结果，再用中文简洁报告 run_id、已完成阶段、门级布局尺寸、QCA 元胞数、主要产物的绝对路径以及目前实际验证了什么、没有验证什么。";
  if(process.argv.includes("--require-artifacts")) prompt += "完成后必须调用 ifcn_artifact 分别读取 pnr.json 和 routed_dag.json，并将其内容与状态中的指标核对后再给最终答复。";
  summary.explicit_artifact_requirement=process.argv.includes("--require-artifacts");
  output("request.txt",prompt);
  const settings=sdk.SettingsManager.inMemory({defaultProvider:provider,defaultModel:modelId,defaultThinkingLevel:originalSettings.getDefaultThinkingLevel(),compaction:{enabled:false},retry:{enabled:false},providerRetry:{maxRetries:0}});
  const customTools=toolNames.map(name=>{
    const definition=definitions.get(name).definition;
    return {...definition,async execute(id,params,signal,onUpdate,ctx) {
      if(++callCount>12) { void session?.abort(); throw new Error("Validation tool-call limit reached."); }
      mark({event:"tool_start",id,tool:name,arguments:params});
      try {
        const response=await definition.execute(id,params,signal,onUpdate,ctx);
        const value=JSON.parse(response.content[0].text);
        if(name==="ifcn_run" && params.action==="start" && value.run_id)runIds.add(value.run_id);
        mark({event:"tool_end",id,tool:name,result:response});
        return response;
      } catch(e) {mark({event:"tool_error",id,tool:name,error:redact(e.message)});throw e;}
    }};
  });
  const loader=new sdk.DefaultResourceLoader({cwd:root,agentDir:sdk.getAgentDir(),settingsManager:settings,noExtensions:true,noSkills:true,noPromptTemplates:true,noThemes:true,noContextFiles:true,systemPrompt:body});
  await loader.reload();
  const stream=runtime.streamSimple.bind(runtime);
  runtime.streamSimple=(model,context,options={})=>{
    requestCount++;
    if(requestCount>13 || outputTokens>=5000) throw new Error("Validation model budget reached.");
    mark({event:"model_request",request:requestCount,allowed_tools:context.tools?.map(t=>t.name),max_output_tokens:Math.max(1,Math.min(2048,5000-outputTokens))});
    return stream(model,context,{...options,maxTokens:Math.max(1,Math.min(2048,5000-outputTokens)),maxRetries:0,timeoutMs:Math.min(60000,Math.max(1,deadline-Date.now()))});
  };
  ({session}=await sdk.createAgentSession({cwd:root,model,modelRuntime:runtime,thinkingLevel:originalSettings.getDefaultThinkingLevel(),tools:toolNames,customTools,resourceLoader:loader,sessionManager:sdk.SessionManager.inMemory(root),settingsManager:settings}));
  summary.active_tools=session.agent.state.tools.map(t=>t.name);
  output("effective-system-prompt.txt",session.agent.state.systemPrompt);
  session.subscribe(event=>{
    if(event.type==="message_end" && event.message.role==="assistant") {
      const m=event.message;
      const saved={role:m.role,content:m.content.filter(c=>c.type==="text" || c.type==="toolCall"),usage:m.usage,stopReason:m.stopReason,errorMessage:m.errorMessage ? redact(m.errorMessage):undefined};
      assistants.push(saved);
      const usage=m.usage || {};
      for(const key of ["input","output","cacheRead","cacheWrite","totalTokens"])totals[key]+=usage[key]||0;
      totals.cost+=usage.cost?.total||0;
      outputTokens+=usage.output||0;
      output("assistant-messages.json",assistants);
      mark({event:"assistant_message",message:saved});
    }
  });
  deadline=Date.now()+120000;
  timer=setTimeout(()=>{summary.deadline_reached=true;void session.abort();},120000);
  await session.prompt(prompt);
  clearTimeout(timer);
  output("final-answer.txt",assistants.at(-1)?.content.filter(c=>c.type==="text").map(c=>c.text).join("\n") || "");
} catch(e) {
  error=redact(e.message || e);
  summary.error=error;
  console.log(JSON.stringify({error}));
} finally {
  if(timer)clearTimeout(timer);
  if(session)session.dispose();
  const checks=[];
  if(definitions) for(const runId of runIds) {
    const result=spawnSync("python3",[join(root,"scripts/ifcn_agent.py"),"status",runId],{cwd:root,encoding:"utf8",timeout:15000});
    try {
      const value=JSON.parse(result.stdout);
      output("machine-status-"+runId+".json",value);
      if(value.active) {
        const cancelled=spawnSync("python3",[join(root,"scripts/ifcn_agent.py"),"cancel",runId],{cwd:root,encoding:"utf8",timeout:15000});
        mark({event:"harness_cleanup_cancel",run_id:runId,result:JSON.parse(cancelled.stdout)});
      }
      const artifacts=Array.isArray(value.artifacts) ? value.artifacts : Object.values(value.artifacts||{});
      const files=artifacts.map(a=>{
        const relative=typeof a==="string"?a:(a.path||a.relative_path||a.name);
        if(!relative)return {entry:a,verified:false,reason:"unknown artifact schema"};
        const path=join(value.run_directory,relative);
        const exists=existsSync(path);
        const hash=exists?createHash("sha256").update(readFileSync(path)).digest("hex"):undefined;
        return {path,exists,bytes:exists?statSync(path).size:0,sha256:hash,manifest_hash_matches:a.sha256?hash===a.sha256:undefined};
      });
      checks.push({run_id:runId,status:value.status,active:value.active,stages:value.stages,metrics:value.metrics,validation:value.validation,files,requested_stages_completed:["parse","pnr","map"].every(s=>value.stages?.[s]?.status==="completed"),simulation_not_run:value.stages?.simulate?.status!=="completed",energy_not_run:value.stages?.energy?.status!=="completed"});
    } catch(e) {checks.push({run_id:runId,error:redact(e.message)});}
  }
  output("machine-checks.json",checks);
  Object.assign(summary,{finished_at:new Date().toISOString(),requests:requestCount,tool_calls:callCount,tools_called:[...new Set(trace.filter(t=>t.event==="tool_start").map(t=>t.tool))],usage:totals,run_ids:[...runIds],machine_checks:checks.map(c=>({run_id:c.run_id,status:c.status,requested_stages_completed:c.requested_stages_completed})),task_completion:!error && checks.length===1 && checks[0].requested_stages_completed ? "completed" : "incomplete", all_four_tools_covered:toolNames.every(n=>trace.some(t=>t.event==="tool_start"&&t.tool===n)), artifact_files_read:trace.filter(t=>t.event==="tool_start"&&t.tool==="ifcn_artifact").map(t=>t.arguments.artifact)});
  output("summary.json",summary);
  console.log(JSON.stringify(summary,null,2));
}



