param([Parameter(Mandatory=$true)][string]$PackageDir,
      [Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path $PackageDir).Path
New-Item -ItemType Directory -Force $EvidenceDir | Out-Null
$evidence = (Resolve-Path $EvidenceDir).Path
# Remove the compiler, build Python, Qt and Graphviz from the runtime search.
$env:PATH = "$package\bin;$env:SystemRoot\System32;$env:SystemRoot"
$env:PYTHONHOME = "$package\runtime\python"
$env:PYTHONPATH = "$package\python;$package\include\layout_backend\src\algorithm"
$env:PYTHONNOUSERSITE = '1'
$env:PYTHONDONTWRITEBYTECODE = '1'
$env:IFCN_LAYOUT_BINDINGS_DIR = "$package\python\lib"
$env:IFCN_LAYOUT_ROOT = "$package\include\layout_backend"
$env:IFCN_MAPPING_METRICS_EXE = "$package\bin\ifcn_mapping_metrics.exe"
$env:GVBINDIR = "$package\bin\graphviz"
$env:GV_PLUGIN_PATH = $env:GVBINDIR
$env:FONTCONFIG_PATH = "$package\etc\fonts"
$env:MPLCONFIGDIR = "$evidence\matplotlib"
$env:MPLBACKEND = 'Agg'
$env:QT_QPA_PLATFORM = 'offscreen'
Remove-Item Env:QT_PLUGIN_PATH -ErrorAction SilentlyContinue
Remove-Item Env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue

& "$package\bin\fcnx_gui.exe" --version
if ($LASTEXITCODE -ne 0) { throw 'Packaged GUI version check failed' }
& "$package\runtime\python\bin\python.exe" -c 'import numpy, scipy, scipy.optimize, matplotlib.pyplot, networkx; from lib import iFCN_Lab; print("Bundled Python and native extension imported")'
if ($LASTEXITCODE -ne 0) { throw 'Bundled classical backend import failed' }

@'
module release_and(a,b,y);
input a,b;
output y;
assign y = a & b;
endmodule
'@ | Set-Content -Encoding utf8 "$evidence\release_and.v"
& "$package\runtime\python\bin\python.exe" `
    "$package\include\layout_backend\src\algorithm\main\test_normal_graph_draw.py" `
    --benchmark "$evidence\release_and.v" --output-dir "$evidence\classic" `
    --skip-figures --skip-latex --skip-stage-snapshots
if ($LASTEXITCODE -ne 0) { throw 'Bundled fixed 2DDWave layout failed' }
$generated = Get-ChildItem "$evidence\classic" -Filter '*.ifcn' | Select-Object -First 1
if (!$generated) { throw 'Classical backend produced no IFCN circuit' }
& "$package\bin\ifcn_mapping_metrics.exe" $generated.FullName
if ($LASTEXITCODE -ne 0) { throw 'Generated IFCN mapping failed' }

# Exercise Qt plugin loading, IFCN import, QCA mapping, and actual rendering.
$sample = Get-ChildItem "$package\examples\regular_2ddwave" -Recurse -Filter '*xor2_demo*.ifcn' | Select-Object -First 1
if (!$sample) { throw 'Packaged XOR example is missing' }
$env:IFCN_UI_SCREENSHOT_INPUT = $sample.FullName
$env:IFCN_UI_SCREENSHOT_VIEW = 'schematic'
$env:IFCN_UI_SCREENSHOT = "$evidence\windows-circuit.png"
& "$package\bin\fcnx_gui.exe"
if ($LASTEXITCODE -ne 0 -or !(Test-Path "$evidence\windows-circuit.png")) {
    throw 'Packaged GUI circuit rendering failed'
}

# The root launcher must also work after relocating the package into a path
# containing spaces; this is the executable users double-click.
$env:IFCN_UI_SCREENSHOT = "$evidence\windows-launcher.png"
& "$package\iFCN.exe" $sample.FullName
if ($LASTEXITCODE -ne 0) { throw 'Root Windows launcher failed' }
$deadline = (Get-Date).AddSeconds(60)
while (!(Test-Path "$evidence\windows-launcher.png") -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 200
}
if (!(Test-Path "$evidence\windows-launcher.png")) { throw 'Root launcher did not render the circuit' }
if ((Get-ChildItem "$package\examples" -Recurse -File).Count -ne 90) {
    throw 'The release must contain exactly 90 curated IFCN examples'
}
Write-Output 'Windows release runtime checks passed without MSYS2 on PATH.'
