/**
 * @file azure_iot_adu_client.h
 *
 * @brief
 *
 */
#ifndef AZURE_IOT_ADU_CLIENT_H
#define AZURE_IOT_ADU_CLIENT_H

#include <stdint.h>

#include "azure_iot_result.h"
#include "azure_iot_hub_client.h"
#include "azure_iot_json_reader.h"
#include "azure_iot_http.h"
#include <azure/iot/az_iot_adu_ota.h>
#include "azure_iot_flash_platform.h"


/**************************ADU SECTION******************************** // */

/* We have to parse out these values below. */

/*
 *
 * Device Twin for ADU
 *
 * {
 *  "deviceUpdate": {
 *      "__t": "c",
 *      "agent": {
 *          "deviceProperties": {
 *              "manufacturer": "contoso",
 *              "model": "virtual-vacuum-v1",
 *              "interfaceId": "dtmi:azure:iot:deviceUpdate;1",
 *              "aduVer": "DU;agent/0.8.0-rc1-public-preview",
 *              "doVer": "DU;lib/v0.6.0+20211001.174458.c8c4051,DU;agent/v0.6.0+20211001.174418.c8c4051"
 *          },
 *          "compatPropertyNames": "manufacturer,model",
 *          "lastInstallResult": {
 *              "resultCode": 700,
 *              "extendedResultCode": 0,
 *              "resultDetails": "",
 *              "stepResults": {
 *                  "step_0": {
 *                      "resultCode": 700,
 *                      "extendedResultCode": 0,
 *                      "resultDetails": ""
 *                  }
 *              }
 *          },
 *          "state": 0,
 *          "workflow": {
 *              "action": 3,
 *              "id": "11b6a7c3-6956-4b33-b5a9-87fdd79d2f01",
 *              "retryTimestamp": "2022-01-26T11:33:29.9680598Z"
 *          },
 *          "installedUpdateId": "{\"provider\":\"Contoso\",\"name\":\"Virtual-Vacuum\",\"version\":\"5.0\"}"
 *      }
 *  }
 * }
 */


/*Update Manifest */

/*
 *
 * {
 *  "manifestVersion": "4",
 *  "updateId": {
 *      "provider": "Contoso",
 *      "name": "Toaster",
 *      "version": "1.0"
 *  },
 *  "compatibility": [{
 *      "deviceManufacturer": "Contoso",
 *      "deviceModel": "Toaster"
 *  }],
 *  "instructions": {
 *      "steps": [{
 *          "handler": "microsoft/swupdate:1",
 *          "handlerProperties": {
 *              "installedCriteria": "1.0"
 *          },
 *          "files": [
 *              "fileId0"
 *          ]
 *      }]
 *  },
 *  "files": {
 *      "fileId0": {
 *          "filename": "contoso.toaster.1.0.swu",
 *          "sizeInBytes": 718,
 *          "hashes": {
 *              "sha256": "mcB5SexMU4JOOzqmlJqKbue9qMskWY3EI/iVjJxCtAs="
 *          }
 *      }
 *  },
 *  "createdDateTime": "2021-09-28T18:32:01.8404544Z"
 * }
 *
 */

#define azureiotaduWORKFLOW_ID_SIZE                 48
#define azureiotaduWORKFLOW_RETRY_TIMESTAMP_SIZE    80
#define azureiotaduSTEPS_MAX                        2
#define azureiotaduAGENT_FILES_MAX                  2
#define azureiotaduRESULT_DETAILS_SIZE              128
#define azureiotaduDEVICE_INFO_MANUFACTURER_SIZE    16
#define azureiotaduDEVICE_INFO_MODEL_SIZE           24
#define azureiotaduUPDATE_PROVIDER_SIZE             16
#define azureiotaduUPDATE_NAME_SIZE                 24
#define azureiotaduUPDATE_VERSION_SIZE              10
#define azureiotaduSTEP_ID_SIZE                     32

/* ADU.200702.R: root production key from 7/2/2020  */
const unsigned char AzureIoTADURootKeyId[] = "ADU.200702.R";
const unsigned char AzureIoTADURootKeyN[] = {0x00, 0xd5, 0x42, 0x2e, 0xaf, 0x11, 0x54, 0xa3, 0x50, 0x65, 0x87, 0xa2, 0x4d, 0x5b, 0xba,
                                         0x1a, 0xfb, 0xa9, 0x32, 0xdf, 0xe9, 0x99, 0x5f, 0x05, 0x45, 0xc8, 0xaf, 0xbd, 0x35, 0x1d,
                                         0x89, 0xe8, 0x27, 0x27, 0x58, 0xa3, 0xa8, 0xee, 0xc5, 0xc5, 0x1e, 0x4f, 0xf7, 0x92, 0xa6,
                                         0x12, 0x06, 0x7d, 0x3d, 0x7d, 0xb0, 0x07, 0xf6, 0x2c, 0x7f, 0xde, 0x6d, 0x2a, 0xf5, 0xbc,
                                         0x49, 0xbc, 0x15, 0xef, 0xf0, 0x81, 0xcb, 0x3f, 0x88, 0x4f, 0x27, 0x1d, 0x88, 0x71, 0x28,
                                         0x60, 0x08, 0xb6, 0x19, 0xd2, 0xd2, 0x39, 0xd0, 0x05, 0x1f, 0x3c, 0x76, 0x86, 0x71, 0xbb,
                                         0x59, 0x58, 0xbc, 0xb1, 0x88, 0x7b, 0xab, 0x56, 0x28, 0xbf, 0x31, 0x73, 0x44, 0x32, 0x10,
                                         0xfd, 0x3d, 0xd3, 0x96, 0x5c, 0xff, 0x4e, 0x5c, 0xb3, 0x6b, 0xff, 0x8b, 0x84, 0x9b, 0x8b,
                                         0x80, 0xb8, 0x49, 0xd0, 0x7d, 0xfa, 0xd6, 0x40, 0x58, 0x76, 0x4d, 0xc0, 0x72, 0x27, 0x75,
                                         0xcb, 0x9a, 0x2f, 0x9b, 0xb4, 0x9f, 0x0f, 0x25, 0xf1, 0x1c, 0xc5, 0x1b, 0x0b, 0x5a, 0x30,
                                         0x7d, 0x2f, 0xb8, 0xef, 0xa7, 0x26, 0x58, 0x53, 0xaf, 0xd5, 0x1d, 0x55, 0x01, 0x51, 0x0d,
                                         0xe9, 0x1b, 0xa2, 0x0f, 0x3f, 0xd7, 0xe9, 0x1d, 0x20, 0x41, 0xa6, 0xe6, 0x14, 0x0a, 0xae,
                                         0xfe, 0xf2, 0x1c, 0x2a, 0xd6, 0xe4, 0x04, 0x7b, 0xf6, 0x14, 0x7e, 0xec, 0x0f, 0x97, 0x83,
                                         0xfa, 0x58, 0xfa, 0x81, 0x36, 0x21, 0xb9, 0xa3, 0x2b, 0xfa, 0xd9, 0x61, 0x0b, 0x1a, 0x94,
                                         0xf7, 0xc1, 0xbe, 0x7f, 0x40, 0x14, 0x4a, 0xc9, 0xfa, 0x35, 0x7f, 0xef, 0x66, 0x70, 0x00,
                                         0xb1, 0xfd, 0xdb, 0xd7, 0x61, 0x0d, 0x3b, 0x58, 0x74, 0x67, 0x94, 0x89, 0x75, 0x76, 0x96,
                                         0x7c, 0x91, 0x87, 0xd2, 0x8e, 0x11, 0x97, 0xee, 0x7b, 0x87, 0x6c, 0x9a, 0x2f, 0x45, 0xd8,
                                         0x65, 0x3f, 0x52, 0x70, 0x98, 0x2a, 0xcb, 0xc8, 0x04, 0x63, 0xf5, 0xc9, 0x47, 0xcf, 0x70,
                                         0xf4, 0xed, 0x64, 0xa7, 0x74, 0xa5, 0x23, 0x8f, 0xb6, 0xed, 0xf7, 0x1c, 0xd3, 0xb0, 0x1c,
                                         0x64, 0x57, 0x12, 0x5a, 0xa9, 0x81, 0x84, 0x1f, 0xa0, 0xe7, 0x50, 0x19, 0x96, 0xb4, 0x82,
                                         0xb1, 0xac, 0x48, 0xe3, 0xe1, 0x32, 0x82, 0xcb, 0x40, 0x1f, 0xac, 0xc4, 0x59, 0xbc, 0x10,
                                         0x34, 0x51, 0x82, 0xf9, 0x28, 0x8d, 0xa8, 0x1e, 0x9b, 0xf5, 0x79, 0x45, 0x75, 0xb2, 0xdc,
                                         0x9a, 0x11, 0x43, 0x08, 0xbe, 0x61, 0xcc, 0x9a, 0xc4, 0xcb, 0x77, 0x36, 0xff, 0x83, 0xdd,
                                         0xa8, 0x71, 0x4f, 0x51, 0x8e, 0x0e, 0x7b, 0x4d, 0xfa, 0x79, 0x98, 0x8d, 0xbe, 0xfc, 0x82,
                                         0x7e, 0x40, 0x48, 0xa9, 0x12, 0x01, 0xa8, 0xd9, 0x7e, 0xf3, 0xa5, 0x1b, 0xf1, 0xfb, 0x90,
                                         0x77, 0x3e, 0x40, 0x87, 0x18, 0xc9, 0xab, 0xd9, 0xf7, 0x79};
const unsigned char AzureIoTADURootKeyE[] = {0x01, 0x00, 0x01};

/**
 * @brief ADU Update ID.
 *
 *  https://docs.microsoft.com/en-us/azure/iot-hub-device-update/understand-device-update#device-update-agent
 */
typedef struct AzureIoTHubClientADUUpdateId
{
    const uint8_t ucProvider[ azureiotaduUPDATE_PROVIDER_SIZE ];
    uint32_t ulProviderLength;

    const uint8_t ucName[ azureiotaduUPDATE_NAME_SIZE ];
    uint32_t ulNameLength;

    const uint8_t ucVersion[ azureiotaduUPDATE_VERSION_SIZE ];
    uint32_t ulVersionLength;
} AzureIoTHubClientADUUpdateId_t;

/**
 * @brief ADU Device Information.
 *
 *  https://docs.microsoft.com/en-us/azure/iot-hub-device-update/understand-device-update#device-update-agent
 */
typedef struct AzureIoTHubClientADUDeviceInformation
{
    const uint8_t ucManufacturer[ azureiotaduDEVICE_INFO_MANUFACTURER_SIZE ];
    uint32_t ulManufacturerLength;

    const uint8_t ucModel[ azureiotaduDEVICE_INFO_MODEL_SIZE ];
    uint32_t ulModelLength;

    AzureIoTHubClientADUUpdateId_t xCurrentUpdateId;
} AzureIoTHubClientADUDeviceInformation_t;

/**
 * @brief ADU workflow struct.
 * Format:
 *    {
 *      "action": 3,
 *      "id": "someguid",
 *      "retryTimestamp": "2020-04-22T12:12:12.0000000+00:00"
 *  }
 *
 *  https://docs.microsoft.com/en-us/azure/iot-hub-device-update/understand-device-update#device-update-agent
 */
typedef struct AzureIoTHubClientADUWorkflow
{
    int32_t ulAction;

    const uint8_t ucID[ azureiotaduWORKFLOW_ID_SIZE ];
    uint32_t ulIDLength;

    const uint8_t ucRetryTimestamp[ azureiotaduWORKFLOW_RETRY_TIMESTAMP_SIZE ];
    uint32_t ulRetryTimestampLength;
} AzureIoTHubClientADUWorkflow_t;

typedef struct AzureIoTHubClientADUStepResult
{
    const uint8_t ucStepId[ azureiotaduSTEP_ID_SIZE ];
    uint32_t ulStepIdLength;

    uint32_t ulResultCode;
    uint32_t ulExtendedResultCode;

    const uint8_t ucResultDetails[ azureiotaduRESULT_DETAILS_SIZE ];
    uint32_t ulResultDetailsLength;
} AzureIoTHubClientADUStepResult_t;

typedef struct AzureIoTHubClientADUInstallResult
{
    int32_t lResultCode;
    int32_t lExtendedResultCode;

    const uint8_t ucResultDetails[ azureiotaduRESULT_DETAILS_SIZE ];
    uint32_t ulResultDetailsLength;

    AzureIoTHubClientADUStepResult_t * pxStepResults;
    uint32_t ulStepResultsCount;
} AzureIoTHubClientADUInstallResult_t;

/**
 * @brief Actions requested by the ADU Service
 *
 * Used to have `Install` (1) and `Apply` (2)
 *
 * Previously in the public preview protocol, the cloud would push a separate UpdateAction for each
 * individual step (e.g. download, install, apply) and the agent would report to the cloud after
 * every workflow step completion.
 * Each of these steps is now a local workflow step in the agent's workflow processing state machine.
 *
 * https://github.com/danewalton/iot-hub-device-update/blob/main/docs/agent-reference/goal-state-support.md#agent-based-vs-cloud-based-orchestration
 *
 * https://docs.microsoft.com/en-us/azure/iot-hub-device-update/device-update-plug-and-play#action
 *
 */
typedef enum AzureIoTADUAction
{
    eAzureIoTADUActionDownload = 0,
    eAzureIoTADUActionApplyDownload = 3,
    eAzureIoTADUActionCancel = 255
} AzureIoTADUAction_t;

/**
 * @brief States of the ADU agent
 *
 * State is reported in response to an Action
 *
 * Used to have `DownloadSucceeded` and `InstallSucceeded` but not anymore
 *
 * State:
 * https://docs.microsoft.com/en-us/azure/iot-hub-device-update/device-update-plug-and-play#state
 *
 * Action:
 * https://docs.microsoft.com/en-us/azure/iot-hub-device-update/device-update-plug-and-play#action
 *
 */
typedef enum AzureIoTADUState
{
    eAzureIoTADUStateIdle = 0,
    eAzureIoTADUStateDeploymentInProgress = 6,
    eAzureIotADUStateFailed = 255,
    eAzureIoTADUStateError,
} AzureIoTADUState_t;

/**
 * @brief Steps taken once the update is requested.
 *
 * Internal (not relevant to server) steps taken to go through update process.
 *
 */
typedef enum AzureIoTADUUpdateStepState
{
    eAzureIoTADUUpdateStepIdle = 0,
    eAzureIoTADUUpdateStepManifestDownloadStarted = 1,
    eAzureIoTADUUpdateStepManifestDownloadSucceeded = 2,
    eAzureIoTADUUpdateStepFirmwareDownloadStarted = 3,
    eAzureIoTADUUpdateStepFirmwareDownloadSucceeded = 4,
    eAzureIoTADUUpdateStepFirmwareInstallStarted = 5,
    eAzureIoTADUUpdateStepFirmwareInstallSucceeded = 6,
    eAzureIoTADUUpdateStepFirmwareApplyStarted = 7,
    eAzureIoTADUUpdateStepFirmwareApplySucceeded = 8,
    eAzureIoTADUUpdateStepFailed = 255
} AzureIoTADUUpdateStepState_t;

typedef AzureIoTResult_t (* AzureIoT_TransportConnectCallback_t)( AzureIoTTransportInterface_t * pxAzureIoTTransport,
                                                                  const char * pucURL );

/**
 * @brief Azure IoT ADU Client (ADU agent) to handle stages of the ADU process.
 */
typedef struct AzureIoTADUClient
{
    AzureIoTHubClient_t * pxHubClient;
    AzureIoTADUState_t xState;
    AzureIoTADUUpdateStepState_t xUpdateStepState;
    AzureIoTHTTP_t xHTTP;
    AzureADUImage_t xImage;
    AzureIoT_TransportConnectCallback_t xHTTPConnectCallback;
    AzureIoTTransportInterface_t * pxHTTPTransport;
    uint8_t * pucAduContextBuffer;
    uint32_t ulAduContextBufferLength;
    az_span xAduContextAvailableBuffer;
    const AzureIoTHubClientADUDeviceInformation_t * pxDeviceInformation;
    const AzureIoTHubClientADUInstallResult_t * pxLastInstallResult;
    az_iot_adu_ota_update_request xUpdateRequest;
    az_iot_adu_ota_update_manifest xUpdateManifest;
    /* TODO: remove this hack, and use xState instead. */
    bool xSendProperties;
} AzureIoTADUClient_t;

/**
 * @brief Callback which will be invoked to connect to the HTTP endpoint to download the new image.
 *
 */

/**
 * @brief Initializes the Azure IoT ADU Agent Client.
 *
 * @param[in] pxAduClient            The pointer to the #AzureIoTADUClient_t
 *                                   instance to initialize.
 * @param[in] pxAzureIoTHubClient    A pointer to the #AzureIoTHubClient_t,
 *                                   already initialized.
 * @param[in] pxHTTPTransport        An instance of #AzureIoTTransportInterface_t
 *                                   defining the I/O interface for the internal
 *                                   HTTP client used to download image files.
 * @param[in] pxAzureIoTHTTPConnectCallback
 *                                  A callback invoked by the ADU client API
 *                                  requesting the socket connection to be
 *                                  established for the internal HTTP client.
 * @param[in] pxDeviceInformation   A pointer to a
 *                                  #AzureIoTHubClientADUDeviceInformation_t
 *                                  structure with all the details of the device,
 *                                  as required by the ADU service.
 * @param[in] pxLastInstallResult   A pointer to a
 *                                  #AzureIoTHubClientADUInstallResult_t
 *                                  containing the results of the current or last
 *                                  update and its steps.
 * @param[in] pucAduContextBuffer   A pointer to the memory buffer to be used
 *                                  for parsing ADU service requests and
 *                                  composing the ADU agent reports.
 * @param[in] ulAduContextBuffer    The size of `pucAduContextBuffer`.
 * @return An #AzureIoTResult_t with the result of the operation.
 */
AzureIoTResult_t AzureIoTADUClient_Init( AzureIoTADUClient_t * pxAduClient,
                                         AzureIoTHubClient_t * pxAzureIoTHubClient,
                                         AzureIoTTransportInterface_t * pxHTTPTransport,
                                         AzureIoT_TransportConnectCallback_t pxAzureIoTHTTPConnectCallback,
                                         const AzureIoTHubClientADUDeviceInformation_t * pxDeviceInformation,
                                         const AzureIoTHubClientADUInstallResult_t * pxLastInstallResult,
                                         uint8_t * pucAduContextBuffer,
                                         uint32_t ulAduContextBuffer );

/**
 * @brief Process ADU Messages and iterate through ADU state machine.
 * @remark This function must be called frequently enough to properly
 *         allow the ADU client and state machine to perform the
 *         device updates when they are received. This is usually called
 *         side-by-side with AzureIoTHubClient_ProcessLoop.
 *
 * @param[in] pxAduClient The #AzureIoTADUClient_t * to use for this call.
 * @param[in] ulTimeoutMilliseconds Minimum time (in milliseconds) for the
 *                                  loop to run. If `0` is passed,
 *                                  it will only run once.
 * @return An #AzureIoTResult_t with the result of the operation.
 */
AzureIoTResult_t AzureIoTADUClient_ADUProcessLoop( AzureIoTADUClient_t * pxAduClient,
                                                   uint32_t ulTimeoutMilliseconds );

/**
 * @brief Updates the ADU Agent Client with ADU service device update properties.
 * @remark It must be called whenever writable properties are received containing
 *         ADU service properties (verified with AzureIoTADUClient_IsADUComponent).
 *         It effectively parses the properties (aka, the device update request)
 *         from ADU and sets the state machine to perform the update process if the
 *         the update request is applicable (e.g., if the version is not already
 *         installed).
 *         This function also provides the payload to acknowledge the ADU service
 *         Azure Plug-and-Play writable properties.
 *
 * @param[in] pxAduClient The #AzureIoTADUClient_t * to use for this call.
 * @param[in] pxReader  A #AzureIoTJSONReader_t initialized with the ADU
 *                      service writable properties json, set to the
 *                      beginning of the json object that is the value
 *                      of the ADU component.
 * @param[in] ulPropertyVersion Version of the writable properties.
 * @param[in] pucWritablePropertyResponseBuffer
 *              An pointer to the memory buffer where to
 *              write the resulting Azure Plug-and-Play properties acknowledgement
 *              payload.
 * @param[in] ulWritablePropertyResponseBufferSize
 *              Size of `pucWritablePropertyResponseBuffer`
 * @param[out] pulWritablePropertyResponseBufferLength
 *              Length of content writen into `pucWritablePropertyResponseBuffer`.
 * @return An #AzureIoTResult_t with the result of the operation.
 */
AzureIoTResult_t AzureIoTADUClient_ADUProcessComponent( AzureIoTADUClient_t * pxAduClient,
                                                        AzureIoTJSONReader_t * pxReader,
                                                        uint32_t ulPropertyVersion,
                                                        uint8_t * pucWritablePropertyResponseBuffer,
                                                        uint32_t ulWritablePropertyResponseBufferSize,
                                                        uint32_t * pulWritablePropertyResponseBufferLength );

/**
 * @brief Returns whether the component is the ADU component
 *
 * @note If it is, user should follow by parsing the component with the
 *       AzureIoTHubClient_ADUProcessComponent() call. The properties will be
 *       processed into the AzureIoTADUClient.
 *
 * @param[in] pxAduClient The pointer to the instance of #AzureIoTADUClient_t
 *                        previously initialized.
 * @param[in] pucComponentName Name of writable properties component to be
 *                             checked.
 * @param[in] ulComponentNameLength    Length of `pucComponentName`.
 * @return A boolean value indicating if the writable properties component
 *         is from ADU service.
 */
bool AzureIoTADUClient_IsADUComponent( AzureIoTADUClient_t * pxAduClient,
                                       const char * pucComponentName,
                                       uint32_t ulComponentNameLength );

/**
 * @brief Get the current state of the ADU Client.
 *
 * @param[in] pxAduClient The pointer to the instance of #AzureIoTADUClient_t
 *                        previously initialized.
 * @return A value of #AzureIoTADUUpdateStepState_t indicating the current state
 *         of the ADU client.
 */
AzureIoTADUUpdateStepState_t AzureIoTADUClient_GetState( AzureIoTADUClient_t * pxAduClient );

#endif /* AZURE_IOT_ADU_CLIENT_H */
