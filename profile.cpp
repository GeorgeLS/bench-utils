#include "types.h"
#include "platform_utils.cpp"

#ifdef PROFILE

#define MAX_ANCHORS 4096

#define CONCAT(a, b) a##b
#define CONCAT2(a, b) CONCAT(a, b)
#define PROFILE_BANDWIDTH(name, bytes) ProfileBlock CONCAT2(Block, __LINE__)(name, __COUNTER__ + 1, bytes)
#define PROFILE_BLOCK(name) PROFILE_BANDWIDTH(name, 0)
#define PROFILE_FUNCTION PROFILE_BLOCK(__FUNCTION__)
#define PROFILE_FUNCTION_BANDWIDTH(bytes) PROFILE_BANDWIDTH(__FUNCTION__, bytes)

struct ProfileAnchor {
  u64 tsc_elapsed_exclusive; // Without children
  u64 tsc_elapsed_inclusive; // With    children
  u64 num_hits;
  u64 bytes_processed;
  const char* label;
};

struct Profiler {
  ProfileAnchor anchors[MAX_ANCHORS];
  u64 start_tsc;
  u64 end_tsc;
};

static Profiler global_profiler;
static u64 global_parent_profiler;

struct ProfileBlock {
  const char* label;
  u64 start_tsc;
  u64 old_tsc_inclusive;
  u64 anchor_index;
  u64 parent_index;
  u64 bytes_processed;

  ProfileBlock(const char* name, size_t index, u64 bytes) {
    parent_index = global_parent_profiler;
    anchor_index = index;
    label = name;
    bytes_processed = bytes;

    ProfileAnchor* self = &global_profiler.anchors[index];
    old_tsc_inclusive = self->tsc_elapsed_inclusive;

    global_parent_profiler = anchor_index;

    start_tsc = read_cpu_timer();
  }

  ~ProfileBlock() {
    u64 elapsed = read_cpu_timer() - start_tsc;
    global_parent_profiler = parent_index;

    ProfileAnchor* anchor = &global_profiler.anchors[anchor_index];
    ProfileAnchor* parent = &global_profiler.anchors[parent_index];

    parent->tsc_elapsed_exclusive -= elapsed;
    anchor->tsc_elapsed_exclusive += elapsed;
    anchor->tsc_elapsed_inclusive = old_tsc_inclusive + elapsed;
    anchor->bytes_processed += bytes_processed;

    ++anchor->num_hits;
    anchor->label = label;
  }
};

void print_performance_anchors(u64 total_cpu_elapsed, u64 cpu_frequency) {
  for (size_t i = 1U; i != MAX_ANCHORS; ++i) {
    ProfileAnchor* anchor = &global_profiler.anchors[i];

    if (anchor->tsc_elapsed_exclusive) {
      f64 ms_elapsed = 1000.0 * (f64) anchor->tsc_elapsed_exclusive / (f64) cpu_frequency;
      f64 percentage = 100.0 * ((f64) anchor->tsc_elapsed_exclusive / (f64) total_cpu_elapsed);

      printf("    %s[%llu]: %0.10fms (%.2f%%", anchor->label, anchor->num_hits, ms_elapsed, percentage);

      if (anchor->tsc_elapsed_inclusive != anchor->tsc_elapsed_exclusive) {
        f64 percent_with_children = 100.0 * ((f64) anchor->tsc_elapsed_inclusive / (f64) total_cpu_elapsed);
        printf(", %.2f%% w/children", percent_with_children);
      }
      printf(")");

      if (anchor->bytes_processed) {
        f64 megabyte = 1024.0 * 1024.0;
        f64 gigabyte = megabyte * 1024.0;

        f64 seconds = (f64) anchor->tsc_elapsed_inclusive / (f64) cpu_frequency;
        f64 bytes_per_second = (f64) anchor->bytes_processed / seconds;
        f64 megabytes = (f64) anchor->bytes_processed / megabyte;
        f64 gigabytes_per_second = bytes_per_second / gigabyte;

        printf(" %.3fMBs at %.2fGB/s", megabytes, gigabytes_per_second);
      }

      printf("\n");
    }
  }
}

#else

#define PROFILE_BANDWIDTH(...)
#define PROFILE_BLOCK(...)
#define PROFILE_FUNCTION
#define PROFILE_FUNCTION_BANDWIDTH(...)
#define print_performance_anchors(...)

struct Profiler {
  u64 start_tsc;
  u64 end_tsc;
};

static Profiler global_profiler;

#endif

static void begin_profile() {
  global_profiler.start_tsc = read_cpu_timer();
}

static void end_profile_and_print_results() {
  global_profiler.end_tsc = read_cpu_timer();

  u64 cpu_frequency = get_cpu_frequency();
  assert(cpu_frequency > 0);

  u64 total_cpu_elapsed = global_profiler.end_tsc - global_profiler.start_tsc;

  printf("\nPerformance report:\n");
  printf("    CPU frequency: %lluhz\n", cpu_frequency);
  printf("    Total time = %0.4fms\n", 1000.0 * (f64) total_cpu_elapsed / (f64) cpu_frequency);

  print_performance_anchors(total_cpu_elapsed, cpu_frequency);
}
