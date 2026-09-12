import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import crypto from "node:crypto";
import { execFile } from "node:child_process";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const argv = process.argv.slice(2);
const option = (name, fallback) => { const i=argv.indexOf(name); return i<0 ? fallback : argv[i+1]; };
const web = path.resolve(option("--web",path.join(root,"../pi-web-ui/amedac-pi-web")));
const specPath = option("--spec",null);
const runEnabled = argv.includes("--run");
const stamp = new Date().toISOString().replace(/[:.]/g,"-");
const destination = path.resolve(option("--output",path.join(root,"output/pi-validation","agent-comparison-"+stamp)));
const toolsAllowed = ["ifcn_capabilities","ifcn_run","ifcn_status","ifcn_artifact"];
const methods = ["script","generic_pi","domain_agent"];
const limits = {deadline_ms:180000,tool_calls:12,output_tokens:3500,backend_timeout_s:90,concurrency:2};
const sha256 = value => crypto.createHash("sha256").update(value).digest("hex");
const hashFile = file => sha256(fs.readFileSync(file));
const save = (dir,name,value) => fs.writeFileSync(path.join(dir,name),typeof value==="string" ? value : JSON.stringify(value,null,2)+"\n");
const clean = value => String(value).replace(/https?:\/\/[^\s"'<>]+/g,"[endpoint redacted]").replace(/(?:Bearer\s+|sk-)[A-Za-z0-9._-]+/g,"[credential redacted]");
const now = () => new Date().toISOString();
const defaultSpecs = [
  {id:"ordinary_map",description:"普通组合电路器件映射",input:path.join(root,"tests/benchmarks_pi/v1/TOY/xor2.v"),algorithm:"normal_2ddwave",required_artifacts:[]},
  {id:"specified_backend",description:"指定 compact 后端映射多数逻辑网络",input:path.join(root,"tests/benchmarks_pi/v1/MAJ/1bitAdderMaj.v"),algorithm:"compact",required_artifacts:[]},
  {id:"recover_missing_input",description:"输入不存在后使用给定备用路径恢复",input:path.join(root,"tests/benchmarks_pi/v1/TOY/xor2.v"),first_input:path.join(root,"tests/benchmarks_pi/v1/__intentionally_missing__.v"),algorithm:"normal_2ddwave",required_artifacts:[]},
  {id:"artifact_boundaries",description:"读取产物并准确报告验证边界",input:path.join(root,"tests/benchmarks_pi/v1/TOY/mux21.v"),algorithm:"normal_2ddwave",required_artifacts:["pnr.json","routed_dag.json","result.json"]}
];
const specs = specPath ? JSON.parse(fs.readFileSync(path.resolve(specPath),"utf8")).tasks : defaultSpecs;
if(!Array.isArray(specs)||specs.length<4)throw new Error("The pilot needs at least four predefined tasks.");
for(const spec of specs) {
  if(!["normal_2ddwave","compact","june_random","auto"].includes(spec.algorithm))throw new Error("Invalid specified backend.");
  spec.seed=spec.seed??1;
  spec.until="map";
  spec.required_artifacts=spec.required_artifacts??[];
}
const schedule=[];
for(let repeat=1;repeat<=2;repeat++) for(let i=0;i<specs.length;i++) {
  const rotation=(i+repeat-1)%methods.length;
  for(let j=0;j<methods.length;j++)schedule.push({task:specs[i].id,repeat,method:methods[(j+rotation)%methods.length]});
}
const scheduleSeed=20260905;
let randomState=scheduleSeed>>>0;
const scheduleRandom=()=>{randomState=(Math.imul(1664525,randomState)+1013904223)>>>0;return randomState/4294967296;};
for(let i=schedule.length-1;i>0;i--){const j=Math.floor(scheduleRandom()*(i+1));[schedule[i],schedule[j]]=[schedule[j],schedule[i]];}
fs.mkdirSync(destination,{recursive:true});
const protocol = {
  protocol_version:"ifcn-agent-comparison-pilot.v1",created_at:now(),run_enabled:runEnabled,
  source_root:root,output_directory:destination,tasks:specs,schedule,schedule_seed:scheduleSeed,limits,
  fixed_parameters:{until:"map",simulation_model:"bistable",energy_profile:"standard"},
  comparison:"The two LLM methods use exactly the same configured model, four tools, task prompt, parameters and budgets. Their system prompts differ. The script is a strong predefined workflow over structured task fields, not a natural-language agent.",
  scoring:"Task execution, request adherence, reported numbers, evidence provenance, validation boundaries, recovery, interventions, latency and token use are separate. Four-tool coverage is descriptive only.",
  caveat:"Pilot of four task types and two independent repeats. No statistical superiority or generalization claim.",
  cost_caveat:"Script token/cost fields are not applicable. Provider-reported zero price is not proof of zero billing.",
  repeat_policy:"Fresh in-memory sessions, no retries of a trial, no model switching, every timeout/error retained. Repeats use the same backend seed and independent model sessions.",
  ordering:"All 24 predefined trials are shuffled by a fixed seed (20260905) using a documented LCG and Fisher-Yates shuffle; two concurrent trials. Launch order is recorded. Shared CPU/load and provider cache may affect wall time. Backend stage time and inclusive overhead are also recorded separately.",
  setup_costs_not_measured:"Experiment authoring, baseline programming, prompt design and integration effort are not counted as runtime human interventions.",
  script_policy:"The script executes capabilities(deep), start, status(wait=20) until terminal, and requested artifact reads. For the recovery task it first attempts the specified nonexistent input, then uses the given fallback only after an input-not-found error.",
};
save(destination,"protocol.json",protocol);
if(!runEnabled) {
  console.log(JSON.stringify({mode:"prepared_only_no_model_requests",output_directory:destination,trials:schedule.length,inputs:specs.map(s=>({id:s.id,path:s.input,exists:fs.existsSync(s.input)}))},null,2));
  process.exit(0);
}
for(const spec of specs) {
  if(!fs.existsSync(spec.input))throw new Error("Input does not exist: "+spec.input);
  if(spec.first_input && fs.existsSync(spec.first_input))throw new Error("Intentionally missing input unexpectedly exists.");
  spec.input_sha256=hashFile(spec.input);
}
const sdkPath=path.join(web,"node_modules/@earendil-works/pi-coding-agent/dist");
const sdk=await import(pathToFileURL(path.join(sdkPath,"index.js")));
const {loadExtensions}=await import(pathToFileURL(path.join(sdkPath,"core/extensions/loader.js")));
const originalSettings=sdk.SettingsManager.create(path.resolve(web,".."));
const provider=originalSettings.getDefaultProvider();
const modelId=originalSettings.getDefaultModel();
if(!provider||!modelId)throw new Error("An explicit configured default model is required; no fallback selected.");
const availabilityRuntime=await sdk.ModelRuntime.create({signal:AbortSignal.timeout(15000)});
const configuredModel=availabilityRuntime.getModel(provider,modelId);
const available=!!configuredModel && (await availabilityRuntime.getAvailable(provider,{signal:AbortSignal.timeout(15000)})).some(m=>m.id===modelId);
if(!available)throw new Error("Configured default model is unavailable; no credentials or provider changed.");
const installedPath=path.join(root,".pi/extensions/ifcn/index.ts");
const loaded=await loadExtensions([installedPath],root);
if(loaded.errors.length)throw new Error("Installed extension could not load: "+loaded.errors.map(e=>clean(e.error)).join("; "));
const definitions=loaded.extensions[0].tools;
for(const name of toolsAllowed)if(!definitions.has(name))throw new Error("Tool missing: "+name);
if(!definitions.get("ifcn_run").definition.parameters?.properties?.algorithm)throw new Error("The installed tool schema has no algorithm parameter; backend integration is not ready.");
const domainSource=path.join(root,"integrations/pi/agent.md");
const domainBody=sdk.parseFrontmatter(fs.readFileSync(domainSource,"utf8")).body;
const trackedSources=[fileURLToPath(import.meta.url),domainSource,installedPath,...fs.readdirSync(path.join(root,"scripts")).filter(name=>/^ifcn_agent.*\.py$/.test(name)).sort().map(name=>path.join(root,"scripts",name))];
const fingerprintBackends=async()=>{
  const response=await execFileAsync("python3",[path.join(root,"scripts/ifcn_agent.py"),"doctor"],{cwd:root,encoding:"utf8",timeout:15000,maxBuffer:1024*1024});
  const doctor=JSON.parse(response.stdout);
  const executables={};
  for(const role of ["python","native_pnr_binary","mapping_binary","simulation_binary","energy_binary"]){
    const configuredPath=doctor.configuration[role];
    const present=!!configuredPath&&fs.existsSync(configuredPath);
    executables[role]={configured_path:configuredPath,present,resolved_path:present?fs.realpathSync(configuredPath):null,sha256:present?hashFile(configuredPath):null};
  }
  const normalFiles={};
  const walk=dir=>{for(const entry of fs.readdirSync(dir,{withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name))){
    const filename=path.join(dir,entry.name);
    if(entry.isDirectory())walk(filename);
    else if(/\.(py|so)$/.test(entry.name)&&fs.statSync(filename).isFile())normalFiles[filename]=hashFile(filename);
  }};
  walk(path.join(root,"include/gcn_rl_layout/src/algorithm"));
  return {configuration:doctor.configuration,algorithm_capabilities:doctor.algorithms,executables,normal_algorithm_files:normalFiles};
};
protocol.backend_fingerprints_start=await fingerprintBackends();
protocol.backend_comparison_caveats=["Native compact and june_random do not expose seed control; a recorded seed is not evidence of controlled native randomness.","normal_2ddwave can apply parity canonicalization. Across different backends the complete frontend-to-layout pipelines differ. This agent pilot fixes the same algorithm within each task across all methods."];
protocol.source_hashes=Object.fromEntries(trackedSources.map(p=>[p,hashFile(p)]));
protocol.default_model={provider,model:modelId,available:true,thinking:originalSettings.getDefaultThinkingLevel()??"sdk-default"};
protocol.started_at=now();
save(destination,"protocol.json",protocol);
console.log(JSON.stringify({provider,model:modelId,available,trials:schedule.length,output_directory:destination}));
const outputSchema = {
  status:"completed | failed | incomplete",run_id:"actual run id or null",
  completed_stages:["parse","pnr","map"],algorithm:"actual selected backend",
  width:"integer or null",height:"integer or null",cell_count:"integer or null",
  metrics_source:{tool:"ifcn_status or ifcn_artifact",run_id:"actual run id",artifact:"artifact name when using ifcn_artifact, otherwise null"},
  artifact_reads:["only names actually read using ifcn_artifact"],
  input_recovered:"true if requested failed input was actually attempted then fallback succeeded; otherwise false",
  rtl_equivalence:"tool-reported validation.rtl_equivalence",
  physical_function:"tool-reported validation.physical_function",
  simulation_performed:false,energy_performed:false,
  artifact_paths:["actual absolute artifact paths from this run"],
  explanation:"one concise Chinese sentence; do not claim unperformed checks"
};
const promptFor=spec => {
  let text="请完成本次 iFCN 任务："+spec.description+"。\n";
  if(spec.first_input)text+="请先实际尝试输入 "+spec.first_input+"；如果工具报告该输入不存在，则改用已给出的备用输入 "+spec.input+"，无需询问。\n";
  else text+="输入："+spec.input+"。\n";
  text+="必须使用 algorithm="+spec.algorithm+"、seed="+spec.seed+"、timeout_s="+limits.backend_timeout_s+"、model=bistable、energy_profile=standard，运行到 until=map；不要运行仿真或能耗阶段。先检查环境（deep=true），等待真实运行结束后报告。\n";
  if(spec.required_artifacts.length)text+="完成后必须使用 ifcn_artifact 读取这些产物："+spec.required_artifacts.join("、")+"。\n";
  text+="最多 "+limits.tool_calls+" 次工具调用、180 秒、3500 输出 tokens。最终只返回一个 JSON 对象，不加 Markdown 代码围栏，字段结构如下（不要照抄字段说明，填实际值；未知值用 null）：\n"+JSON.stringify(outputSchema,null,2);
  return text;
};
const python=async args=>{
  const result=await execFileAsync("python3",[path.join(root,"scripts/ifcn_agent.py"),...args],{cwd:root,encoding:"utf8",timeout:15000,maxBuffer:16*1024*1024});
  return JSON.parse(result.stdout);
};
const getValue=response=>JSON.parse(response.content.find(c=>c.type==="text").text);
function extractFinalJson(text) {
  try{return {value:JSON.parse(text.trim()),outside_text:false};}catch{}
  const stripped=text.trim().replace(/^\x60\x60\x60(?:json)?\s*/,"").replace(/\s*\x60\x60\x60$/,"");
  try{return {value:JSON.parse(stripped),outside_text:false,code_fence:true};}catch{}
  const start=text.indexOf("{"),end=text.lastIndexOf("}");
  if(start>=0&&end>start)try{return {value:JSON.parse(text.slice(start,end+1)),outside_text:!!(text.slice(0,start)+text.slice(end+1)).trim()};}catch{}
  return {value:null,outside_text:!!text.trim()};
}
async function inspectRun(runId) {
  const status=await python(["status",runId]);
  const files=[];
  for(const [name,entry]of Object.entries(status.artifacts||{})) {
    const filename=path.resolve(status.run_directory,entry.path);
    const contained=filename.startsWith(path.resolve(status.run_directory)+path.sep);
    const exists=contained&&fs.existsSync(filename);
    const hash=exists?hashFile(filename):null;
    files.push({name,path:filename,exists,sha256:hash,manifest_hash_matches:exists&&hash===entry.sha256});
  }
  const qca=files.find(f=>f.name==="device.qca"||f.path.endsWith(".qca"));
  const count=qca?.exists ? fs.readFileSync(qca.path,"utf8").split("[TYPE:QCADCell]").length-1 : null;
  const pnrEntry=files.find(f=>f.name==="pnr.json");
  const pnr=pnrEntry?.exists?JSON.parse(fs.readFileSync(pnrEntry.path,"utf8")):null;
  return {run_id:runId,status,files,qca_cell_blocks:count,qca_metric_matches:count!==null&&count===status.metrics?.mapping?.cell_count,all_files_verified:files.length>0&&files.every(f=>f.manifest_hash_matches),pnr};
}
const actualAlgorithm=state=>state?.metrics?.algorithm_selection?.selected_algorithm??state?.metrics?.pnr?.algorithm_selected??state?.metrics?.pnr?.algorithm??state?.parameters?.algorithm??null;
function evaluate(spec,trial,trace,inspections,finalText) {
  const parsed=extractFinalJson(finalText),report=parsed.value;
  const matching=inspections.filter(c=>c.status.source===spec.input&&c.status.artifacts?.["source.v"]?.sha256===spec.input_sha256);
  const essentialArtifacts=["source.v","dag.json","pnr.json","routed_dag.json","layout.ifcn","device.qca","result.json"];
  const completed=matching.filter(c=>c.status.status==="completed"&&!c.status.active&&["parse","pnr","map"].every(s=>c.status.stages?.[s]?.status==="completed")&&c.all_files_verified&&c.qca_metric_matches&&c.qca_cell_blocks>0&&essentialArtifacts.every(name=>c.files.some(f=>f.name===name&&f.manifest_hash_matches)));
  const chosen=completed.find(c=>c.run_id===report?.run_id)||completed.at(-1)||matching.at(-1);
  const starts=trace.filter(e=>e.event==="tool_start"&&e.tool==="ifcn_run"&&e.arguments.action==="start");
  const validStarts=starts.filter(e=>e.arguments.input===spec.input);
  const start=validStarts.at(-1);
  const parsedStatus=report?.status;
  const readNames=trace.filter(e=>e.event==="tool_end"&&e.tool==="ifcn_artifact"&&!e.isError).map(e=>({name:e.arguments.artifact,run_id:e.arguments.run_id}));
  const namesForRun=readNames.filter(e=>e.run_id===chosen?.run_id).map(e=>e.name);
  const firstInvalid=spec.first_input?starts.find(e=>e.arguments.input===spec.first_input):null;
  const invalidError=firstInvalid&&trace.find(e=>e.event==="tool_error"&&e.id===firstInvalid.id&&/not found/i.test(e.error));
  const recovered=!!(invalidError&&completed.length&&start&&firstInvalid.sequence<start.sequence);
  const actualParams=chosen?.status.parameters;
  const parameterMatches=!!start&&chosen?.status.requested_until==="map"&&actualParams?.algorithm===spec.algorithm&&actualParams?.seed===spec.seed&&actualParams?.timeout_s===limits.backend_timeout_s&&actualParams?.model==="bistable"&&actualParams?.energy_profile==="standard";
  const artifactsFollowed=spec.required_artifacts.every(n=>namesForRun.includes(n));
  const reportReads=Array.isArray(report?.artifact_reads)?report.artifact_reads:[];
  const readsCorrect=!!report&&Array.isArray(report.artifact_reads)&&reportReads.every(n=>namesForRun.includes(n));
  const metricSource=report?.metrics_source;
  const sourceCorrect=!!chosen&&metricSource?.run_id===chosen.run_id&&(
    (metricSource.tool==="ifcn_status"&&trace.some(e=>e.event==="tool_end"&&e.tool==="ifcn_status"&&e.arguments.run_id===chosen.run_id))||
    (metricSource.tool==="ifcn_artifact"&&namesForRun.includes(metricSource.artifact))
  );
  const sourceActuallyContainsMetrics=sourceCorrect&&trace.some(event=>{
    if(event.event!=="tool_end"||event.arguments.run_id!==chosen.run_id||event.tool!==metricSource.tool)return false;
    if(event.tool==="ifcn_artifact"&&event.arguments.artifact!==metricSource.artifact)return false;
    try {
      const envelope=JSON.parse(event.result.content.find(c=>c.type==="text").text);
      const payload=event.tool==="ifcn_status"?envelope:JSON.parse(envelope.content);
      return payload.metrics?.pnr?.width===report.width&&payload.metrics?.pnr?.height===report.height&&payload.metrics?.mapping?.cell_count===report.cell_count;
    } catch{return false;}
  });
  const reportPaths=Array.isArray(report?.artifact_paths)?report.artifact_paths:[];
  const pathsCorrect=!!chosen&&Array.isArray(report?.artifact_paths)&&reportPaths.length>0&&reportPaths.every(p=>chosen.files.some(f=>f.path===p&&f.manifest_hash_matches));
  const numbersCorrect=!!chosen&&!!report&&report.width===chosen.pnr?.width&&report.height===chosen.pnr?.height&&report.cell_count===chosen.qca_cell_blocks;
  const boundsCorrect=!!chosen&&!!report&&report.rtl_equivalence===chosen.status.validation?.rtl_equivalence&&report.physical_function===chosen.status.validation?.physical_function&&report.simulation_performed===false&&report.energy_performed===false;
  const stagesCorrect=!!report&&Array.isArray(report.completed_stages)&&["parse","pnr","map"].every(s=>report.completed_stages.includes(s))&&report.completed_stages.every(s=>chosen?.status.stages?.[s]?.status==="completed");
  const reportedAlgorithmCorrect=!!chosen&&report?.algorithm===actualAlgorithm(chosen.status);
  const backendTime=chosen?Object.values(chosen.status.stages).reduce((sum,s)=>sum+(s.started_at&&s.completed_at?(Date.parse(s.completed_at)-Date.parse(s.started_at))/1000:0),0):null;
  return {
    execution_completed:completed.length>0,requested_work_completed:completed.length>0&&parameterMatches,final_json_parseable:!!report,extra_unscored_text:parsed.outside_text,
    successful_run_id:completed.at(-1)?.run_id??null,reported_success:parsedStatus==="completed",
    false_success_claim:parsedStatus==="completed"&&completed.length===0,
    unsupported_numeric_claim:!!report&&["width","height","cell_count"].some(key=>report[key]!==null&&report[key]!==undefined&&report[key]!==({width:chosen?.pnr?.width,height:chosen?.pnr?.height,cell_count:chosen?.qca_cell_blocks}[key])),
    all_required_parameters_followed:parameterMatches,required_artifacts_read:artifactsFollowed,
    request_adherence:parameterMatches&&artifactsFollowed&&(!spec.first_input||recovered),
    numeric_claims_correct:numbersCorrect,provenance_claims_correct:readsCorrect&&sourceActuallyContainsMetrics&&pathsCorrect,
    artifact_read_claims_correct:readsCorrect,metrics_source_contains_claimed_numbers:sourceActuallyContainsMetrics,
    artifact_paths_correct:pathsCorrect,validation_boundaries_correct:boundsCorrect,
    completed_stage_claims_correct:stagesCorrect,algorithm_claim_correct:reportedAlgorithmCorrect,
    input_failure_recovery:spec.first_input?recovered:null,reported_recovery_correct:!!report&&report.input_recovered===recovered,
    human_interventions:0,external_evaluator_interventions:trial.cleanup_cancellations||0,
    task_delivery_correct:completed.length>0&&parameterMatches&&artifactsFollowed&&(!spec.first_input||recovered)&&parsedStatus==="completed"&&numbersCorrect&&boundsCorrect&&stagesCorrect&&reportedAlgorithmCorrect&&readsCorrect&&sourceActuallyContainsMetrics&&pathsCorrect,
    backend_stage_wall_s:backendTime,artifact_names_read:namesForRun,model_report:report,
    caveat:"Free-form explanation is retained for transparent human inspection; categorical and numeric fields are mechanically checked. No LLM judge used.",
  };
}
async function executeTrial(slot) {
  const spec=specs.find(s=>s.id===slot.task);
  const name=String(schedule.indexOf(slot)+1).padStart(2,"0")+"-"+slot.task+"-r"+slot.repeat+"-"+slot.method;
  const dir=path.join(destination,name);fs.mkdirSync(dir,{recursive:true});
  const trial={...slot,id:name,started_at:now(),model:slot.method==="script"?null:modelId,provider:slot.method==="script"?null:provider,limits,source_sha256:spec.input_sha256,status:"running",human_interventions:0};
  const trace=[],messages=[],runIds=new Set();
  let session,runtime,deadline,timer,toolCalls=0,requests=0,outputTokens=0,finalText="",wallStart;
  const usage={input:0,output:0,cacheRead:0,cacheWrite:0,totalTokens:0,cost_reported:0};
  const mark=event=>{trace.push({sequence:trace.length+1,at:now(),...event});save(dir,"trace.json",trace);};
  const taskPrompt=promptFor(spec);save(dir,"request.txt",taskPrompt);
  const aborter=new AbortController();
  const invoke=async(name,id,params,signal,onUpdate,ctx)=>{
    if(toolCalls>=limits.tool_calls)throw new Error("Tool-call budget exhausted.");
    toolCalls++;
    mark({event:"tool_start",id,tool:name,arguments:params});
    try {
      if(name==="ifcn_run"&&params.action==="start") {
        if(![spec.input,spec.first_input].filter(Boolean).includes(params.input))throw new Error("Input is outside this isolated trial.");
        if(params.until!=="map")throw new Error("This trial permits until=map only.");
        if((params.timeout_s??limits.backend_timeout_s)>limits.backend_timeout_s)throw new Error("Requested backend timeout exceeds trial budget.");
      }
      if(params.run_id&&!runIds.has(params.run_id))throw new Error("Run id is outside this isolated trial.");
      const response=await definitions.get(name).definition.execute(id,params,signal,onUpdate,ctx||{cwd:root});
      const value=getValue(response);
      if(name==="ifcn_run"&&params.action==="start"&&value.run_id)runIds.add(value.run_id);
      mark({event:"tool_end",id,tool:name,arguments:params,result:response});
      return response;
    }catch(e){mark({event:"tool_error",id,tool:name,arguments:params,error:clean(e.message)});throw e;}
  };
  try {
    if(slot.method!=="script") {
      runtime=await sdk.ModelRuntime.create({signal:AbortSignal.timeout(15000)});
      const model=runtime.getModel(provider,modelId);
      const settings=sdk.SettingsManager.inMemory({defaultProvider:provider,defaultModel:modelId,defaultThinkingLevel:originalSettings.getDefaultThinkingLevel(),compaction:{enabled:false},retry:{enabled:false},providerRetry:{maxRetries:0}});
      const opts={cwd:root,agentDir:sdk.getAgentDir(),settingsManager:settings,noExtensions:true,noSkills:true,noPromptTemplates:true,noThemes:true,noContextFiles:true};
      if(slot.method==="domain_agent")opts.systemPrompt=domainBody;
      const loader=new sdk.DefaultResourceLoader(opts);await loader.reload();
      const stream=runtime.streamSimple.bind(runtime);
      runtime.streamSimple=(model,context,options={})=>{
        if(++requests>13||outputTokens>=limits.output_tokens)throw new Error("Model output budget exhausted.");
        const maxTokens=Math.max(1,Math.min(1500,limits.output_tokens-outputTokens));
        mark({event:"model_request",request:requests,allowed_tools:context.tools?.map(t=>t.name),max_output_tokens:maxTokens});
        return stream(model,context,{...options,maxTokens,maxRetries:0,timeoutMs:Math.min(60000,Math.max(1,deadline-Date.now()))});
      };
      const customTools=toolsAllowed.map(name=>({...definitions.get(name).definition,execute:(id,params,signal,onUpdate,ctx)=>invoke(name,id,params,signal,onUpdate,ctx)}));
      ({session}=await sdk.createAgentSession({cwd:root,model,modelRuntime:runtime,thinkingLevel:originalSettings.getDefaultThinkingLevel(),tools:toolsAllowed,customTools,resourceLoader:loader,sessionManager:sdk.SessionManager.inMemory(root),settingsManager:settings}));
      save(dir,"effective-system-prompt.txt",session.agent.state.systemPrompt);
      trial.active_tools=session.agent.state.tools.map(t=>t.name);
      session.subscribe(event=>{
        if(event.type==="message_end"&&event.message.role==="assistant") {
          const m=event.message;
          const record={role:m.role,content:m.content.filter(c=>c.type==="text"||c.type==="toolCall"),usage:m.usage,stopReason:m.stopReason,errorMessage:m.errorMessage?clean(m.errorMessage):undefined};
          messages.push(record);
          for(const key of ["input","output","cacheRead","cacheWrite","totalTokens"])usage[key]+=m.usage?.[key]||0;
          usage.cost_reported+=m.usage?.cost?.total||0;
          outputTokens+=m.usage?.output||0;
          save(dir,"assistant-messages.json",messages);mark({event:"assistant_message",message:record});
        }
      });
    } else save(dir,"baseline-policy.json",{policy:protocol.script_policy,structured_task:spec,natural_language_understanding:false});
    wallStart=Date.now();deadline=wallStart+limits.deadline_ms;
    timer=setTimeout(()=>{trial.deadline_reached=true;aborter.abort();void session?.abort();},limits.deadline_ms);
    if(slot.method==="script") {
      let index=0;
      const call=async(name,args)=>getValue(await invoke(name,"script-"+(++index),args,aborter.signal));
      await call("ifcn_capabilities",{deep:true});
      const args={action:"start",input:spec.first_input||spec.input,until:"map",algorithm:spec.algorithm,seed:spec.seed,timeout_s:limits.backend_timeout_s,model:"bistable",energy_profile:"standard"};
      let state,recovered=false;
      try{state=await call("ifcn_run",args);}
      catch(e){if(spec.first_input&&/not found/i.test(e.message)){recovered=true;state=await call("ifcn_run",{...args,input:spec.input});}else throw e;}
      do{state=await call("ifcn_status",{run_id:state.run_id,wait_seconds:20});}while(state.active&&Date.now()<deadline);
      const artifactReads=[];
      if(state.status==="completed")for(const artifact of spec.required_artifacts){await call("ifcn_artifact",{run_id:state.run_id,artifact});artifactReads.push(artifact);}
      const report={status:state.status==="completed"?"completed":state.status==="failed"?"failed":"incomplete",run_id:state.run_id,completed_stages:Object.entries(state.stages||{}).filter(([,v])=>v.status==="completed").map(([s])=>s),algorithm:actualAlgorithm(state),width:state.metrics?.pnr?.width??null,height:state.metrics?.pnr?.height??null,cell_count:state.metrics?.mapping?.cell_count??null,metrics_source:{tool:"ifcn_status",run_id:state.run_id,artifact:null},artifact_reads:artifactReads,input_recovered:recovered&&state.status==="completed",rtl_equivalence:state.validation?.rtl_equivalence??null,physical_function:state.validation?.physical_function??null,simulation_performed:state.stages?.simulate?.status==="completed",energy_performed:state.stages?.energy?.status==="completed",artifact_paths:Object.values(state.artifacts||{}).map(a=>path.join(state.run_directory,a.path)),explanation:"预定脚本根据真实运行状态和清单生成报告；未执行自然语言理解。"};
      finalText=JSON.stringify(report,null,2);
    } else {
      await session.prompt(taskPrompt);
      finalText=messages.at(-1)?.content.filter(c=>c.type==="text").map(c=>c.text).join("\n")||"";
      const providerError=messages.at(-1)?.errorMessage;
      if(providerError)trial.error=providerError;
    }
    trial.status=trial.deadline_reached?"timeout":trial.error?"error":"finished";
  }catch(e){trial.status=trial.deadline_reached?"timeout":"error";trial.error=clean(e.message||e);mark({event:"trial_error",error:trial.error});}
  finally{
    if(timer)clearTimeout(timer);
    trial.wall_time_s=wallStart?(Date.now()-wallStart)/1000:null;
    trial.finished_at=now();trial.tool_calls=toolCalls;trial.model_requests=requests;trial.usage=slot.method==="script"?null:usage;
    trial.tool_names=[...new Set(trace.filter(t=>t.event==="tool_start").map(t=>t.tool))];
    if(!finalText&&messages.length)finalText=messages.at(-1)?.content.filter(c=>c.type==="text").map(c=>c.text).join("\n")||"";
    save(dir,"final-answer.txt",finalText);
    if(session)session.dispose();
    const inspections=[];
    for(const runId of runIds)try{
      const inspection=await inspectRun(runId);inspections.push(inspection);
      if(inspection.status.active){trial.cleanup_cancellations=(trial.cleanup_cancellations||0)+1;mark({event:"harness_cleanup_cancel",run_id:runId,result:await python(["cancel",runId])});}
    }catch(e){mark({event:"machine_check_error",run_id:runId,error:clean(e.message)});}
    save(dir,"machine-checks.json",inspections);
    trial.metrics=evaluate(spec,trial,trace,inspections,finalText);
    trial.model_inclusive_overhead_s=trial.wall_time_s!==null&&trial.metrics.backend_stage_wall_s!==null?trial.wall_time_s-trial.metrics.backend_stage_wall_s:null;
    save(dir,"summary.json",trial);
    console.log(JSON.stringify({trial:name,status:trial.status,completed:trial.metrics.requested_work_completed,delivery_correct:trial.metrics.task_delivery_correct,wall_s:trial.wall_time_s,output_tokens:trial.usage?.output??null}));
  }
  return trial;
}
const results=[];
let next=0;
async function worker(){while(next<schedule.length){const slot=schedule[next++];const trial=await executeTrial(slot);results.push(trial);save(destination,"results.json",results);}}
await Promise.all(Array.from({length:limits.concurrency},worker));
protocol.backend_fingerprints_end=await fingerprintBackends();
protocol.backend_changed_during_experiment=JSON.stringify(protocol.backend_fingerprints_start)!==JSON.stringify(protocol.backend_fingerprints_end);
const finalHashes=Object.fromEntries(trackedSources.map(p=>[p,hashFile(p)]));
protocol.source_hashes_at_end=finalHashes;
protocol.source_changed_during_experiment=trackedSources.some(p=>protocol.source_hashes[p]!==finalHashes[p]);
protocol.finished_at=now();
save(destination,"protocol.json",protocol);
const groups=methods.map(method=>{
 const rows=results.filter(r=>r.method===method);
 const count=key=>rows.filter(r=>r.metrics[key]===true).length;
 const mean=key=>{const values=rows.map(r=>r[key]).filter(v=>typeof v==="number");return values.length?values.reduce((a,b)=>a+b,0)/values.length:null;};
 return {method,trials:rows.length,completed:count("requested_work_completed"),request_adherence:count("request_adherence"),delivery_correct:count("task_delivery_correct"),numbers_correct:count("numeric_claims_correct"),provenance_correct:count("provenance_claims_correct"),validation_boundaries_correct:count("validation_boundaries_correct"),false_success_claims:count("false_success_claim"),timeouts:rows.filter(r=>r.status==="timeout").length,errors:rows.filter(r=>r.status==="error").length,human_interventions:rows.reduce((n,r)=>n+r.metrics.human_interventions,0),mean_wall_s:mean("wall_time_s"),mean_backend_stage_wall_s:rows.reduce((n,r)=>n+(r.metrics.backend_stage_wall_s||0),0)/rows.length,total_usage:method==="script"?null:rows.reduce((u,r)=>{for(const k of ["input","output","cacheRead","cacheWrite","totalTokens","cost_reported"])u[k]=(u[k]||0)+(r.usage?.[k]||0);return u;},{})};
});
save(destination,"aggregate.json",{protocol_version:protocol.protocol_version,model:modelId,groups,source_changed_during_experiment:protocol.source_changed_during_experiment,backend_changed_during_experiment:protocol.backend_changed_during_experiment,caveat:protocol.caveat});
console.log(JSON.stringify({finished:true,output_directory:destination,groups},null,2));

