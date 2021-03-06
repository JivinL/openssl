/*
 * Copyright 1995-2020 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/opensslconf.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "apps.h"
#include "progs.h"
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/bn.h>
#include <openssl/dsa.h>
#include <openssl/x509.h>
#include <openssl/pem.h>

static int verbose = 0;

static int gendsa_cb(EVP_PKEY_CTX *ctx);

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP,
    OPT_INFORM, OPT_OUTFORM, OPT_IN, OPT_OUT, OPT_TEXT, OPT_C,
    OPT_NOOUT, OPT_GENKEY, OPT_ENGINE, OPT_VERBOSE,
    OPT_R_ENUM, OPT_PROV_ENUM
} OPTION_CHOICE;

const OPTIONS dsaparam_options[] = {
    {OPT_HELP_STR, 1, '-', "Usage: %s [options] [numbits]\n"},

    OPT_SECTION("General"),
    {"help", OPT_HELP, '-', "Display this summary"},
#ifndef OPENSSL_NO_ENGINE
    {"engine", OPT_ENGINE, 's', "Use engine e, possibly a hardware device"},
#endif

    OPT_SECTION("Input"),
    {"in", OPT_IN, '<', "Input file"},
    {"inform", OPT_INFORM, 'F', "Input format - DER or PEM"},

    OPT_SECTION("Output"),
    {"out", OPT_OUT, '>', "Output file"},
    {"outform", OPT_OUTFORM, 'F', "Output format - DER or PEM"},
    {"text", OPT_TEXT, '-', "Print as text"},
    {"C", OPT_C, '-', "Output C code"},
    {"noout", OPT_NOOUT, '-', "No output"},
    {"verbose", OPT_VERBOSE, '-', "Verbose output"},
    {"genkey", OPT_GENKEY, '-', "Generate a DSA key"},

    OPT_R_OPTIONS,
    OPT_PROV_OPTIONS,

    OPT_PARAMETERS(),
    {"numbits", 0, 0, "Number of bits if generating parameters (optional)"},
    {NULL}
};

int dsaparam_main(int argc, char **argv)
{
    ENGINE *e = NULL;
    BIO *in = NULL, *out = NULL;
    EVP_PKEY *params = NULL, *pkey = NULL;
    EVP_PKEY_CTX *ctx = NULL;
    int numbits = -1, num = 0, genkey = 0;
    int informat = FORMAT_PEM, outformat = FORMAT_PEM, noout = 0, C = 0;
    int ret = 1, i, text = 0, private = 0;
    char *infile = NULL, *outfile = NULL, *prog;
    OPTION_CHOICE o;

    prog = opt_init(argc, argv, dsaparam_options);
    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
 opthelp:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            opt_help(dsaparam_options);
            ret = 0;
            goto end;
        case OPT_INFORM:
            if (!opt_format(opt_arg(), OPT_FMT_PEMDER, &informat))
                goto opthelp;
            break;
        case OPT_IN:
            infile = opt_arg();
            break;
        case OPT_OUTFORM:
            if (!opt_format(opt_arg(), OPT_FMT_PEMDER, &outformat))
                goto opthelp;
            break;
        case OPT_OUT:
            outfile = opt_arg();
            break;
        case OPT_ENGINE:
            e = setup_engine(opt_arg(), 0);
            break;
        case OPT_TEXT:
            text = 1;
            break;
        case OPT_C:
            C = 1;
            break;
        case OPT_GENKEY:
            genkey = 1;
            break;
        case OPT_R_CASES:
            if (!opt_rand(o))
                goto end;
            break;
        case OPT_PROV_CASES:
            if (!opt_provider(o))
                goto end;
            break;
        case OPT_NOOUT:
            noout = 1;
            break;
        case OPT_VERBOSE:
            verbose = 1;
            break;
        }
    }
    argc = opt_num_rest();
    argv = opt_rest();

    if (argc == 1) {
        if (!opt_int(argv[0], &num) || num < 0)
            goto end;
        /* generate a key */
        numbits = num;
    }
    private = genkey ? 1 : 0;

    in = bio_open_default(infile, 'r', informat);
    if (in == NULL)
        goto end;
    out = bio_open_owner(outfile, outformat, private);
    if (out == NULL)
        goto end;

    ctx = EVP_PKEY_CTX_new_from_name(NULL, "DSA", NULL);
    if (ctx == NULL) {
        ERR_print_errors(bio_err);
        BIO_printf(bio_err,
                   "Error, DSA parameter generation context allocation failed\n");
        goto end;
    }
    if (numbits > 0) {
        if (numbits > OPENSSL_DSA_MAX_MODULUS_BITS)
            BIO_printf(bio_err,
                       "Warning: It is not recommended to use more than %d bit for DSA keys.\n"
                       "         Your key size is %d! Larger key size may behave not as expected.\n",
                       OPENSSL_DSA_MAX_MODULUS_BITS, numbits);

        EVP_PKEY_CTX_set_cb(ctx, gendsa_cb);
        EVP_PKEY_CTX_set_app_data(ctx, bio_err);
        if (verbose) {
            BIO_printf(bio_err, "Generating DSA parameters, %d bit long prime\n",
                       num);
            BIO_printf(bio_err, "This could take some time\n");
        }
        if (EVP_PKEY_paramgen_init(ctx) <= 0) {
            ERR_print_errors(bio_err);
            BIO_printf(bio_err,
                       "Error, DSA key generation paramgen init failed\n");
            goto end;
        }
        if (!EVP_PKEY_CTX_set_dsa_paramgen_bits(ctx, num)) {
            ERR_print_errors(bio_err);
            BIO_printf(bio_err,
                       "Error, DSA key generation setting bit length failed\n");
            goto end;
        }
        if (EVP_PKEY_paramgen(ctx, &params) <= 0) {
            ERR_print_errors(bio_err);
            BIO_printf(bio_err, "Error, DSA key generation failed\n");
            goto end;
        }
    } else if (informat == FORMAT_ASN1) {
        params = d2i_KeyParams_bio(EVP_PKEY_DSA, NULL, in);
    } else {
        params = PEM_read_bio_Parameters(in, NULL);
    }
    if (params == NULL) {
        BIO_printf(bio_err, "unable to load DSA parameters\n");
        ERR_print_errors(bio_err);
        goto end;
    }

    if (text) {
        EVP_PKEY_print_params(out, params, 0, NULL);
    }

    if (C) {
        BIGNUM *p = NULL, *q = NULL, *g = NULL;
        unsigned char *data;
        int len, bits_p;

        EVP_PKEY_get_bn_param(params, "p", &p);
        EVP_PKEY_get_bn_param(params, "q", &q);
        EVP_PKEY_get_bn_param(params, "g", &g);
        len = BN_num_bytes(p);
        bits_p = BN_num_bits(p);

        data = app_malloc(len + 20, "BN space");

        BIO_printf(bio_out, "static DSA *get_dsa%d(void)\n{\n", bits_p);
        print_bignum_var(bio_out, p, "dsap", bits_p, data);
        print_bignum_var(bio_out, q, "dsaq", bits_p, data);
        print_bignum_var(bio_out, g, "dsag", bits_p, data);
        BIO_printf(bio_out, "    DSA *dsa = DSA_new();\n"
                            "    BIGNUM *p, *q, *g;\n"
                            "\n");
        BIO_printf(bio_out, "    if (dsa == NULL)\n"
                            "        return NULL;\n");
        BIO_printf(bio_out, "    if (!DSA_set0_pqg(dsa, p = BN_bin2bn(dsap_%d, sizeof(dsap_%d), NULL),\n",
                   bits_p, bits_p);
        BIO_printf(bio_out, "                           q = BN_bin2bn(dsaq_%d, sizeof(dsaq_%d), NULL),\n",
                   bits_p, bits_p);
        BIO_printf(bio_out, "                           g = BN_bin2bn(dsag_%d, sizeof(dsag_%d), NULL))) {\n",
                   bits_p, bits_p);
        BIO_printf(bio_out, "        DSA_free(dsa);\n"
                            "        BN_free(p);\n"
                            "        BN_free(q);\n"
                            "        BN_free(g);\n"
                            "        return NULL;\n"
                            "    }\n"
                            "    return dsa;\n}\n");
        OPENSSL_free(data);
    }

    if (outformat == FORMAT_ASN1 && genkey)
        noout = 1;

    if (!noout) {
        if (outformat == FORMAT_ASN1)
            i = i2d_KeyParams_bio(out, params);
        else
            i = PEM_write_bio_Parameters(out, params);
        if (!i) {
            BIO_printf(bio_err, "unable to write DSA parameters\n");
            ERR_print_errors(bio_err);
            goto end;
        }
    }
    if (genkey) {
        EVP_PKEY_CTX_free(ctx);
        ctx = EVP_PKEY_CTX_new(params, NULL);
        if (ctx == NULL) {
            ERR_print_errors(bio_err);
            BIO_printf(bio_err,
                       "Error, DSA key generation context allocation failed\n");
            goto end;
        }
        if (!EVP_PKEY_keygen_init(ctx)) {
            BIO_printf(bio_err, "unable to initialise for key generation\n");
            ERR_print_errors(bio_err);
            goto end;
        }
        if (!EVP_PKEY_keygen(ctx, &pkey)) {
            BIO_printf(bio_err, "unable to generate key\n");
            ERR_print_errors(bio_err);
            goto end;
        }
        assert(private);
        if (outformat == FORMAT_ASN1)
            i = i2d_PrivateKey_bio(out, pkey);
        else
            i = PEM_write_bio_PrivateKey(out, pkey, NULL, NULL, 0, NULL, NULL);
    }
    ret = 0;
 end:
    BIO_free(in);
    BIO_free_all(out);
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    EVP_PKEY_free(params);
    release_engine(e);
    return ret;
}

static int gendsa_cb(EVP_PKEY_CTX *ctx)
{
    static const char symbols[] = ".+*\n";
    int p;
    char c;
    BIO *b;

    if (!verbose)
        return 1;

    b = EVP_PKEY_CTX_get_app_data(ctx);
    p = EVP_PKEY_CTX_get_keygen_info(ctx, 0);
    c = (p >= 0 && (size_t)p < sizeof(symbols) - 1) ? symbols[p] : '?';

    BIO_write(b, &c, 1);
    (void)BIO_flush(b);
    return 1;
}
