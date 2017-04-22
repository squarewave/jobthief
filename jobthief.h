#ifndef JOBTHIEF_H__
#define JOBTHIEF_H__

typedef struct jt_thread_handle
{
    int index;
} jt_thread_handle;

struct jt_job_data;

typedef void (*jt_job_function)(struct jt_job_data* j);

typedef struct jt_job_data {
    struct jt_job_data* parent;
    long unfinished_jobs;
    long weak_reference;
    long waited_on;
    jt_job_function job_function;
    char padding[];
} jt_job_data;

const int MAX_JOBS_PER_THREAD = 4096 * 16;

typedef struct jt_job_deque {
    jt_job_data** jobs;
    int front;
    int back;
} jt_job_deque;

void jt_init(void);
jt_job_data* jt_create_job(jt_job_function fn, void* data, size_t size);
jt_job_data* jt_create_child_job(jt_job_data* parent, jt_job_function fn,
                                 void* data, size_t size);
void jt_run_job(jt_job_data* j, int keep_reference);
void jt_wait_on_job(jt_job_data* j);

void _free_job(jt_job_data* job);
jt_job_data* _allocate_job(void);
void _jt_init_worker_job_deque(void);
void _jt_execute_job(jt_job_data* job);
void _jt_worker_run_loop(void);

#endif /* end of include guard: JOBTHIEF_H__ */

#ifdef JOBTHIEF_IMPLEMENTATION

static int _g_cache_line_size = {0};
static int _g_main_thread_id = 0;
static long _g_thread_count = 1;
static jt_job_deque* _g_job_deques = NULL;

#include <assert.h>
#include <malloc.h>

#ifdef _WIN32
#pragma warning(push, 0)
#include "Windows.h"
#pragma warning(pop)

__declspec( thread ) int _t_worker_id;
__declspec( thread ) int _t_counter;
__declspec( thread ) int _t_job_count;
__declspec( thread ) jt_job_data* _t_job_buffer;

CRITICAL_SECTION _jt_deque_section;

void _jt_push(jt_job_data* job)
{
    jt_job_deque* deque = &_g_job_deques[_t_worker_id];
    deque->jobs[deque->back & (MAX_JOBS_PER_THREAD - 1)] = job;
    deque->back++;
}

jt_job_data* _jt_pop(void)
{
    jt_job_data* result = NULL;
    jt_job_deque* deque = &_g_job_deques[_t_worker_id];

    int back = deque->back;
    MemoryBarrier();
    int front = deque->front;

    // if we're taking the last element, then this needs to operate on front
    // to sync with _jt_steal. Otherwise we can modify back, which only our
    // thread touches.
    if (back == front + 1) {

    } else if (back > front) {

    }

    if (back > front) {
        deque->back--;
        result = deque->jobs[deque->back & (MAX_JOBS_PER_THREAD - 1)];
    }

    return result;
}

jt_job_data* _jt_steal(int worker_id)
{
    jt_job_data* result = NULL;
    jt_job_deque* deque = &_g_job_deques[worker_id];

    int front = deque->front;
    // MemoryBarrier(); TODO?
    int back = deque->back;

    if (back > front) {
        if (InterlockedCompareExchange(deque->front, deque->front + 1, front) == front) {
            result = deque->jobs[front & (MAX_JOBS_PER_THREAD - 1)];
        }
	}

    return result;
}

DWORD WINAPI _jt_worker_thread_proc(void* parameter)
{
    _t_worker_id = InterlockedIncrement(&_g_thread_count) - 1;
    _t_counter = _t_worker_id;
    _jt_init_worker_job_deque();

    jt_job_deque* deque = &_g_job_deques[_t_worker_id];

    while (1) {
        _jt_worker_run_loop();
    }
}

int _jt_get_number_of_cores(void)
{
    SYSTEM_INFO sysinfo;

    GetSystemInfo(&sysinfo);

    return sysinfo.dwNumberOfProcessors;
}

int _jt_get_cache_line_size(void)
{
    int result = -1;

    SYSTEM_LOGICAL_PROCESSOR_INFORMATION* buffer = NULL;
    DWORD result_size = 0;

    GetLogicalProcessorInformation(buffer, (PDWORD)&result_size);
    buffer = malloc(result_size);
    GetLogicalProcessorInformation(buffer, (PDWORD)&result_size);

    int count = result_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);

    for (int i = 0; i < count; ++i) {
        if (buffer[i].Relationship == RelationCache && buffer[i].Cache.Level == 1) {
            result = buffer[i].Cache.LineSize;
            break;
        }
    }

    assert(result != -1);

    return result;
}

int _jt_get_current_thread_id(void)
{
    return GetCurrentThreadId();
}

jt_thread_handle _jt_spawn_thread(void)
{
    assert(_jt_get_current_thread_id() == _g_main_thread_id);

    CreateThread(NULL, 0, _jt_worker_thread_proc, NULL, 0, NULL);

    jt_thread_handle result = {0};
    return result;
}

void _jt_execute_job(jt_job_data* job)
{
    job->job_function(job);
    jt_job_data* ptr = job;

    int weak_reference = ptr->weak_reference;
    long child_finished = !InterlockedDecrement(&job->unfinished_jobs);
    while (child_finished && ptr->parent) {
        jt_job_data* parent = ptr->parent;;
        if (weak_reference) {
            _free_job(ptr);
        }
        ptr = parent;
        weak_reference = ptr->weak_reference;
        child_finished = !InterlockedDecrement(&ptr->unfinished_jobs);
    }
}

jt_job_data* jt_create_child_job(jt_job_data* parent, jt_job_function fn,
                                 void* data, size_t size)
{
    jt_job_data* j = _allocate_job();
    j->parent = parent;
    InterlockedIncrement(&parent->unfinished_jobs);
    j->unfinished_jobs = 1;
    j->job_function = fn;
    memcpy(j->padding, data, size);
    return j;
}

#endif

void _jt_worker_run_loop(void)
{
    jt_job_data* job = _jt_pop();
    if (job) {
        _jt_execute_job(job);
    } else {
        _t_counter = (_t_counter + 1) % _g_thread_count;
        if (_t_counter == _t_worker_id) {
            _t_counter = (_t_counter + 1) % _g_thread_count;
        }
        job = _jt_steal(_t_counter);
        if (job) {
            _jt_execute_job(job);
        }
    }
}

jt_job_data* _allocate_job(void)
{
    return calloc(_g_cache_line_size, 1);
    // char* buffer_start = (char*)_t_job_buffer;
    // int count = _t_job_count++ & (MAX_JOBS_PER_THREAD - 1);
    // return (jt_job_data*)(buffer_start + count * _g_cache_line_size);
}

void _free_job(jt_job_data* job)
{
    free(job);
}

void _jt_init_worker_job_deque(void)
{
    _t_job_buffer = malloc(MAX_JOBS_PER_THREAD * _g_cache_line_size);
    jt_job_deque* deque = &_g_job_deques[_t_worker_id];
    deque->jobs = malloc(MAX_JOBS_PER_THREAD * sizeof(jt_job_data*));
    deque->front = 0;
    deque->back = 0;
}

void _jt_auto_spawn_threads(void)
{
    int tc = _jt_get_number_of_cores();

    // spawn (tc - 1) threads (since the main thread will also need a core)
    for (int i = 1; i < tc; ++i) {
        _jt_spawn_thread();
    }
}

void jt_init(void)
{
    _g_main_thread_id = _jt_get_current_thread_id();
    _g_cache_line_size = _jt_get_cache_line_size();

    int tc = _jt_get_number_of_cores();
    _g_job_deques = calloc(tc, sizeof(jt_job_deque));

    _t_worker_id = 0; // the main thread gets worker ID 0
    _jt_init_worker_job_deque();

    InitializeCriticalSectionAndSpinCount(&_jt_deque_section, 0x00000400);

    _jt_auto_spawn_threads();
}

jt_job_data* jt_create_job(jt_job_function fn, void* data, size_t size)
{
    jt_job_data* j = _allocate_job();
    j->parent = NULL;
    j->unfinished_jobs = 1;
    j->job_function = fn;
    memcpy(j->padding, data, size);
    return j;
}

void jt_run_job(jt_job_data* job, int keep_reference)
{
    job->weak_reference = !keep_reference;
    _jt_push(job);
}

void jt_wait_on_job(jt_job_data* job)
{
    assert(!job->weak_reference);
    while (job->unfinished_jobs) {
        _jt_worker_run_loop();
    }
    _free_job(job);
}

#endif