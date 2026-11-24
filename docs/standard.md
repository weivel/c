# `standard`, optional and its own library

C's access to the standard's defaults, each feeding the host. It is a library apart from the host so that a host that
wants none of the standard's defaults links none of its code (see [the host's side of that](host/standard-boundary.md)).
The rules the module keeps in any language are the core's `docs/contract/standard.md`.

`include/weivel/c/standard.h` is C's access to the standard's defaults, each feeding the host. It is optional and a
library of its own, `weivel_c_standard` (`weivel_c_standard_shared` for the shared build), the one target that links
the standard; the host never links, loads or requires it, and every host test links without it. The standard is a C
library, so C reaches it by calling its functions: where nothing needs translating the caller calls the standard
directly, `weivel_standard_discover` to lay out and discover plugins on disk, `weivel_standard_read_library`, whose
declaration is exactly what `weivel_c_host_load_library` takes with the path the caller gives, and the identity
parsing and ordering. The module adds what joins the standard's answers to the host:

- `weivel_c_standard_register_manifest` validates and reads a JSON manifest with the standard and registers it with
  an entry and the identity the caller supplies. The standard's manifest is the standard's, freed by its own release;
  the module releases it before returning, since the host copies what it registers. A document the standard declines
  answers `WEIVEL_C_STATUS_REFUSED_BY_THE_STANDARD`, the validation saying why, with nothing registered.
- `weivel_c_standard_discovered_manifest` fills a manifest value with a discovered plugin's manifest and the identity
  its identity file gives; `weivel_c_standard_register_discovered` registers it with the entry read from the file its
  manifest names. The layout names a file and no symbol, so the entry's symbol is always the caller's: the module has
  none of its own. An entry the standard did not find present, or a manifest naming none, opens nothing.
- `weivel_c_standard_offer_config`, `_offer_secrets` and `_offer_paths` register the caller's own definition of each
  service as a host service under the id the standard's binary answers, so a plugin resolving it through its context
  holds that very object.
- `weivel_c_standard_status` translates a status of the standard's into the layer's.
- `weivel_c_standard_check_description` is the standard's `weivel_standard_description_check` as a host's description
  check, its `data` the caller's empty refusal or NULL: a description the check refuses answers
  `WEIVEL_C_STATUS_REFUSED_BY_THE_STANDARD`, which the host's load turns into its refusal of the library.

The module holds no default of its own, since a default is the standard's to own and a replacement must be able to
take every one of them away: a test reads its sources and header and fails on any string, a literal, an identifier
stringized or a character literal; on a service id or placeholder macro of the standard's that would compile one of
its values into this binary; on a name that reads the environment or opens a file, the platform's macro forms and the
environment block among them; on a header named in quotes that leaves `src/standard`; and on a definition the build
gives the module's targets that carries a value.
