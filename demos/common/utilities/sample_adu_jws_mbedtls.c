/* Copyright (c) Microsoft Corporation.
 * Licensed under the MIT License. */

#include "sample_adu_jws.h"

#include "azure/az_core.h"
#include "azure/az_iot.h"

#include "azure_iot_result.h"
#include "azure_iot_json_reader.h"
#include "azure_iot_adu_client.h"

#include "mbedtls/base64.h"
#include "mbedtls/rsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/cipher.h"

/* For logging */
#include "demo_config.h"

/**
 * @brief Convenience macro to return if an operation failed.
 */
#define azureiotresultRETURN_IF_FAILED( exp )        \
    do                                               \
    {                                                \
        AzureIoTResult_t const _xAzResult = ( exp ); \
        if( _xAzResult != eAzureIoTSuccess )         \
        {                                            \
            return _xAzResult;                       \
        }                                            \
    } while( 0 )

const uint8_t jws_sha256_json_value[] = "sha256";
const uint8_t jws_sjwk_json_value[] = "sjwk";
const uint8_t jws_kid_json_value[] = "kid";
const uint8_t jws_n_json_value[] = "n";
const uint8_t jws_e_json_value[] = "e";
const uint8_t jws_alg_json_value[] = "alg";

/* prvSplitJWS takes a JWS payload and returns pointers to its constituent header, payload, and signature parts. */
static uint32_t prvSplitJWS( unsigned char * pucJWS,
                             uint32_t ulJWSLength,
                             unsigned char ** ppucHeader,
                             uint32_t * pulHeaderLength,
                             unsigned char ** ppucPayload,
                             uint32_t * pulPayloadLength,
                             unsigned char ** ppucSignature,
                             uint32_t * pulSignatureLength )
{
    unsigned char * pucFirstDot;
    unsigned char * pucSecondDot;
    uint32_t ulDotCount = 0;
    uint32_t ulIndex = 0;

    *ppucHeader = pucJWS;

    while( ulIndex < ulJWSLength )
    {
        if( *pucJWS == '.' )
        {
            ulDotCount++;

            if( ulDotCount == 1 )
            {
                pucFirstDot = pucJWS;
            }
            else if( ulDotCount == 2 )
            {
                pucSecondDot = pucJWS;
            }
            else if( ulDotCount > 2 )
            {
                return eAzureIoTErrorFailed;
            }
        }

        pucJWS++;
        ulIndex++;
    }

    if( ( ulDotCount != 2 ) || ( pucSecondDot >= ( *ppucHeader + ulJWSLength - 1 ) ) )
    {
        return eAzureIoTErrorFailed;
    }

    *pulHeaderLength = pucFirstDot - *ppucHeader;
    *ppucPayload = pucFirstDot + 1;
    *pulPayloadLength = pucSecondDot - *ppucPayload;
    *ppucSignature = pucSecondDot + 1;
    *pulSignatureLength = *ppucHeader + ulJWSLength - *ppucSignature;

    return 0;
}

/* Usual base64 encoded characters use `+` and `/` for the two extra characters */
/* In URL encoded schemes, those aren't allowed, so the characters are swapped */
/* for `-` and `_`. We have to swap them back to the usual characters. */
static void prvSwapToUrlEncodingChars( unsigned char * pucSignature,
                                       uint32_t ulSignatureLength )
{
    uint32_t ulIndex = 0;

    while( ulIndex < ulSignatureLength )
    {
        if( *pucSignature == '-' )
        {
            *pucSignature = '+';
        }
        else if( *pucSignature == '_' )
        {
            *pucSignature = '/';
        }

        pucSignature++;
        ulIndex++;
    }
}

/**
 * @brief Calculate the SHA256 over a buffer of bytes
 *
 * @param pucInput The input buffer over which to calculate the SHA256.
 * @param ulInputLength The length of \p pucInput.
 * @param pucOutput The output buffer into which the SHA256. It must be 32 bytes in length.
 * @return uint32_t The result of the operation.
 * @retval 0 if successful.
 * @retval Non-0 if not successful.
 */
static uint32_t prvJWS_SHA256Calculate( const unsigned char * pucInput,
                                        uint32_t ulInputLength,
                                        unsigned char * pucOutput )
{
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

    mbedtls_md_init( &ctx );
    mbedtls_md_setup( &ctx, mbedtls_md_info_from_type( md_type ), 0 );
    mbedtls_md_starts( &ctx );
    mbedtls_md_update( &ctx, pucInput, ulInputLength );
    mbedtls_md_finish( &ctx, pucOutput );
    mbedtls_md_free( &ctx );

    return 0;
}

/**
 * @brief Verify the manifest via RS256 for the JWS.
 *
 * @param pucInput The input over which the RS256 will be verified.
 * @param ulInputLength The length of \p pucInput.
 * @param pucSignature The encrypted signature which will be decrypted by \p pucN and \p pucE.
 * @param ulSignatureLength The length of \p pucSignature.
 * @param pucN The key's modulus which is used to decrypt \p signature.
 * @param ulNLength The length of \p pucN.
 * @param pucE The exponent used for the key.
 * @param ulELength The length of \p pucE.
 * @param pucBuffer The buffer used as scratch space to make the calculations. It should be at least
 * `jwsRSA3072_SIZE` + `jwsSHA256_SIZE` in size.
 * @param ulBufferLength The length of \p pucBuffer.
 * @return uint32_t The result of the operation.
 * @retval 0 if successful.
 * @retval Non-0 if not successful.
 */
static uint32_t prvJWS_RS256Verify( unsigned char * pucInput,
                                    uint32_t ulInputLength,
                                    unsigned char * pucSignature,
                                    uint32_t ulSignatureLength,
                                    unsigned char * pucN,
                                    uint32_t ulNLength,
                                    unsigned char * pucE,
                                    uint32_t ulELength,
                                    unsigned char * pucBuffer,
                                    uint32_t ulBufferLength )
{
    AzureIoTResult_t xResult;
    int32_t lMbedTLSResult;
    unsigned char * pucShaBuffer;
    size_t ulDecryptedLength;
    mbedtls_rsa_context ctx;
    int shaMatchResult;

    if( ulBufferLength < jwsSHA_CALCULATION_SCRATCH_SIZE )
    {
        LogError( ( "[JWS] Buffer Not Large Enough" ) );
        return 1;
    }

    pucShaBuffer = pucBuffer + jwsRSA3072_SIZE;

    /* The signature is encrypted using the input key. We need to decrypt the */
    /* signature which gives us the SHA256 inside a PKCS7 structure. We then compare */
    /* that to the SHA256 of the input. */
    mbedtls_rsa_init( &ctx, MBEDTLS_RSA_PKCS_V15, 0 );

    lMbedTLSResult = mbedtls_rsa_import_raw( &ctx,
                                             pucN, ulNLength,
                                             NULL, 0,
                                             NULL, 0,
                                             NULL, 0,
                                             pucE, ulELength );

    if( lMbedTLSResult != 0 )
    {
        LogError( ( "[JWS] mbedtls_rsa_import_raw res: %i", lMbedTLSResult ) );
        mbedtls_rsa_free( &ctx );
        return lMbedTLSResult;
    }

    lMbedTLSResult = mbedtls_rsa_complete( &ctx );

    if( lMbedTLSResult != 0 )
    {
        LogError( ( "[JWS] mbedtls_rsa_complete res: %i", lMbedTLSResult ) );
        mbedtls_rsa_free( &ctx );
        return lMbedTLSResult;
    }

    lMbedTLSResult = mbedtls_rsa_check_pubkey( &ctx );

    if( lMbedTLSResult != 0 )
    {
        LogError( ( "[JWS] mbedtls_rsa_check_pubkey res: %i", lMbedTLSResult ) );
        mbedtls_rsa_free( &ctx );
        return lMbedTLSResult;
    }

    /* RSA */
    lMbedTLSResult = mbedtls_rsa_pkcs1_decrypt( &ctx, NULL, NULL, MBEDTLS_RSA_PUBLIC, &ulDecryptedLength, pucSignature, pucBuffer, jwsRSA3072_SIZE );

    if( lMbedTLSResult != 0 )
    {
        LogError( ( "[JWS] mbedtls_rsa_pkcs1_decrypt res: %i", lMbedTLSResult ) );
        mbedtls_rsa_free( &ctx );
        return lMbedTLSResult;
    }

    mbedtls_rsa_free( &ctx );

    xResult = prvJWS_SHA256Calculate( pucInput, ulInputLength,
                                      pucShaBuffer );

    /* TODO: remove this once we have a valid PKCS7 parser. */
    shaMatchResult = memcmp( pucBuffer + jwsPKCS7_PAYLOAD_OFFSET, pucShaBuffer, jwsSHA256_SIZE );

    if( shaMatchResult )
    {
        LogError( ( "[JWS] SHA of JWK does NOT match" ) );
        xResult = 1;
    }

    return xResult;
}

static uint32_t prvFindJWSValue( AzureIoTJSONReader_t * pxPayload )
{
    AzureIoTResult_t xResult = eAzureIoTSuccess;
    AzureIoTJSONTokenType_t xJSONTokenType;

    azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_NextToken( pxPayload ) );

    while( xResult == eAzureIoTSuccess )
    {
        if( AzureIoTJSONReader_TokenIsTextEqual( pxPayload, ( const uint8_t * ) jws_sjwk_json_value, sizeof( jws_sjwk_json_value ) - 1 ) )
        {
            /* Found name, move to value */
            azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_NextToken( pxPayload ) );
            break;
        }
        else
        {
            azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_NextToken( pxPayload ) );
            azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_SkipChildren( pxPayload ) );
            xResult = AzureIoTJSONReader_NextToken( pxPayload );
        }
    }

    azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_TokenType( pxPayload, &xJSONTokenType ) );

    if( ( xResult != eAzureIoTSuccess ) && ( xJSONTokenType == eAzureIoTJSONTokenSTRING ) )
    {
        LogError( ( "[JWS] Parse JSK JSON Payload Error: 0x%08x", xResult ) );
        return xResult;
    }

    return 0;
}

static int32_t prvFindRootKeyValue( AzureIoTJSONReader_t * pxPayload,
                                    az_span * pxKIDSpan )
{
    AzureIoTResult_t xResult = eAzureIoTSuccess;
    AzureIoTJSONTokenType_t xJSONTokenType;

    /*Begin object */
    azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_NextToken( pxPayload ) );
    /*Property Name */
    azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_NextToken( pxPayload ) );

    while( xResult == eAzureIoTSuccess )
    {
        if( AzureIoTJSONReader_TokenIsTextEqual( pxPayload, ( const uint8_t * ) jws_kid_json_value, sizeof( jws_kid_json_value ) - 1 ) )
        {
            azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_NextToken( pxPayload ) );
            *pxKIDSpan = pxPayload->_internal.xCoreReader.token.slice;

            break;
        }
        else
        {
            azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_NextToken( pxPayload ) );
            azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_SkipChildren( pxPayload ) );
            xResult = AzureIoTJSONReader_NextToken( pxPayload );
        }
    }

    azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_TokenType( pxPayload, &xJSONTokenType ) );

    if( ( xResult != eAzureIoTSuccess ) && ( xJSONTokenType != eAzureIoTJSONTokenSTRING ) )
    {
        LogError( ( "[JWS] Parse Root Key Error: %i", xResult ) );
        return xResult;
    }
}

static int32_t prvFindKeyParts( AzureIoTJSONReader_t * xPayload,
                                az_span * pxBase64EncodedNSpan,
                                az_span * pxBase64EncodedESpan,
                                az_span * pxAlgSpan )
{
    AzureIoTResult_t xResult = eAzureIoTSuccess;

    /*Begin object */
    azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_NextToken( xPayload ) );
    /*Property Name */
    azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_NextToken( xPayload ) );

    while( xResult == eAzureIoTSuccess && ( az_span_size( *pxBase64EncodedNSpan ) == 0 || az_span_size( *pxBase64EncodedESpan ) == 0 || az_span_size( *pxAlgSpan ) == 0 ) )
    {
        if( AzureIoTJSONReader_TokenIsTextEqual( xPayload, ( const uint8_t * ) jws_n_json_value, sizeof( jws_n_json_value ) - 1 ) )
        {
            azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_NextToken( xPayload ) );
            *pxBase64EncodedNSpan = xPayload->_internal.xCoreReader.token.slice;

            xResult = AzureIoTJSONReader_NextToken( xPayload );
        }
        else if( AzureIoTJSONReader_TokenIsTextEqual( xPayload, ( const uint8_t * ) jws_e_json_value, sizeof( jws_e_json_value ) - 1 ) )
        {
            azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_NextToken( xPayload ) );
            *pxBase64EncodedESpan = xPayload->_internal.xCoreReader.token.slice;

            xResult = AzureIoTJSONReader_NextToken( xPayload );
        }
        else if( AzureIoTJSONReader_TokenIsTextEqual( xPayload, ( const uint8_t * ) jws_alg_json_value, sizeof( jws_alg_json_value ) - 1 ) )
        {
            azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_NextToken( xPayload ) );
            *pxAlgSpan = xPayload->_internal.xCoreReader.token.slice;

            xResult = AzureIoTJSONReader_NextToken( xPayload );
        }
        else
        {
            azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_NextToken( xPayload ) );
            azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_SkipChildren( xPayload ) );
            xResult = AzureIoTJSONReader_NextToken( xPayload );
        }
    }

    if( ( xResult != eAzureIoTSuccess ) && ( xResult != eAzureIoTErrorJSONReaderDone ) )
    {
        LogError( ( "[JWS] Parse Signing Key Payload Error: %i", xResult ) );
        return xResult;
    }

    return 0;
}

uint32_t JWS_ManifestAuthenticate( const char * pucManifest,
                                   uint32_t ulManifestLength,
                                   char * pucJWS,
                                   uint32_t ulJWSLength,
                                   char * pucScratchBuffer,
                                   uint32_t ulScratchBufferLength )
{
    uint32_t ulVerificationResult;

    int lMbedResult;
    unsigned char * pucBase64EncodedHeader;
    unsigned char * pucBase64EncodedPayload;
    unsigned char * pucBase64EncodedSignature;
    uint32_t ulBase64EncodedHeaderLength;
    uint32_t ulBase64EncodedPayloadLength;
    uint32_t ulBase64SignatureLength;
    az_result xCoreResult;
    AzureIoTResult_t xResult;
    AzureIoTJSONReader_t xJSONReader;
    AzureIoTJSONTokenType_t xJSONTokenType;

    if( ulScratchBufferLength < jwsSHA_CALCULATION_SCRATCH_SIZE )
    {
        LogError( ( "[JWS] Scratch buffer size too small" ) );
        return eAzureIoTErrorFailed;
    }

    /* Partition the scratch buffer */
    char * ucJWSHeader = pucScratchBuffer;
    pucScratchBuffer += jwsJWS_HEADER_SIZE;

    char * ucJWSPayload = pucScratchBuffer;
    pucScratchBuffer += jwsJWS_PAYLOAD_SIZE;

    char * ucJWSSignature = pucScratchBuffer;
    pucScratchBuffer += jwsJWS_SIGNATURE_SIZE;

    char * ucJWKHeader = pucScratchBuffer;
    pucScratchBuffer += jwsJWK_HEADER_SIZE;

    char * ucJWKPayload = pucScratchBuffer;
    pucScratchBuffer += jwsJWK_PAYLOAD_SIZE;

    char * ucJWKSignature = pucScratchBuffer;
    pucScratchBuffer += jwsJWK_SIGNATURE_SIZE;

    char * ucSigningKeyN = pucScratchBuffer;
    pucScratchBuffer += jwsRSA3072_SIZE;

    char * ucSigningKeyE = pucScratchBuffer;
    pucScratchBuffer += jwsSIGNING_KEY_E_SIZE;

    char * ucManifestSHACalculation = pucScratchBuffer;
    pucScratchBuffer += jwsSHA256_SIZE;

    char * ucParsedManifestSha = pucScratchBuffer;
    pucScratchBuffer += jwsSHA256_SIZE;

    char * ucScratchCalculatationBuffer = pucScratchBuffer;

    /*------------------- Parse and Decode the Manifest Sig ------------------------*/

    xResult = prvSplitJWS( ( unsigned char * ) pucJWS, ulJWSLength,
                           &pucBase64EncodedHeader, &ulBase64EncodedHeaderLength,
                           &pucBase64EncodedPayload, &ulBase64EncodedPayloadLength,
                           &pucBase64EncodedSignature, &ulBase64SignatureLength );

    if( xResult != eAzureIoTSuccess )
    {
        LogError( ( "[JWS] prvSplitJWS failed: result %i", xResult ) );
        return xResult;
    }

    prvSwapToUrlEncodingChars( pucBase64EncodedSignature, ulBase64SignatureLength );

    /* Note that we do not use mbedTLS to base64 decode values since we need the ability to assume padding characters. */
    /* mbedTLS will stop the decoding short and we would then need to add in the remaining characters. */
    int32_t outJWSHeaderLength;
    az_span xJWSBase64EncodedHeaderSpan = az_span_create( pucBase64EncodedHeader, ulBase64EncodedHeaderLength );
    az_span xJWSHeaderSpan = az_span_create( ucJWSHeader, jwsJWS_HEADER_SIZE );
    xCoreResult = az_base64_decode( xJWSHeaderSpan, xJWSBase64EncodedHeaderSpan, &outJWSHeaderLength );

    if( az_result_failed( xCoreResult ) )
    {
        LogError( ( "[JWS] az_base64_decode failed: result %i", xCoreResult ) );
        return eAzureIoTErrorFailed;
    }

    int32_t outJWSPayloadLength;
    az_span xJWSBase64EncodedPayloadSpan = az_span_create( pucBase64EncodedPayload, ulBase64EncodedPayloadLength );
    az_span xJWSPayloadSpan = az_span_create( ucJWSPayload, jwsJWS_PAYLOAD_SIZE );
    xCoreResult = az_base64_decode( xJWSPayloadSpan, xJWSBase64EncodedPayloadSpan, &outJWSPayloadLength );

    if( az_result_failed( xCoreResult ) )
    {
        LogError( ( "[JWS] az_base64_decode failed: result %i", xCoreResult ) );
        return eAzureIoTErrorFailed;
    }

    int32_t outJWSSignatureLength;
    az_span xJWSBase64EncodedSignatureSpan = az_span_create( pucBase64EncodedSignature, ulBase64SignatureLength );
    az_span xJWSSignatureSpan = az_span_create( ucJWSSignature, jwsJWS_SIGNATURE_SIZE );
    xCoreResult = az_base64_decode( xJWSSignatureSpan, xJWSBase64EncodedSignatureSpan, &outJWSSignatureLength );

    if( az_result_failed( xCoreResult ) )
    {
        LogError( ( "[JWS] az_base64_decode failed: result %i", xCoreResult ) );
        return eAzureIoTErrorFailed;
    }

    /*------------------- Parse JSK JSON Payload ------------------------*/

    /* The "sjwk" is the signed signing public key */
    unsigned char * pucJWKManifest;
    az_span xJWKManifestSpan;

    AzureIoTJSONReader_Init( &xJSONReader, ( const uint8_t * ) az_span_ptr( xJWSHeaderSpan ), outJWSHeaderLength );

    if( prvFindJWSValue( &xJSONReader ) != 0 )
    {
        LogError( ( "Error finding sjwk value in payload" ) );
        return eAzureIoTErrorFailed;
    }

    xJWKManifestSpan = xJSONReader._internal.xCoreReader.token.slice;
    pucJWKManifest = az_span_ptr( xJWKManifestSpan );
    uint32_t ulJWKManifestLength = az_span_size( xJWKManifestSpan );

    /*------------------- Base64 Decode the JWK Payload ------------------------*/

    unsigned char * pucJWKBase64EncodedHeader;
    unsigned char * pucJWKBase64EncodedPayload;
    unsigned char * pucJWKBase64EncodedSignature;
    uint32_t ulJWKBase64EncodedHeaderLength;
    uint32_t ulJWKBase64EncodedPayloadLength;
    uint32_t ulJWKBase64EncodedSignatureLength;

    xResult = prvSplitJWS( pucJWKManifest, ulJWKManifestLength,
                           &pucJWKBase64EncodedHeader, &ulJWKBase64EncodedHeaderLength,
                           &pucJWKBase64EncodedPayload, &ulJWKBase64EncodedPayloadLength,
                           &pucJWKBase64EncodedSignature, &ulJWKBase64EncodedSignatureLength );

    if( xResult != eAzureIoTSuccess )
    {
        LogError( ( "[JWS] prvSplitJWS failed: result %i", xResult ) );
        return xResult;
    }

    prvSwapToUrlEncodingChars( pucJWKBase64EncodedSignature, ulJWKBase64EncodedSignatureLength );

    int32_t outJWKHeaderLength;
    az_span xJWKBase64EncodedHeaderSpan = az_span_create( pucJWKBase64EncodedHeader, ulJWKBase64EncodedHeaderLength );
    az_span xJWKHeaderSpan = az_span_create( ucJWKHeader, jwsJWK_HEADER_SIZE );
    xCoreResult = az_base64_decode( xJWKHeaderSpan, xJWKBase64EncodedHeaderSpan, &outJWKHeaderLength );

    if( az_result_failed( xCoreResult ) )
    {
        LogError( ( "[JWS] az_base64_decode failed: result %i", xCoreResult ) );
        return eAzureIoTErrorFailed;
    }

    int32_t outJWKPayloadLength;
    az_span xJWKBase64EncodedPayloadSpan = az_span_create( pucJWKBase64EncodedPayload, ulJWKBase64EncodedPayloadLength );
    az_span xJWKPayloadSpan = az_span_create( ucJWKPayload, jwsJWK_PAYLOAD_SIZE );
    xCoreResult = az_base64_decode( xJWKPayloadSpan, xJWKBase64EncodedPayloadSpan, &outJWKPayloadLength );

    if( az_result_failed( xCoreResult ) )
    {
        LogError( ( "[JWS] az_base64_decode failed: result %i", xCoreResult ) );
        return eAzureIoTErrorFailed;
    }

    int32_t outJWKSignatureLength;
    az_span xJWKBase64EncodedSignatureSpan = az_span_create( pucJWKBase64EncodedSignature, ulJWKBase64EncodedSignatureLength );
    az_span xJWKSignatureSpan = az_span_create( ucJWKSignature, jwsJWK_SIGNATURE_SIZE );
    xCoreResult = az_base64_decode( xJWKSignatureSpan, xJWKBase64EncodedSignatureSpan, &outJWKSignatureLength );

    if( az_result_failed( xCoreResult ) )
    {
        LogError( ( "[JWS] az_base64_decode failed: result %i", xCoreResult ) );
        return eAzureIoTErrorFailed;
    }

    /*------------------- Parse id for root key ------------------------*/

    az_span kidSpan;

    AzureIoTJSONReader_Init( &xJSONReader, ( const uint8_t * ) ucJWKHeader, outJWKHeaderLength );

    if( prvFindRootKeyValue( &xJSONReader, &kidSpan ) != 0 )
    {
        LogError( ( "Could not find kid in JSON" ) );
        return eAzureIoTErrorFailed;
    }

    az_span rootKeyIDSpan = az_span_create( ( uint8_t * ) AzureIoTADURootKeyId, sizeof( AzureIoTADURootKeyId ) - 1 );

    if( !az_span_is_content_equal( rootKeyIDSpan, kidSpan ) )
    {
        LogError( ( "[JWS] Using the wrong root key" ) );
        return eAzureIoTErrorFailed;
    }

    /*------------------- Parse necessary pieces for the verification ------------------------*/

    az_span xBase64EncodedNSpan = AZ_SPAN_EMPTY;
    az_span xBase64EncodedESpan = AZ_SPAN_EMPTY;
    az_span xAlgSpan = AZ_SPAN_EMPTY;

    AzureIoTJSONReader_Init( &xJSONReader, ( const uint8_t * ) ucJWKPayload, outJWKPayloadLength );

    if( prvFindKeyParts( &xJSONReader, &xBase64EncodedNSpan, &xBase64EncodedESpan, &xAlgSpan ) != 0 )
    {
        LogError( ( "Could not find parts for the signing key" ) );
        return eAzureIoTErrorFailed;
    }

    /*------------------- Base64 decode the key ------------------------*/
    int32_t outSigningKeyNLength;
    az_span xNSpan = az_span_create( ucSigningKeyN, jwsRSA3072_SIZE );
    xCoreResult = az_base64_decode( xNSpan, xBase64EncodedNSpan, &outSigningKeyNLength );

    if( az_result_failed( xCoreResult ) )
    {
        LogError( ( "[JWS] az_base64_decode failed: result %i", xCoreResult ) );
        return eAzureIoTErrorFailed;
    }

    int32_t outSigningKeyELength;
    az_span xESpan = az_span_create( ucSigningKeyE, jwsSIGNING_KEY_E_SIZE );
    xCoreResult = az_base64_decode( xESpan, xBase64EncodedESpan, &outSigningKeyELength );

    if( az_result_failed( xCoreResult ) )
    {
        LogError( ( "[JWS] az_base64_decode failed: result %i", xCoreResult ) );
        return eAzureIoTErrorFailed;
    }

    /*------------------- Verify the signature ------------------------*/
    ulVerificationResult = prvJWS_RS256Verify( pucJWKBase64EncodedHeader, ulJWKBase64EncodedHeaderLength + ulJWKBase64EncodedPayloadLength + 1,
                                               ucJWKSignature, outJWKSignatureLength,
                                               ( unsigned char * ) AzureIoTADURootKeyN, sizeof( AzureIoTADURootKeyN ),
                                               ( unsigned char * ) AzureIoTADURootKeyE, sizeof( AzureIoTADURootKeyE ),
                                               ucScratchCalculatationBuffer, jwsSHA_CALCULATION_SCRATCH_SIZE );

    if( ulVerificationResult != 0 )
    {
        LogError( ( "[JWS] Verification of signing key failed" ) );
        return ulVerificationResult;
    }

    /*------------------- Verify that the signature was signed by signing key ------------------------*/
    ulVerificationResult = prvJWS_RS256Verify( pucBase64EncodedHeader, ulBase64EncodedHeaderLength + ulBase64EncodedPayloadLength + 1,
                                               ucJWSSignature, outJWSSignatureLength,
                                               ucSigningKeyN, outSigningKeyNLength,
                                               ucSigningKeyE, outSigningKeyELength,
                                               ucScratchCalculatationBuffer, jwsSHA_CALCULATION_SCRATCH_SIZE );

    if( ulVerificationResult != 0 )
    {
        LogError( ( "[JWS] Verification of signed manifest SHA failed" ) );
        return ulVerificationResult;
    }

    /*------------------- Verify that the SHAs match ------------------------*/
    ulVerificationResult = prvJWS_SHA256Calculate( ( const unsigned char * ) pucManifest,
                                                   ulManifestLength,
                                                   ucManifestSHACalculation );

    if( ulVerificationResult != 0 )
    {
        LogError( ( "[JWS] SHA256 Calculation failed" ) );
        return ulVerificationResult;
    }

    AzureIoTJSONReader_Init( &xJSONReader, ( const uint8_t * ) ucJWSPayload, outJWSPayloadLength );
    /*Begin object */
    azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_NextToken( &xJSONReader ) );
    /*Property Name */
    azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_NextToken( &xJSONReader ) );

    az_span sha256Span;

    while( xResult == eAzureIoTSuccess )
    {
        if( AzureIoTJSONReader_TokenIsTextEqual( &xJSONReader, ( const uint8_t * ) jws_sha256_json_value, sizeof( jws_sha256_json_value ) - 1 ) )
        {
            azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_NextToken( &xJSONReader ) );
            sha256Span = xJSONReader._internal.xCoreReader.token.slice;
            break;
        }
        else
        {
            azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_NextToken( &xJSONReader ) );
            azureiotresultRETURN_IF_FAILED( AzureIoTJSONReader_SkipChildren( &xJSONReader ) );
            xResult = AzureIoTJSONReader_NextToken( &xJSONReader );
        }
    }

    if( ( xResult != eAzureIoTSuccess ) && ( xResult != eAzureIoTErrorJSONReaderDone ) )
    {
        LogError( ( "[JWS] Parse SHA256 Error: %i", xResult ) );
        return xResult;
    }

    int32_t outParsedManifestShaSize;
    az_span xParsedManifestSHA = az_span_create( ucParsedManifestSha, jwsSHA256_SIZE );
    xCoreResult = az_base64_decode( xParsedManifestSHA, sha256Span, &outParsedManifestShaSize );

    if( az_result_failed( xCoreResult ) )
    {
        LogError( ( "[JWS] az_base64_decode failed: result %i", xCoreResult ) );
        return eAzureIoTErrorFailed;
    }

    if( outParsedManifestShaSize != jwsSHA256_SIZE )
    {
        LogError( ( "[JWS] Base64 decoded SHA256 is not the correct length" ) );
        return 1;
    }

    ulVerificationResult = memcmp( ucManifestSHACalculation, ucParsedManifestSha, jwsSHA256_SIZE );

    if( ulVerificationResult != 0 )
    {
        LogError( ( "[JWS] Calculated manifest SHA does not match SHA in payload" ) );
        return ulVerificationResult;
    }
    else
    {
        LogInfo( ( "[JWS] Calculated manifest SHA matches parsed SHA" ) );
    }

    /*------------------- Done (Loop) ------------------------*/
    return ulVerificationResult;
}