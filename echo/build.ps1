<#
  Echo —— 构建脚本（不需要 cmake / make）。

  产物（全部在 build\ 下，已被 .gitignore 忽略）：
    build\libimgui.a      Dear ImGui + GLFW/OpenGL3 后端 + imgui_stdlib
    build\libecho.a       Echo 本体（src\*.cpp）
    build\EchoHello.exe   examples\hello\main.cpp
    build\EchoCalc.exe    examples\calculator\main.cpp
    build\echo-tests.exe  tests\test_main.cpp（只有 -Test 才编）

  前置：把两样第三方源码放到本目录下（仓库不收录它们）
    libs\imgui\            imgui.cpp / imgui_draw.cpp / imgui_tables.cpp /
                           imgui_widgets.cpp / imgui_stdlib.cpp / backends\
    libs\glfw\include\ + libs\glfw\lib-mingw-w64\libglfw3.a
  （官方 GLFW 的 Windows 预编译包就是这个目录结构，解压到 libs\glfw 即可。）

  用法：
    .\build.ps1              # 库 + 两个示例
    .\build.ps1 -Test        # 编并跑单元测试（74 项，headless）
    .\build.ps1 -Clean       # 先清 build\
    .\build.ps1 -Debug       # -g -O0，保留控制台
    .\build.ps1 -Warn        # 库加 -Wall -Wextra
    .\build.ps1 -LibOnly     # 只编 libimgui.a / libecho.a
    .\build.ps1 -Run         # 编完直接启动 hello 示例
    .\build.ps1 -Console     # 示例保留控制台（看 log 输出）
    .\build.ps1 -Mingw <dir> # 指定 g++.exe 所在目录
#>
param(
    [string]$Mingw = '',
    [switch]$Debug,
    [switch]$Clean,
    [switch]$Test,
    [switch]$Warn,
    [switch]$Run,
    [switch]$Console,
    [switch]$LibOnly
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
Push-Location $root

function Invoke-Native {
    param([string]$Exe, [string[]]$Arguments)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Exe @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
        return $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previous
    }
}

try {
    # ---- 工具链 -----------------------------------------------------------
    # 默认用 PATH 上的 g++；找不到就报错让用户用 -Mingw 指定。
    $gxx = $null
    if ($Mingw) {
        $candidate = Join-Path $Mingw 'g++.exe'
        if (Test-Path $candidate) { $gxx = $candidate }
    }
    if (-not $gxx) {
        $found = Get-Command g++.exe -ErrorAction SilentlyContinue
        if (-not $found) {
            throw "找不到 g++.exe。把它加进 PATH，或用 -Mingw <含 g++.exe 的目录> 指定。"
        }
        $gxx = $found.Source
    }
    $Mingw = Split-Path -Parent $gxx
    $ar = Join-Path $Mingw 'ar.exe'
    if (-not (Test-Path $ar)) {
        throw "g++ 旁边没有 ar.exe（$Mingw），无法打包静态库。"
    }

    # 必须：cc1plus.exe 需要 g++ 旁边的 DLL。少了这一行，所有编译都会
    # 以"退出码 1、零错误信息"静默失败。
    $env:PATH = "$Mingw;$env:PATH"

    # ---- 前置检查 ---------------------------------------------------------
    $imguiRoot = Join-Path $root 'libs\imgui'
    $glfwRoot = Join-Path $root 'libs\glfw'
    if (-not (Test-Path (Join-Path $imguiRoot 'imgui.cpp'))) {
        throw "缺少 libs\imgui（Dear ImGui 源码）。见本脚本开头说明。"
    }
    if (-not (Test-Path (Join-Path $glfwRoot 'include\GLFW\glfw3.h'))) {
        throw "缺少 libs\glfw（GLFW 头文件与 lib-mingw-w64\libglfw3.a）。见本脚本开头说明。"
    }

    $build = Join-Path $root 'build'
    $objDir = Join-Path $build 'obj'
    if ($Clean -and (Test-Path $build)) {
        Remove-Item $build -Recurse -Force
    }
    New-Item -ItemType Directory -Force $objDir | Out-Null

    # 最新的头文件时间戳：比它旧的 .o 一律重编。
    $stamp = Get-ChildItem (Join-Path $root 'include'), $imguiRoot, (Join-Path $imguiRoot 'backends') `
        -Recurse -File -Include *.h, *.hpp -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty LastWriteTime

    # Echo 用了指定初始化器（如 .drag = ...），所以是 C++20。
    $common = @('-std=c++20')
    if ($Debug) { $common += @('-g', '-O0') } else { $common += @('-O2', '-DNDEBUG') }

    $libFlags = $common + @('-Iinclude', '-Ilibs/imgui', '-Ilibs/imgui/backends', '-Ilibs/glfw/include')
    if ($Warn) { $libFlags += @('-Wall', '-Wextra') }

    # ImGui 自己的警告不归本项目管，静音。
    $imguiFlags = $common + @('-w', '-Ilibs/imgui', '-Ilibs/imgui/backends', '-Ilibs/glfw/include')

    function Build-Object {
        param([string]$Tag, [string]$Source, [string[]]$Flags)
        $object = Join-Path $objDir "$Tag.o"
        $rebuild = $true
        if (Test-Path $object) {
            $objectTime = (Get-Item $object).LastWriteTime
            $sourceTime = (Get-Item $Source).LastWriteTime
            if ($stamp -and $objectTime -ge $stamp -and $objectTime -ge $sourceTime) {
                $rebuild = $false
            }
        }
        if ($rebuild) {
            Write-Host ("  cc  {0}" -f (Split-Path $Source -Leaf))
            $code = Invoke-Native $gxx ($Flags + @('-c', $Source, '-o', $object))
            if ($code -ne 0) { throw "编译失败: $Source" }
        }
        return $object
    }

    function Build-Archive {
        param([string]$Name, [string[]]$Objects)
        $archive = Join-Path $build $Name
        if (Test-Path $archive) { Remove-Item $archive -Force }
        Write-Host ("  ar  {0}  ({1} objects)" -f $Name, $Objects.Count)
        $code = Invoke-Native $ar (@('rcs', $archive) + $Objects)
        if ($code -ne 0) { throw "ar 失败: $Name" }
        return $archive
    }

    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    Write-Host "[echo] compiler : $gxx"
    Write-Host ("[echo] mode     : {0}" -f $(if ($Debug) { 'debug' } else { 'release' }))

    # ---- imgui ------------------------------------------------------------
    $imguiFiles = @(
        @{ Tag = 'imgui'; File = 'imgui.cpp' },
        @{ Tag = 'imgui_draw'; File = 'imgui_draw.cpp' },
        @{ Tag = 'imgui_tables'; File = 'imgui_tables.cpp' },
        @{ Tag = 'imgui_widgets'; File = 'imgui_widgets.cpp' },
        @{ Tag = 'imgui_stdlib'; File = 'imgui_stdlib.cpp' },
        @{ Tag = 'imgui_impl_glfw'; File = 'backends\imgui_impl_glfw.cpp' },
        @{ Tag = 'imgui_impl_opengl3'; File = 'backends\imgui_impl_opengl3.cpp' }
    )
    $imguiObjects = @()
    foreach ($entry in $imguiFiles) {
        $imguiObjects += (Build-Object $entry.Tag (Join-Path $imguiRoot $entry.File) $imguiFlags)
    }
    $imguiArchive = Build-Archive 'libimgui.a' $imguiObjects

    # ---- echo 本体 --------------------------------------------------------
    $echoObjects = @()
    foreach ($source in Get-ChildItem (Join-Path $root 'src') -Filter *.cpp) {
        $echoObjects += (Build-Object ('echo_' + $source.BaseName) $source.FullName $libFlags)
    }
    $echoArchive = Build-Archive 'libecho.a' $echoObjects

    if ($LibOnly) {
        $stopwatch.Stop()
        Write-Host ("[echo] OK       : 只编库，{0:N1}s" -f $stopwatch.Elapsed.TotalSeconds)
        return
    }

    # ---- 示例 / 测试 ------------------------------------------------------
    $linkFlags = @('-static', '-static-libgcc', '-static-libstdc++')
    $demoLink = $linkFlags + @('-mwindows')
    if ($Console -or $Debug) { $demoLink = $linkFlags }

    $appFlags = $common + @('-Iinclude', '-Ilibs/imgui', '-Ilibs/imgui/backends', '-Ilibs/glfw/include')
    $glfwLib = Join-Path $glfwRoot 'lib-mingw-w64\libglfw3.a'

    if ($Test) {
        $testSource = Join-Path $root 'tests\test_main.cpp'
        if (-not (Test-Path $testSource)) { throw "缺少 tests\test_main.cpp" }
        $testObject = Build-Object 'test_main' $testSource $appFlags
        $testExe = Join-Path $build 'echo-tests.exe'
        $code = Invoke-Native $gxx (@($testObject, $echoArchive, $imguiArchive, $glfwLib,
                    '-lopengl32', '-lgdi32', '-o', $testExe) + $linkFlags)
        if ($code -ne 0) { throw '测试链接失败' }
        Write-Host '[echo] running tests'
        $testCode = Invoke-Native $testExe @()
        if ($testCode -ne 0) { throw "测试失败（exit $testCode）" }
    }
    else {
        # 一个 examples\<name>\main.cpp 一个 exe。
        $examples = @(
            @{ Tag = 'ex_hello'; Source = 'examples\hello\main.cpp'; Exe = 'EchoHello.exe' },
            @{ Tag = 'ex_calculator'; Source = 'examples\calculator\main.cpp'; Exe = 'EchoCalc.exe' }
        )
        foreach ($example in $examples) {
            $source = Join-Path $root $example.Source
            if (-not (Test-Path $source)) { continue }
            $object = Build-Object $example.Tag $source $appFlags
            $exe = Join-Path $build $example.Exe
            $code = Invoke-Native $gxx (@($object, $echoArchive, $imguiArchive, $glfwLib,
                        '-lopengl32', '-lgdi32', '-o', $exe) + $demoLink)
            if ($code -ne 0) { throw "链接 $($example.Exe) 失败" }
        }
    }

    $stopwatch.Stop()
    $kb = [math]::Round((Get-Item $echoArchive).Length / 1KB)
    Write-Host ("[echo] OK       : libecho.a {0} KB, {1:N1}s" -f $kb, $stopwatch.Elapsed.TotalSeconds)

    if ($Run -and (Test-Path (Join-Path $build 'EchoHello.exe'))) {
        Write-Host '[echo] launching hello'
        Start-Process -FilePath (Join-Path $build 'EchoHello.exe') -WorkingDirectory $root
    }
}
finally {
    Pop-Location
}
