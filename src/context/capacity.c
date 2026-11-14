#include "contexts.h"

/**
 * Reports one answer to the core, with the host's time.
 *
 * @implNote The core takes an answer only with the time it was given at, and a constrained want is only ever made
 * through a host that has a clock, so a question met without one was asked around the contexts. A status of NONE is
 * an answer that came after its question closed, which is no failure.
 */
static void send_the_answer(weivel_c_contexts *contexts, const weivel_c_event *event,
                            const weivel_capacity *capacity) {
    const weivel_capacity_event *asked = &event->data.capacity;
    weivel_c_status status = WEIVEL_C_STATUS_MISUSE;
    int64_t now = 0;
    if (contexts->timer != NULL) {
        status = weivel_c_timer_now(contexts->timer, &now);
    }
    if (status == WEIVEL_C_STATUS_OK) {
        status = weivel_c_core_report_capacity(contexts->core, asked->want, asked->registration, capacity, now, NULL);
    }
    if (status != WEIVEL_C_STATUS_OK && status != WEIVEL_C_STATUS_NONE && status != WEIVEL_C_STATUS_DESTROYED) {
        weivel_c_contexts_report(contexts, WEIVEL_C_FAILURE_CAPACITY_NOT_REPORTED, status, event,
                                 "a capacity answer could not be reported, so its question stays unanswered");
    }
}

/** Puts the question to the registration's answerer. Answers 0 when it failed, which the sink was told. */
static int ask_the_answerer(weivel_c_contexts *contexts, const weivel_c_event *event,
                            const weivel_c_record *registration, weivel_capacity *capacity) {
    weivel_c_capacity_question question = WEIVEL_INIT(weivel_c_capacity_question);
    weivel_c_capacity_answer answer = WEIVEL_INIT(weivel_c_capacity_answer);
    weivel_c_status answered;
    question.question = event->data.capacity.question;
    question.id = registration->id;
    question.constraints = event->data.capacity.constraints;
    answered = registration->answerer.answer(registration->answerer.data, &question, &answer);
    if (answered != WEIVEL_C_STATUS_OK) {
        weivel_c_contexts_report(contexts, WEIVEL_C_FAILURE_CAPACITY_ANSWERER, answered, event,
                                 "a capacity answerer answered a failure, so its question was left unanswered");
        return 0;
    }
    capacity->attributes = answer.attributes;
    capacity->serves = answer.serves;
    return 1;
}

/**
 * @implNote An incomplete question is left unanswered: its constraints are missing, not empty, and the wrapper told
 * the sink when it drained it. A registration that offered no answerer answers at once: no live attributes, so its
 * manifest's stand, and that it serves.
 */
void weivel_c_contexts_answer_capacity(weivel_c_contexts *contexts, const weivel_c_event *event) {
    weivel_capacity capacity = WEIVEL_INIT(weivel_capacity);
    weivel_c_record *registration;
    int answered = 1;
    if (event->incomplete) {
        return;
    }
    registration = weivel_c_record_borrow(contexts, event->data.capacity.registration, WEIVEL_C_RECORD_SERVICE);
    if (registration == NULL) {
        return;
    }
    capacity.request = event->data.capacity.request;
    capacity.serves = WEIVEL_CHOICE_YES;
    if (registration->answerer.answer != NULL) {
        answered = ask_the_answerer(contexts, event, registration, &capacity);
    }
    weivel_c_record_put_back(registration);
    if (answered) {
        send_the_answer(contexts, event, &capacity);
    }
}
