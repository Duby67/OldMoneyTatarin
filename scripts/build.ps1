param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [string]$QtRoot,
    [string]$VcpkgRoot,
    [string]$JavaHome,
    [ValidateRange(1, 64)]
    [int]$Jobs = 2,
    [switch]$ConfigureOnly,
    [switch]$Install
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $QtRoot) { $QtRoot = Join-Path $repoRoot '.cache/deps/Qt/6.10.2/msvc2022_64' }
if (-not $VcpkgRoot) { $VcpkgRoot = Join-Path $repoRoot '.cache/deps/vcpkg' }
if (-not $JavaHome) {
    $javaCandidates = @(Get-ChildItem (Join-Path $repoRoot '.cache/deps/java') -Directory -ErrorAction SilentlyContinue)
    if ($javaCandidates.Count -eq 1) { $JavaHome = $javaCandidates[0].FullName }
    else { $JavaHome = $env:JAVA_HOME }
}
foreach ($entry in @(
    @{ Path = (Join-Path $QtRoot 'lib/cmake/Qt6/Qt6Config.cmake'); Name = 'QtRoot' },
    @{ Path = (Join-Path $VcpkgRoot 'scripts/buildsystems/vcpkg.cmake'); Name = 'VcpkgRoot' },
    @{ Path = $(if ($JavaHome) { Join-Path $JavaHome 'bin/javac.exe' } else { '' }); Name = 'JavaHome (JDK 17)' }
)) {
    if (-not $entry.Path -or -not (Test-Path -LiteralPath $entry.Path)) {
        throw "Не найден $($entry.Name). Укажите путь параметром scripts/build.ps1."
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Не найден vswhere. Требуется Visual Studio Build Tools с C++ x64.' }
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Не найдены инструменты MSVC x64.' }
$devCmd = Join-Path $vsRoot 'Common7/Tools/VsDevCmd.bat'
$distDir = Join-Path $repoRoot 'dist'
New-Item -ItemType Directory -Force $distDir | Out-Null
$envScript = Join-Path $distDir 'msvc-env.cmd'
[IO.File]::WriteAllLines($envScript, @('@echo off', ('call "' + $devCmd + '" -arch=x64 -host_arch=x64 >nul'), 'if errorlevel 1 exit /b 1', 'set'), [Text.Encoding]::Default)
$devEnvironment = & $env:ComSpec /d /c $envScript
if ($LASTEXITCODE -ne 0) { throw 'Не удалось подключить окружение MSVC.' }
foreach ($line in $devEnvironment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}

$env:VCPKG_ROOT = (Resolve-Path -LiteralPath $VcpkgRoot).Path
$env:JAVA_HOME = (Resolve-Path -LiteralPath $JavaHome).Path
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$javaVersion = & (Join-Path $env:JAVA_HOME 'bin/javac.exe') -version 2>&1
if ($javaVersion -notmatch 'javac 17\.') { throw 'Для Java-части проекта требуется JDK 17. Укажите -JavaHome.' }
$ninjaDir = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
$env:PATH = "$ninjaDir;$env:JAVA_HOME/bin;$QtRoot/bin;$env:PATH"
Push-Location $repoRoot
try {
    & cmake --preset windows_msvc "-DCMAKE_MAKE_PROGRAM=$ninjaDir/ninja.exe" "-DCMAKE_PREFIX_PATH=$QtRoot" "-DVCPKG_INSTALLED_DIR=$repoRoot/dist/vcpkg_installed" '-DVCPKG_TARGET_TRIPLET=x64-windows' '-DENABLE_LTO=OFF'
    if ($LASTEXITCODE -ne 0) { throw 'Ошибка конфигурации CMake.' }
    if ($ConfigureOnly) { return }
    & cmake --build --preset windows_msvc --config $Configuration --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Ошибка сборки.' }
    if ($Install) {
        & cmake --install (Join-Path $repoRoot 'dist/build/windows_msvc') --config $Configuration
        if ($LASTEXITCODE -ne 0) { throw 'Ошибка установки в dist/app/windows_msvc.' }
    }
}
finally { Pop-Location }
