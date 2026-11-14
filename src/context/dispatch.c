#include "contexts.h"

/**
 * @file
 * The one listener of the contexts: each event of the core is routed, through the handles table, to the record or
 * the context it names. It runs in the dispatch, outside every lock, so each callback it calls may call back in.
 */

/** Tells the sink when what a plugin was handed could not be recorded as held, or has no object in the layer. */
static void report_unheld(weivel_c_contexts *contexts, weivel_c_status held, const weivel_c_event *event) {
    if (held == WEIVEL_C_STATUS_FAILED) {
        weivel_c_contexts_report(contexts, WEIVEL_C_FAILURE_OBJECT_UNKNOWN, held, event,
                                 "the core named a registration or implementation the layer holds no object for");
    } else if (held != WEIVEL_C_STATUS_OK) {
        weivel_c_contexts_report(contexts, WEIVEL_C_FAILURE_OBJECT_UNKNOWN, held, event,
                                 "what a plugin was handed could not be recorded as held, so its revocation will "
                                 "not be heard");
    }
}

static void tell_degraded(weivel_c_contexts *contexts, const weivel_c_event *event,
                          const weivel_c_settlement *settlement) {
    weivel_c_plugin_event told = WEIVEL_INIT(weivel_c_plugin_event);
    told.kind = WEIVEL_C_PLUGIN_EVENT_DEGRADED;
    told.reach = WEIVEL_REACH_SERVICE;
    told.id = settlement->id;
    told.refusal = settlement->refusal;
    weivel_c_context_emit(weivel_c_contexts_context_of(contexts, event->data.want.plugin, event), &told, event);
}

/**
 * @implNote Claiming the record is what makes a want settle exactly once: a withdrawal the core took left it ended,
 * and one the core refused, because the want had settled, never marked it. The claim also leaves the freeing to this
 * dispatch, so a callback that ends its own entries or frees its context frees nothing under it.
 */
static void settle_want(weivel_c_contexts *contexts, const weivel_c_event *event) {
    const weivel_want_event *settled = &event->data.want;
    weivel_c_record *want = weivel_c_record_claim(contexts, settled->want, WEIVEL_C_RECORD_WANT);
    weivel_c_settlement settlement = WEIVEL_INIT(weivel_c_settlement);
    if (want == NULL) {
        return;
    }
    settlement.want = settled->want;
    settlement.id = want->id;
    settlement.refusal = weivel_c_contexts_refusal_of(&settled->refusal, settled->subject, settled->has_fix);
    settlement.degraded = settled->degraded;
    if (settled->implementation != 0) {
        report_unheld(contexts,
                      weivel_c_record_hold_settled(want, settled->plugin, settled->implementation, &settlement.service),
                      event);
    }
    if (settled->degraded) {
        tell_degraded(contexts, event, &settlement);
    }
    weivel_c_contexts_called_back(contexts, want->want_callback.settled(want->want_callback.data, &settlement),
                                  event);
    weivel_c_record_end_in_dispatch(want);
}

/**
 * Answers 0, the sink told, when what a registration hands over has no object in the layer to hear it with.
 *
 * @implNote The holding is made here with no reservation, since a watch hears registrations for as long as it stands
 * and no room for them can be taken before the call that started it. So when memory runs out the watcher is still
 * handed the object, the sink is told the holding was not recorded, and the later revocation of that registration is
 * not heard by the watcher.
 */
static int hand_over_the_registered(weivel_c_contexts *contexts, const weivel_c_event *event,
                                    weivel_c_service_change *change) {
    const weivel_watch_event *heard = &event->data.watch;
    weivel_c_status held;
    if (heard->change != WEIVEL_CHANGE_REGISTERED) {
        return 1;
    }
    held = weivel_c_contexts_hold(contexts, heard->plugin, heard->implementation, WEIVEL_C_RECORD_SERVICE, 0,
                                  &change->service);
    report_unheld(contexts, held, event);
    return held != WEIVEL_C_STATUS_FAILED;
}

static void hear_service(weivel_c_contexts *contexts, const weivel_c_event *event) {
    const weivel_watch_event *heard = &event->data.watch;
    weivel_c_service_change change = WEIVEL_INIT(weivel_c_service_change);
    weivel_c_record *watch = weivel_c_record_borrow(contexts, heard->watch, WEIVEL_C_RECORD_SERVICE_WATCH);
    if (watch == NULL) {
        return;
    }
    change.watch = heard->watch;
    change.id = watch->id;
    change.change = heard->change;
    if (hand_over_the_registered(contexts, event, &change)) {
        weivel_c_contexts_called_back(contexts, watch->service_watcher.hear(watch->service_watcher.data, &change),
                                      event);
    }
    weivel_c_record_put_back(watch);
}

/** Answers 0, the sink told, when the implementation a change names has no object in the layer to hear it with. */
static int find_the_changed_implementation(weivel_c_contexts *contexts, const weivel_c_event *event,
                                           weivel_c_capability_change *change) {
    const weivel_capability_event *heard = &event->data.capability;
    weivel_c_status held = WEIVEL_C_STATUS_OK;
    weivel_c_record provided;
    if (heard->change == WEIVEL_CAPABILITY_PROVIDED) {
        held = weivel_c_contexts_hold(contexts, heard->plugin, heard->implementation, WEIVEL_C_RECORD_IMPLEMENTATION,
                                      0, &change->implementation);
    } else if (weivel_c_record_read(contexts, heard->implementation, WEIVEL_C_RECORD_IMPLEMENTATION, &provided)) {
        change->implementation = provided.object;
    } else {
        held = WEIVEL_C_STATUS_FAILED;
    }
    report_unheld(contexts, held, event);
    return held != WEIVEL_C_STATUS_FAILED;
}

static void hear_capability(weivel_c_contexts *contexts, const weivel_c_event *event) {
    const weivel_capability_event *heard = &event->data.capability;
    weivel_c_capability_change change = WEIVEL_INIT(weivel_c_capability_change);
    weivel_c_record *watch = weivel_c_record_borrow(contexts, heard->watch, WEIVEL_C_RECORD_CAPABILITY_WATCH);
    if (watch == NULL) {
        return;
    }
    change.watch = heard->watch;
    change.id = watch->id;
    change.change = heard->change;
    if (find_the_changed_implementation(contexts, event, &change)) {
        weivel_c_contexts_called_back(
            contexts, watch->capability_watcher.hear(watch->capability_watcher.data, &change), event);
    }
    weivel_c_record_put_back(watch);
}

/**
 * Under the lock of the contexts: fills `told` from the provider's record a holding points at, when that record is
 * still the one kept under the handle.
 *
 * @implNote A holding keeps the provider's record, which says which id and which object were handed over. The
 * record is read only while the table still keeps that very record under the revoked handle, so a holding that
 * outlived its record, as one can when a revocation was lost for want of memory, is dropped without being read.
 */
static int describe_holding(weivel_c_contexts *contexts, const weivel_revoked_event *revoked,
                            weivel_c_plugin_event *told) {
    weivel_c_handle_entry entry = WEIVEL_INIT(weivel_c_handle_entry);
    void *held = NULL;
    const weivel_c_record *record;
    if (weivel_c_handles_held(contexts->handles, revoked->plugin, revoked->implementation, &held) !=
            WEIVEL_C_STATUS_OK ||
        weivel_c_handles_find(contexts->handles, revoked->implementation, &entry) != WEIVEL_C_STATUS_OK ||
        entry.object != held) {
        return 0;
    }
    record = held;
    told->reach = record->kind == WEIVEL_C_RECORD_SERVICE ? WEIVEL_REACH_SERVICE : WEIVEL_REACH_CAPABILITY;
    told->id = record->id;
    told->object = record->object;
    return 1;
}

static void revoke(weivel_c_contexts *contexts, const weivel_c_event *event) {
    weivel_c_plugin_event told = WEIVEL_INIT(weivel_c_plugin_event);
    int described;
    told.kind = WEIVEL_C_PLUGIN_EVENT_REVOKED;
    weivel_c_mutex_lock(&contexts->lock);
    described = describe_holding(contexts, &event->data.revoked, &told);
    weivel_c_handles_revoke(contexts->handles, event, NULL);
    weivel_c_mutex_unlock(&contexts->lock);
    if (described) {
        weivel_c_context_emit(weivel_c_contexts_context_of(contexts, event->data.revoked.plugin, event), &told,
                              event);
    }
}

static void refuse_reach(weivel_c_contexts *contexts, const weivel_c_event *event) {
    const weivel_reach_refusal *refused = &event->data.reach;
    weivel_c_plugin_event told = WEIVEL_INIT(weivel_c_plugin_event);
    told.kind = WEIVEL_C_PLUGIN_EVENT_REACH_REFUSED;
    told.reach = refused->kind;
    told.id = refused->id;
    told.refusal = weivel_c_contexts_refusal_of(&refused->refusal, refused->subject, refused->has_fix);
    weivel_c_context_emit(weivel_c_contexts_context_of(contexts, refused->plugin, event), &told, event);
}

static void write_the_cores_log(weivel_c_contexts *contexts, const weivel_c_event *event) {
    weivel_c_log_record record = WEIVEL_INIT(weivel_c_log_record);
    record.level = event->data.log.level;
    record.text = event->data.log.text == NULL ? "" : event->data.log.text;
    weivel_c_contexts_write_log(contexts, &record, event);
}

weivel_c_status weivel_c_contexts_hear(void *data, weivel_c_core *core, const weivel_c_event *event) {
    weivel_c_contexts *contexts = data;
    (void)core;
    switch (event->kind) {
    case WEIVEL_EVENT_LOG:
        write_the_cores_log(contexts, event);
        break;
    case WEIVEL_EVENT_WANT_SETTLED:
        settle_want(contexts, event);
        break;
    case WEIVEL_EVENT_WATCH:
        hear_service(contexts, event);
        break;
    case WEIVEL_EVENT_CAPABILITY:
        hear_capability(contexts, event);
        break;
    case WEIVEL_EVENT_REVOKED:
        revoke(contexts, event);
        break;
    case WEIVEL_EVENT_REACH_REFUSED:
        refuse_reach(contexts, event);
        break;
    case WEIVEL_EVENT_CAPACITY_WANTED:
        weivel_c_contexts_answer_capacity(contexts, event);
        break;
    default:
        break;
    }
    return WEIVEL_C_STATUS_OK;
}
