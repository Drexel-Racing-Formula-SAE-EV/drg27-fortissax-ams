#include <ams_core/ams_imd.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

static ams_imd_t state;
static atomic_bool start;
static void *writer(void *unused)
{
    (void)unused;
    while (!atomic_load(&start)) {}
    for (unsigned i=0; i<100000U; ++i)
        ams_imd_capture_publish(&state, i&1U ? 25U:100U,
                               i&1U ? 100U:200U, 1000U);
    return NULL;
}
int main(void)
{
    pthread_t thread;
    ams_imd_init(&state,1000U,true);
    atomic_init(&start,false);
    assert(pthread_create(&thread,NULL,writer,NULL)==0);
    atomic_store(&start,true);
    for (unsigned i=0; i<100000U; ++i) {
        if (ams_imd_read_at(&state,true,true,1000U)==0) {
            assert((state.total_count==100U && state.high_count==25U) ||
                   (state.total_count==200U && state.high_count==100U));
        }
    }
    assert(pthread_join(thread,NULL)==0);
    assert(ams_imd_read_at(&state,true,true,1000U)==0);
    puts("PASS IMD concurrent tuple publication: 100000 writes/reads");
    return 0;
}
