$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    $cc=(Get-Command g++.exe -ErrorAction Stop).Source
    $rc=(Get-Command windres.exe -ErrorAction Stop).Source
    & $rc launcher.rc -O coff -o launcher-res.o
    if($LASTEXITCODE -ne 0){throw 'Resource build failed'}
    $args=@('-O2','-static','-static-libgcc','-static-libstdc++','-municode','-mwindows','-fno-ident','-s','bootstrap.cpp','launcher-res.o','-lurlmon','-lcomctl32','-lole32','-loleaut32','-lshell32','-lshlwapi','-ladvapi32','-lwintrust','-lcrypt32','-luuid')
    & $cc @args -o RRFF.exe
    if($LASTEXITCODE -ne 0){throw 'Launcher build failed'}
    & $cc @args -DRRFF_INSTALLER -o 'Install RRFF.exe'
    if($LASTEXITCODE -ne 0){throw 'Installer build failed'}
} finally {Pop-Location}
