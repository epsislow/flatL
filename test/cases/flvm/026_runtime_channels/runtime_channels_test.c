#include "fb_format.h"
#include "flat_channel.h"
#include "flat_task.h"
#include "flat_runtime.h"
#include "flat_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int expect_number(const FlatValue *value, double expected) {
    return value && value->tag == VAL_NUMBER && value->u.number.payload.f64 == expected;
}

int main(int argc, char **argv) {
    FlatRuntime *runtime = NULL;
    FlatCollectionPool *source_coll = NULL;
    FlatStringPool *source_pool = NULL;
    FlatCollectionPool *dest_coll = NULL;
    FlatStringPool *dest_pool = NULL;
    FlatChannelId channel = 0;
    FlatChannelId independent = 0;
    FlatBytecode bc;
    FlatBytecode fail_bc;
    FlatTaskId owner_task = 0;
    FlatTaskId wait_target = 0;
    FlatValue value;
    FlatValue out;
    FlatValue items[2];
    FlatValue object_value;
    FlatValue nested_list_value;
    uint32_t list_id = 0;
    uint32_t object_id = 0;
    int len = 0;
    int rc = 0;
    memset(&bc, 0, sizeof bc);
    memset(&fail_bc, 0, sizeof fail_bc);
    memset(&value, 0, sizeof value);
    memset(&out, 0, sizeof out);
    memset(items, 0, sizeof items);
    memset(&object_value, 0, sizeof object_value);
    memset(&nested_list_value, 0, sizeof nested_list_value);

    if (argc != 3 || flat_fb_load_file(argv[1], &bc) != 0 ||
        flat_fb_load_file(argv[2], &fail_bc) != 0) return 21;
    runtime = flat_runtime_create(NULL);
    source_coll = flat_coll_create();
    source_pool = flat_pool_create();
    dest_coll = flat_coll_create();
    dest_pool = flat_pool_create();
    if (!runtime || !source_coll || !source_pool || !dest_coll || !dest_pool) { rc = 1; goto done; }
    if (flat_runtime_channel_create(runtime, &channel) != 0 ||
        flat_runtime_channel_create(runtime, &independent) != 0 ||
        channel == 0 || independent == 0 || channel == independent) { rc = 2; goto done; }
    if (flat_runtime_destroy(runtime) == 0) { rc = 3; goto done; }

    /* Inline string is copied into Channel-owned storage. */
    flat_value_set_str_inline(&value, "shared", 6);
    if (flat_runtime_channel_set(runtime, channel, 0, "label", &value,
                                 source_coll, source_pool, NULL) != 0 ||
        flat_runtime_channel_var(runtime, channel, 0, "label", dest_coll, dest_pool,
                                 NULL, &out) != 0 || out.tag != VAL_STR_INLINE ||
        out.u.ins.len != 6 || memcmp(out.u.ins.bytes, "shared", 6) != 0) { rc = 4; goto done; }
    flat_value_release(dest_coll, dest_pool, &out);
    out.tag = VAL_EMPTY;

    /* LIST is deep-copied between independent VM pools. */
    flat_value_set_number(&items[0], 3.0);
    flat_value_set_number(&items[1], 5.0);
    if (flat_list_build(source_coll, source_pool, items, 2, &list_id) != 0) { rc = 5; goto done; }
    flat_value_set_list(&value, list_id);
    flat_value_retain(source_coll, source_pool, &value);
    if (flat_runtime_channel_set(runtime, channel, 0, "numbers", &value,
                                 source_coll, source_pool, NULL) != 0) { rc = 6; goto done; }
    flat_value_release(source_coll, source_pool, &value);
    value.tag = VAL_EMPTY;
    if (flat_runtime_channel_var(runtime, channel, 0, "numbers", dest_coll, dest_pool,
                                 NULL, &out) != 0 || out.tag != VAL_LIST ||
        flat_list_len(dest_coll, out.u.coll_id, &len) != 0 || len != 2) { rc = 7; goto done; }
    {
        FlatValue first;
        FlatValue second;
        if (flat_list_get(dest_coll, out.u.coll_id, 0, &first) != 0 ||
            flat_list_get(dest_coll, out.u.coll_id, 1, &second) != 0 ||
            !expect_number(&first, 3.0) || !expect_number(&second, 5.0)) { rc = 22; goto done; }
    }
    flat_value_release(dest_coll, dest_pool, &out);
    out.tag = VAL_EMPTY;

    /* OBJECT values are copied into Channel-owned collection storage. */
    if (flat_obj_empty(source_coll, source_pool, &object_id) != 0) { rc = 8; goto done; }
    flat_value_set_number(&value, 42.0);
    if (flat_obj_set_copy(source_coll, source_pool, object_id, "answer", &value, &object_id) != 0 ||
        flat_list_build(source_coll, source_pool, items, 2, &list_id) != 0) {
        rc = 9; goto done;
    }
    flat_value_set_list(&nested_list_value, list_id);
    flat_value_retain(source_coll, source_pool, &nested_list_value);
    if (flat_obj_set_copy(source_coll, source_pool, object_id, "nested", &nested_list_value,
                          &object_id) != 0) { rc = 34; goto done; }
    flat_value_release(source_coll, source_pool, &nested_list_value);
    nested_list_value.tag = VAL_EMPTY;
    flat_value_set_object(&object_value, object_id);
    flat_value_retain(source_coll, source_pool, &object_value);
    if (flat_runtime_channel_set(runtime, channel, 0, "object", &object_value,
                                 source_coll, source_pool, NULL) != 0) { rc = 10; goto done; }
    flat_value_release(source_coll, source_pool, &object_value);
    object_value.tag = VAL_EMPTY;
    if (flat_runtime_channel_var(runtime, channel, 0, "object", dest_coll, dest_pool,
                                 NULL, &out) != 0 || out.tag != VAL_OBJECT) { rc = 11; goto done; }
    {
        FlatValue answer;
        FlatValue nested;
        int nested_len = 0;
        if (flat_obj_get(dest_coll, out.u.coll_id, "answer", &answer) != 0 ||
            !expect_number(&answer, 42.0) ||
            flat_obj_get(dest_coll, out.u.coll_id, "nested", &nested) != 0 ||
            nested.tag != VAL_LIST || flat_list_len(dest_coll, nested.u.coll_id, &nested_len) != 0 ||
            nested_len != 2) { rc = 26; goto done; }
    }
    flat_value_release(dest_coll, dest_pool, &out);
    out.tag = VAL_EMPTY;

    /* A registered task owns explicit transactions; revert preserves lock, unlock commits. */
    if (flat_runtime_task_create(runtime, &bc, NULL, &owner_task) != 0 ||
        flat_runtime_task_create(runtime, &bc, NULL, &wait_target) != 0 ||
        flat_runtime_channel_lock(runtime, channel, 999999) == 0) { rc = 27; goto done; }
    flat_value_set_number(&value, 1.0);
    if (flat_runtime_channel_set(runtime, channel, 0, "count", &value,
                                 source_coll, source_pool, NULL) != 0 ||
        flat_runtime_channel_lock(runtime, channel, owner_task) != 0 ||
        flat_runtime_task_start(runtime, owner_task) != 0 ||
        flat_runtime_task_wait(runtime, owner_task, wait_target) == 0 ||
        flat_runtime_task_destroy(runtime, wait_target) != 0) { rc = 12; goto done; }
    wait_target = 0;
    flat_value_set_number(&value, 2.0);
    if (flat_runtime_channel_set(runtime, channel, owner_task, "count", &value,
                                 source_coll, source_pool, NULL) != 0 ||
        flat_runtime_channel_set(runtime, channel, 88, "count", &value,
                                 source_coll, source_pool, NULL) != -2 ||
        flat_runtime_channel_var(runtime, channel, 88, "count", dest_coll, dest_pool,
                                 NULL, &out) != -2 ||
        flat_runtime_channel_revert(runtime, channel, owner_task) != 0) { rc = 13; goto done; }
    if (flat_runtime_channel_var(runtime, channel, owner_task, "count", dest_coll, dest_pool,
                                 NULL, &out) != 0 || !expect_number(&out, 1.0)) { rc = 14; goto done; }
    flat_value_release(dest_coll, dest_pool, &out);
    out.tag = VAL_EMPTY;
    flat_value_set_number(&value, 3.0);
    if (flat_runtime_channel_set(runtime, channel, owner_task, "count", &value,
                                 source_coll, source_pool, NULL) != 0 ||
        flat_runtime_channel_unlock(runtime, channel, owner_task) != 0 ||
        flat_runtime_channel_var(runtime, channel, 88, "count", dest_coll, dest_pool,
                                 NULL, &out) != 0 || !expect_number(&out, 3.0)) { rc = 15; goto done; }
    flat_value_release(dest_coll, dest_pool, &out);
    out.tag = VAL_EMPTY;

    /* A failed transfer (file handle) must not mutate the existing value. */
    memset(&value, 0, sizeof value);
    value.tag = VAL_FILE;
    value.u.file_id = 1;
    if (flat_runtime_channel_set(runtime, channel, 0, "count", &value,
                                 source_coll, source_pool, NULL) == 0 ||
        flat_runtime_channel_var(runtime, channel, 0, "count", dest_coll, dest_pool,
                                 NULL, &out) != 0 || !expect_number(&out, 3.0)) { rc = 16; goto done; }
    flat_value_release(dest_coll, dest_pool, &out);
    out.tag = VAL_EMPTY;

    if (flat_runtime_channel_var(runtime, channel, 0, "missing", dest_coll, dest_pool,
                                 NULL, &out) == 0 ||
        flat_runtime_channel_set(runtime, independent, 0, "count", &value,
                                 source_coll, source_pool, NULL) == 0) { rc = 17; goto done; }
    flat_value_set_number(&value, 99.0);
    if (flat_runtime_channel_set(runtime, independent, 0, "count", &value,
                                 source_coll, source_pool, NULL) != 0 ||
        flat_runtime_channel_var(runtime, channel, 0, "count", dest_coll, dest_pool,
                                 NULL, &out) != 0 || !expect_number(&out, 3.0)) { rc = 18; goto done; }
    flat_value_release(dest_coll, dest_pool, &out);
    out.tag = VAL_EMPTY;

    /* A task that terminates while owning a transaction rolls it back and unlocks. */
    if (flat_runtime_channel_lock(runtime, channel, owner_task) != 0) { rc = 23; goto done; }
    flat_value_set_number(&value, 4.0);
    if (flat_runtime_channel_set(runtime, channel, owner_task, "count", &value,
                                 source_coll, source_pool, NULL) != 0 ||
        flat_runtime_task_cancel(runtime, owner_task) != 0 ||
        flat_runtime_channel_var(runtime, channel, 0, "count", dest_coll, dest_pool,
                                 NULL, &out) != 0 || !expect_number(&out, 3.0)) { rc = 24; goto done; }
    flat_value_release(dest_coll, dest_pool, &out);
    out.tag = VAL_EMPTY;
    if (flat_runtime_task_destroy(runtime, owner_task) != 0) { rc = 25; goto done; }
    owner_task = 0;

    /* Normal task completion also rolls back an uncommitted transaction. */
    if (flat_runtime_task_create(runtime, &bc, NULL, &owner_task) != 0 ||
        flat_runtime_channel_lock(runtime, channel, owner_task) != 0) { rc = 28; goto done; }
    flat_value_set_number(&value, 5.0);
    if (flat_runtime_channel_set(runtime, channel, owner_task, "count", &value,
                                 source_coll, source_pool, NULL) != 0 ||
        flat_runtime_task_start(runtime, owner_task) != 0 ||
        flat_runtime_schedule(runtime, 1, 0, NULL) != 0 ||
        flat_runtime_task_state(runtime, owner_task) != FLAT_TASK_COMPLETED ||
        flat_runtime_channel_var(runtime, channel, 0, "count", dest_coll, dest_pool,
                                 NULL, &out) != 0 || !expect_number(&out, 3.0)) { rc = 29; goto done; }
    flat_value_release(dest_coll, dest_pool, &out);
    out.tag = VAL_EMPTY;
    if (flat_runtime_task_destroy(runtime, owner_task) != 0) { rc = 30; goto done; }
    owner_task = 0;

    /* Failure also rolls back and releases a task-owned transaction. */
    if (flat_runtime_task_create(runtime, &fail_bc, NULL, &owner_task) != 0 ||
        flat_runtime_channel_lock(runtime, channel, owner_task) != 0) { rc = 31; goto done; }
    flat_value_set_number(&value, 6.0);
    if (flat_runtime_channel_set(runtime, channel, owner_task, "count", &value,
                                 source_coll, source_pool, NULL) != 0 ||
        flat_runtime_task_start(runtime, owner_task) != 0 ||
        flat_runtime_schedule(runtime, 1, 0, NULL) != 0 ||
        flat_runtime_task_state(runtime, owner_task) != FLAT_TASK_FAILED ||
        flat_runtime_channel_var(runtime, channel, 0, "count", dest_coll, dest_pool,
                                 NULL, &out) != 0 || !expect_number(&out, 3.0)) { rc = 32; goto done; }
    flat_value_release(dest_coll, dest_pool, &out);
    out.tag = VAL_EMPTY;
    if (flat_runtime_task_destroy(runtime, owner_task) != 0) { rc = 33; goto done; }
    owner_task = 0;

    if (flat_runtime_task_create(runtime, &bc, NULL, &owner_task) != 0) { rc = 191; goto done; }
    if (flat_runtime_channel_lock(runtime, channel, owner_task) != 0) { rc = 192; goto done; }
    if (flat_runtime_channel_destroy(runtime, channel) == 0) { rc = 193; goto done; }
    if (flat_runtime_channel_unlock(runtime, channel, owner_task) != 0) { rc = 194; goto done; }
    if (flat_runtime_task_destroy(runtime, owner_task) != 0) { rc = 195; goto done; }
    if (flat_runtime_channel_destroy(runtime, independent) != 0) { rc = 196; goto done; }
    if (flat_runtime_channel_destroy(runtime, channel) != 0) { rc = 197; goto done; }
    if (flat_runtime_channel_destroy(runtime, channel) == 0) { rc = 198; goto done; }
    if (flat_runtime_destroy(runtime) != 0) { rc = 20; goto done; }
    runtime = NULL;
    flat_fb_free(&bc);
    flat_fb_free(&fail_bc);

done:
    if (out.tag != VAL_EMPTY && dest_coll && dest_pool) flat_value_release(dest_coll, dest_pool, &out);
    out.tag = VAL_EMPTY;
    if (value.tag != VAL_EMPTY && source_coll && source_pool && value.tag != VAL_FILE)
        flat_value_release(source_coll, source_pool, &value);
    value.tag = VAL_EMPTY;
    if (object_value.tag != VAL_EMPTY && source_coll && source_pool)
        flat_value_release(source_coll, source_pool, &object_value);
    object_value.tag = VAL_EMPTY;
    if (nested_list_value.tag != VAL_EMPTY && source_coll && source_pool)
        flat_value_release(source_coll, source_pool, &nested_list_value);
    nested_list_value.tag = VAL_EMPTY;
    flat_coll_destroy(source_coll);
    flat_pool_destroy(source_pool);
    flat_coll_destroy(dest_coll);
    flat_pool_destroy(dest_pool);
    if (runtime) {
        if (wait_target) flat_runtime_task_destroy(runtime, wait_target);
        if (owner_task) flat_runtime_task_destroy(runtime, owner_task);
        if (channel) flat_runtime_channel_destroy(runtime, channel);
        if (independent) flat_runtime_channel_destroy(runtime, independent);
        flat_runtime_destroy(runtime);
    }
    flat_fb_free(&bc);
    flat_fb_free(&fail_bc);
    if (rc) {
        fprintf(stderr, "runtime Channel test failed: %d\n", rc);
        return rc;
    }
    puts("runtime-channels-ok");
    return 0;
}
