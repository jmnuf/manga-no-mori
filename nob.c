#include <stdio.h>
#include <stdint.h>

#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

#define BUILD_FOLDER "./build"
#define VENDOR_FOLDER "./vendor"

#define streq(a, b) (strcmp(a, b) == 0)

typedef enum {
  COMP_UNIT_FLAG_DEBUG_INFO   = 1 << 0,
  COMP_UNIT_FLAG_COMPILE_ONLY = 1 << 1,
  COMP_UNIT_FLAG_FSANITIZE    = 1 << 2,
} Comp_Unit_Flag;

typedef struct {
  const char *output_path;
  const char *input_paths[5];
  size_t input_paths_count;
  uint8_t flags;
} Comp_Unit;

#define comp_unit_add_input(unit, path) (unit)->input_paths[(unit)->input_paths_count++] = path

bool build_demanded = false;

void usage(const char *program) {
  printf("Usage: %s [run|build]\n", program);
  printf("    run        ---        Execute program after compiling\n", program);
  printf("    build      ---        Force building of program\n", program);
}

bool build_if_needed(Cmd *cmd, Comp_Unit *unit) {
  bool result = true;
  if (build_demanded || needs_rebuild(unit->output_path, unit->input_paths, unit->input_paths_count)) {
    if (build_demanded) nob_log(NOB_INFO, "Rebuild demanded for: '%s'", unit->output_path);
    else nob_log(NOB_INFO, "Rebuild needed for: '%s'", unit->output_path);

    nob_cc(cmd);
    cmd_append(cmd, "-Wall", "-Wextra");
    bool compile_only = unit->flags & COMP_UNIT_FLAG_COMPILE_ONLY;
    if (unit->flags & COMP_UNIT_FLAG_DEBUG_INFO) nob_cmd_append(cmd, "-ggdb");
    if (unit->flags & COMP_UNIT_FLAG_FSANITIZE) nob_cmd_append(cmd, "-fsanitize=address,undefined");
    if (compile_only) cmd_append(cmd, "-c");
    nob_cc_output(cmd, unit->output_path);
    for (size_t i = 0; i < unit->input_paths_count; ++i) {
      if (!compile_only && sv_end_with(sv_from_cstr(unit->input_paths[i]), ".h")) continue;
      nob_cc_inputs(cmd, unit->input_paths[i]);
    }
    nob_return_defer(cmd_run(cmd));
  }
  nob_log(NOB_INFO, "No rebuild needed for: '%s'", unit->output_path);

defer:
  unit->input_paths_count = 0;
  memset(unit, 0, sizeof(Comp_Unit));
  return true;
}

int main(int argc, char **argv) {
  NOB_GO_REBUILD_URSELF(argc, argv);

  const char *program_name = shift(argv, argc);
  bool run_requested = false;
  bool use_debug = false;

  while (argc > 0) {
    const char *arg = shift(argv, argc);
    if (streq(arg, "run")) {
      run_requested = true;
      continue;
    }
    if (streq(arg, "build")) {
      build_demanded = true;
      continue;
    }

    if (streq(arg, "-g")) {
      use_debug = true;
      continue;
    }
    
    nob_log(ERROR, "Unknown argument provided to build system: %s", arg);
    usage(program_name);
    return 1;
  }

  Cmd cmd = {0};
  if (!mkdir_if_not_exists(BUILD_FOLDER)) return 1;
  Comp_Unit unit = {0};

  unit.output_path = BUILD_FOLDER"/mori";
  comp_unit_add_input(&unit, "./main.c");
  comp_unit_add_input(&unit, "./ext_sv.h");
  comp_unit_add_input(&unit, "./ansi_term.h");
  unit.flags = COMP_UNIT_FLAG_FSANITIZE;
  if (use_debug) unit.flags |= COMP_UNIT_FLAG_DEBUG_INFO;
  if (!build_if_needed(&cmd, &unit)) return 1;

  if (run_requested) {
    cmd_append(&cmd, BUILD_FOLDER"/mori");
    if (!cmd_run(&cmd)) return 1;
  }

  return 0;
}
