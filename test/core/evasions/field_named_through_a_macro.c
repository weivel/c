#define THROUGH(table, function) ((table)->function)

static weivel_status evade(const weivel_c_core_table *table, weivel_core *core, uint32_t *pending_events) {
    return THROUGH(table, advance)(core, 0, pending_events);
}
