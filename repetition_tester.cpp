#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "types.h"
#include "platform_utils.cpp"

#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))
#define MALLOC(size, type) ((type*) malloc((size) * sizeof(type)))

struct RepetitionTester;
struct RepetitionTesterTestConfig;

using RepetitionTesterTestFn = void (*) (RepetitionTester*);
using RepetitionTesterTestGenParamsFn = void* (*) (int, char*[]);
using RepetitionTesterTestConfigFn = RepetitionTesterTestConfig (*) ();
using RepetitionTesterTestParamsPrintFn = void (*) (const void*);

inline void* RepetitionTester_gen_empty_params(int _argc, char* _args[]) {
    (void)_argc;
    (void)_args;
    return nullptr;
}

struct RepetitionTesterTest {
    const char* name;
    RepetitionTesterTestFn test;
    RepetitionTesterTestGenParamsFn gen_params;
    RepetitionTesterTestConfigFn get_test_config;
    RepetitionTesterTestParamsPrintFn print_params;
};

struct RepetitionTesterTestConfig {
    u64 max_seconds_to_run;
    u64 repetition_count;
    bool print_new_minimums : 1;
    bool cache_params: 1;
};

RepetitionTesterTestConfig RepetitionTesterTestConfig_default() {
    RepetitionTesterTestConfig res = {};
    res.max_seconds_to_run = 10;
    res.repetition_count = 1;
    res.print_new_minimums = true;
    res.cache_params = true;
    return res;
}

enum TestValue {
    TestValue_TestCount,
    TestValue_MemPageFaults,
    TestValue_CpuElapsed,
    TestValue_BytesProcessed,
    TestValue_Count,
};

struct RepetitionTesterTestValue {
    u64 acc[TestValue_Count];
};

struct RepetitionTesterTestContext {
    RepetitionTesterTestValue iteration_value;
    u64 start_time;
    bool error_happened;
};

struct RepetitionTesterTestResults {
    RepetitionTesterTestValue total;
    RepetitionTesterTestValue min;
    RepetitionTesterTestValue max;
};

struct RepetitionTester {
    RepetitionTesterTest* tests;
    usize num_tests;
    usize current_test;
    u64 cpu_frequency;
    void** test_params_cache;
    char** args;
    int argc;
    RepetitionTesterTestContext context;
    RepetitionTesterTestResults results;
};

inline const RepetitionTesterTest* RepetitionTester_get_current_test(const RepetitionTester* tester) {
    return &tester->tests[tester->current_test];
}

inline RepetitionTesterTestConfig RepetitionTester_get_current_config(const RepetitionTester* tester) {
    return RepetitionTester_get_current_test(tester)->get_test_config();
}

void RepetitionTester_initialize(RepetitionTester* tester, int argc, char* args[], RepetitionTesterTest* tests, usize num_tests) {
    assert(num_tests > 0);

    for (usize i = 0U; i != num_tests; ++i) {
        RepetitionTesterTest* test = &tests[i];
        assert(test != nullptr);
        assert(test->name != nullptr);
        assert(test->test != nullptr);

        if (test->gen_params == nullptr) {
            test->gen_params = RepetitionTester_gen_empty_params;
        }

        if (test->get_test_config == nullptr) {
            test->get_test_config = RepetitionTesterTestConfig_default;
        }
    }

    tester->tests = tests;
    tester->num_tests = num_tests;
    tester->current_test = 0;
    tester->argc = argc;
    tester->args = args;
    tester->cpu_frequency = get_cpu_frequency();
    tester->test_params_cache = MALLOC(num_tests, void*);
    bzero(tester->test_params_cache, num_tests * sizeof(void*));
}

const void* RepetitionTester_get_params_for_test_internal(const RepetitionTester* tester, bool use_cached) {
    const void* params;
    const RepetitionTesterTest* current_test = RepetitionTester_get_current_test(tester);

    if (use_cached) {
        params = tester->test_params_cache[tester->current_test];
        if (params == nullptr) {
            tester->test_params_cache[tester->current_test] = current_test->gen_params(tester->argc, tester->args);
        }
        params = tester->test_params_cache[tester->current_test];
    } else {
        params = current_test->gen_params(tester->argc, tester->args);
    }

    return params;
}

const void* RepetitionTester_get_params_for_test(RepetitionTester* tester) {
    RepetitionTesterTestConfig config = RepetitionTester_get_current_config(tester);
    return RepetitionTester_get_params_for_test_internal(tester, config.cache_params);
}

void print_progress(const char* label, RepetitionTesterTestValue value, u64 cpu_freq) {
    u64 test_count = value.acc[TestValue_TestCount];
    f64 divisor = test_count ? (f64) test_count : 1.0;

    f64 values[TestValue_Count];
    for (usize i = 0U; i != ARRAY_COUNT(values); ++i) {
        values[i] = (f64) value.acc[i] / divisor;
    }

    f64 seconds = values[TestValue_CpuElapsed] / (f64) cpu_freq;
    f64 ms = 1000.0 * seconds;

    printf("%s: %.4fms", label, ms);

    if (values[TestValue_BytesProcessed] > 0) {
        f64 gigabyte = 1024.0 * 1024.0 * 1024.0;
        f64 bandwidth = values[TestValue_BytesProcessed] / (gigabyte * seconds);
        printf(" %.2fGB/s", bandwidth);
    }

    if ((u64) values[TestValue_MemPageFaults] > 0) {
        f64 bytes_per_faults = values[TestValue_BytesProcessed] / (values[TestValue_MemPageFaults] * 1024.0);
        printf(" PF: %.0f (%0.2fK/fault)", values[TestValue_MemPageFaults], bytes_per_faults);
    }
}

void print_results(const RepetitionTester* tester) {
    u64 cpu_frequency = tester->cpu_frequency;
    RepetitionTesterTestResults results = tester->results;

    print_progress("Min", results.min, cpu_frequency);
    printf("\n");

    print_progress("Max", results.max, cpu_frequency);
    printf("\n");

    print_progress("Avg", results.total, cpu_frequency);
    printf("\n");
}

inline void RepetitionTester_begin_time(RepetitionTester* tester) {
    RepetitionTesterTestValue* it_value = &tester->context.iteration_value;
    it_value->acc[TestValue_CpuElapsed] -= read_cpu_timer();
    it_value->acc[TestValue_MemPageFaults] -= get_os_page_faults();
}

inline void RepetitionTester_end_time(RepetitionTester* tester) {
    RepetitionTesterTestValue* it_value = &tester->context.iteration_value;
    it_value->acc[TestValue_CpuElapsed] += read_cpu_timer();
    it_value->acc[TestValue_MemPageFaults] += get_os_page_faults();
}

inline void RepetitionTester_bytes_processed(RepetitionTester* tester, u64 bytes_processed) {
    RepetitionTesterTestValue* it_value = &tester->context.iteration_value;
    it_value->acc[TestValue_BytesProcessed] += bytes_processed;
}

void RepetitionTester_error(RepetitionTester* tester, const char* message) {
    const char* test_name = RepetitionTester_get_current_test(tester)->name;
    RepetitionTesterTestContext* context = &tester->context;;
    context->error_happened = true;

    fprintf(stderr, "[%s]: %s\n", test_name, message);
}

void RepetitionTester_run(RepetitionTester* tester) {
    RepetitionTesterTestContext* context = &tester->context;
    RepetitionTesterTestResults* results = &tester->results;

    for (usize i = 0U; i != tester->num_tests; ++i) {
        tester->current_test = i;

        RepetitionTesterTestConfig config = RepetitionTester_get_current_config(tester);
        for (usize iteration = 0U; iteration != config.repetition_count; ++iteration) {
            const RepetitionTesterTest* test = RepetitionTester_get_current_test(tester);
            const void* params = RepetitionTester_get_params_for_test_internal(tester, config.cache_params);

            if (test->print_params != nullptr && params == nullptr) {
                RepetitionTester_error(tester, "A printing method for parameters was provided but parameters cannot be generated");
                continue;
            }

            *context = {};
            *results = {};
            for (usize v_index = 0; v_index != ARRAY_COUNT(results->max.acc); ++v_index) {
                results->min.acc[v_index] = UINT64_MAX;
            }

            printf("------ %s ------\n", test->name);
            printf("Iteration: %zu\n", iteration + 1);
            printf("Configuration:\n");
            printf("  - Max time to run: %llu seconds\n", config.max_seconds_to_run);
            printf("  - Print new minimums: %s\n", config.print_new_minimums ? "yes" : "no");
            printf("  - Cache parameters: %s\n", config.cache_params ? "yes" : "no");
            if (test->print_params != nullptr) {
                printf("Parameters:\n");
                test->print_params(params);
            }
            printf("\n");

            context->start_time = read_cpu_timer();

            for (;;) {
                context->iteration_value = {};
                u64 current_time = read_cpu_timer();

                test->test(tester);

                RepetitionTesterTestValue it_value = context->iteration_value;

                if (context->error_happened) {
                    RepetitionTester_error(tester, "Aborting due to error");
                    goto next_test_loop;
                }

                if (it_value.acc[TestValue_CpuElapsed] == 0) {
                    printf("Seems like test %s is not measuring anything. Skipping\n", test->name);
                    goto next_test_loop;
                }

                it_value.acc[TestValue_TestCount] = 1;
                for (usize v_index = 0U; v_index != ARRAY_COUNT(it_value.acc); ++v_index) {
                    results->total.acc[v_index] += it_value.acc[v_index];
                }

                if (results->max.acc[TestValue_CpuElapsed] < it_value.acc[TestValue_CpuElapsed]) {
                    results->max = it_value;
                }

                if (results->min.acc[TestValue_CpuElapsed] > it_value.acc[TestValue_CpuElapsed]) {
                    results->min = it_value;
                    context->start_time = current_time;

                    if (config.print_new_minimums) {
                        print_progress("Min", results->min ,tester->cpu_frequency);
                        fflush(stdout);
                        printf("                                   \r");
                    }
                }

                if (current_time - context->start_time > config.max_seconds_to_run * tester->cpu_frequency) {
                    break;
                }
            }

            print_results(tester);
            printf("\n");
        }
        next_test_loop:
    }
}

#define REPETITION_TESTER_RUN_TESTS(tests) \
int main(int argc, char* args[]) { \
    RepetitionTester tester = {}; \
    RepetitionTester_initialize(&tester, argc, args, tests, ARRAY_COUNT(tests)); \
    RepetitionTester_run(&tester); \
    return 0; \
}
