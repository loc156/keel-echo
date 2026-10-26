// Keel 的最小宿主：装载一段脚本并跑完，把错误翻成"文件:行"打出来。
//
// 编译（不需要 cmake）：
//     g++ -std=c++17 -O1 -Wall -Wextra -I keel/include examples/hello.cpp -o hello
// 运行：
//     hello                # 跑内置的那段示例
//     hello path/to/x.keel # 跑文件（表面语法）
#include "KeelLoader.hpp"
#include "KeelStd.hpp"

#include <cstdio>
#include <string>

int main(int argc, char **argv)
{
    keel::Keel vm;
    keelstd::register_all(vm); // 装 8 组内置库

    keelld::Options opt;
    keelld::Program program;

    if (argc > 1)
    {
        opt.syntax = keelld::Syntax::Surface; // 文件按表面语法读（let/if/while/fn）
        program = keelld::load_file(argv[1], opt);
        if (!program.ok())
        {
            std::fprintf(stderr, "%s\n", keelld::format_diags(program).c_str());
            return 1;
        }
    }
    else
    {
        // 核心语法：文件顶层就是入口块的块体
        const char *src =
            "\"sq\" { dup * }        // 运行时定义块\n"
            "5 \"sq\" call println   // 25\n"
            "\"hello, keel\" println\n";
        program = keelld::load_text(src, "<内置示例>", opt);
        if (!program.ok())
        {
            std::fprintf(stderr, "%s\n", keelld::format_diags(program).c_str());
            return 1;
        }
    }

    keelld::Environment env;
    env.stdout_sink = [](const char *p, std::size_t n) { std::fwrite(p, 1, n, stdout); };

    keelld::Prepared prep = keelld::install(vm, program, env);
    if (!prep.ok)
    {
        std::fprintf(stderr, "%s\n", prep.error_text.c_str());
        return 1;
    }

    keelld::execute(vm);
    if (!keel::keel_errors().empty())
    {
        std::fprintf(stderr, "%s\n", keelld::format_runtime_errors(vm, program).c_str());
        return 1;
    }
    return 0;
}
