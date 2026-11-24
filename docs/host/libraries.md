# C libraries as host services

How a C host offers a C library's functions to plugins as services. What a library declaration is, its two kinds of
function and its descriptions, is the standard's `docs/library.md`; the rules a host keeps for one in any language are
the core's `docs/contract/host.md`.

`weivel_c_host_load_library` (`include/weivel/c/host/library.h`) loads a C library from a declaration value, the
standard's published `weivel_standard_library`, whether the standard read it or the caller built it, and a path the
caller gives, since a declaration names none. A declared function is of one of the standard's two kinds, bytes or
definition. The host binds the declared release symbol, which a library whose services are all definitions need not
declare, and each declared function, and registers each declared service under `WEIVEL_HOST`: a service of bytes
functions as one `weivel_c_library_service`, the service id, the declared function names in order, and a `call` that
takes the object, a function's index, the input and an answer; and a service that declares a definition as that
definition. A consumer that resolves the service is answered that very object.

- A definition is a symbol `const void *f(void)` answering a static struct of function pointers that begins with its
  `uint32_t size`, whose layout is the library's own. The host calls it once at the load and registers the pointer it
  answered, with no wrapper, count or copy, through `weivel_c_context_register_service_awaiting_releases`, so a
  consumer's call through it is a plain call into the library. It reads the `size` and nothing past it: NULL, or a
  `size` below `2 * sizeof(void *)`, refuses the load as the caller's mistake, the symbol named in the failure's
  `refused_definition`, with nothing registered. So is a definition beside another function of its service, or one
  given context facts (named in `wrong_kind`).
- A definition's service may declare a description, the symbol of a `const weivel_standard_description *f(void)`
  (`weivel/description.h` of the standard), which the declaration's `descriptions` names. The host binds nothing from
  it, since a C plugin casts a definition to its header's struct, but it does not load one it cannot vouch for: it
  calls the symbol once at the load and hands what it answered to the description check of the host's options, a
  `weivel_c_description_check`, beside the definition the service's definition symbol answered, so the check can
  refuse a description that describes another definition than the one served beside it. It registers the definition
  only when the check accepts it, as the very pointer the definition's symbol answered. The host never links the
  standard, so the check is the caller's to give; the `standard` module's `weivel_c_standard_check_description` is the
  standard's own, which also requires the description's `answered_by` to answer that very definition. A description
  that is missing answers `WEIVEL_C_STATUS_MISSING_SYMBOL`; one that answers NULL, that the check refuses, that is
  empty or beside a service of bytes functions, or that a host given no check is asked to load, refuses the load as
  the caller's mistake, named in the failure's `refused_description`, with nothing registered and the binary closed
  again; a check that runs out of memory answers `WEIVEL_C_STATUS_OUT_OF_MEMORY`. A declaration that ends before
  `descriptions` is loaded as one that declares none. A grown description, one marking `blocking` operations or
  nesting the description of a definition it carries, is checked at the load as any is, by the same check, and bound
  to nothing.
- A call crosses no core call and takes no lock: the input is the caller's own pointer and length, handed to the
  library for the call, and the output is answered in place, as a `weivel_c_bytes` holding the library's own
  pointer. `weivel_c_bytes_release`, also reachable as the object's `release_bytes` for a plugin that links nothing of
  the layer's, hands it to the library's release symbol once and empties it; releasing the emptied struct again does
  nothing.
- A status other than 0 answers `WEIVEL_C_STATUS_FAILED` with the status and the body, and `body_is_utf8` says
  whether the body is well-formed UTF-8, so it reads as the text saying why; an empty body counts as UTF-8.
- A declaration the caller built is refused as the caller's mistake, `WEIVEL_C_STATUS_MISUSE` told to nobody else,
  when it has a function of another kind (named), no service or a service of no function, an id it lists twice, or an
  id a registration under the host already holds, a plugin's, another live library's or the host's own (the id
  named): registering under `WEIVEL_HOST` replaces a registration, so loading it would silently take that service
  over. The id is looked for in the layer's own records of every registration, never by a get, so a refused load
  leaves the host holding nothing it would later hear revoked. The refusal holds against every registration made
  before the load call; a plugin registering the same id on another thread while the load runs can still be
  replaced, and closing that window needs a register-if-absent call for `WEIVEL_HOST` in the core, which it does not
  have. The host's own registrations are held in the layer both ways, on whichever thread each runs:
  `weivel_c_host_register_service` claims its id on the host's list under the host's lock before it asks the core,
  refusing, `WEIVEL_C_STATUS_MISUSE`, an id a live library declares, and a load kept while that claim stands is
  refused naming the id. A library that is none, or lacks its release symbol or a function's, is answered as a value
  naming what is missing.
- The library's record is kept on the host's list before any service is registered, so a load that does not finish
  once one is (the destroy begun meanwhile, or a later service's registration refused) is undone as an unload is: an
  object a consumer was told of meanwhile answers `WEIVEL_C_STATUS_UNLOADED`, nothing stays registered, and the binary
  closes once nothing holds it.
- `weivel_c_host_unload_library` unregisters every service of the library at once, and every later call through a
  bytes object a consumer still holds answers `WEIVEL_C_STATUS_UNLOADED`. A definition is called directly, so no call
  to it can be refused: its unregistration revokes every holding of it, and each holder lets go with
  `weivel_c_context_release_holding`. Its binary closes only once no call runs in it, no bytes it answered are held and
  the host's own context has heard `WEIVEL_C_PLUGIN_EVENT_HOLDINGS_RELEASED` for every one of its definitions, on
  whichever of them ends last, so nothing ever calls into a closed library and bytes still held stay readable. A call,
  each held body and each definition not yet released count themselves on one atomic word per library, which is also
  how an unload is seen; the host's own lock guards only its list of libraries. A holding the core ends with its
  holder, released, quarantined or forgotten, counts as released.
- The destroy unloads every library, the last loaded first, after every plugin release and its events and steps,
  before the core is destroyed, and `weivel_c_host_free` refuses while bytes a library answered are still held. A
  definition whose release the core never told, its holder never letting go or its service still registered as the
  destroy began, keeps its binary open until `weivel_c_host_free`, which closes it. A close refused once the host is
  destroyed, when the last body held across the destroy is released, is told to nobody.
