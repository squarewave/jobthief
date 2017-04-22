#include <stdio.h>
#include <stdbool.h>
#pragma warning(push, 0)
#include <Windows.h>
#pragma warning(pop)

#define JOBTHIEF_IMPLEMENTATION
#include "jobthief.h"

#define SPAWN_AMOUNT 128

static long job_count = 0;

void leaf_job(jt_job_data* job)
{
    InterlockedIncrement(&job_count);
}

void spawn_level_2(jt_job_data* job)
{
    for (int i = 0; i < SPAWN_AMOUNT; ++i) {
        jt_job_data* child = jt_create_child_job(job, leaf_job, NULL, 0);
        jt_run_job(child, false);
    }
}

void spawn_level_1(jt_job_data* job)
{
    for (int i = 0; i < SPAWN_AMOUNT; ++i) {
        jt_job_data* child = jt_create_child_job(job, spawn_level_2, NULL, 0);
        jt_run_job(child, false);
    }
}

void spawn_level_0(jt_job_data* job)
{
    for (int i = 0; i < SPAWN_AMOUNT; ++i) {
        jt_job_data* child = jt_create_child_job(job, spawn_level_1, NULL, 0);
        jt_run_job(child, false);
    }
}


int main(void)
{
    jt_init();
    jt_job_data* root = jt_create_job(spawn_level_0, NULL, 0);
    jt_run_job(root, true);

    jt_wait_on_job(root);
}