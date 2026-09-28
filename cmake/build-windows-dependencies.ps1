param(
    [string]$W64DevkitRoot = 'D:\tools\w64devkit',
    [int]$Jobs = [Environment]::ProcessorCount
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$distfiles = Join-Path $projectRoot 'src\third_party\distfiles'
$wxArchive = Join-Path $distfiles 'wxWidgets-3.2.11.zip'
$cryptoArchive = Join-Path $distfiles 'cryptopp890.zip'
$wxHash = '02C4FDC8EC104A10EFD809238F800B632C4D5CC6A2D54582BFF775240007F01A'
$cryptoHash = '4CC0CCC324625B80B695FCD3DEE63A66F1A460D3E51B71640CDBFC4CD1A3779C'
$depsRoot = Join-Path $projectRoot 'build\dependencies\windows-x86'
$sourceRoot = Join-Path $depsRoot 'source'
$buildRoot = Join-Path $depsRoot 'build'
$installRoot = Join-Path $depsRoot 'install'
$stampRoot = Join-Path $depsRoot 'stamps'
$toolBin = Join-Path $W64DevkitRoot 'bin'

function Require-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing file: $Path" }
}

function Verify-Archive([string]$Path, [string]$ExpectedHash) {
    $hashOutput = & (Join-Path $toolBin 'cmake.exe') -E sha256sum $Path
    if ($LASTEXITCODE -ne 0) { throw "Unable to hash $Path" }
    $actual = (($hashOutput -split '\s+')[0]).ToUpperInvariant()
    if ($actual -ne $ExpectedHash) {
        throw "SHA256 mismatch for $Path`nExpected: $ExpectedHash`nActual:   $actual"
    }
}

function Expand-CachedArchive(
    [string]$Archive, [string]$Destination, [string]$Stamp, [string]$Signature
) {
    if ((Test-Path -LiteralPath $Stamp) -and
        ((Get-Content -LiteralPath $Stamp -Raw).Trim() -eq $Signature)) { return }
    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & (Join-Path $toolBin 'cmake.exe') -E chdir $Destination `
        (Join-Path $toolBin 'cmake.exe') -E tar xf $Archive
    if ($LASTEXITCODE -ne 0) { throw "Unable to extract $Archive" }
    Set-Content -LiteralPath $Stamp -Value $Signature -NoNewline
}

$requiredTools = @(
    'cmake.exe', 'ninja.exe', 'mingw32-make.exe',
    'i686-w64-mingw32-gcc.exe', 'i686-w64-mingw32-g++.exe',
    'i686-w64-mingw32-windres.exe', 'i686-w64-mingw32-ar.exe',
    'i686-w64-mingw32-ranlib.exe'
)
Require-File $wxArchive
Require-File $cryptoArchive
foreach ($tool in $requiredTools) { Require-File (Join-Path $toolBin $tool) }
Verify-Archive $wxArchive $wxHash
Verify-Archive $cryptoArchive $cryptoHash

$env:PATH = "$toolBin;$env:PATH"
New-Item -ItemType Directory -Force -Path $sourceRoot, $buildRoot, $installRoot, $stampRoot | Out-Null
Expand-CachedArchive $wxArchive (Join-Path $sourceRoot 'wxWidgets') `
    (Join-Path $stampRoot 'wx-source') "3.2.11-$wxHash"
Expand-CachedArchive $cryptoArchive (Join-Path $sourceRoot 'cryptopp') `
    (Join-Path $stampRoot 'cryptopp-source') "8.9.0-$cryptoHash"

$wxSource = Join-Path $sourceRoot 'wxWidgets'
$wxBuild = Join-Path $buildRoot 'wxWidgets'
$wxStamp = Join-Path $stampRoot 'wx-installed'
$wxSignature = "3.2.11-$wxHash-windows-x86"
$wxReady = (Test-Path -LiteralPath $wxStamp) -and
           ((Get-Content -LiteralPath $wxStamp -Raw).Trim() -eq $wxSignature)
if (-not $wxReady) {
    if (Test-Path -LiteralPath $wxBuild) { Remove-Item -LiteralPath $wxBuild -Recurse -Force }
    if (Test-Path -LiteralPath $installRoot) { Remove-Item -LiteralPath $installRoot -Recurse -Force }
    Remove-Item -LiteralPath (Join-Path $stampRoot 'cryptopp-installed') -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $wxBuild, $installRoot | Out-Null
    $installCmake = $installRoot -replace '\\', '/'
    $toolCmake = $toolBin -replace '\\', '/'
    & (Join-Path $toolBin 'cmake.exe') -S $wxSource -B $wxBuild -G Ninja `
        -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_INSTALL_PREFIX=$installCmake" `
        "-DCMAKE_C_COMPILER=$toolCmake/i686-w64-mingw32-gcc.exe" `
        "-DCMAKE_CXX_COMPILER=$toolCmake/i686-w64-mingw32-g++.exe" `
        "-DCMAKE_RC_COMPILER=$toolCmake/i686-w64-mingw32-windres.exe" `
        -DwxBUILD_SHARED=OFF -DwxBUILD_MONOLITHIC=OFF `
        -DwxBUILD_SAMPLES=OFF -DwxBUILD_TESTS=OFF -DwxBUILD_DEMOS=OFF `
        -DwxUSE_WEBVIEW=OFF
    if ($LASTEXITCODE -ne 0) { throw 'wxWidgets configuration failed.' }
    & (Join-Path $toolBin 'cmake.exe') --build $wxBuild --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'wxWidgets build failed.' }
    & (Join-Path $toolBin 'cmake.exe') --install $wxBuild
    if ($LASTEXITCODE -ne 0) { throw 'wxWidgets installation failed.' }
    Set-Content -LiteralPath $wxStamp -Value $wxSignature -NoNewline
}

$cryptoSource = Join-Path $sourceRoot 'cryptopp'
$cryptoStamp = Join-Path $stampRoot 'cryptopp-installed'
$cryptoSignature = "8.9.0-$cryptoHash-windows-x86"
$cryptoReady = (Test-Path -LiteralPath $cryptoStamp) -and
               ((Get-Content -LiteralPath $cryptoStamp -Raw).Trim() -eq $cryptoSignature)
if (-not $cryptoReady) {
    & (Join-Path $toolBin 'mingw32-make.exe') -C $cryptoSource -f GNUmakefile clean
    & (Join-Path $toolBin 'mingw32-make.exe') -C $cryptoSource -f GNUmakefile `
        -j $Jobs static `
        'CXX=i686-w64-mingw32-g++.exe' `
        'AR=i686-w64-mingw32-ar.exe' `
        'RANLIB=i686-w64-mingw32-ranlib.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Crypto++ build failed.' }
    $cryptoInclude = Join-Path $installRoot 'include\cryptopp'
    $cryptoLib = Join-Path $installRoot 'lib'
    New-Item -ItemType Directory -Force -Path $cryptoInclude, $cryptoLib | Out-Null
    Copy-Item -Path (Join-Path $cryptoSource '*.h') -Destination $cryptoInclude -Force
    Copy-Item -LiteralPath (Join-Path $cryptoSource 'libcryptopp.a') -Destination $cryptoLib -Force
    Set-Content -LiteralPath $cryptoStamp -Value $cryptoSignature -NoNewline
}

Write-Host "Static dependencies ready: $installRoot"
