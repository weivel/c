#ifndef WEIVEL_C_H
#define WEIVEL_C_H

/**
 * @file
 * The whole public surface of the weivel C language implementation, one header per module that publishes one, but the
 * optional `standard` module's and the `conformance` module's: each is a library of its own that links the standard,
 * so its header, weivel/c/standard.h or weivel/c/conformance.h, is included on its own by what links
 * weivel_c_standard or weivel_c_conformance.
 */

#include <weivel/c/context.h>
#include <weivel/c/core.h>
#include <weivel/c/handles.h>
#include <weivel/c/host.h>
#include <weivel/c/host/library.h>
#include <weivel/c/timer.h>

#endif
