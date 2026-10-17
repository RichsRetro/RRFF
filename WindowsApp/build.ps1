param([string]$OutputName = 'RRFF.exe')
$ErrorActionPreference = 'Stop'
$projectDir = $PSScriptRoot
$compilerPath = 'C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe'
$compilerArgs = @('/nologo','/codepage:65001','/target:winexe','/platform:x64','/optimize+',"/win32manifest:$projectDir\app.manifest","/win32icon:$projectDir\RRXD.ico","/out:$projectDir\$OutputName",'/r:System.dll','/r:System.Core.dll','/r:System.Drawing.dll','/r:System.Windows.Forms.dll','/r:System.Management.dll','/r:System.Web.Extensions.dll','/r:System.IO.Compression.dll','/r:System.IO.Compression.FileSystem.dll',"/resource:$projectDir\payload\C.zip,C.zip","/resource:$projectDir\payload\E.zip,E.zip","/resource:$projectDir\payload\F.zip,F.zip","$projectDir\Program.cs","$projectDir\Storage.cs","$projectDir\Fatx.cs","$projectDir\InstallPlan.cs","$projectDir\FolderPicker.cs","$projectDir\ExplorerDisk.cs","$projectDir\ExplorerForm.cs","$projectDir\GameCompatibility.cs","$projectDir\XisoImage.cs","$projectDir\XisoReader.cs","/resource:$projectDir\xiso-launcher.xbe,xiso-launcher.xbe","$projectDir\FatxNames.cs","$projectDir\ShellFileDrop.cs","$projectDir\ShellExportData.cs","/resource:$projectDir\game-compatibility.md,game-compatibility.md")
foreach($profileFile in Get-ChildItem -LiteralPath (Join-Path $projectDir 'game-patches') -Filter '*.json'){
    $compilerArgs += "/resource:$($profileFile.FullName),game-patch-$($profileFile.BaseName)"
}
& $compilerPath @compilerArgs
if ($LASTEXITCODE -ne 0) { throw 'Windows formatter build failed.' }




