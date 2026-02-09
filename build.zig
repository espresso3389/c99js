const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "c99js",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });

    const sources = .{
        "src/util.c",
        "src/type.c",
        "src/lexer.c",
        "src/ast.c",
        "src/symtab.c",
        "src/preprocess.c",
        "src/parser.c",
        "src/sema.c",
        "src/codegen.c",
        "src/main.c",
    };

    const flags = .{
        "-std=c99",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-Wall",
        "-Wextra",
    };

    exe.root_module.addCSourceFiles(.{
        .files = &sources,
        .flags = &flags,
    });

    b.installArtifact(exe);

    // Run step: `zig build run -- <args>`
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const run_step = b.step("run", "Build and run c99js");
    run_step.dependOn(&run_cmd.step);
}
