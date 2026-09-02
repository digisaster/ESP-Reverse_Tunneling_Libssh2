"""Patch the pinned libssh2_esp RSA public-key buffer overflow.

libssh2_esp tag 1.1 increments the RSA modulus length after allocating the
SSH public-key blob. A full-size RSA modulus therefore writes one byte beyond
the allocation. Keep this strict and fail if the dependency no longer matches
either the vulnerable or corrected form.
"""

from pathlib import Path

Import("env")

if env.IsIntegrationDump():
    Return()

dependency_source = (
    Path(env.subst("$PROJECT_LIBDEPS_DIR"))
    / env.subst("$PIOENV")
    / "libssh2_esp"
    / "src"
    / "mbedtls.c"
)

vulnerable_size = (
    "    n_bytes = (uint32_t)mbedtls_mpi_size(&rsa->MBEDTLS_PRIVATE(N));"
)
corrected_size = (
    "    n_bytes = (uint32_t)mbedtls_mpi_size(&rsa->MBEDTLS_PRIVATE(N)) + 1;"
)
late_increment = "    n_bytes++;      /* Add 1 to bignum size */\n"

if not dependency_source.is_file():
    raise RuntimeError(
        f"Cannot patch libssh2_esp: source file not found at {dependency_source}"
    )

source = dependency_source.read_text(encoding="utf-8")
already_corrected = corrected_size in source and late_increment not in source

if not already_corrected:
    if source.count(vulnerable_size) != 1 or source.count(late_increment) != 1:
        raise RuntimeError(
            "Cannot patch libssh2_esp safely: the RSA implementation differs "
            "from the pinned 1.1 source"
        )

    source = source.replace(vulnerable_size, corrected_size, 1)
    source = source.replace(late_increment, "", 1)
    dependency_source.write_text(source, encoding="utf-8", newline="\n")
    print("Patched libssh2_esp RSA public-key buffer allocation")
