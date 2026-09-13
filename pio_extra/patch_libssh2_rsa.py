"""Patch pinned libssh2_esp key-handling bugs.

libssh2_esp tag 1.1 increments the RSA modulus length after allocating the
SSH public-key blob. A full-size RSA modulus therefore writes one byte beyond
the allocation. It also copies a parsed private key into an uninitialized RSA
context, which makes signing fail.

Its ECDSA in-memory loader allocates one extra byte for PEM termination but
never initializes that byte before passing data_len + 1 to mbedTLS. That can
make the exact same EC private key parse intermittently depending on heap
contents. Keep these patches strict and fail if the dependency no longer
matches either the vulnerable or corrected form.
"""

from pathlib import Path

Import("env")

project_env = DefaultEnvironment()

if project_env.IsIntegrationDump():
    Return()

dependency_source = (
    Path(project_env.subst("$PROJECT_LIBDEPS_DIR"))
    / project_env.subst("$PIOENV")
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
vulnerable_rsa_init = """    *rsa = (libssh2_rsa_ctx *) mbedtls_calloc(1, sizeof(libssh2_rsa_ctx));
    if(!*rsa)
        return -1;

    /*
"""
corrected_rsa_init = """    *rsa = (libssh2_rsa_ctx *) mbedtls_calloc(1, sizeof(libssh2_rsa_ctx));
    if(!*rsa)
        return -1;

    mbedtls_rsa_init(*rsa);

    /*
"""

plain_parse_result = """    _libssh2_mbedtls_safe_free(filedata_nullterm, filedata_len);

    if(ret || mbedtls_pk_get_type(&pkey) != MBEDTLS_PK_RSA) {
"""
diagnostic_parse_result = """    _libssh2_mbedtls_safe_free(filedata_nullterm, filedata_len);

    printf("[SSH-DIAG] RSA parse rc=%d type=%d\\n", ret,
           ret ? -1 : (int)mbedtls_pk_get_type(&pkey));
    if(ret || mbedtls_pk_get_type(&pkey) != MBEDTLS_PK_RSA) {
"""

plain_copy = """    pk_rsa = mbedtls_pk_rsa(pkey);
    mbedtls_rsa_copy(*rsa, pk_rsa);
    mbedtls_pk_free(&pkey);

    return 0;
}

int
_libssh2_mbedtls_rsa_sha2_verify"""
diagnostic_copy = """    pk_rsa = mbedtls_pk_rsa(pkey);
    ret = mbedtls_rsa_copy(*rsa, pk_rsa);
    if(!ret)
        ret = mbedtls_rsa_check_privkey(*rsa);
    printf("[SSH-DIAG] RSA copy/check rc=%d bits=%u\\n", ret,
           (unsigned)(mbedtls_rsa_get_len(*rsa) * 8));
    mbedtls_pk_free(&pkey);
    if(ret) {
        mbedtls_rsa_free(*rsa);
        LIBSSH2_FREE(session, *rsa);
        *rsa = NULL;
        return -1;
    }

    return 0;
}

int
_libssh2_mbedtls_rsa_sha2_verify"""

plain_sign_error = """    if(ret) {
        LIBSSH2_FREE(session, sig);
        return -1;
    }

    *signature = sig;
"""
diagnostic_sign_error = """    if(ret) {
        char errbuf[96];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        printf("[SSH-DIAG] RSA sign rc=%d (%s), hash_len=%u, bits=%u\\n",
               ret, errbuf, (unsigned)hash_len, (unsigned)(sig_len * 8));
        LIBSSH2_FREE(session, sig);
        return -1;
    }

    printf("[SSH-DIAG] RSA sign succeeded, hash_len=%u, bits=%u\\n",
           (unsigned)hash_len, (unsigned)(sig_len * 8));
    *signature = sig;
"""

uninitialized_public_key_result = """    int ret;
    mbedtls_rsa_context *rsa;

    if(mbedtls_pk_get_type(pkey) != MBEDTLS_PK_RSA) {
"""
initialized_public_key_result = """    int ret = 0;
    mbedtls_rsa_context *rsa;

    if(mbedtls_pk_get_type(pkey) != MBEDTLS_PK_RSA) {
"""

unterminated_ecdsa_pem = """    memcpy(ntdata, data, data_len);

    if(_libssh2_mbedtls_parse_eckey(ctx, &pkey, session,
                                    ntdata, data_len + 1, pwd) == 0)
"""
terminated_ecdsa_pem = """    memcpy(ntdata, data, data_len);
    ntdata[data_len] = '\\0';

    if(_libssh2_mbedtls_parse_eckey(ctx, &pkey, session,
                                    ntdata, data_len + 1, pwd) == 0)
"""
short_ecdsa_wipe = "    _libssh2_mbedtls_safe_free(ntdata, data_len);\n"
full_ecdsa_wipe = "    _libssh2_mbedtls_safe_free(ntdata, data_len + 1);\n"

if not dependency_source.is_file():
    raise RuntimeError(
        f"Cannot patch libssh2_esp: source file not found at {dependency_source}"
    )

source = dependency_source.read_text(encoding="utf-8")
changed = False
buffer_corrected = corrected_size in source and late_increment not in source

if not buffer_corrected:
    if source.count(vulnerable_size) != 1 or source.count(late_increment) != 1:
        raise RuntimeError(
            "Cannot patch libssh2_esp safely: the RSA implementation differs "
            "from the pinned 1.1 source"
        )

    source = source.replace(vulnerable_size, corrected_size, 1)
    source = source.replace(late_increment, "", 1)
    changed = True

rsa_initialized = corrected_rsa_init in source
if not rsa_initialized:
    if source.count(vulnerable_rsa_init) != 1:
        raise RuntimeError(
            "Cannot patch libssh2_esp safely: the RSA private-key loader "
            "differs from the pinned 1.1 source"
        )
    source = source.replace(vulnerable_rsa_init, corrected_rsa_init, 1)
    changed = True

public_key_result_initialized = initialized_public_key_result in source
if not public_key_result_initialized:
    if source.count(uninitialized_public_key_result) != 1:
        raise RuntimeError(
            "Cannot patch libssh2_esp safely: the RSA public-key derivation "
            "differs from the pinned 1.1 source"
        )
    source = source.replace(
        uninitialized_public_key_result, initialized_public_key_result, 1
    )
    changed = True

if diagnostic_parse_result not in source:
    if source.count(plain_parse_result) != 1:
        raise RuntimeError("Cannot add RSA parse diagnostics safely")
    source = source.replace(plain_parse_result, diagnostic_parse_result, 1)
    changed = True

if diagnostic_copy not in source:
    if source.count(plain_copy) != 1:
        raise RuntimeError("Cannot add RSA copy diagnostics safely")
    source = source.replace(plain_copy, diagnostic_copy, 1)
    changed = True

if diagnostic_sign_error not in source:
    if source.count(plain_sign_error) != 1:
        raise RuntimeError("Cannot add RSA signing diagnostics safely")
    source = source.replace(plain_sign_error, diagnostic_sign_error, 1)
    changed = True

if terminated_ecdsa_pem not in source:
    if source.count(unterminated_ecdsa_pem) != 1:
        raise RuntimeError(
            "Cannot patch libssh2_esp safely: the ECDSA in-memory loader "
            "differs from the pinned 1.1 source"
        )
    source = source.replace(unterminated_ecdsa_pem, terminated_ecdsa_pem, 1)
    changed = True

if full_ecdsa_wipe not in source:
    if source.count(short_ecdsa_wipe) != 1:
        raise RuntimeError(
            "Cannot patch libssh2_esp safely: the ECDSA temporary-key cleanup "
            "differs from the pinned 1.1 source"
        )
    source = source.replace(short_ecdsa_wipe, full_ecdsa_wipe, 1)
    changed = True

if changed:
    dependency_source.write_text(source, encoding="utf-8", newline="\n")
    print("Patched libssh2_esp RSA/ECDSA in-memory key handling")
