#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

#define BUILD_FOLDER "./build"
#define VENDOR_FOLDER "./vendor"

#define streq(a, b) (strcmp(a, b) == 0)

typedef struct {
  const char *output_path;
  const char *input_paths[5];
  size_t input_paths_count;
  bool compile_only;
} Comp_Unit;

#define comp_unit_add_input(unit, path) (unit)->input_paths[(unit)->input_paths_count++] = path

bool build_demanded = false;

void usage(const char *program) {
  printf("Usage: %s [run|build]\n", program);
  printf("    run        ---        Execute program after compiling\n", program);
  printf("    build      ---        Force building of program\n", program);
}

bool build_if_needed(Cmd *cmd, Comp_Unit *unit) {
  if (build_demanded || needs_rebuild(unit->output_path, unit->input_paths, unit->input_paths_count)) {
    nob_log(NOB_INFO, "Rebuild needed for: '%s'", unit->output_path);
    nob_cc(cmd);
    if (unit->compile_only) {
      unit->compile_only = false;
      cmd_append(cmd, "-c");
    }
    nob_cc_output(cmd, unit->output_path);
    for (size_t i = 0; i < unit->input_paths_count; ++i) {
      nob_cc_inputs(cmd, unit->input_paths[i]);
    }
    unit->input_paths_count = 0;
    return cmd_run(cmd);
  }
  nob_log(NOB_INFO, "No rebuild needed for: '%s'", unit->output_path);
  unit->input_paths_count = 0;
  return true;
}

int main(int argc, char **argv) {
  NOB_GO_REBUILD_URSELF(argc, argv);

  const char *program_name = shift(argv, argc);
  bool run_requested = false;
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
    
    nob_log(ERROR, "Unknown argument provided to build system: %s", arg);
    usage(program_name);
    return 1;
  }

  Cmd cmd = {0};
  if (!mkdir_if_not_exists(BUILD_FOLDER)) return 1;
  Comp_Unit unit = {0};

  // unit.output_path = BUILD_FOLDER"/sqlite.o";
  // unit.compile_only = true;
  // comp_unit_add_input(&unit, VENDOR_FOLDER"/sqlite3.c");
  // if (!build_if_needed(&cmd, &unit)) return 1;

  unit.output_path = BUILD_FOLDER"/mori";
  comp_unit_add_input(&unit, "./main.c");
  comp_unit_add_input(&unit, "./ext_sv.h");
  comp_unit_add_input(&unit, "./ansi_term.h");
  // comp_unit_add_input(&unit, BUILD_FOLDER"/sqlite.o");
  if (!build_if_needed(&cmd, &unit)) return 1;

  if (run_requested) {
    cmd_append(&cmd, BUILD_FOLDER"/mori");
    if (!cmd_run(&cmd)) return 1;
  }

  return 0;
}
