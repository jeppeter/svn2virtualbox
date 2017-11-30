/*
 * Generated by util/mkerr.pl DO NOT EDIT
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

/* BEGIN ERROR CODES */
#ifndef OPENSSL_NO_ERR

# define ERR_FUNC(func) ERR_PACK(ERR_LIB_X509V3,func,0)
# define ERR_REASON(reason) ERR_PACK(ERR_LIB_X509V3,0,reason)

static ERR_STRING_DATA X509V3_str_functs[] = {
    {ERR_FUNC(X509V3_F_A2I_GENERAL_NAME), "a2i_GENERAL_NAME"},
    {ERR_FUNC(X509V3_F_ADDR_VALIDATE_PATH_INTERNAL),
     "addr_validate_path_internal"},
    {ERR_FUNC(X509V3_F_ASIDENTIFIERCHOICE_CANONIZE),
     "ASIdentifierChoice_canonize"},
    {ERR_FUNC(X509V3_F_ASIDENTIFIERCHOICE_IS_CANONICAL),
     "ASIdentifierChoice_is_canonical"},
    {ERR_FUNC(X509V3_F_COPY_EMAIL), "copy_email"},
    {ERR_FUNC(X509V3_F_COPY_ISSUER), "copy_issuer"},
    {ERR_FUNC(X509V3_F_DO_DIRNAME), "do_dirname"},
    {ERR_FUNC(X509V3_F_DO_EXT_I2D), "do_ext_i2d"},
    {ERR_FUNC(X509V3_F_DO_EXT_NCONF), "do_ext_nconf"},
    {ERR_FUNC(X509V3_F_GNAMES_FROM_SECTNAME), "gnames_from_sectname"},
    {ERR_FUNC(X509V3_F_I2S_ASN1_ENUMERATED), "i2s_ASN1_ENUMERATED"},
    {ERR_FUNC(X509V3_F_I2S_ASN1_IA5STRING), "i2s_ASN1_IA5STRING"},
    {ERR_FUNC(X509V3_F_I2S_ASN1_INTEGER), "i2s_ASN1_INTEGER"},
    {ERR_FUNC(X509V3_F_I2V_AUTHORITY_INFO_ACCESS),
     "i2v_AUTHORITY_INFO_ACCESS"},
    {ERR_FUNC(X509V3_F_NOTICE_SECTION), "notice_section"},
    {ERR_FUNC(X509V3_F_NREF_NOS), "nref_nos"},
    {ERR_FUNC(X509V3_F_POLICY_SECTION), "policy_section"},
    {ERR_FUNC(X509V3_F_PROCESS_PCI_VALUE), "process_pci_value"},
    {ERR_FUNC(X509V3_F_R2I_CERTPOL), "r2i_certpol"},
    {ERR_FUNC(X509V3_F_R2I_PCI), "r2i_pci"},
    {ERR_FUNC(X509V3_F_S2I_ASN1_IA5STRING), "s2i_ASN1_IA5STRING"},
    {ERR_FUNC(X509V3_F_S2I_ASN1_INTEGER), "s2i_ASN1_INTEGER"},
    {ERR_FUNC(X509V3_F_S2I_ASN1_OCTET_STRING), "s2i_ASN1_OCTET_STRING"},
    {ERR_FUNC(X509V3_F_S2I_SKEY_ID), "s2i_skey_id"},
    {ERR_FUNC(X509V3_F_SET_DIST_POINT_NAME), "set_dist_point_name"},
    {ERR_FUNC(X509V3_F_SXNET_ADD_ID_ASC), "SXNET_add_id_asc"},
    {ERR_FUNC(X509V3_F_SXNET_ADD_ID_INTEGER), "SXNET_add_id_INTEGER"},
    {ERR_FUNC(X509V3_F_SXNET_ADD_ID_ULONG), "SXNET_add_id_ulong"},
    {ERR_FUNC(X509V3_F_SXNET_GET_ID_ASC), "SXNET_get_id_asc"},
    {ERR_FUNC(X509V3_F_SXNET_GET_ID_ULONG), "SXNET_get_id_ulong"},
    {ERR_FUNC(X509V3_F_V2I_ASIDENTIFIERS), "v2i_ASIdentifiers"},
    {ERR_FUNC(X509V3_F_V2I_ASN1_BIT_STRING), "v2i_ASN1_BIT_STRING"},
    {ERR_FUNC(X509V3_F_V2I_AUTHORITY_INFO_ACCESS),
     "v2i_AUTHORITY_INFO_ACCESS"},
    {ERR_FUNC(X509V3_F_V2I_AUTHORITY_KEYID), "v2i_AUTHORITY_KEYID"},
    {ERR_FUNC(X509V3_F_V2I_BASIC_CONSTRAINTS), "v2i_BASIC_CONSTRAINTS"},
    {ERR_FUNC(X509V3_F_V2I_CRLD), "v2i_crld"},
    {ERR_FUNC(X509V3_F_V2I_EXTENDED_KEY_USAGE), "v2i_EXTENDED_KEY_USAGE"},
    {ERR_FUNC(X509V3_F_V2I_GENERAL_NAMES), "v2i_GENERAL_NAMES"},
    {ERR_FUNC(X509V3_F_V2I_GENERAL_NAME_EX), "v2i_GENERAL_NAME_ex"},
    {ERR_FUNC(X509V3_F_V2I_IDP), "v2i_idp"},
    {ERR_FUNC(X509V3_F_V2I_IPADDRBLOCKS), "v2i_IPAddrBlocks"},
    {ERR_FUNC(X509V3_F_V2I_ISSUER_ALT), "v2i_issuer_alt"},
    {ERR_FUNC(X509V3_F_V2I_NAME_CONSTRAINTS), "v2i_NAME_CONSTRAINTS"},
    {ERR_FUNC(X509V3_F_V2I_POLICY_CONSTRAINTS), "v2i_POLICY_CONSTRAINTS"},
    {ERR_FUNC(X509V3_F_V2I_POLICY_MAPPINGS), "v2i_POLICY_MAPPINGS"},
    {ERR_FUNC(X509V3_F_V2I_SUBJECT_ALT), "v2i_subject_alt"},
    {ERR_FUNC(X509V3_F_V2I_TLS_FEATURE), "v2i_TLS_FEATURE"},
    {ERR_FUNC(X509V3_F_V3_GENERIC_EXTENSION), "v3_generic_extension"},
    {ERR_FUNC(X509V3_F_X509V3_ADD1_I2D), "X509V3_add1_i2d"},
    {ERR_FUNC(X509V3_F_X509V3_ADD_VALUE), "X509V3_add_value"},
    {ERR_FUNC(X509V3_F_X509V3_EXT_ADD), "X509V3_EXT_add"},
    {ERR_FUNC(X509V3_F_X509V3_EXT_ADD_ALIAS), "X509V3_EXT_add_alias"},
    {ERR_FUNC(X509V3_F_X509V3_EXT_I2D), "X509V3_EXT_i2d"},
    {ERR_FUNC(X509V3_F_X509V3_EXT_NCONF), "X509V3_EXT_nconf"},
    {ERR_FUNC(X509V3_F_X509V3_GET_SECTION), "X509V3_get_section"},
    {ERR_FUNC(X509V3_F_X509V3_GET_STRING), "X509V3_get_string"},
    {ERR_FUNC(X509V3_F_X509V3_GET_VALUE_BOOL), "X509V3_get_value_bool"},
    {ERR_FUNC(X509V3_F_X509V3_PARSE_LIST), "X509V3_parse_list"},
    {ERR_FUNC(X509V3_F_X509_PURPOSE_ADD), "X509_PURPOSE_add"},
    {ERR_FUNC(X509V3_F_X509_PURPOSE_SET), "X509_PURPOSE_set"},
    {0, NULL}
};

static ERR_STRING_DATA X509V3_str_reasons[] = {
    {ERR_REASON(X509V3_R_BAD_IP_ADDRESS), "bad ip address"},
    {ERR_REASON(X509V3_R_BAD_OBJECT), "bad object"},
    {ERR_REASON(X509V3_R_BN_DEC2BN_ERROR), "bn dec2bn error"},
    {ERR_REASON(X509V3_R_BN_TO_ASN1_INTEGER_ERROR),
     "bn to asn1 integer error"},
    {ERR_REASON(X509V3_R_DIRNAME_ERROR), "dirname error"},
    {ERR_REASON(X509V3_R_DISTPOINT_ALREADY_SET), "distpoint already set"},
    {ERR_REASON(X509V3_R_DUPLICATE_ZONE_ID), "duplicate zone id"},
    {ERR_REASON(X509V3_R_ERROR_CONVERTING_ZONE), "error converting zone"},
    {ERR_REASON(X509V3_R_ERROR_CREATING_EXTENSION),
     "error creating extension"},
    {ERR_REASON(X509V3_R_ERROR_IN_EXTENSION), "error in extension"},
    {ERR_REASON(X509V3_R_EXPECTED_A_SECTION_NAME), "expected a section name"},
    {ERR_REASON(X509V3_R_EXTENSION_EXISTS), "extension exists"},
    {ERR_REASON(X509V3_R_EXTENSION_NAME_ERROR), "extension name error"},
    {ERR_REASON(X509V3_R_EXTENSION_NOT_FOUND), "extension not found"},
    {ERR_REASON(X509V3_R_EXTENSION_SETTING_NOT_SUPPORTED),
     "extension setting not supported"},
    {ERR_REASON(X509V3_R_EXTENSION_VALUE_ERROR), "extension value error"},
    {ERR_REASON(X509V3_R_ILLEGAL_EMPTY_EXTENSION), "illegal empty extension"},
    {ERR_REASON(X509V3_R_INCORRECT_POLICY_SYNTAX_TAG),
     "incorrect policy syntax tag"},
    {ERR_REASON(X509V3_R_INVALID_ASNUMBER), "invalid asnumber"},
    {ERR_REASON(X509V3_R_INVALID_ASRANGE), "invalid asrange"},
    {ERR_REASON(X509V3_R_INVALID_BOOLEAN_STRING), "invalid boolean string"},
    {ERR_REASON(X509V3_R_INVALID_EXTENSION_STRING),
     "invalid extension string"},
    {ERR_REASON(X509V3_R_INVALID_INHERITANCE), "invalid inheritance"},
    {ERR_REASON(X509V3_R_INVALID_IPADDRESS), "invalid ipaddress"},
    {ERR_REASON(X509V3_R_INVALID_MULTIPLE_RDNS), "invalid multiple rdns"},
    {ERR_REASON(X509V3_R_INVALID_NAME), "invalid name"},
    {ERR_REASON(X509V3_R_INVALID_NULL_ARGUMENT), "invalid null argument"},
    {ERR_REASON(X509V3_R_INVALID_NULL_NAME), "invalid null name"},
    {ERR_REASON(X509V3_R_INVALID_NULL_VALUE), "invalid null value"},
    {ERR_REASON(X509V3_R_INVALID_NUMBER), "invalid number"},
    {ERR_REASON(X509V3_R_INVALID_NUMBERS), "invalid numbers"},
    {ERR_REASON(X509V3_R_INVALID_OBJECT_IDENTIFIER),
     "invalid object identifier"},
    {ERR_REASON(X509V3_R_INVALID_OPTION), "invalid option"},
    {ERR_REASON(X509V3_R_INVALID_POLICY_IDENTIFIER),
     "invalid policy identifier"},
    {ERR_REASON(X509V3_R_INVALID_PROXY_POLICY_SETTING),
     "invalid proxy policy setting"},
    {ERR_REASON(X509V3_R_INVALID_PURPOSE), "invalid purpose"},
    {ERR_REASON(X509V3_R_INVALID_SAFI), "invalid safi"},
    {ERR_REASON(X509V3_R_INVALID_SECTION), "invalid section"},
    {ERR_REASON(X509V3_R_INVALID_SYNTAX), "invalid syntax"},
    {ERR_REASON(X509V3_R_ISSUER_DECODE_ERROR), "issuer decode error"},
    {ERR_REASON(X509V3_R_MISSING_VALUE), "missing value"},
    {ERR_REASON(X509V3_R_NEED_ORGANIZATION_AND_NUMBERS),
     "need organization and numbers"},
    {ERR_REASON(X509V3_R_NO_CONFIG_DATABASE), "no config database"},
    {ERR_REASON(X509V3_R_NO_ISSUER_CERTIFICATE), "no issuer certificate"},
    {ERR_REASON(X509V3_R_NO_ISSUER_DETAILS), "no issuer details"},
    {ERR_REASON(X509V3_R_NO_POLICY_IDENTIFIER), "no policy identifier"},
    {ERR_REASON(X509V3_R_NO_PROXY_CERT_POLICY_LANGUAGE_DEFINED),
     "no proxy cert policy language defined"},
    {ERR_REASON(X509V3_R_NO_PUBLIC_KEY), "no public key"},
    {ERR_REASON(X509V3_R_NO_SUBJECT_DETAILS), "no subject details"},
    {ERR_REASON(X509V3_R_OPERATION_NOT_DEFINED), "operation not defined"},
    {ERR_REASON(X509V3_R_OTHERNAME_ERROR), "othername error"},
    {ERR_REASON(X509V3_R_POLICY_LANGUAGE_ALREADY_DEFINED),
     "policy language already defined"},
    {ERR_REASON(X509V3_R_POLICY_PATH_LENGTH), "policy path length"},
    {ERR_REASON(X509V3_R_POLICY_PATH_LENGTH_ALREADY_DEFINED),
     "policy path length already defined"},
    {ERR_REASON(X509V3_R_POLICY_WHEN_PROXY_LANGUAGE_REQUIRES_NO_POLICY),
     "policy when proxy language requires no policy"},
    {ERR_REASON(X509V3_R_SECTION_NOT_FOUND), "section not found"},
    {ERR_REASON(X509V3_R_UNABLE_TO_GET_ISSUER_DETAILS),
     "unable to get issuer details"},
    {ERR_REASON(X509V3_R_UNABLE_TO_GET_ISSUER_KEYID),
     "unable to get issuer keyid"},
    {ERR_REASON(X509V3_R_UNKNOWN_BIT_STRING_ARGUMENT),
     "unknown bit string argument"},
    {ERR_REASON(X509V3_R_UNKNOWN_EXTENSION), "unknown extension"},
    {ERR_REASON(X509V3_R_UNKNOWN_EXTENSION_NAME), "unknown extension name"},
    {ERR_REASON(X509V3_R_UNKNOWN_OPTION), "unknown option"},
    {ERR_REASON(X509V3_R_UNSUPPORTED_OPTION), "unsupported option"},
    {ERR_REASON(X509V3_R_UNSUPPORTED_TYPE), "unsupported type"},
    {ERR_REASON(X509V3_R_USER_TOO_LONG), "user too long"},
    {0, NULL}
};

#endif

int ERR_load_X509V3_strings(void)
{
#ifndef OPENSSL_NO_ERR

    if (ERR_func_error_string(X509V3_str_functs[0].error) == NULL) {
        ERR_load_strings(0, X509V3_str_functs);
        ERR_load_strings(0, X509V3_str_reasons);
    }
#endif
    return 1;
}
