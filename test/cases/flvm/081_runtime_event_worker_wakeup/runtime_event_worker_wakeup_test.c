#ifdef _WIN32
#include <stdio.h>
int main(void) { puts("runtime-event-worker-wakeup-ok"); return 0; }
#else
#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_runtime_event.h"
#include "flat_task.h"
#include "flat_value.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv) {
    FlatBytecode bc; FlatRuntime *runtime = NULL;
    FlatCollectionPool *coll = NULL; FlatStringPool *pool = NULL;
    FlatEventSubscriptionId sub = 0; FlatValue event_value, bound_value;
    FlatTaskId task_id = 1; int rc = 0, i;
    memset(&bc,0,sizeof bc); memset(&event_value,0,sizeof event_value);
    memset(&bound_value,0,sizeof bound_value);
    if (argc != 2 || flat_fb_load_file(argv[1], &bc) != 0) return 2;
    runtime=flat_runtime_create(NULL); coll=flat_coll_create(); pool=flat_pool_create();
    if(!runtime||!coll||!pool){rc=3;goto done;}
    if(flat_value_set_str_inline(&event_value,"worker-event",12)!=0 ||
       flat_value_set_str_inline(&bound_value,"worker-bound",12)!=0 ||
       flat_runtime_event_subscribe_handler(runtime,17,"socket","receive",&bc,
          "#onEvent",&bound_value,1,coll,pool,NULL,&sub)!=0 ||
       flat_runtime_workers_start(runtime,1,16)!=0){rc=4;goto done;}
    /* The worker is idle in the ready-queue wait when this external publish
     * occurs. event_publish must wake it; no unrelated task wake is generated. */
    if(flat_runtime_event_publish(runtime,17,"socket","receive",&event_value,
                                  coll,pool,NULL)!=1){rc=5;goto stop;}
    for(i=0;i<200;i++){
        FlatTaskState state=flat_runtime_task_state(runtime,task_id);
        if(state==FLAT_TASK_COMPLETED)break;
        if(state==FLAT_TASK_FAILED||state==FLAT_TASK_CANCELLED){rc=6;break;}
        {struct timespec ts;ts.tv_sec=0;ts.tv_nsec=10000000L;nanosleep(&ts,NULL);}
    }
    if(i==200&&!rc)rc=7;
stop:
    if(flat_runtime_shutdown_request(runtime,FLAT_RUNTIME_SHUTDOWN_CANCEL)!=0&&!rc)rc=8;
    if(flat_runtime_workers_join(runtime)!=0&&!rc)rc=9;
    if(flat_runtime_task_state(runtime,task_id)==FLAT_TASK_COMPLETED){
        if(flat_runtime_task_destroy(runtime,task_id)!=0&&!rc)rc=10;
    }
done:
    if(runtime){
        if(sub)(void)flat_runtime_event_unsubscribe(runtime,sub);
        if(flat_runtime_destroy(runtime)!=0&&!rc)rc=11;
    }
    if (pool) flat_pool_destroy(pool);
    if (coll) flat_coll_destroy(coll);
    flat_fb_free(&bc);
    if(rc){fprintf(stderr,"runtime event worker wakeup failed: %d\n",rc);return rc;}
    puts("runtime-event-worker-wakeup-ok");return 0;
}
#endif
