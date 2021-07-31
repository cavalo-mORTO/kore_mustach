#include <kore/kore.h>
#include <kore/http.h>
#include <kore/tasks.h>
#include <mustach/mustach.h>
#include <mustach/kore_mustach.h>
#include "assets.h"

int asset_serve_mustach(struct http_request *, int, const void *, const void *);
int run_task(struct kore_task *);

struct rstate {
    struct kore_task task;
};

int
asset_serve_mustach(struct http_request *req, int status, const void *template, const void *data)
{
    struct rstate *state;
    size_t  l;
    char    *message;

    if (!http_state_exists(req)) {
        state = http_state_create(req, sizeof(*state), NULL);

        kore_task_create(&state->task, run_task);
        kore_task_bind_request(&state->task, req);

        kore_task_run(&state->task);

        message = kore_strdup(template);
        kore_task_channel_write(&state->task, message, strlen(message));
        kore_free(message);

        message = kore_strdup(data);
        kore_task_channel_write(&state->task, message, strlen(message));
        kore_free(message);

        return (KORE_RESULT_RETRY);
    } else {
        state = http_state_get(req);
    }

    if (!kore_task_finished(&state->task)) {
        http_request_sleep(req);
        return (KORE_RESULT_RETRY);
    }

    if (kore_task_result(&state->task) != KORE_RESULT_OK) {
        kore_task_destroy(&state->task);
        http_state_cleanup(req);
        http_response(req, 500, NULL, 0);
        return (KORE_RESULT_OK);
    }

    message = kore_malloc(USHRT_MAX);
    l = kore_task_channel_read(&state->task, message, USHRT_MAX);
    if (l > USHRT_MAX) {
        http_response(req, 500, NULL, 0);
    } else {
        http_response(req, 200, message, l);
    }

    kore_task_destroy(&state->task);
    http_state_cleanup(req);
    kore_free(message);

    return (KORE_RESULT_OK);
}

int
run_task(struct kore_task *t)
{
    size_t  l;
    char    *template, *data, *result;

    template = kore_malloc(USHRT_MAX);
    l = kore_task_channel_read(t, template, USHRT_MAX);
    if (l > USHRT_MAX) {
        kore_free(template);
        return (KORE_RESULT_ERROR);
    }

    data = kore_malloc(USHRT_MAX);
    l = kore_task_channel_read(t, data, USHRT_MAX);
    if (l > USHRT_MAX) {
        kore_free(template);
        kore_free(data);
        return (KORE_RESULT_ERROR);
    }

    kore_mustach(template, data, Mustach_With_AllExtensions, &result, &l);

    kore_task_channel_write(t, result, l);
    kore_free(result);
    kore_free(template);
    kore_free(data);
    return (KORE_RESULT_OK);
}
