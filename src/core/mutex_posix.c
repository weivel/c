#include "mutex.h"

int weivel_c_mutex_start(weivel_c_mutex *mutex) {
    return pthread_mutex_init(&mutex->state, NULL) == 0;
}

void weivel_c_mutex_end(weivel_c_mutex *mutex) {
    pthread_mutex_destroy(&mutex->state);
}

void weivel_c_mutex_lock(weivel_c_mutex *mutex) {
    pthread_mutex_lock(&mutex->state);
}

void weivel_c_mutex_unlock(weivel_c_mutex *mutex) {
    pthread_mutex_unlock(&mutex->state);
}
