#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

char *test_getenv(const char *name);
#define getenv test_getenv
#include "blur_locale.h"
#undef getenv

static atomic_uint getenv_calls;

char *test_getenv(const char *name) {
    const struct timespec delay = {0, 20000000};
    atomic_fetch_add_explicit(&getenv_calls, 1, memory_order_relaxed);
    nanosleep(&delay, NULL);
    return getenv(name);
}

struct start_gate {
    pthread_mutex_t mutex;
    pthread_cond_t ready_condition;
    pthread_cond_t start_condition;
    unsigned ready;
    int released;
};

struct reader {
    struct start_gate *gate;
    unsigned language_id;
};

static void *read_language(void *opaque) {
    struct reader *reader = opaque;
    struct start_gate *gate = reader->gate;

    assert(pthread_mutex_lock(&gate->mutex) == 0);
    gate->ready++;
    assert(pthread_cond_signal(&gate->ready_condition) == 0);
    while (!gate->released)
        assert(pthread_cond_wait(&gate->start_condition, &gate->mutex) == 0);
    assert(pthread_mutex_unlock(&gate->mutex) == 0);

    reader->language_id = blur_language_id();
    return NULL;
}

int main(void) {
    assert(blur_language_id_from_locale(NULL) == 0x0409);
    assert(blur_language_id_from_locale("en") == 0x0409);
    assert(blur_language_id_from_locale("fr") == 0x040c);
    assert(blur_language_id_from_locale("fr-FR") == 0x040c);
    assert(blur_language_id_from_locale("FR_fr") == 0x040c);
    assert(blur_language_id_from_locale("fr--FR") == 0x0409);
    assert(blur_language_id_from_locale("french") == 0x0409);
    assert(blur_language_id_from_locale("fr-../../") == 0x0409);
    assert(blur_language_id_from_locale("unsupported") == 0x0409);

    setenv("BLUR_LANG", "fr", 1);
    enum { READER_COUNT = 16 };
    pthread_t threads[READER_COUNT];
    struct reader readers[READER_COUNT];
    struct start_gate gate = {
        PTHREAD_MUTEX_INITIALIZER,
        PTHREAD_COND_INITIALIZER,
        PTHREAD_COND_INITIALIZER,
        0,
        0,
    };

    for (unsigned i = 0; i < READER_COUNT; i++) {
        readers[i].gate = &gate;
        readers[i].language_id = 0;
        assert(pthread_create(&threads[i], NULL, read_language, &readers[i]) == 0);
    }

    assert(pthread_mutex_lock(&gate.mutex) == 0);
    while (gate.ready != READER_COUNT)
        assert(pthread_cond_wait(&gate.ready_condition, &gate.mutex) == 0);
    gate.released = 1;
    assert(pthread_cond_broadcast(&gate.start_condition) == 0);
    assert(pthread_mutex_unlock(&gate.mutex) == 0);

    for (unsigned i = 0; i < READER_COUNT; i++) {
        assert(pthread_join(threads[i], NULL) == 0);
        assert(readers[i].language_id == 0x040c);
    }
    assert(atomic_load_explicit(&getenv_calls, memory_order_relaxed) == 1);
    assert(pthread_cond_destroy(&gate.start_condition) == 0);
    assert(pthread_cond_destroy(&gate.ready_condition) == 0);
    assert(pthread_mutex_destroy(&gate.mutex) == 0);
    return 0;
}
