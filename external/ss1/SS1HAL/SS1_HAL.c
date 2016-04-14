/*****< SS1_HAL.c >******************************************************/
/*      Copyright 2013 - 2014 Stonestreet One.                          */
/*      All Rights Reserved.                                            */
/*                                                                      */
/*  SS1_HAL - Bluetoth Hardware Abstraction Layer module for Android for*/
/*            the Stonestreet One Bluetopia Platform Manager.           */
/*                                                                      */
/************************************************************************/

#define LOG_TAG "SS1HAL"

#include <hardware/bluetooth.h>
#include <cutils/properties.h>
#include <cutils/log.h>

#include "HALUtil.h"
#include "HALVend.h"
#include "SS1_HAL.h"

#include "SS1_SOCK.h"
#include "SS1_AV.h"
#include "SS1_RC.h"
#include "SS1_HF.h"
#include "SS1_HH.h"
#include "SS1_PAN.h"
#include "SS1_GATT.h"

#define DEFAULT_DISCOVERY_TIMEOUT_SECONDS       15
#define DEFAULT_DISCOVERABLE_TIMEOUT_SECONDS    120

#define BTSNOOP_FILE_DEFAULT_LOCATION "/sdcard/btsnoop_hci.log"

   /* Forward declarations                                              */
struct _tagAsyncTask_t;
struct _tagCallbackTask_t;

   /* Internal types                                                    */
typedef struct _tagProfileRecord_t
{
   char         *ProfileName;
   unsigned int  NameLength;
   const void   *(*Init)();
   void          (*Cleanup)();
} ProfileRecord_t;

typedef enum
{
   atReportKnownRemoteDevices
} AsyncTaskType_t;

typedef struct _tagAsyncTask_t
{
   AsyncTaskType_t Type;
   union
   {
      void *Reserved;
   } TaskData;
} AsyncTask_t;

typedef enum
{
   ctReportStateChanged,
   ctReportAdapterProperties,
   ctReportRemoteProperties,
   ctReportDeviceFound,
   ctReportDiscoveryStateChanged,
   ctIssuePinRequest,
   ctIssueSspRequest,
   ctReportBondStateChanged,
   ctReportConnectionStateChanged,
   ctReportHCIEvent,
   ctReportLeTestMode,
} CallbackTaskType_t;

typedef struct _tagCallbackTask_t
{
   CallbackTaskType_t Type;
   union
   {
      struct
      {
         bt_state_t AdapterState;
      } ReportStateChangedData;
      struct
      {
         bt_status_t    Status;
         unsigned int   NumberProperties;
         bt_property_t *Properties;
      } ReportAdapterPropertiesData;
      struct
      {
         bt_status_t    Status;
         bt_bdaddr_t    RemoteAddress;
         int            NumberProperties;
         bt_property_t *Properties;
      } ReportRemotePropertiesData;
      struct
      {
         int            NumberProperties;
         bt_property_t *Properties;
      } ReportDeviceFoundData;
      struct
      {
         bt_discovery_state_t State;
      } ReportDiscoveryStateChangedData;
      struct
      {
         bt_bdaddr_t RemoteAddress;
         bt_bdname_t Name;
         uint32_t    ClassOfDevice;
      } IssuePinRequestData;
      struct
      {
         bt_bdaddr_t      RemoteAddress;
         bt_bdname_t      Name;
         uint32_t         ClassOfDevice;
         bt_ssp_variant_t PairingVariant;
         uint32_t         Passkey;
      } IssueSspRequestData;
      struct
      {
         bt_status_t     Status;
         bt_bdaddr_t     RemoteAddress;
         bt_bond_state_t State;
      } ReportBondStateChangedData;
      struct
      {
         bt_status_t    Status;
         bt_bdaddr_t    RemoteAddress;
         bt_acl_state_t State;
      } ReportConnectionStateChangedData;
      struct
      {
         uint16_t  Opcode;
         uint8_t  *Buffer;
         uint8_t   Length;
      } ReportHCIEventData;
      struct
      {
         bt_status_t Status;
         uint16_t    NumberPackets;
      } ReportLeTestModeData;
   } TaskData;
} CallbackTask_t;

   /* Constants for use with the Collect*Properties() utility functions.*/
#define HAL_BT_PROP_BDNAME                      0x0001
#define HAL_BT_PROP_BDADDR                      0x0002
#define HAL_BT_PROP_UUIDS                       0x0004
#define HAL_BT_PROP_CLASS_OF_DEVICE             0x0008
#define HAL_BT_PROP_TYPE_OF_DEVICE              0x0010
#define HAL_BT_PROP_SERVICE_RECORD              0x0020
#define HAL_BT_PROP_ADAPTER_SCAN_MODE           0x0040
#define HAL_BT_PROP_ADAPTER_BONDED_DEVICES      0x0080
#define HAL_BT_PROP_ADAPTER_DISCOVERY_TIMEOUT   0x0100
#define HAL_BT_PROP_REMOTE_FRIENDLY_NAME        0x0200
#define HAL_BT_PROP_REMOTE_RSSI                 0x0400
#define HAL_BT_PROP_REMOTE_VERSION_INFO         0x0800
#define HAL_BT_PROP_REMOTE_DEVICE_TIMESTAMP     0x1000
#define HAL_BT_PROP_ALL                         0xFFFF

   /* Internal Variables                                                */
static volatile Boolean_t Initialized;

static Mutex_t HAL_ModuleMutex;

static bt_callbacks_t *BtCallbacks;

static ThreadHandle_t StackThread;
static Event_t        StackShutdown;

static TaskPool_t AsyncTaskPool;
static TaskPool_t CallbackTaskPool;

static pthread_once_t CallbackTaskPool_OnceInit = PTHREAD_ONCE_INIT;
static pthread_key_t  CallbackTaskPool_VMKey;

static unsigned int DEVMEventCallbackID;
static unsigned int DEVMAuthenticationCallbackID;

static unsigned int DiscoverableTimeout = DEFAULT_DISCOVERABLE_TIMEOUT_SECONDS;

DEVM_Authentication_Information_t *PendingAuthencationRequest;

ProfileRecord_t ProfileList[] =
{
   {BT_PROFILE_SOCKETS_ID,        sizeof(BT_PROFILE_SOCKETS_ID),        SS1SOCK_InitModule, SS1SOCK_CleanupModule},

#if SS1_SUPPORT_HFRE_HSP
   {BT_PROFILE_HANDSFREE_ID,      sizeof(BT_PROFILE_HANDSFREE_ID),      SS1HF_InitModule,   SS1HF_CleanupModule},
#endif

#if SS1_SUPPORT_A2DP
   {BT_PROFILE_ADVANCED_AUDIO_ID, sizeof(BT_PROFILE_ADVANCED_AUDIO_ID), SS1AV_InitModule,   SS1AV_CleanupModule},
#endif

#if SS1_SUPPORT_HEALTH
   {BT_PROFILE_HEALTH_ID,         sizeof(BT_PROFILE_HEALTH_ID),         NULL,               NULL},
#endif

#if SS1_SUPPORT_HID
   {BT_PROFILE_HIDHOST_ID,        sizeof(BT_PROFILE_HIDHOST_ID),        SS1HH_InitModule,   SS1HH_CleanupModule},
#endif

#if SS1_SUPPORT_PAN
   {BT_PROFILE_PAN_ID,            sizeof(BT_PROFILE_PAN_ID),            SS1PAN_InitModule,  SS1PAN_CleanupModule},
#endif

#if SS1_PLATFORM_SDK_VERSION >= 18

#if SS1_SUPPORT_GATT
   {BT_PROFILE_GATT_ID,           sizeof(BT_PROFILE_GATT_ID),           SS1GATT_InitModule, SS1GATT_CleanupModule},
#endif

#if SS1_SUPPORT_A2DP
   {BT_PROFILE_AV_RC_ID,          sizeof(BT_PROFILE_AV_RC_ID),          SS1RC_InitModule,   SS1RC_CleanupModule},
#endif

#endif /* SS1_PLATFORM_SDK_VERSION */
};

   /* Internal function prototypes.                                     */
static Boolean_t StorePendingAuthCopy(const DEVM_Authentication_Information_t *AuthRequestInfo);
static Boolean_t RetrievePendingAuth(DEVM_Authentication_Information_t *AuthInfo);

static bt_property_t *PackBluetoothProperties(bt_property_t *PropertyList, unsigned int NumberProperties);
static unsigned int CollectLocalProperties(Word_t PropertyMask, DEVM_Local_Device_Properties_t *LocalProps, bt_property_t **PropListPtr);
static unsigned int CollectRemoteProperties(Word_t PropertyMask, DEVM_Remote_Device_Properties_t *RemoteProps, bt_property_t **PropListPtr);

static unsigned int QueryRemoteServices(BD_ADDR_t BD_ADDR, bt_uuid_t **ServiceList);

static void HAL_AsyncTaskHandler(void *Task);
static void HAL_CallbackTaskHandler(void *Task);
static void HAL_CallbackTaskHandler_Init(void);
static void HAL_CallbackTaskHandler_Cleanup(void *Param);

static void HAL_DEVMEventCallback(DEVM_Event_Data_t *EventData, void *CallbackParameter);
static void HAL_DEVMAuthCallback(DEVM_Authentication_Information_t *AuthRequestInfo, void *CallbackParameter);
static void BTPMDispatchCallback_Initialization(void *CallbackParameter);

static void *BluetopiaPlatformManagerMainThread(void *Parameter);

static int SS1HAL_Init(bt_callbacks_t *Callbacks);
static int SS1HAL_Enable(void);
static int SS1HAL_Disable(void);
static void SS1HAL_Cleanup(void);
static int SS1HAL_GetAdapterProperties(void);
static int SS1HAL_GetAdapterProperty(bt_property_type_t PropertyType);
static int SS1HAL_SetAdapterProperty(const bt_property_t *Property);
static int SS1HAL_GetRemoteDeviceProperties(bt_bdaddr_t *RemoteAddress);
static int SS1HAL_GetRemoteDeviceProperty(bt_bdaddr_t *RemoteAddress, bt_property_type_t PropertyType);
static int SS1HAL_SetRemoteDeviceProperty(bt_bdaddr_t *RemoteAddress, const bt_property_t *Property);
static int SS1HAL_GetRemoteServiceRecord(bt_bdaddr_t *RemoteAddress, bt_uuid_t *Uuid);
static int SS1HAL_GetRemoteServices(bt_bdaddr_t *RemoteAddress);
static int SS1HAL_StartDiscovery(void);
static int SS1HAL_CancelDiscovery(void);
static int SS1HAL_CreateBond(const bt_bdaddr_t *RemoteAddress);
static int SS1HAL_RemoveBond(const bt_bdaddr_t *RemoteAddress);
static int SS1HAL_CancelBond(const bt_bdaddr_t *RemoteAddress);
static int SS1HAL_PinReply(const bt_bdaddr_t *RemoteAddress, uint8_t Accept, uint8_t PinLength, bt_pin_code_t *PinCode);
static int SS1HAL_SspReply(const bt_bdaddr_t *RemoteAddress, bt_ssp_variant_t Variant, uint8_t Accept, uint32_t Passkey);
static const void *SS1HAL_GetProfileInterface(const char *ProfileID);
static int SS1HAL_DutModeConfigure(uint8_t Enable);
static int SS1HAL_DutModeSend(uint16_t Opcode, uint8_t *Buffer, uint8_t Length);
static int SS1HAL_LowEnergyTestMode(uint16_t Opcode, uint8_t *Buffer, uint8_t Length);
static int SS1HAL_OpenModule(const struct hw_module_t *Module, char const *Name, struct hw_device_t **HardwareDevice);
static int SS1HAL_CloseModule(struct hw_device_t *HardwareDevice);
static const bt_interface_t *SS1HAL_GetBluetoothInterface();

Boolean_t SS1HAL_AcquireLock(unsigned int Timeout)
{
   Boolean_t ret_val;

   SS1_LOGD("Enter");

   ret_val = BTPS_WaitMutex(HAL_ModuleMutex, Timeout);

   SS1_LOGD("Exit");
   
   return(ret_val);
}

void SS1HAL_ReleaseLock()
{
   SS1_LOGD("Enter");

   BTPS_ReleaseMutex(HAL_ModuleMutex);

   SS1_LOGD("Exit");
}

static Boolean_t StorePendingAuthCopy(const DEVM_Authentication_Information_t *AuthRequestInfo)
{
   Boolean_t ret_val = FALSE;

   if(AuthRequestInfo)
   {
      if(SS1HAL_AcquireLock(BTPS_INFINITE_WAIT))
      {
         if(PendingAuthencationRequest == NULL)
         {
            if((PendingAuthencationRequest = (DEVM_Authentication_Information_t *)BTPS_AllocateMemory(sizeof(DEVM_Authentication_Information_t))) != NULL)
            {
               BTPS_MemCopy(PendingAuthencationRequest, AuthRequestInfo, sizeof(DEVM_Authentication_Information_t));

               ret_val = TRUE;
            }
         }

         SS1HAL_ReleaseLock();
      }
   }

   return(ret_val);
}

static Boolean_t RetrievePendingAuth(DEVM_Authentication_Information_t *AuthInfo)
{
   Boolean_t ret_val = FALSE;

   if(SS1HAL_AcquireLock(BTPS_INFINITE_WAIT))
   {
      if(PendingAuthencationRequest)
      {
         if(AuthInfo)
            BTPS_MemCopy(AuthInfo, PendingAuthencationRequest, sizeof(DEVM_Authentication_Information_t));

         BTPS_FreeMemory(PendingAuthencationRequest);

         PendingAuthencationRequest = NULL;

         ret_val = TRUE;
      }

      SS1HAL_ReleaseLock();
   }

   return(ret_val);
}

static bt_property_t *PackBluetoothProperties(bt_property_t *Properties, unsigned int NumberProperties)
{
   void          *Buffer;
   Byte_t        *NextVal;
   unsigned int   Index;
   unsigned int   BufferSize;
   bt_property_t *PropList;

   Buffer = NULL;

   if((Properties) && (NumberProperties))
   {
      BufferSize = 0;

      /* Count the total number of bytes required for this property     */
      /* list.                                                          */
      for(Index = 0; Index < NumberProperties; Index++)
      {
         BufferSize += sizeof(Properties[Index]);
         BufferSize += Properties[Index].len;
      }

      if((BufferSize) && ((Buffer = (Byte_t *)BTPS_AllocateMemory(BufferSize)) != NULL))
      {
         PropList = (bt_property_t *)Buffer;
         NextVal  = (Byte_t *)(((bt_property_t *)Buffer) + NumberProperties);

         for(Index = 0; Index < NumberProperties; Index++)
         {
            PropList[Index].type = Properties[Index].type;
            PropList[Index].len  = Properties[Index].len;
            PropList[Index].val  = NextVal;

            BTPS_MemCopy(NextVal, Properties[Index].val, Properties[Index].len);

            NextVal += Properties[Index].len;
         }
      }
   }

   return((bt_property_t *)Buffer);
}

static unsigned int CollectLocalProperties(Word_t PropertyMask, DEVM_Local_Device_Properties_t *LocalProps, bt_property_t **PropListPtr)
{
   uint32_t           ClassOfDevice;
   uint32_t           DiscoveryTimeout;
   bt_uuid_t         *UuidList;
   BD_ADDR_t         *BD_ADDRList;
   bt_bdaddr_t        DeviceAddress;
   bt_bdaddr_t       *PairedDevices;
   bt_bdname_t        DeviceName;
   unsigned int       Index;
   unsigned int       PropIndex;
   unsigned int       NumberDevices;
   bt_property_t      PropList[16];
   bt_scan_mode_t     ScanMode;
   bt_device_type_t   DeviceType;
   Class_of_Device_t  NullClassOfDevice;

   //XXX Temporary until dynamic local service querying is supported
   bt_uuid_t          TempLocalUuidList[] = { {{0x00, 0x00, 0x11, 0x0A, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}}, /* Audio Source */
                                              {{0x00, 0x00, 0x11, 0x1F, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}}, /* HF Audio Gateway */
                                              {{0x00, 0x00, 0x11, 0x12, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}}, /* HS Audio Gateway */
                                              {{0x00, 0x00, 0x11, 0x24, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}}, /* HID */
                                              {{0x00, 0x00, 0x11, 0x15, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}}, /* PAN PANU */
                                              {{0x00, 0x00, 0x11, 0x16, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}}, /* PAN NAP */
                                            //{{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}},
                                            };

   PropIndex = 0;

   /* Check that the parameters appear valid.                           */
   if((LocalProps) && (PropListPtr))
   {
      /* Initialize buffer pointers.                                    */
      UuidList      = NULL;
      PairedDevices = NULL;

      PropIndex     = 0;

      /* Bluetooth Device Name                                          */
      if(PropertyMask & HAL_BT_PROP_BDNAME)
      {
         PropList[PropIndex].type = BT_PROPERTY_BDNAME;
         PropList[PropIndex].len  = LocalProps->DeviceNameLength;
         PropList[PropIndex].val  = &DeviceName;

         BTPS_MemInitialize(&DeviceName, 0, sizeof(DeviceName));
         BTPS_MemCopy(DeviceName.name, LocalProps->DeviceName, LocalProps->DeviceNameLength);

         SS1_LOGD("Added: Adapter Name: %.*s", PropList[PropIndex].len, (char *)(DeviceName.name));

         PropIndex++;
      }

      /* Bluetooth Device Address                                       */
      if(PropertyMask & HAL_BT_PROP_BDADDR)
      {
         PropList[PropIndex].type = BT_PROPERTY_BDADDR;
         PropList[PropIndex].len  = sizeof(bt_bdaddr_t);
         PropList[PropIndex].val  = &DeviceAddress;

         ConvertBluetopiaAddrToAndroid(&(LocalProps->BD_ADDR), &(DeviceAddress));

         SS1_LOGD("Added: Adapter Address: %02X:%02X:%02X:%02X:%02X:%02X", DeviceAddress.address[0], DeviceAddress.address[1], DeviceAddress.address[2], DeviceAddress.address[3], DeviceAddress.address[4], DeviceAddress.address[5]);

         PropIndex++;
      }

      /* UUIDs                                                          */
      if(PropertyMask & HAL_BT_PROP_UUIDS)
      {
         PropList[PropIndex].type = BT_PROPERTY_UUIDS;
         PropList[PropIndex].len  = 0;
         PropList[PropIndex].val  = NULL;

         /* XXX Currently assigning fixed set of supported profiles.    */
         PropList[PropIndex].len = sizeof(TempLocalUuidList);
         PropList[PropIndex].val = TempLocalUuidList;

         //XXX FIXME
         UuidList = NULL;

         SS1_LOGD("Added: Services");

         PropIndex++;
      }

      /* Class of Device                                                */
      if(PropertyMask & HAL_BT_PROP_CLASS_OF_DEVICE)
      {
         PropList[PropIndex].type = BT_PROPERTY_CLASS_OF_DEVICE;
         PropList[PropIndex].len  = sizeof(uint32_t);
         PropList[PropIndex].val  = &ClassOfDevice;

         ConvertBluetopiaCoDToAndroid(&(LocalProps->ClassOfDevice), &(ClassOfDevice));

         SS1_LOGD("Added: Class of Device: 0x%08X", ClassOfDevice);

         PropIndex++;
      }

      /* Type of Device                                                 */
      if(PropertyMask & HAL_BT_PROP_TYPE_OF_DEVICE)
      {
         PropList[PropIndex].type = BT_PROPERTY_TYPE_OF_DEVICE;
         PropList[PropIndex].len  = sizeof(bt_device_type_t);
         PropList[PropIndex].val  = &DeviceType;
         
         DeviceType = ((LocalProps->LocalDeviceFlags & DEVM_LOCAL_DEVICE_FLAGS_DEVICE_SUPPORTS_LOW_ENERGY) ? BT_DEVICE_DEVTYPE_DUAL : BT_DEVICE_DEVTYPE_BREDR);

         SS1_LOGD("Added: Type of Device: %d", DeviceType);

         PropIndex++;
      }

      /* Service Record                                                 */
      if(PropertyMask & HAL_BT_PROP_SERVICE_RECORD)
      {
         /* **NOTE ** Android does not currently use the                */
         /*           BT_PROPERTY_SERVICE_RECORD property.              */
      }

      /* Scan Mode                                                      */
      if(PropertyMask & HAL_BT_PROP_ADAPTER_SCAN_MODE)
      {
         PropList[PropIndex].type = BT_PROPERTY_ADAPTER_SCAN_MODE;
         PropList[PropIndex].len  = sizeof(bt_scan_mode_t);
         PropList[PropIndex].val = &ScanMode;

         ScanMode = (LocalProps->ConnectableMode ? (LocalProps->DiscoverableMode ? BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE : BT_SCAN_MODE_CONNECTABLE) : BT_SCAN_MODE_NONE);

         SS1_LOGD("Added: Scan Mode: %d", ScanMode);

         PropIndex++;
      }

      /* Paired Devices                                                 */
      if(PropertyMask & HAL_BT_PROP_ADAPTER_BONDED_DEVICES)
      {
         PropList[PropIndex].type = BT_PROPERTY_ADAPTER_BONDED_DEVICES;
         PropList[PropIndex].len  = 0;
         PropList[PropIndex].val  = NULL;

         ASSIGN_CLASS_OF_DEVICE(NullClassOfDevice, 0, 0, 0);

         if(DEVM_QueryRemoteDeviceList(DEVM_QUERY_REMOTE_DEVICE_LIST_CURRENTLY_PAIRED, NullClassOfDevice, 0, NULL, &NumberDevices) >= 0)
         {
            if(NumberDevices > 0)
            {
               if((BD_ADDRList = (BD_ADDR_t *)BTPS_AllocateMemory(sizeof(BD_ADDR_t) * NumberDevices)) != NULL)
               {
                  if(DEVM_QueryRemoteDeviceList(DEVM_QUERY_REMOTE_DEVICE_LIST_CURRENTLY_PAIRED, NullClassOfDevice, NumberDevices, BD_ADDRList, NULL) >= 0)
                  {
                     if((PairedDevices = (bt_bdaddr_t *)BTPS_AllocateMemory(sizeof(bt_bdaddr_t) * NumberDevices)) != NULL)
                     {
                        for(Index = 0; Index < NumberDevices; Index++)
                        {
                           ConvertBluetopiaAddrToAndroid(&(BD_ADDRList[Index]), &(PairedDevices[Index]));

                           SS1_LOGD("Added: Paired Device: %02X:%02X:%02X:%02X:%02X:%02X", PairedDevices[Index].address[0], PairedDevices[Index].address[1], PairedDevices[Index].address[2], PairedDevices[Index].address[3], PairedDevices[Index].address[4], PairedDevices[Index].address[5]);
                        }

                        PropList[PropIndex].len = (sizeof(bt_bdaddr_t) * NumberDevices);
                        PropList[PropIndex].val = PairedDevices;

                        PropIndex++;
                     }
                  }

                  BTPS_FreeMemory(BD_ADDRList);
               }
            }
         }
      }

      /* Discovery Timeout                                              */
      if(PropertyMask & HAL_BT_PROP_ADAPTER_DISCOVERY_TIMEOUT)
      {
         PropList[PropIndex].type = BT_PROPERTY_ADAPTER_DISCOVERY_TIMEOUT;
         PropList[PropIndex].len  = sizeof(uint32_t);
         PropList[PropIndex].val  = &DiscoveryTimeout;

         DiscoveryTimeout = DiscoverableTimeout;

         SS1_LOGD("Added: Discoverable Timeout: %u", DiscoveryTimeout);

         PropIndex++;
      }

      /* Pack the properties into a single buffer.                      */
      *PropListPtr = PackBluetoothProperties(PropList, PropIndex);

      if(UuidList)
         BTPS_FreeMemory(UuidList);

      if(PairedDevices)
         BTPS_FreeMemory(PairedDevices);
   }

   /* Return the number of parameters added to the list.                */
   return(PropIndex);
}

static unsigned int CollectRemoteProperties(Word_t PropertyMask, DEVM_Remote_Device_Properties_t *RemoteProps, bt_property_t **PropListPtr)
{
   uint32_t          ClassOfDevice;
   bt_uuid_t        *UuidList;
   bt_bdaddr_t       DeviceAddress;
   bt_bdaddr_t      *PairedDevices;
   bt_bdname_t       DeviceName;
   bt_bdname_t       FriendlyDeviceName;
   unsigned int      PropIndex;
   unsigned int      RemoteRSSI;
   bt_property_t     PropList[16];
   bt_device_type_t  DeviceType;

   PropIndex = 0;

   /* Check that the parameters appear valid.                           */
   if((RemoteProps) && (PropListPtr))
   {
      /* Initialize buffer pointers.                                    */
      UuidList      = NULL;
      PairedDevices = NULL;

      PropIndex     = 0;

      /* Bluetooth Device Name                                          */
      if((PropertyMask & HAL_BT_PROP_BDNAME) && (RemoteProps->RemoteDeviceFlags & DEVM_REMOTE_DEVICE_FLAGS_DEVICE_NAME_KNOWN))
      {
         PropList[PropIndex].type = BT_PROPERTY_BDNAME;
         PropList[PropIndex].len  = RemoteProps->DeviceNameLength;
         PropList[PropIndex].val  = &DeviceName;

         BTPS_MemInitialize(&DeviceName, 0, sizeof(DeviceName));
         BTPS_MemCopy(DeviceName.name, RemoteProps->DeviceName, RemoteProps->DeviceNameLength);

         SS1_LOGD("Added: Device Name: %.*s", PropList[PropIndex].len, (char *)(DeviceName.name));

         PropIndex++;
      }

      /* Bluetooth Device Address                                       */
      if(PropertyMask & HAL_BT_PROP_BDADDR)
      {
         PropList[PropIndex].type = BT_PROPERTY_BDADDR;
         PropList[PropIndex].len  = sizeof(bt_bdaddr_t);
         PropList[PropIndex].val  = &DeviceAddress;

         ConvertBluetopiaAddrToAndroid(&(RemoteProps->BD_ADDR), &(DeviceAddress));

         SS1_LOGD("Added: Device Address: %02X:%02X:%02X:%02X:%02X:%02X", DeviceAddress.address[0], DeviceAddress.address[1], DeviceAddress.address[2], DeviceAddress.address[3], DeviceAddress.address[4], DeviceAddress.address[5]);

         PropIndex++;
      }

      /* UUIDs                                                          */
      if(PropertyMask & HAL_BT_PROP_UUIDS)
      {
         PropList[PropIndex].type = BT_PROPERTY_UUIDS;
         PropList[PropIndex].len  = QueryRemoteServices(RemoteProps->BD_ADDR, (bt_uuid_t **)(&(PropList[PropIndex].val)));

         if(PropList[PropIndex].len > 0)
         {
            /* Fix len to represent the total bytes in val.             */
            PropList[PropIndex].len = (sizeof(bt_uuid_t) * PropList[PropIndex].len);

            SS1_LOGD("Added: Services");

            PropIndex++;
         }
      }

      /* Class of Device                                                */
      if(PropertyMask & HAL_BT_PROP_CLASS_OF_DEVICE)
      {
         PropList[PropIndex].type = BT_PROPERTY_CLASS_OF_DEVICE;
         PropList[PropIndex].len  = sizeof(uint32_t);
         PropList[PropIndex].val  = &ClassOfDevice;

         ConvertBluetopiaCoDToAndroid(&(RemoteProps->ClassOfDevice), &(ClassOfDevice));

         SS1_LOGD("Added: Class of Device: 0x%08X", ClassOfDevice);

         PropIndex++;
      }

      /* Type of Device                                                 */
      if(PropertyMask & HAL_BT_PROP_TYPE_OF_DEVICE)
      {
         PropList[PropIndex].type = BT_PROPERTY_TYPE_OF_DEVICE;
         PropList[PropIndex].len  = sizeof(bt_device_type_t);
         PropList[PropIndex].val  = &DeviceType;
         
         if(RemoteProps->RemoteDeviceFlags & DEVM_REMOTE_DEVICE_FLAGS_DEVICE_SUPPORTS_LOW_ENERGY)
         {
            if(RemoteProps->RemoteDeviceFlags & DEVM_REMOTE_DEVICE_FLAGS_DEVICE_SUPPORTS_BR_EDR)
               DeviceType = BT_DEVICE_DEVTYPE_DUAL;
            else
               DeviceType = BT_DEVICE_DEVTYPE_BLE;
         }
         else
            DeviceType = BT_DEVICE_DEVTYPE_BREDR;

         SS1_LOGD("Added: Type of Device: %d", DeviceType);

         PropIndex++;
      }

      /* Service Record                                                 */
      if(PropertyMask & HAL_BT_PROP_SERVICE_RECORD)
      {
         /* **NOTE ** Android does not currently use the                */
         /*           BT_PROPERTY_SERVICE_RECORD property.              */
      }

      /* Friendly name.                                                 */
      if(PropertyMask & HAL_BT_PROP_REMOTE_FRIENDLY_NAME)
      {
         PropList[PropIndex].type = BT_PROPERTY_REMOTE_FRIENDLY_NAME;

         if((RemoteProps->RemoteDeviceFlags & DEVM_REMOTE_DEVICE_FLAGS_DEVICE_APPLICATION_DATA_VALID) && (RemoteProps->ApplicationData.FriendlyNameLength > 0))
         {
            /* A Friendly Name as been assigned to this device,         */
            /* previously.                                              */
            PropList[PropIndex].len = RemoteProps->ApplicationData.FriendlyNameLength;

            if(RemoteProps->ApplicationData.FriendlyNameLength > 0)
            {
               PropList[PropIndex].val = &FriendlyDeviceName;

               BTPS_MemInitialize(&FriendlyDeviceName, 0, sizeof(FriendlyDeviceName));
               BTPS_MemCopy(FriendlyDeviceName.name, RemoteProps->ApplicationData.FriendlyName, RemoteProps->ApplicationData.FriendlyNameLength);
            }

            SS1_LOGD("Added: Friendly Name: %.*s", PropList[PropIndex].len, (char *)(FriendlyDeviceName.name));

            PropIndex++;
         }
      }

      if(PropertyMask & HAL_BT_PROP_REMOTE_RSSI)
      {
         PropList[PropIndex].type = BT_PROPERTY_REMOTE_RSSI;
         PropList[PropIndex].len  = sizeof(int32_t);
         PropList[PropIndex].val  = &RemoteRSSI;

         /* Use the BR/EDR RSSI, by default. Only use the LE RSSI if    */
         /* BR/EDR is not supported.                                    */
         if(RemoteProps->RemoteDeviceFlags & DEVM_REMOTE_DEVICE_FLAGS_DEVICE_SUPPORTS_BR_EDR)
            RemoteRSSI = RemoteProps->RSSI;
         else
            RemoteRSSI = RemoteProps->LE_RSSI;

         SS1_LOGD("Added: Remote RSSI: %u", RemoteRSSI);

         PropIndex++;
      }

      if(PropertyMask & HAL_BT_PROP_REMOTE_VERSION_INFO)
      {
         /* **NOTE ** Android does not currently use the                */
         /*           BT_PROPERTY_REMOTE_VERSION_INFO property.         */
      }

      if(PropertyMask & HAL_BT_PROP_REMOTE_DEVICE_TIMESTAMP)
      {
         /* **NOTE ** Android does not currently use the                */
         /*           BT_PROPERTY_REMOTE_DEVICE_TIMESTAMP property.     */
      }

      /* Pack the properties into a single buffer.                      */
      *PropListPtr = PackBluetoothProperties(PropList, PropIndex);

      if(UuidList)
         BTPS_FreeMemory(UuidList);

      if(PairedDevices)
         BTPS_FreeMemory(PairedDevices);
   }

   /* Return the number of parameters added to the list.                */
   return(PropIndex);
}

static unsigned int QueryRemoteServices(BD_ADDR_t BD_ADDR, bt_uuid_t **ServiceList)
{
   int                          Result;
   UUID_16_t                    TempUUID_16;
   UUID_32_t                    TempUUID_32;
   UUID_128_t                   TempUUID_128;
   unsigned int                 Index;
   unsigned int                 ServiceCount;
   unsigned int                 LEServicesLength;
   unsigned int                 TotalSDPServices;
   unsigned char               *LEServicesBuffer;
   SDP_UUID_Entry_t            *SDPServices;
   DEVM_Parsed_Services_Data_t  ParsedServices;

   ServiceCount = 0;

   if((!COMPARE_NULL_BD_ADDR(BD_ADDR)) && (ServiceList))
   {
      /* Count total number of services.                                */
      if(DEVM_QueryRemoteDeviceServiceClasses(BD_ADDR, 0, NULL, &TotalSDPServices) < 0)
      {
         /* Failed to retreive any SDP services.                        */
         TotalSDPServices = 0;
      }

      if(DEVM_QueryRemoteDeviceServices(BD_ADDR, DEVM_QUERY_REMOTE_DEVICE_SERVICES_FLAGS_LOW_ENERGY, 0, NULL, &LEServicesLength) < 0)
      {
         /* Either no LE services exist or they are unknown.            */
         LEServicesLength = 0;

         if(LEServicesLength == 0)
         {
            //XXX Try to use advertising data to get LE services
         }
      }

      if((TotalSDPServices) || (LEServicesLength))
      {
         BTPS_MemInitialize(&ParsedServices, 0, sizeof(ParsedServices));

         if(LEServicesLength)
         {
            if((LEServicesBuffer = BTPS_AllocateMemory(LEServicesLength)) != NULL)
            {
               if((Result = DEVM_QueryRemoteDeviceServices(BD_ADDR, DEVM_QUERY_REMOTE_DEVICE_SERVICES_FLAGS_LOW_ENERGY, LEServicesLength, LEServicesBuffer, NULL)) > 0)
               {
                  LEServicesLength = (unsigned int)Result;

                  if(DEVM_ConvertRawServicesStreamToParsedServicesData(LEServicesLength, LEServicesBuffer, &ParsedServices) != 0)
                  {
                     /* Unable to parse services data.  Free allocated  */
                     /* resources now.                                  */
                     BTPS_FreeMemory(LEServicesBuffer);

                     LEServicesBuffer = NULL;
                     LEServicesLength = 0;
                  }
               }
               else
               {
                  /* Unable to access services data.  Free allocated    */
                  /* resources now.                                     */
                  BTPS_FreeMemory(LEServicesBuffer);

                  LEServicesBuffer = NULL;
                  LEServicesLength = 0;
               }
            }
         }
         else
            LEServicesBuffer = NULL;

         if(TotalSDPServices)
         {
            if((SDPServices = (SDP_UUID_Entry_t *)BTPS_AllocateMemory(sizeof(SDP_UUID_Entry_t) * TotalSDPServices)) != NULL)
            {
               if((Result = DEVM_QueryRemoteDeviceServiceClasses(BD_ADDR, TotalSDPServices, SDPServices, NULL)) > 0)
               {
                  TotalSDPServices = (unsigned int)Result;
               }
               else
               {
                  /* No services returned.  Free allocated resources.   */
                  BTPS_FreeMemory(SDPServices);

                  SDPServices      = NULL;
                  TotalSDPServices = 0;
               }
            }
         }
         else
            SDPServices = NULL;

         if(((TotalSDPServices) && (SDPServices)) || ((LEServicesLength) && (ParsedServices.NumberServices)))
         {
            if((*ServiceList = BTPS_AllocateMemory(sizeof(bt_uuid_t) * (TotalSDPServices + ParsedServices.NumberServices))) != NULL)
            {
               BTPS_MemInitialize(*ServiceList, 0, (sizeof(bt_uuid_t) * (TotalSDPServices + ParsedServices.NumberServices)));

               ServiceCount = 0;

               /* Load SDP services into the list.                      */
               if((TotalSDPServices) && (SDPServices))
               {
                  for(Index = 0; Index < TotalSDPServices; Index++)
                  {
                     switch(SDPServices[Index].SDP_Data_Element_Type)
                     {
                        case deUUID_128:
                           BTPS_MemCopy((*ServiceList)[ServiceCount].uu, &(SDPServices[Index].UUID_Value.UUID_128), UUID_128_SIZE);

                           SS1_LOGD("Added: Service (%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X)", SDPServices[Index].UUID_Value.UUID_128.UUID_Byte0, SDPServices[Index].UUID_Value.UUID_128.UUID_Byte1, SDPServices[Index].UUID_Value.UUID_128.UUID_Byte2, SDPServices[Index].UUID_Value.UUID_128.UUID_Byte3, SDPServices[Index].UUID_Value.UUID_128.UUID_Byte4, SDPServices[Index].UUID_Value.UUID_128.UUID_Byte5, SDPServices[Index].UUID_Value.UUID_128.UUID_Byte6, SDPServices[Index].UUID_Value.UUID_128.UUID_Byte7, SDPServices[Index].UUID_Value.UUID_128.UUID_Byte8, SDPServices[Index].UUID_Value.UUID_128.UUID_Byte9, SDPServices[Index].UUID_Value.UUID_128.UUID_Byte10, SDPServices[Index].UUID_Value.UUID_128.UUID_Byte11, SDPServices[Index].UUID_Value.UUID_128.UUID_Byte12, SDPServices[Index].UUID_Value.UUID_128.UUID_Byte13, SDPServices[Index].UUID_Value.UUID_128.UUID_Byte14, SDPServices[Index].UUID_Value.UUID_128.UUID_Byte15);
                           ServiceCount++;
                           break;

                        case deUUID_32:
                           SDP_ASSIGN_BASE_UUID(TempUUID_128);
                           ASSIGN_SDP_UUID_32_TO_SDP_UUID_128(TempUUID_128, SDPServices[Index].UUID_Value.UUID_32);
                           ConvertBluetopiaUUIDToAndroid(&TempUUID_128, (*ServiceList)[ServiceCount].uu);

                           SS1_LOGD("Added: Service (%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X)", TempUUID_128.UUID_Byte0, TempUUID_128.UUID_Byte1, TempUUID_128.UUID_Byte2, TempUUID_128.UUID_Byte3, TempUUID_128.UUID_Byte4, TempUUID_128.UUID_Byte5, TempUUID_128.UUID_Byte6, TempUUID_128.UUID_Byte7, TempUUID_128.UUID_Byte8, TempUUID_128.UUID_Byte9, TempUUID_128.UUID_Byte10, TempUUID_128.UUID_Byte11, TempUUID_128.UUID_Byte12, TempUUID_128.UUID_Byte13, TempUUID_128.UUID_Byte14, TempUUID_128.UUID_Byte15);
                           ServiceCount++;
                           break;

                        case deUUID_16:
                           SDP_ASSIGN_BASE_UUID(TempUUID_128);
                           ASSIGN_SDP_UUID_16_TO_SDP_UUID_128(TempUUID_128, SDPServices[Index].UUID_Value.UUID_16);
                           ConvertBluetopiaUUIDToAndroid(&TempUUID_128, (*ServiceList)[ServiceCount].uu);

                           SS1_LOGD("Added: Service (%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X)", TempUUID_128.UUID_Byte0, TempUUID_128.UUID_Byte1, TempUUID_128.UUID_Byte2, TempUUID_128.UUID_Byte3, TempUUID_128.UUID_Byte4, TempUUID_128.UUID_Byte5, TempUUID_128.UUID_Byte6, TempUUID_128.UUID_Byte7, TempUUID_128.UUID_Byte8, TempUUID_128.UUID_Byte9, TempUUID_128.UUID_Byte10, TempUUID_128.UUID_Byte11, TempUUID_128.UUID_Byte12, TempUUID_128.UUID_Byte13, TempUUID_128.UUID_Byte14, TempUUID_128.UUID_Byte15);
                           ServiceCount++;
                           break;

                        default:
                           break;
                     }
                  }
               }

#if BTPM_CONFIGURATION_DEVICE_MANAGER_SUPPORT_LOW_ENERGY

               /* Load LE services into the list.                       */
               if((LEServicesLength) && (ParsedServices.NumberServices))
               {
                  for(Index = 0; Index < ParsedServices.NumberServices; Index++)
                  {
                     switch(ParsedServices.GATTServiceDiscoveryIndicationData[Index].ServiceInformation.UUID.UUID_Type)
                     {
                        case guUUID_128:
                           CONVERT_BLUETOOTH_UUID_128_TO_SDP_UUID_128(TempUUID_128, ParsedServices.GATTServiceDiscoveryIndicationData[Index].ServiceInformation.UUID.UUID.UUID_128);
                           ConvertBluetopiaUUIDToAndroid(&TempUUID_128, (*ServiceList)[ServiceCount].uu);

                           SS1_LOGD("Added: Service (%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X)", TempUUID_128.UUID_Byte0, TempUUID_128.UUID_Byte1, TempUUID_128.UUID_Byte2, TempUUID_128.UUID_Byte3, TempUUID_128.UUID_Byte4, TempUUID_128.UUID_Byte5, TempUUID_128.UUID_Byte6, TempUUID_128.UUID_Byte7, TempUUID_128.UUID_Byte8, TempUUID_128.UUID_Byte9, TempUUID_128.UUID_Byte10, TempUUID_128.UUID_Byte11, TempUUID_128.UUID_Byte12, TempUUID_128.UUID_Byte13, TempUUID_128.UUID_Byte14, TempUUID_128.UUID_Byte15);
                           ServiceCount++;
                           break;

                        case guUUID_16:
                           CONVERT_BLUETOOTH_UUID_16_TO_SDP_UUID_16(TempUUID_16, ParsedServices.GATTServiceDiscoveryIndicationData[Index].ServiceInformation.UUID.UUID.UUID_16);
                           SDP_ASSIGN_BASE_UUID(TempUUID_128);
                           ASSIGN_SDP_UUID_16_TO_SDP_UUID_128(TempUUID_128, TempUUID_16);
                           ConvertBluetopiaUUIDToAndroid(&TempUUID_128, (*ServiceList)[ServiceCount].uu);

                           SS1_LOGD("Added: Service (%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X)", TempUUID_128.UUID_Byte0, TempUUID_128.UUID_Byte1, TempUUID_128.UUID_Byte2, TempUUID_128.UUID_Byte3, TempUUID_128.UUID_Byte4, TempUUID_128.UUID_Byte5, TempUUID_128.UUID_Byte6, TempUUID_128.UUID_Byte7, TempUUID_128.UUID_Byte8, TempUUID_128.UUID_Byte9, TempUUID_128.UUID_Byte10, TempUUID_128.UUID_Byte11, TempUUID_128.UUID_Byte12, TempUUID_128.UUID_Byte13, TempUUID_128.UUID_Byte14, TempUUID_128.UUID_Byte15);
                           ServiceCount++;
                           break;

#if BTPS_CONFIGURATION_GATT_SUPPORT_32_BIT_UUIDS
                        case guUUID_32:
                           CONVERT_BLUETOOTH_UUID_32_TO_SDP_UUID_32(TempUUID_32, ParsedServices.GATTServiceDiscoveryIndicationData[Index].ServiceInformation.UUID.UUID.UUID_32);
                           SDP_ASSIGN_BASE_UUID(TempUUID_128);
                           ASSIGN_SDP_UUID_32_TO_SDP_UUID_128(TempUUID_128, TempUUID_32);
                           ConvertBluetopiaUUIDToAndroid(&TempUUID_128, (*ServiceList)[ServiceCount].uu);

                           SS1_LOGD("Added: Service (%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X)", TempUUID_128.UUID_Byte0, TempUUID_128.UUID_Byte1, TempUUID_128.UUID_Byte2, TempUUID_128.UUID_Byte3, TempUUID_128.UUID_Byte4, TempUUID_128.UUID_Byte5, TempUUID_128.UUID_Byte6, TempUUID_128.UUID_Byte7, TempUUID_128.UUID_Byte8, TempUUID_128.UUID_Byte9, TempUUID_128.UUID_Byte10, TempUUID_128.UUID_Byte11, TempUUID_128.UUID_Byte12, TempUUID_128.UUID_Byte13, TempUUID_128.UUID_Byte14, TempUUID_128.UUID_Byte15);
                           ServiceCount++;
                           break;
#endif
                     }
                  }
               }

#endif

            }
         }

         if(SDPServices)
            BTPS_FreeMemory(SDPServices);

         if(LEServicesBuffer)
         {
            DEVM_FreeParsedServicesData(&ParsedServices);

            BTPS_FreeMemory(LEServicesBuffer);
         }
      }
   }

   return(ServiceCount);
}

//XXX Needs locking around global data accesses and for read-modify-write patterns with DEVM properties
static void HAL_AsyncTaskHandler(void *Task)
{
   BD_ADDR_t         *BD_ADDRList;
   AsyncTask_t       *AsyncTask;
   bt_bdaddr_t        DeviceAddress;
   unsigned int       Index;
   unsigned int       NumberDevices;
   CallbackTask_t     CallbackTask;
   Class_of_Device_t  ClassOfDevice;

   SS1_LOGD("Enter: %p", Task);

   if(Task)
   {
      AsyncTask = (AsyncTask_t *)Task;

      BTPS_MemInitialize(&CallbackTask, 0, sizeof(CallbackTask));

      switch(AsyncTask->Type)
      {
         case atReportKnownRemoteDevices:
            SS1_LOGD("(%p): Processing 'Report Known Remote Devices'", BTPS_CurrentThreadHandle());

            NumberDevices = 0;
            ASSIGN_CLASS_OF_DEVICE(ClassOfDevice, 0, 0, 0);

            if((DEVM_QueryRemoteDeviceList(0, ClassOfDevice, 0, NULL, &NumberDevices) == 0) && (NumberDevices > 0))
            {
               if((BD_ADDRList = (BD_ADDR_t *)BTPS_AllocateMemory(sizeof(BD_ADDR_t) * NumberDevices)) != NULL)
               {
                  if((NumberDevices = DEVM_QueryRemoteDeviceList(0, ClassOfDevice, NumberDevices, BD_ADDRList, NULL)) > 0)
                  {
                     /* For each device, queue an async task to fetch   */
                     /* and report the device's properties.             */
                     for(Index = 0; Index < NumberDevices; Index++)
                     {
                        ConvertBluetopiaAddrToAndroid(&(BD_ADDRList[Index]), &DeviceAddress);
                        SS1HAL_GetRemoteDeviceProperties(&DeviceAddress);
                     }
                  }

                  BTPS_FreeMemory(BD_ADDRList);
               }
            }

            break;

         default:
            SS1_LOGD("(%p): Unrecognized async task (%u)", BTPS_CurrentThreadHandle(), AsyncTask->Type);
            break;
      }

      /* Clean up task object.  First, handle special cases for tasks   */
      /* requiring deep cleaning.                                       */
      switch(AsyncTask->Type)
      {
         default:
            //XXX
            break;
      }

      BTPS_FreeMemory(AsyncTask);
   }

   SS1_LOGD("Exit");
}

static void HAL_CallbackTaskHandler(void *Task)
{
   AsyncTask_t     AsyncTask;
   bt_callbacks_t  Callbacks;
   CallbackTask_t *CallbackTask;

   SS1_LOGD("Enter: %p", Task);

   /* Initialize this context for issuing callbacks to the HAL layer.   */
   pthread_once(&CallbackTaskPool_OnceInit, HAL_CallbackTaskHandler_Init);

   if(Task)
   {
      CallbackTask = (CallbackTask_t *)Task;

      if(SS1HAL_AcquireLock(BTPS_INFINITE_WAIT))
      {
         if(BtCallbacks)
         {
            Callbacks = *BtCallbacks;
            SS1HAL_ReleaseLock();

            switch(CallbackTask->Type)
            {
               case ctReportStateChanged:
                  SS1_LOGD("(%p): Processing 'Report State Changed'", BTPS_CurrentThreadHandle());
                  Callbacks.adapter_state_changed_cb(CallbackTask->TaskData.ReportStateChangedData.AdapterState);

                  /* If the stack has just intialized, Android expects  */
                  /* to automatically receive details of all paired     */
                  /* devices.                                           */
                  if(CallbackTask->TaskData.ReportStateChangedData.AdapterState == BT_STATE_ON)
                  {
                     AsyncTask.Type = atReportKnownRemoteDevices;

                     SS1HAL_QueueAsyncTask(tcWork, HAL_AsyncTaskHandler, sizeof(AsyncTask), &AsyncTask);
                  }

                  break;

               case ctReportAdapterProperties:
                  SS1_LOGD("(%p): Processing 'Report Adapter Properties'", BTPS_CurrentThreadHandle());
                  Callbacks.adapter_properties_cb(CallbackTask->TaskData.ReportAdapterPropertiesData.Status, CallbackTask->TaskData.ReportAdapterPropertiesData.NumberProperties, CallbackTask->TaskData.ReportAdapterPropertiesData.Properties);
                  break;

               case ctReportRemoteProperties:
                  SS1_LOGD("(%p): Processing 'Report Adapter Properties'", BTPS_CurrentThreadHandle());
                  Callbacks.remote_device_properties_cb(CallbackTask->TaskData.ReportRemotePropertiesData.Status, &(CallbackTask->TaskData.ReportRemotePropertiesData.RemoteAddress), CallbackTask->TaskData.ReportRemotePropertiesData.NumberProperties, CallbackTask->TaskData.ReportRemotePropertiesData.Properties);
                  break;

               case ctReportDeviceFound:
                  SS1_LOGD("(%p): Processing 'Report Device Found'", BTPS_CurrentThreadHandle());
                  Callbacks.device_found_cb(CallbackTask->TaskData.ReportDeviceFoundData.NumberProperties, CallbackTask->TaskData.ReportDeviceFoundData.Properties);
                  break;

               case ctReportDiscoveryStateChanged:
                  SS1_LOGD("(%p): Processing 'Report Discovery State Changed'", BTPS_CurrentThreadHandle());
                  Callbacks.discovery_state_changed_cb(CallbackTask->TaskData.ReportDiscoveryStateChangedData.State);
                  break;

               case ctIssuePinRequest:
                  SS1_LOGD("(%p): Processing 'Issue PIN Request'", BTPS_CurrentThreadHandle());
                  Callbacks.pin_request_cb(&(CallbackTask->TaskData.IssuePinRequestData.RemoteAddress), &(CallbackTask->TaskData.IssuePinRequestData.Name), CallbackTask->TaskData.IssuePinRequestData.ClassOfDevice);
                  break;

               case ctIssueSspRequest:
                  SS1_LOGD("(%p): Processing 'Issue SSP Request'", BTPS_CurrentThreadHandle());
                  Callbacks.ssp_request_cb(&(CallbackTask->TaskData.IssueSspRequestData.RemoteAddress), &(CallbackTask->TaskData.IssueSspRequestData.Name), CallbackTask->TaskData.IssueSspRequestData.ClassOfDevice, CallbackTask->TaskData.IssueSspRequestData.PairingVariant, CallbackTask->TaskData.IssueSspRequestData.Passkey);
                  break;

               case ctReportBondStateChanged:
                  SS1_LOGD("(%p): Processing 'Report Bond State Changed'", BTPS_CurrentThreadHandle());
                  Callbacks.bond_state_changed_cb(CallbackTask->TaskData.ReportBondStateChangedData.Status, &(CallbackTask->TaskData.ReportBondStateChangedData.RemoteAddress), CallbackTask->TaskData.ReportBondStateChangedData.State);
                  break;

               case ctReportConnectionStateChanged:
                  SS1_LOGD("(%p): Processing 'Report Connection State Changed'", BTPS_CurrentThreadHandle());
                  Callbacks.acl_state_changed_cb(CallbackTask->TaskData.ReportConnectionStateChangedData.Status, &(CallbackTask->TaskData.ReportConnectionStateChangedData.RemoteAddress), CallbackTask->TaskData.ReportConnectionStateChangedData.State);
                  break;

               case ctReportHCIEvent:
                  SS1_LOGD("(%p): Processing 'Report HCI Event'", BTPS_CurrentThreadHandle());
                  Callbacks.dut_mode_recv_cb(CallbackTask->TaskData.ReportHCIEventData.Opcode, CallbackTask->TaskData.ReportHCIEventData.Buffer, CallbackTask->TaskData.ReportHCIEventData.Length);
                  break;

#if SS1_PLATFORM_SDK_VERSION >= 18
               case ctReportLeTestMode:
                  SS1_LOGD("(%p): Processing 'Report LE Test Mode'", BTPS_CurrentThreadHandle());
                  Callbacks.le_test_mode_cb(CallbackTask->TaskData.ReportLeTestModeData.Status, CallbackTask->TaskData.ReportLeTestModeData.NumberPackets);
                  break;
#endif

               default:
                  SS1_LOGE("(%p): Unrecognized task (%d)", BTPS_CurrentThreadHandle(), CallbackTask->Type);
                  break;
            }
         }
         else
            SS1HAL_ReleaseLock();
      }
      //XXX Add error logs

      /* Clean up task-specific resources.                              */
      switch(CallbackTask->Type)
      {
         case ctReportAdapterProperties:
            BTPS_FreeMemory(CallbackTask->TaskData.ReportAdapterPropertiesData.Properties);
            break;

         case ctReportRemoteProperties:
            BTPS_FreeMemory(CallbackTask->TaskData.ReportRemotePropertiesData.Properties);
            break;

         case ctReportDeviceFound:
            BTPS_FreeMemory(CallbackTask->TaskData.ReportDeviceFoundData.Properties);
            break;

         case ctIssuePinRequest:
            //XXX Might need special cleanup
            break;

         case ctIssueSspRequest:
            //XXX Might need special cleanup
            break;

         case ctReportHCIEvent:
            BTPS_FreeMemory(CallbackTask->TaskData.ReportHCIEventData.Buffer);
            break;

         default:
            break;
      }

      BTPS_FreeMemory(CallbackTask);
   }

   SS1_LOGD("Exit");
}

static void HAL_CallbackTaskHandler_Init(void)
{
   SS1_LOGD("Enter");

   if(SS1HAL_AcquireLock(BTPS_INFINITE_WAIT))
   {
      /* Make sure the HAL callbacks are available.                     */
      if(BtCallbacks)
      {
         if(pthread_key_create(&CallbackTaskPool_VMKey, HAL_CallbackTaskHandler_Cleanup) == 0)
         {
            pthread_setspecific(CallbackTaskPool_VMKey, BtCallbacks->thread_evt_cb);

            BtCallbacks->thread_evt_cb(ASSOCIATE_JVM);

            SS1_LOGD("Callback thread context associated with the JVM");
         }
      }

      SS1HAL_ReleaseLock();
   }

   SS1_LOGD("Exit");
}

static void HAL_CallbackTaskHandler_Cleanup(void *Param)
{
   SS1_LOGD("Enter");

   if(Param)
   {
      ((callback_thread_event)Param)(DISASSOCIATE_JVM);
      SS1_LOGD("Callback thread context disassociated from the JVM");
   }

   SS1_LOGD("Exit");
}

   /* The Device Manager callback.                                      */
static void HAL_DEVMEventCallback(DEVM_Event_Data_t *EventData, void *CallbackParameter)
{
   Word_t                            PropertyMask;
   bt_bdaddr_t                       DeviceAddress;
   CallbackTask_t                    CallbackTask;
   DEVM_Authentication_Information_t AuthInfo;

   SS1_LOGD("Enter: %p, %p", EventData, CallbackParameter);

   if(EventData)
   {
      switch(EventData->EventType)
      {
         case detDevicePoweredOn:
            SS1_LOGD("Device Power On Occurred");

            /* Android expects to automatically receive the properties  */
            /* of the local adapter on startup.                         */
            SS1HAL_GetAdapterProperties();

            /* Issue a Callback Task to report the new power state.     */
            CallbackTask.Type                                         = ctReportStateChanged;
            CallbackTask.TaskData.ReportStateChangedData.AdapterState = BT_STATE_ON;

            /* Queue the callback event.                                */
            SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);

            break;

         case detDevicePoweringOff:
            SS1_LOGD("Device Powering Off");

            //XXX Perform emergency cleanup

            /* Clear any pending authentication status.                 */
            RetrievePendingAuth(NULL);

            DEVM_AcknowledgeDevicePoweringDown(DEVMEventCallbackID);
            break;

         case detDevicePoweredOff:
            SS1_LOGD("Device Power Off Occurred");

            CallbackTask.Type = ctReportStateChanged;

            CallbackTask.TaskData.ReportStateChangedData.AdapterState = BT_STATE_OFF;

            /* Queue the callback event.                                */
            SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);
            break;

         case detLocalDevicePropertiesChanged:
            SS1_LOGD("Local Device Properties Changed (0x%08lX)", EventData->EventData.LocalDevicePropertiesChangedEventData.ChangedMemberMask);

            CallbackTask.Type = ctReportAdapterProperties;

            CallbackTask.TaskData.ReportAdapterPropertiesData.Status = BT_STATUS_SUCCESS;

            PropertyMask = 0;

            if(EventData->EventData.LocalDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_LOCAL_DEVICE_PROPERTIES_CHANGED_CLASS_OF_DEVICE)
               PropertyMask |= HAL_BT_PROP_CLASS_OF_DEVICE;

            if(EventData->EventData.LocalDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_LOCAL_DEVICE_PROPERTIES_CHANGED_DEVICE_NAME)
               PropertyMask |= HAL_BT_PROP_BDNAME;

            if(EventData->EventData.LocalDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_LOCAL_DEVICE_PROPERTIES_CHANGED_DISCOVERABLE_MODE)
               PropertyMask |= HAL_BT_PROP_ADAPTER_SCAN_MODE;

            if(EventData->EventData.LocalDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_LOCAL_DEVICE_PROPERTIES_CHANGED_CONNECTABLE_MODE)
               PropertyMask |= HAL_BT_PROP_ADAPTER_SCAN_MODE;

            if(EventData->EventData.LocalDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_LOCAL_DEVICE_PROPERTIES_CHANGED_PAIRABLE_MODE)
            {
               //XXX
            }

            if(EventData->EventData.LocalDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_LOCAL_DEVICE_PROPERTIES_CHANGED_DEVICE_APPEARANCE)
            {
               //XXX
            }

            if(EventData->EventData.LocalDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_LOCAL_DEVICE_PROPERTIES_CHANGED_BLE_ADDRESS)
            {
               //XXX
            }

            if(PropertyMask)
            {
               SS1_LOGD("Properties requested (0x%04X)", PropertyMask);

               CallbackTask.TaskData.ReportAdapterPropertiesData.NumberProperties = CollectLocalProperties(PropertyMask, &(EventData->EventData.LocalDevicePropertiesChangedEventData.LocalDeviceProperties), &(CallbackTask.TaskData.ReportAdapterPropertiesData.Properties));

               if(CallbackTask.TaskData.ReportAdapterPropertiesData.NumberProperties > 0)
               {
                  /* Queue the callback event.                          */
                  if(SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask) != BT_STATUS_SUCCESS)
                  {
                     /* Queuing failed.  Clean up message.              */
                     if(CallbackTask.TaskData.ReportRemotePropertiesData.Properties)
                        BTPS_FreeMemory(CallbackTask.TaskData.ReportRemotePropertiesData.Properties);
                  }
               }
               else
                  SS1_LOGD("No properties collected");
            }
            break;

         case detDeviceDiscoveryStarted:
            SS1_LOGD("Device Discovery Started");

            CallbackTask.Type = ctReportDiscoveryStateChanged;

            CallbackTask.TaskData.ReportDiscoveryStateChangedData.State = BT_DISCOVERY_STARTED;

            /* Queue the callback event.                                */
            SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);
            break;

         case detDeviceDiscoveryStopped:
            SS1_LOGD("Device Discovery Stopped");

            CallbackTask.Type = ctReportDiscoveryStateChanged;

            CallbackTask.TaskData.ReportDiscoveryStateChangedData.State = BT_DISCOVERY_STOPPED;

            /* Queue the callback event.                                */
            SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);
            break;

         case detRemoteDeviceFound:
            SS1_LOGD("Remote Device Found (%02X:%02X:%02X:%02X:%02X:%02X)", EventData->EventData.RemoteDeviceFoundEventData.RemoteDeviceProperties.BD_ADDR.BD_ADDR5, EventData->EventData.RemoteDeviceFoundEventData.RemoteDeviceProperties.BD_ADDR.BD_ADDR4, EventData->EventData.RemoteDeviceFoundEventData.RemoteDeviceProperties.BD_ADDR.BD_ADDR3, EventData->EventData.RemoteDeviceFoundEventData.RemoteDeviceProperties.BD_ADDR.BD_ADDR2, EventData->EventData.RemoteDeviceFoundEventData.RemoteDeviceProperties.BD_ADDR.BD_ADDR1, EventData->EventData.RemoteDeviceFoundEventData.RemoteDeviceProperties.BD_ADDR.BD_ADDR0);

            CallbackTask.Type = ctReportDeviceFound;

            CallbackTask.TaskData.ReportDeviceFoundData.NumberProperties = CollectRemoteProperties(HAL_BT_PROP_ALL, &(EventData->EventData.RemoteDeviceFoundEventData.RemoteDeviceProperties), &(CallbackTask.TaskData.ReportDeviceFoundData.Properties));

            if(CallbackTask.TaskData.ReportDeviceFoundData.NumberProperties > 0)
            {
               /* Queue the callback event.                                */
               if(SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask) != BT_STATUS_SUCCESS)
               {
                  /* Queuing failed.  Clean up message.              */
                  if(CallbackTask.TaskData.ReportRemotePropertiesData.Properties)
                     BTPS_FreeMemory(CallbackTask.TaskData.ReportRemotePropertiesData.Properties);
               }
            }
            break;

         case detRemoteDeviceDeleted:
            SS1_LOGD("Remote Device Deleted (%02X:%02X:%02X:%02X:%02X:%02X)", EventData->EventData.RemoteDeviceDeletedEventData.RemoteDeviceAddress.BD_ADDR5, EventData->EventData.RemoteDeviceDeletedEventData.RemoteDeviceAddress.BD_ADDR4, EventData->EventData.RemoteDeviceDeletedEventData.RemoteDeviceAddress.BD_ADDR3, EventData->EventData.RemoteDeviceDeletedEventData.RemoteDeviceAddress.BD_ADDR2, EventData->EventData.RemoteDeviceDeletedEventData.RemoteDeviceAddress.BD_ADDR1, EventData->EventData.RemoteDeviceDeletedEventData.RemoteDeviceAddress.BD_ADDR0);
            //XXX
            break;

         case detRemoteDevicePropertiesChanged:
            SS1_LOGD("Remote Device Properties Changed (%02X:%02X:%02X:%02X:%02X:%02X)", EventData->EventData.RemoteDevicePropertiesChangedEventData.RemoteDeviceProperties.BD_ADDR.BD_ADDR5, EventData->EventData.RemoteDevicePropertiesChangedEventData.RemoteDeviceProperties.BD_ADDR.BD_ADDR4, EventData->EventData.RemoteDevicePropertiesChangedEventData.RemoteDeviceProperties.BD_ADDR.BD_ADDR3, EventData->EventData.RemoteDevicePropertiesChangedEventData.RemoteDeviceProperties.BD_ADDR.BD_ADDR2, EventData->EventData.RemoteDevicePropertiesChangedEventData.RemoteDeviceProperties.BD_ADDR.BD_ADDR1, EventData->EventData.RemoteDevicePropertiesChangedEventData.RemoteDeviceProperties.BD_ADDR.BD_ADDR0);

            ConvertBluetopiaAddrToAndroid(&(EventData->EventData.RemoteDevicePropertiesChangedEventData.RemoteDeviceProperties.BD_ADDR), &DeviceAddress);

            PropertyMask = 0;

            if(EventData->EventData.RemoteDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_REMOTE_DEVICE_PROPERTIES_CHANGED_CLASS_OF_DEVICE)
               PropertyMask |= HAL_BT_PROP_CLASS_OF_DEVICE;

            if(EventData->EventData.RemoteDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_REMOTE_DEVICE_PROPERTIES_CHANGED_DEVICE_NAME)
               PropertyMask |= HAL_BT_PROP_BDNAME;

            if(EventData->EventData.RemoteDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_REMOTE_DEVICE_PROPERTIES_CHANGED_APPLICATION_DATA)
               PropertyMask |= HAL_BT_PROP_REMOTE_FRIENDLY_NAME;

            if(EventData->EventData.RemoteDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_REMOTE_DEVICE_PROPERTIES_CHANGED_RSSI)
               PropertyMask |= HAL_BT_PROP_REMOTE_RSSI;

            if(EventData->EventData.RemoteDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_REMOTE_DEVICE_PROPERTIES_CHANGED_PAIRING_STATE)
            {
               CallbackTask.Type = ctReportBondStateChanged;

               CallbackTask.TaskData.ReportBondStateChangedData.Status        = BT_STATUS_SUCCESS;
               CallbackTask.TaskData.ReportBondStateChangedData.RemoteAddress = DeviceAddress;

               if(EventData->EventData.RemoteDevicePropertiesChangedEventData.RemoteDeviceProperties.RemoteDeviceFlags & (DEVM_REMOTE_DEVICE_FLAGS_DEVICE_CURRENTLY_PAIRED | DEVM_REMOTE_DEVICE_FLAGS_DEVICE_AUTHENTICATED_KEY))
                  CallbackTask.TaskData.ReportBondStateChangedData.State = BT_BOND_STATE_BONDED;
               else
                  CallbackTask.TaskData.ReportBondStateChangedData.State = BT_BOND_STATE_NONE;

               SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);
            }

            if(EventData->EventData.RemoteDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_REMOTE_DEVICE_PROPERTIES_CHANGED_CONNECTION_STATE)
            {
               /* If we are disconnected, clear any pending             */
               /* authentication state for this remote device.          */
               if((EventData->EventData.RemoteDevicePropertiesChangedEventData.RemoteDeviceProperties.RemoteDeviceFlags & DEVM_REMOTE_DEVICE_FLAGS_DEVICE_CURRENTLY_CONNECTED) == 0)
               {
                  if(SS1HAL_AcquireLock(BTPS_INFINITE_WAIT))
                  {
                     if(RetrievePendingAuth(&AuthInfo))
                     {
                        if(COMPARE_BD_ADDR(AuthInfo.BD_ADDR, EventData->EventData.RemoteDevicePropertiesChangedEventData.RemoteDeviceProperties.BD_ADDR))
                        {
                           /* This device was pairing.  Announce that it*/
                           /* is no longer paired.                      */
                           BTPS_MemInitialize(&CallbackTask, 0, sizeof(CallbackTask));

                           CallbackTask.Type = ctReportBondStateChanged;

                           CallbackTask.TaskData.ReportBondStateChangedData.Status        = BT_STATUS_SUCCESS;
                           CallbackTask.TaskData.ReportBondStateChangedData.RemoteAddress = DeviceAddress;
                           CallbackTask.TaskData.ReportBondStateChangedData.State         = BT_BOND_STATE_NONE;

                           SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);

                           /* Remove any existing link key for this     */
                           /* device.                                   */
                           DEVM_UnPairRemoteDevice(AuthInfo.BD_ADDR, 0);
                        }
                        else
                        {
                           /* This pending auth event does not match the   */
                           /* disconnected device, so restore the event.   */
                           StorePendingAuthCopy(&AuthInfo);
                        }
                     }

                     SS1HAL_ReleaseLock();
                  }
               }

               CallbackTask.Type = ctReportConnectionStateChanged;

               CallbackTask.TaskData.ReportConnectionStateChangedData.Status        = BT_STATUS_SUCCESS;
               CallbackTask.TaskData.ReportConnectionStateChangedData.RemoteAddress = DeviceAddress;
               CallbackTask.TaskData.ReportConnectionStateChangedData.State         = ((EventData->EventData.RemoteDevicePropertiesChangedEventData.RemoteDeviceProperties.RemoteDeviceFlags & DEVM_REMOTE_DEVICE_FLAGS_DEVICE_CURRENTLY_CONNECTED) ? BT_ACL_STATE_CONNECTED : BT_ACL_STATE_DISCONNECTED);

               SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);
            }

            if(EventData->EventData.RemoteDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_REMOTE_DEVICE_PROPERTIES_CHANGED_ENCRYPTION_STATE)
            {
               //XXX
            }

            if(EventData->EventData.RemoteDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_REMOTE_DEVICE_PROPERTIES_CHANGED_SNIFF_STATE)
            {
               //XXX
            }

            if(EventData->EventData.RemoteDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_REMOTE_DEVICE_PROPERTIES_CHANGED_SERVICES_STATE)
               PropertyMask |= HAL_BT_PROP_UUIDS;

            if(EventData->EventData.RemoteDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_REMOTE_DEVICE_PROPERTIES_CHANGED_LE_RSSI)
               PropertyMask |= HAL_BT_PROP_REMOTE_RSSI;

            if(EventData->EventData.RemoteDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_REMOTE_DEVICE_PROPERTIES_CHANGED_LE_PAIRING_STATE)
            {
               //XXX
               // PropertyMask |= HAL_BT_PROP_ADAPTER_BONDED_DEVICES;
               // BtCallbacks->bond_state_changed_callback(bt_status_t status, bt_bdaddr_t *remote_bd_addr, bt_bond_state_t state);
            }

            if(EventData->EventData.RemoteDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_REMOTE_DEVICE_PROPERTIES_CHANGED_LE_CONNECTION_STATE)
            {
               //XXX
               // BtCallbacks->acl_state_changed_callback(bt_status_t status, bt_bdaddr_t *remote_bd_addr, bt_acl_state_t state);
            }

            if(EventData->EventData.RemoteDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_REMOTE_DEVICE_PROPERTIES_CHANGED_LE_ENCRYPTION_STATE)
            {
               //XXX
            }

            if(EventData->EventData.RemoteDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_REMOTE_DEVICE_PROPERTIES_CHANGED_PRIOR_RESOLVABLE_ADDRESS)
            {
               //XXX
            }

            if(EventData->EventData.RemoteDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_REMOTE_DEVICE_PROPERTIES_CHANGED_DEVICE_APPEARANCE)
            {
               //XXX
            }

            if(EventData->EventData.RemoteDevicePropertiesChangedEventData.ChangedMemberMask & DEVM_REMOTE_DEVICE_PROPERTIES_CHANGED_LE_SERVICES_STATE)
               PropertyMask |= HAL_BT_PROP_UUIDS;

            if(PropertyMask)
            {
               CallbackTask.Type = ctReportRemoteProperties;

               CallbackTask.TaskData.ReportRemotePropertiesData.Status           = BT_STATUS_SUCCESS;
               CallbackTask.TaskData.ReportRemotePropertiesData.RemoteAddress    = DeviceAddress;

               CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties = CollectRemoteProperties(PropertyMask, &(EventData->EventData.RemoteDevicePropertiesChangedEventData.RemoteDeviceProperties), &(CallbackTask.TaskData.ReportRemotePropertiesData.Properties));

               if(CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties > 0)
               {
                  /* Queue the callback event.                          */
                  if(SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask) != BT_STATUS_SUCCESS)
                  {
                     /* Queuing failed.  Clean up message.              */
                     if(CallbackTask.TaskData.ReportRemotePropertiesData.Properties)
                        BTPS_FreeMemory(CallbackTask.TaskData.ReportRemotePropertiesData.Properties);
                  }
               }
            }

            break;

         case detRemoteDevicePropertiesStatus:
            SS1_LOGD("Remote Device Properties Status (%02X:%02X:%02X:%02X:%02X:%02X)", EventData->EventData.RemoteDevicePropertiesStatusEventData.RemoteDeviceProperties.BD_ADDR.BD_ADDR5, EventData->EventData.RemoteDevicePropertiesStatusEventData.RemoteDeviceProperties.BD_ADDR.BD_ADDR4, EventData->EventData.RemoteDevicePropertiesStatusEventData.RemoteDeviceProperties.BD_ADDR.BD_ADDR3, EventData->EventData.RemoteDevicePropertiesStatusEventData.RemoteDeviceProperties.BD_ADDR.BD_ADDR2, EventData->EventData.RemoteDevicePropertiesStatusEventData.RemoteDeviceProperties.BD_ADDR.BD_ADDR1, EventData->EventData.RemoteDevicePropertiesStatusEventData.RemoteDeviceProperties.BD_ADDR.BD_ADDR0);

            SS1_LOGD("Properties status: %s", ((EventData->EventData.RemoteDevicePropertiesStatusEventData.Success != FALSE) ? "Success" : "Failure"));
            //XXX
            break;

         case detRemoteDeviceServicesStatus:
            SS1_LOGD("Remote Device Services Status (%02X:%02X:%02X:%02X:%02X:%02X)", EventData->EventData.RemoteDeviceServicesStatusEventData.RemoteDeviceAddress.BD_ADDR5, EventData->EventData.RemoteDeviceServicesStatusEventData.RemoteDeviceAddress.BD_ADDR4, EventData->EventData.RemoteDeviceServicesStatusEventData.RemoteDeviceAddress.BD_ADDR3, EventData->EventData.RemoteDeviceServicesStatusEventData.RemoteDeviceAddress.BD_ADDR2, EventData->EventData.RemoteDeviceServicesStatusEventData.RemoteDeviceAddress.BD_ADDR1, EventData->EventData.RemoteDeviceServicesStatusEventData.RemoteDeviceAddress.BD_ADDR0);

            /* Android provides no way to report a failed SDP query.    */
            /* Instead, it relies on a timeout handled internally.      */
            SS1_LOGD("Service query status: %s (%s)", ((EventData->EventData.RemoteDeviceServicesStatusEventData.StatusFlags & DEVM_REMOTE_DEVICE_SERVICES_STATUS_FLAGS_SUCCESS) ? "Success" : "Failure"), ((EventData->EventData.RemoteDeviceServicesStatusEventData.StatusFlags & DEVM_REMOTE_DEVICE_SERVICES_STATUS_FLAGS_LOW_ENERGY) ? "LE" : "BR/EDR"));
            break;

         case detRemoteDevicePairingStatus:
            SS1_LOGD("Remote Device Pairing Status (%02X:%02X:%02X:%02X:%02X:%02X)", EventData->EventData.RemoteDevicePairingStatusEventData.RemoteDeviceAddress.BD_ADDR5, EventData->EventData.RemoteDevicePairingStatusEventData.RemoteDeviceAddress.BD_ADDR4, EventData->EventData.RemoteDevicePairingStatusEventData.RemoteDeviceAddress.BD_ADDR3, EventData->EventData.RemoteDevicePairingStatusEventData.RemoteDeviceAddress.BD_ADDR2, EventData->EventData.RemoteDevicePairingStatusEventData.RemoteDeviceAddress.BD_ADDR1, EventData->EventData.RemoteDevicePairingStatusEventData.RemoteDeviceAddress.BD_ADDR0);

            SS1_LOGD("Pairing status: %s (%s)", ((EventData->EventData.RemoteDevicePairingStatusEventData.Success != FALSE) ? "Success" : "Failure"), ((EventData->EventData.RemoteDevicePairingStatusEventData.AuthenticationStatus & DEVM_REMOTE_DEVICE_PAIRING_STATUS_FLAGS_LOW_ENERGY) ? "LE" : "BR/EDR"));

            /* If the pairing attempt failed, clear any pending         */
            /* authentication state for this remote device.             */
            if(EventData->EventData.RemoteDevicePairingStatusEventData.Success == FALSE)
            {
               if(SS1HAL_AcquireLock(BTPS_INFINITE_WAIT))
               {
                  if(RetrievePendingAuth(&AuthInfo))
                  {
                     if(COMPARE_BD_ADDR(AuthInfo.BD_ADDR, EventData->EventData.RemoteDevicePropertiesChangedEventData.RemoteDeviceProperties.BD_ADDR) == FALSE)
                     {
                        /* This pending auth event does not match the   */
                        /* current device, so restore the event.        */
                        StorePendingAuthCopy(&AuthInfo);
                     }
                  }

                  SS1HAL_ReleaseLock();
               }
            }

            if(EventData->EventData.RemoteDevicePairingStatusEventData.Success == FALSE)
            {
               /* Report the pairing failure to Android.                */
               CallbackTask.Type = ctReportBondStateChanged;

               /* XXX More preceise error code based on                 */
               /* AuthenticationStatus.                                 */
               CallbackTask.TaskData.ReportBondStateChangedData.Status = BT_STATUS_AUTH_FAILURE;
               CallbackTask.TaskData.ReportBondStateChangedData.State  = BT_BOND_STATE_NONE;

               ConvertBluetopiaAddrToAndroid(&(EventData->EventData.RemoteDevicePairingStatusEventData.RemoteDeviceAddress), &(CallbackTask.TaskData.ReportBondStateChangedData.RemoteAddress));

               SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);
            }

            break;

         case detRemoteDeviceAuthenticationStatus:
            SS1_LOGD("Remote Device Authentication Status (%02X:%02X:%02X:%02X:%02X:%02X)", EventData->EventData.RemoteDeviceAuthenticationStatusEventData.RemoteDeviceAddress.BD_ADDR5, EventData->EventData.RemoteDeviceAuthenticationStatusEventData.RemoteDeviceAddress.BD_ADDR4, EventData->EventData.RemoteDeviceAuthenticationStatusEventData.RemoteDeviceAddress.BD_ADDR3, EventData->EventData.RemoteDeviceAuthenticationStatusEventData.RemoteDeviceAddress.BD_ADDR2, EventData->EventData.RemoteDeviceAuthenticationStatusEventData.RemoteDeviceAddress.BD_ADDR1, EventData->EventData.RemoteDeviceAuthenticationStatusEventData.RemoteDeviceAddress.BD_ADDR0);

            SS1_LOGD("Authentication status: %d", EventData->EventData.RemoteDeviceAuthenticationStatusEventData.Status);
            //XXX
            break;

         case detRemoteDeviceEncryptionStatus:
            SS1_LOGD("Remote Device Encryption Status (%02X:%02X:%02X:%02X:%02X:%02X)", EventData->EventData.RemoteDeviceEncryptionStatusEventData.RemoteDeviceAddress.BD_ADDR5, EventData->EventData.RemoteDeviceEncryptionStatusEventData.RemoteDeviceAddress.BD_ADDR4, EventData->EventData.RemoteDeviceEncryptionStatusEventData.RemoteDeviceAddress.BD_ADDR3, EventData->EventData.RemoteDeviceEncryptionStatusEventData.RemoteDeviceAddress.BD_ADDR2, EventData->EventData.RemoteDeviceEncryptionStatusEventData.RemoteDeviceAddress.BD_ADDR1, EventData->EventData.RemoteDeviceEncryptionStatusEventData.RemoteDeviceAddress.BD_ADDR0);

            SS1_LOGD("Encryption status: %d", EventData->EventData.RemoteDeviceEncryptionStatusEventData.Status);
            //XXX
            break;

         case detRemoteDeviceConnectionStatus:
            SS1_LOGD("Remote Device Connection Status (%02X:%02X:%02X:%02X:%02X:%02X)", EventData->EventData.RemoteDeviceConnectionStatusEventData.RemoteDeviceAddress.BD_ADDR5, EventData->EventData.RemoteDeviceConnectionStatusEventData.RemoteDeviceAddress.BD_ADDR4, EventData->EventData.RemoteDeviceConnectionStatusEventData.RemoteDeviceAddress.BD_ADDR3, EventData->EventData.RemoteDeviceConnectionStatusEventData.RemoteDeviceAddress.BD_ADDR2, EventData->EventData.RemoteDeviceConnectionStatusEventData.RemoteDeviceAddress.BD_ADDR1, EventData->EventData.RemoteDeviceConnectionStatusEventData.RemoteDeviceAddress.BD_ADDR0);

            SS1_LOGD("Connection status: %d", EventData->EventData.RemoteDeviceConnectionStatusEventData.Status);

            if(EventData->EventData.RemoteDeviceConnectionStatusEventData.Status != 0)
            {
               /* The connection attempt failed, so clear any pending   */
               /* authentication state for this remote device.          */
               if(SS1HAL_AcquireLock(BTPS_INFINITE_WAIT))
               {
                  if(RetrievePendingAuth(&AuthInfo))
                  {
                     if(COMPARE_BD_ADDR(AuthInfo.BD_ADDR, EventData->EventData.RemoteDeviceConnectionStatusEventData.RemoteDeviceAddress))
                     {
                        /* This device was pairing.  Announce that it is*/
                        /* no longer paired.                            */
                        BTPS_MemInitialize(&CallbackTask, 0, sizeof(CallbackTask));

                        CallbackTask.Type = ctReportBondStateChanged;

                        CallbackTask.TaskData.ReportBondStateChangedData.Status = BT_STATUS_SUCCESS;
                        CallbackTask.TaskData.ReportBondStateChangedData.State  = BT_BOND_STATE_NONE;

                        ConvertBluetopiaAddrToAndroid(&(EventData->EventData.RemoteDeviceConnectionStatusEventData.RemoteDeviceAddress), &(CallbackTask.TaskData.ReportBondStateChangedData.RemoteAddress));

                        SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);

                        /* Remove any existing link key for this device.*/
                        DEVM_UnPairRemoteDevice(AuthInfo.BD_ADDR, 0);
                     }
                     else
                     {
                        /* This pending auth event does not match the   */
                        /* current device, so restore the event.        */
                        StorePendingAuthCopy(&AuthInfo);
                     }
                  }

                  SS1HAL_ReleaseLock();
               }

               /* Report the pairing failure to Android.                */
               CallbackTask.Type = ctReportConnectionStateChanged;

               CallbackTask.TaskData.ReportConnectionStateChangedData.Status = BT_STATUS_FAIL;
               CallbackTask.TaskData.ReportConnectionStateChangedData.State  = BT_ACL_STATE_DISCONNECTED;

               ConvertBluetopiaAddrToAndroid(&(EventData->EventData.RemoteDeviceConnectionStatusEventData.RemoteDeviceAddress), &(CallbackTask.TaskData.ReportConnectionStateChangedData.RemoteAddress));

               SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);
            }

            break;

         case detDeviceScanStarted:
            SS1_LOGD("Device Scan Started");
            //XXX
            break;

         case detDeviceScanStopped:
            SS1_LOGD("Device Scan Stopped");
            //XXX
            break;

         case detRemoteDeviceAddressChanged:
            SS1_LOGD("Remote Device Address Changed (%02X:%02X:%02X:%02X:%02X:%02X)", EventData->EventData.RemoteDeviceAddressChangeEventData.RemoteDeviceAddress.BD_ADDR5, EventData->EventData.RemoteDeviceAddressChangeEventData.RemoteDeviceAddress.BD_ADDR4, EventData->EventData.RemoteDeviceAddressChangeEventData.RemoteDeviceAddress.BD_ADDR3, EventData->EventData.RemoteDeviceAddressChangeEventData.RemoteDeviceAddress.BD_ADDR2, EventData->EventData.RemoteDeviceAddressChangeEventData.RemoteDeviceAddress.BD_ADDR1, EventData->EventData.RemoteDeviceAddressChangeEventData.RemoteDeviceAddress.BD_ADDR0);

            SS1_LOGD("Previous address: %02X:%02X:%02X:%02X:%02X:%02X", EventData->EventData.RemoteDeviceAddressChangeEventData.PreviousRemoteDeviceAddress.BD_ADDR5, EventData->EventData.RemoteDeviceAddressChangeEventData.PreviousRemoteDeviceAddress.BD_ADDR4, EventData->EventData.RemoteDeviceAddressChangeEventData.PreviousRemoteDeviceAddress.BD_ADDR3, EventData->EventData.RemoteDeviceAddressChangeEventData.PreviousRemoteDeviceAddress.BD_ADDR2, EventData->EventData.RemoteDeviceAddressChangeEventData.PreviousRemoteDeviceAddress.BD_ADDR1, EventData->EventData.RemoteDeviceAddressChangeEventData.PreviousRemoteDeviceAddress.BD_ADDR0);
            //XXX
            break;

         case detDeviceAdvertisingStarted:
            SS1_LOGD("Device Advertising Started");
            //XXX
            break;

         case detDeviceAdvertisingStopped:
            SS1_LOGD("Device Advertising Stopped");
            //XXX
            break;

         case detAdvertisingTimeout:
            SS1_LOGD("Advertising Timeout");
            //XXX
            break;

         default:
            /* We are not interested in any other type of event.        */
            SS1_LOGD("Received event (%d). Not processed here.", EventData->EventType);
            break;
      }
   }

   SS1_LOGD("Exit");
}

   /* The Device Manager Authentication callback.                       */
static void HAL_DEVMAuthCallback(DEVM_Authentication_Information_t *AuthRequestInfo, void *CallbackParameter)
{
   Boolean_t                       Error;
   CallbackTask_t                  CallbackTask;
   DEVM_Remote_Device_Properties_t RemoteProps;

   SS1_LOGD("Enter: %p, %p", AuthRequestInfo, CallbackParameter);

   if(AuthRequestInfo)
   {
      switch(AuthRequestInfo->AuthenticationAction & DEVM_AUTHENTICATION_ACTION_AUTHENTICATION_ACTION_MASK)
      {
         case DEVM_AUTHENTICATION_ACTION_PIN_CODE_REQUEST:
            SS1_LOGD("(%p): Processing 'PIN Code Request' (%02X:%02X:%02X:%02X:%02X:%02X)", BTPS_CurrentThreadHandle(), AuthRequestInfo->BD_ADDR.BD_ADDR5, AuthRequestInfo->BD_ADDR.BD_ADDR4, AuthRequestInfo->BD_ADDR.BD_ADDR3, AuthRequestInfo->BD_ADDR.BD_ADDR2, AuthRequestInfo->BD_ADDR.BD_ADDR1, AuthRequestInfo->BD_ADDR.BD_ADDR0);

            if(StorePendingAuthCopy(AuthRequestInfo))
            {
               /* A PIN Request begins a classic pairing process, so       */
               /* announce that the bond state for this device has changed */
               /* to 'Bonding'.                                            */
               /* ** NOTE ** Since the 'bonding' state is informational    */
               /*            only, we issue this event on a best-effort    */
               /*            basis -- we don't care if it fails.           */
               BTPS_MemInitialize(&CallbackTask, 0, sizeof(CallbackTask));

               CallbackTask.Type                                       = ctReportBondStateChanged;
               CallbackTask.TaskData.ReportBondStateChangedData.Status = BT_STATUS_SUCCESS;
               CallbackTask.TaskData.ReportBondStateChangedData.State  = BT_BOND_STATE_BONDING;

               ConvertBluetopiaAddrToAndroid(&(AuthRequestInfo->BD_ADDR), &(CallbackTask.TaskData.ReportBondStateChangedData.RemoteAddress));

               SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);

               /* Now, issue the actual PIN request.                       */
               BTPS_MemInitialize(&CallbackTask, 0, sizeof(CallbackTask));

               CallbackTask.Type = ctIssuePinRequest;
               ConvertBluetopiaAddrToAndroid(&(AuthRequestInfo->BD_ADDR), &(CallbackTask.TaskData.IssuePinRequestData.RemoteAddress));

               /* Attempt to query the known properties of the remote      */
               /* device.  This will be used to populate the name and CoD  */
               /* fields of the PIN request.                               */
               BTPS_MemInitialize(&RemoteProps, 0, sizeof(RemoteProps));
               DEVM_QueryRemoteDeviceProperties(AuthRequestInfo->BD_ADDR, 0, &RemoteProps);

               if(RemoteProps.RemoteDeviceFlags & DEVM_REMOTE_DEVICE_FLAGS_DEVICE_NAME_KNOWN)
                  BTPS_MemCopy(CallbackTask.TaskData.IssuePinRequestData.Name.name, RemoteProps.DeviceName, RemoteProps.DeviceNameLength);
               else
                  snprintf((char *)(CallbackTask.TaskData.IssuePinRequestData.Name.name), sizeof(CallbackTask.TaskData.IssuePinRequestData.Name.name), "%02X:%02X:%02X:%02X:%02X:%02X", AuthRequestInfo->BD_ADDR.BD_ADDR5, AuthRequestInfo->BD_ADDR.BD_ADDR4, AuthRequestInfo->BD_ADDR.BD_ADDR3, AuthRequestInfo->BD_ADDR.BD_ADDR2, AuthRequestInfo->BD_ADDR.BD_ADDR1, AuthRequestInfo->BD_ADDR.BD_ADDR0);

               ConvertBluetopiaCoDToAndroid(&(RemoteProps.ClassOfDevice), &(CallbackTask.TaskData.IssuePinRequestData.ClassOfDevice));

               if(SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask) != BT_STATUS_SUCCESS)
               {
                  SS1_LOGD("Failed to queue PIN request");

                  /* Clear the previously-stored authentication info.   */
                  RetrievePendingAuth(NULL);

                  CallbackTask.Type                                       = ctReportBondStateChanged;
                  CallbackTask.TaskData.ReportBondStateChangedData.Status = BT_STATUS_FAIL;
                  CallbackTask.TaskData.ReportBondStateChangedData.State  = BT_BOND_STATE_NONE;
                  ConvertBluetopiaAddrToAndroid(&(AuthRequestInfo->BD_ADDR), &(CallbackTask.TaskData.ReportBondStateChangedData.RemoteAddress));
                  SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);

                  /* Unable to queue the callback event, so immediately    */
                  /* reject the pairing attempt.                           */
                  AuthRequestInfo->AuthenticationAction     = (DEVM_AUTHENTICATION_ACTION_PIN_CODE_RESPONSE | (AuthRequestInfo->AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
                  AuthRequestInfo->AuthenticationDataLength = 0;

                  DEVM_AuthenticationResponse(DEVMAuthenticationCallbackID, AuthRequestInfo);

                  /* The pairing process has failed, so remove any link */
                  /* key that existed before authentication began.      */
                  DEVM_UnPairRemoteDevice(AuthRequestInfo->BD_ADDR, 0);
               }
            }
            else
            {
               /* Unable to store the pending authentication event, so  */
               /* immediately reject the pairing attempt.               */
               AuthRequestInfo->AuthenticationAction     = (DEVM_AUTHENTICATION_ACTION_PIN_CODE_RESPONSE | (AuthRequestInfo->AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
               AuthRequestInfo->AuthenticationDataLength = 0;

               DEVM_AuthenticationResponse(DEVMAuthenticationCallbackID, AuthRequestInfo);
            }

            break;

         case DEVM_AUTHENTICATION_ACTION_USER_CONFIRMATION_REQUEST:
            SS1_LOGD("(%p): Processing 'User Confirmation Request' (%02X:%02X:%02X:%02X:%02X:%02X)", BTPS_CurrentThreadHandle(), AuthRequestInfo->BD_ADDR.BD_ADDR5, AuthRequestInfo->BD_ADDR.BD_ADDR4, AuthRequestInfo->BD_ADDR.BD_ADDR3, AuthRequestInfo->BD_ADDR.BD_ADDR2, AuthRequestInfo->BD_ADDR.BD_ADDR1, AuthRequestInfo->BD_ADDR.BD_ADDR0);

            if(StorePendingAuthCopy(AuthRequestInfo))
            {
               Error = FALSE;

               BTPS_MemInitialize(&CallbackTask, 0, sizeof(CallbackTask));

               CallbackTask.Type                                              = ctIssueSspRequest;

               ConvertBluetopiaAddrToAndroid(&(AuthRequestInfo->BD_ADDR), &(CallbackTask.TaskData.IssueSspRequestData.RemoteAddress));

               switch(AuthRequestInfo->AuthenticationData.UserConfirmationRequestData.IOCapabilities.IO_Capability)
               {
                  case icDisplayOnly:
                  case icDisplayYesNo:
                     CallbackTask.TaskData.IssueSspRequestData.PairingVariant = BT_SSP_VARIANT_PASSKEY_CONFIRMATION;
                     CallbackTask.TaskData.IssueSspRequestData.Passkey        = AuthRequestInfo->AuthenticationData.UserConfirmationRequestData.Passkey;
                     break;

                  case icKeyboardOnly:
                     SS1_LOGD("(%p): User confirmation not expected from device which claims KeyboardOnly IO. Rejecting. (%02X:%02X:%02X:%02X:%02X:%02X)", BTPS_CurrentThreadHandle(), AuthRequestInfo->BD_ADDR.BD_ADDR5, AuthRequestInfo->BD_ADDR.BD_ADDR4, AuthRequestInfo->BD_ADDR.BD_ADDR3, AuthRequestInfo->BD_ADDR.BD_ADDR2, AuthRequestInfo->BD_ADDR.BD_ADDR1, AuthRequestInfo->BD_ADDR.BD_ADDR0);
                     Error = TRUE;
                     break;

                  case icNoInputNoOutput:
                     CallbackTask.TaskData.IssueSspRequestData.PairingVariant = BT_SSP_VARIANT_CONSENT;
                     CallbackTask.TaskData.IssueSspRequestData.Passkey        = 0;
                     break;

                  default:
                     Error = TRUE;
                     break;
               }

               if(!Error)
               {
                  /* Attempt to query the known properties of the remote   */
                  /* device.  This will be used to populate the name and   */
                  /* CoD fields of the PIN request.                        */
                  BTPS_MemInitialize(&RemoteProps, 0, sizeof(RemoteProps));
                  DEVM_QueryRemoteDeviceProperties(AuthRequestInfo->BD_ADDR, 0, &RemoteProps);

                  if(RemoteProps.RemoteDeviceFlags & DEVM_REMOTE_DEVICE_FLAGS_DEVICE_NAME_KNOWN)
                     BTPS_MemCopy(CallbackTask.TaskData.IssueSspRequestData.Name.name, RemoteProps.DeviceName, RemoteProps.DeviceNameLength);
                  else
                     snprintf((char *)(CallbackTask.TaskData.IssueSspRequestData.Name.name), sizeof(CallbackTask.TaskData.IssueSspRequestData.Name.name), "%02X:%02X:%02X:%02X:%02X:%02X", AuthRequestInfo->BD_ADDR.BD_ADDR5, AuthRequestInfo->BD_ADDR.BD_ADDR4, AuthRequestInfo->BD_ADDR.BD_ADDR3, AuthRequestInfo->BD_ADDR.BD_ADDR2, AuthRequestInfo->BD_ADDR.BD_ADDR1, AuthRequestInfo->BD_ADDR.BD_ADDR0);

                  ConvertBluetopiaCoDToAndroid(&(RemoteProps.ClassOfDevice), &(CallbackTask.TaskData.IssueSspRequestData.ClassOfDevice));

                  if(SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask) != BT_STATUS_SUCCESS)
                  {
                     SS1_LOGD("Failed to queue User Auth request");

                     /* Clear the previously-stored authentication info.*/
                     RetrievePendingAuth(NULL);

                     CallbackTask.Type                                       = ctReportBondStateChanged;
                     CallbackTask.TaskData.ReportBondStateChangedData.Status = BT_STATUS_FAIL;
                     CallbackTask.TaskData.ReportBondStateChangedData.State  = BT_BOND_STATE_NONE;
                     ConvertBluetopiaAddrToAndroid(&(AuthRequestInfo->BD_ADDR), &(CallbackTask.TaskData.ReportBondStateChangedData.RemoteAddress));
                     SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);

                     /* Unable to queue the callback event, so immediately */
                     /* reject the pairing attempt.                        */
                     AuthRequestInfo->AuthenticationAction     = (DEVM_AUTHENTICATION_ACTION_USER_CONFIRMATION_RESPONSE | (AuthRequestInfo->AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
                     AuthRequestInfo->AuthenticationDataLength = 0;

                     DEVM_AuthenticationResponse(DEVMAuthenticationCallbackID, AuthRequestInfo);

                     /* The pairing process has failed, so remove any link */
                     /* key that existed before authentication began.      */
                     DEVM_UnPairRemoteDevice(AuthRequestInfo->BD_ADDR, 0);
                  }
               }
               else
               {
                  /* Clear the previously-stored authentication info.   */
                  RetrievePendingAuth(NULL);

                  /* The pairing request is invalid, so reject it          */
                  /* immediately.                                          */
                  AuthRequestInfo->AuthenticationAction     = (DEVM_AUTHENTICATION_ACTION_USER_CONFIRMATION_RESPONSE | (AuthRequestInfo->AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
                  AuthRequestInfo->AuthenticationDataLength = 0;

                  DEVM_AuthenticationResponse(DEVMAuthenticationCallbackID, AuthRequestInfo);
               }
            }
            else
            {
               /* Unable to store the pending authentication event, so  */
               /* immediately reject the pairing attempt.               */
               AuthRequestInfo->AuthenticationAction     = (DEVM_AUTHENTICATION_ACTION_USER_CONFIRMATION_RESPONSE | (AuthRequestInfo->AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
               AuthRequestInfo->AuthenticationDataLength = 0;

               DEVM_AuthenticationResponse(DEVMAuthenticationCallbackID, AuthRequestInfo);
            }

            break;

         case DEVM_AUTHENTICATION_ACTION_PASSKEY_REQUEST:
            SS1_LOGD("(%p): Processing 'Passkey Request' (%02X:%02X:%02X:%02X:%02X:%02X)", BTPS_CurrentThreadHandle(), AuthRequestInfo->BD_ADDR.BD_ADDR5, AuthRequestInfo->BD_ADDR.BD_ADDR4, AuthRequestInfo->BD_ADDR.BD_ADDR3, AuthRequestInfo->BD_ADDR.BD_ADDR2, AuthRequestInfo->BD_ADDR.BD_ADDR1, AuthRequestInfo->BD_ADDR.BD_ADDR0);

            if(StorePendingAuthCopy(AuthRequestInfo))
            {
               BTPS_MemInitialize(&CallbackTask, 0, sizeof(CallbackTask));

               CallbackTask.Type                                        = ctIssueSspRequest;
               CallbackTask.TaskData.IssueSspRequestData.PairingVariant = BT_SSP_VARIANT_PASSKEY_ENTRY;
               CallbackTask.TaskData.IssueSspRequestData.Passkey        = 0;

               ConvertBluetopiaAddrToAndroid(&(AuthRequestInfo->BD_ADDR), &(CallbackTask.TaskData.IssueSspRequestData.RemoteAddress));

               /* Attempt to query the known properties of the remote      */
               /* device.  This will be used to populate the name and CoD  */
               /* fields of the PIN request.                               */
               BTPS_MemInitialize(&RemoteProps, 0, sizeof(RemoteProps));
               DEVM_QueryRemoteDeviceProperties(AuthRequestInfo->BD_ADDR, 0, &RemoteProps);

               if(RemoteProps.RemoteDeviceFlags & DEVM_REMOTE_DEVICE_FLAGS_DEVICE_NAME_KNOWN)
                  BTPS_MemCopy(CallbackTask.TaskData.IssueSspRequestData.Name.name, RemoteProps.DeviceName, RemoteProps.DeviceNameLength);
               else
                  snprintf((char *)(CallbackTask.TaskData.IssueSspRequestData.Name.name), sizeof(CallbackTask.TaskData.IssueSspRequestData.Name.name), "%02X:%02X:%02X:%02X:%02X:%02X", AuthRequestInfo->BD_ADDR.BD_ADDR5, AuthRequestInfo->BD_ADDR.BD_ADDR4, AuthRequestInfo->BD_ADDR.BD_ADDR3, AuthRequestInfo->BD_ADDR.BD_ADDR2, AuthRequestInfo->BD_ADDR.BD_ADDR1, AuthRequestInfo->BD_ADDR.BD_ADDR0);

               ConvertBluetopiaCoDToAndroid(&(RemoteProps.ClassOfDevice), &(CallbackTask.TaskData.IssueSspRequestData.ClassOfDevice));

               if(SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask) != BT_STATUS_SUCCESS)
               {
                  SS1_LOGD("Failed to queue Passkey request");

                  /* Clear the previously-stored authentication info.   */
                  RetrievePendingAuth(NULL);

                  CallbackTask.Type                                       = ctReportBondStateChanged;
                  CallbackTask.TaskData.ReportBondStateChangedData.Status = BT_STATUS_FAIL;
                  CallbackTask.TaskData.ReportBondStateChangedData.State  = BT_BOND_STATE_NONE;
                  ConvertBluetopiaAddrToAndroid(&(AuthRequestInfo->BD_ADDR), &(CallbackTask.TaskData.ReportBondStateChangedData.RemoteAddress));
                  SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);

                  /* Unable to queue the callback event, so immediately    */
                  /* reject the pairing attempt.                           */
                  AuthRequestInfo->AuthenticationAction     = (DEVM_AUTHENTICATION_ACTION_PASSKEY_RESPONSE | (AuthRequestInfo->AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
                  AuthRequestInfo->AuthenticationDataLength = 0;

                  DEVM_AuthenticationResponse(DEVMAuthenticationCallbackID, AuthRequestInfo);

                  /* The pairing process has failed, so remove any link */
                  /* key that existed before authentication began.      */
                  DEVM_UnPairRemoteDevice(AuthRequestInfo->BD_ADDR, 0);
               }
            }
            else
            {
               /* Unable to store the pending authentication event, so  */
               /* immediately reject the pairing attempt.               */
               AuthRequestInfo->AuthenticationAction     = (DEVM_AUTHENTICATION_ACTION_PASSKEY_RESPONSE | (AuthRequestInfo->AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
               AuthRequestInfo->AuthenticationDataLength = 0;

               DEVM_AuthenticationResponse(DEVMAuthenticationCallbackID, AuthRequestInfo);
            }

            break;

         case DEVM_AUTHENTICATION_ACTION_PASSKEY_INDICATION:
            SS1_LOGD("(%p): Processing 'Passkey Indication' (%02X:%02X:%02X:%02X:%02X:%02X)", BTPS_CurrentThreadHandle(), AuthRequestInfo->BD_ADDR.BD_ADDR5, AuthRequestInfo->BD_ADDR.BD_ADDR4, AuthRequestInfo->BD_ADDR.BD_ADDR3, AuthRequestInfo->BD_ADDR.BD_ADDR2, AuthRequestInfo->BD_ADDR.BD_ADDR1, AuthRequestInfo->BD_ADDR.BD_ADDR0);

            //XXX This might be inappropriate if Android doesn't generate a reply, since we will have to auto-generate an immediate 'success' reply
            if(StorePendingAuthCopy(AuthRequestInfo))
            {
               BTPS_MemInitialize(&CallbackTask, 0, sizeof(CallbackTask));

               CallbackTask.Type                                        = ctIssueSspRequest;
               CallbackTask.TaskData.IssueSspRequestData.PairingVariant = BT_SSP_VARIANT_PASSKEY_NOTIFICATION;
               CallbackTask.TaskData.IssueSspRequestData.Passkey        = AuthRequestInfo->AuthenticationData.Passkey;

               ConvertBluetopiaAddrToAndroid(&(AuthRequestInfo->BD_ADDR), &(CallbackTask.TaskData.IssueSspRequestData.RemoteAddress));

               /* Attempt to query the known properties of the remote      */
               /* device.  This will be used to populate the name and CoD  */
               /* fields of the PIN request.                               */
               BTPS_MemInitialize(&RemoteProps, 0, sizeof(RemoteProps));
               DEVM_QueryRemoteDeviceProperties(AuthRequestInfo->BD_ADDR, 0, &RemoteProps);

               if(RemoteProps.RemoteDeviceFlags & DEVM_REMOTE_DEVICE_FLAGS_DEVICE_NAME_KNOWN)
                  BTPS_MemCopy(CallbackTask.TaskData.IssueSspRequestData.Name.name, RemoteProps.DeviceName, RemoteProps.DeviceNameLength);
               else
                  snprintf((char *)(CallbackTask.TaskData.IssueSspRequestData.Name.name), sizeof(CallbackTask.TaskData.IssueSspRequestData.Name.name), "%02X:%02X:%02X:%02X:%02X:%02X", AuthRequestInfo->BD_ADDR.BD_ADDR5, AuthRequestInfo->BD_ADDR.BD_ADDR4, AuthRequestInfo->BD_ADDR.BD_ADDR3, AuthRequestInfo->BD_ADDR.BD_ADDR2, AuthRequestInfo->BD_ADDR.BD_ADDR1, AuthRequestInfo->BD_ADDR.BD_ADDR0);

               ConvertBluetopiaCoDToAndroid(&(RemoteProps.ClassOfDevice), &(CallbackTask.TaskData.IssueSspRequestData.ClassOfDevice));

               if(SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask) != BT_STATUS_SUCCESS)
               {
                  SS1_LOGD("Failed to queue Passkey indication");

                  /* Clear the previously-stored authentication info.   */
                  RetrievePendingAuth(NULL);

                  CallbackTask.Type                                       = ctReportBondStateChanged;
                  CallbackTask.TaskData.ReportBondStateChangedData.Status = BT_STATUS_FAIL;
                  CallbackTask.TaskData.ReportBondStateChangedData.State  = BT_BOND_STATE_NONE;
                  ConvertBluetopiaAddrToAndroid(&(AuthRequestInfo->BD_ADDR), &(CallbackTask.TaskData.ReportBondStateChangedData.RemoteAddress));
                  SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);

                  /* Unable to queue the callback event, so immediately    */
                  /* reject the pairing attempt.                           */
                  AuthRequestInfo->AuthenticationAction     = (DEVM_AUTHENTICATION_ACTION_PASSKEY_RESPONSE | (AuthRequestInfo->AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
                  AuthRequestInfo->AuthenticationDataLength = 0;

                  DEVM_AuthenticationResponse(DEVMAuthenticationCallbackID, AuthRequestInfo);

                  /* The pairing process has failed, so remove any link */
                  /* key that existed before authentication began.      */
                  DEVM_UnPairRemoteDevice(AuthRequestInfo->BD_ADDR, 0);
               }
               else
               {
                  //XXX Might need to auto-accept here, if Android doesn't generate a reply
               }
            }
            else
            {
               /* Unable to store the pending authentication event, so  */
               /* immediately reject the pairing attempt.               */
               AuthRequestInfo->AuthenticationAction     = (DEVM_AUTHENTICATION_ACTION_PASSKEY_RESPONSE | (AuthRequestInfo->AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
               AuthRequestInfo->AuthenticationDataLength = 0;

               DEVM_AuthenticationResponse(DEVMAuthenticationCallbackID, AuthRequestInfo);
            }

            break;

         case DEVM_AUTHENTICATION_ACTION_KEYPRESS_INDICATION:
            SS1_LOGD("(%p): Processing 'Keypress Indication' (%02X:%02X:%02X:%02X:%02X:%02X)", BTPS_CurrentThreadHandle(), AuthRequestInfo->BD_ADDR.BD_ADDR5, AuthRequestInfo->BD_ADDR.BD_ADDR4, AuthRequestInfo->BD_ADDR.BD_ADDR3, AuthRequestInfo->BD_ADDR.BD_ADDR2, AuthRequestInfo->BD_ADDR.BD_ADDR1, AuthRequestInfo->BD_ADDR.BD_ADDR0);
            break;

         case DEVM_AUTHENTICATION_ACTION_OUT_OF_BAND_DATA_REQUEST:
            SS1_LOGD("(%p): Processing 'Out of Band Data Request' (%02X:%02X:%02X:%02X:%02X:%02X)", BTPS_CurrentThreadHandle(), AuthRequestInfo->BD_ADDR.BD_ADDR5, AuthRequestInfo->BD_ADDR.BD_ADDR4, AuthRequestInfo->BD_ADDR.BD_ADDR3, AuthRequestInfo->BD_ADDR.BD_ADDR2, AuthRequestInfo->BD_ADDR.BD_ADDR1, AuthRequestInfo->BD_ADDR.BD_ADDR0);
           
            AuthRequestInfo->AuthenticationAction     = (DEVM_AUTHENTICATION_ACTION_OUT_OF_BAND_DATA_RESPONSE | (AuthRequestInfo->AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
            AuthRequestInfo->AuthenticationDataLength = 0;

            DEVM_AuthenticationResponse(DEVMAuthenticationCallbackID, AuthRequestInfo);
            break;

         case DEVM_AUTHENTICATION_ACTION_IO_CAPABILITIES_REQUEST:
            SS1_LOGD("(%p): Processing 'IO Capabilities Request' (%02X:%02X:%02X:%02X:%02X:%02X)", BTPS_CurrentThreadHandle(), AuthRequestInfo->BD_ADDR.BD_ADDR5, AuthRequestInfo->BD_ADDR.BD_ADDR4, AuthRequestInfo->BD_ADDR.BD_ADDR3, AuthRequestInfo->BD_ADDR.BD_ADDR2, AuthRequestInfo->BD_ADDR.BD_ADDR1, AuthRequestInfo->BD_ADDR.BD_ADDR0);

            /* An IO Capability Request beings a Secure Simple Pairing  */
            /* process, so announce that the bond state for this device */
            /* has changed to 'Bonding'.                                */
            /* ** NOTE ** Since the 'bonding' state is informational    */
            /*            only, we issue this event on a best-effort    */
            /*            basis -- we don't care if it fails.           */
            BTPS_MemInitialize(&CallbackTask, 0, sizeof(CallbackTask));

            CallbackTask.Type                                              = ctReportBondStateChanged;
            CallbackTask.TaskData.ReportBondStateChangedData.Status        = BT_STATUS_SUCCESS;
            CallbackTask.TaskData.ReportBondStateChangedData.State         = BT_BOND_STATE_BONDING;

            ConvertBluetopiaAddrToAndroid(&(AuthRequestInfo->BD_ADDR), &(CallbackTask.TaskData.ReportBondStateChangedData.RemoteAddress));

            SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);

            /* Now issue the actual IO Caps response.                   */
            AuthRequestInfo->AuthenticationAction     = (DEVM_AUTHENTICATION_ACTION_IO_CAPABILITIES_RESPONSE | (AuthRequestInfo->AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
            AuthRequestInfo->AuthenticationDataLength = sizeof(AuthRequestInfo->AuthenticationData.IOCapabilities);

            if(AuthRequestInfo->AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK)
            {
               AuthRequestInfo->AuthenticationData.LEIOCapabilities.IO_Capability = licDisplayYesNo;
               AuthRequestInfo->AuthenticationData.LEIOCapabilities.MITM          = TRUE;
               AuthRequestInfo->AuthenticationData.LEIOCapabilities.OOB_Present   = FALSE;
            }
            else
            {
               AuthRequestInfo->AuthenticationData.IOCapabilities.IO_Capability            = icDisplayYesNo;
               AuthRequestInfo->AuthenticationData.IOCapabilities.MITM_Protection_Required = TRUE;
               AuthRequestInfo->AuthenticationData.IOCapabilities.OOB_Data_Present         = FALSE;
               //AuthRequestInfo->AuthenticationData.IOCapabilities.OOB_256_Data_Present     = FALSE;
            }

            DEVM_AuthenticationResponse(DEVMAuthenticationCallbackID, AuthRequestInfo);
            break;

         case DEVM_AUTHENTICATION_ACTION_IO_CAPABILITIES_RESPONSE:
            SS1_LOGD("(%p): Processing 'IO Capabilities Response' (%02X:%02X:%02X:%02X:%02X:%02X)", BTPS_CurrentThreadHandle(), AuthRequestInfo->BD_ADDR.BD_ADDR5, AuthRequestInfo->BD_ADDR.BD_ADDR4, AuthRequestInfo->BD_ADDR.BD_ADDR3, AuthRequestInfo->BD_ADDR.BD_ADDR2, AuthRequestInfo->BD_ADDR.BD_ADDR1, AuthRequestInfo->BD_ADDR.BD_ADDR0);

            if(AuthRequestInfo->AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK)
            {
               SS1_LOGD("(%p): Received IO Caps: %s  MITM: %d  OOB: %d  Type: %sBonding", BTPS_CurrentThreadHandle(),
                                                                                          ((AuthRequestInfo->AuthenticationData.LEIOCapabilities.IO_Capability == licKeyboardDisplay) ? "Keyboard Display" : 
                                                                                             ((AuthRequestInfo->AuthenticationData.LEIOCapabilities.IO_Capability == licDisplayOnly) ? "Display Only" : 
                                                                                             ((AuthRequestInfo->AuthenticationData.LEIOCapabilities.IO_Capability == licDisplayYesNo) ? "Display Yes/No" : 
                                                                                             ((AuthRequestInfo->AuthenticationData.LEIOCapabilities.IO_Capability == licKeyboardOnly) ? "Keyboard Only" : 
                                                                                             ((AuthRequestInfo->AuthenticationData.LEIOCapabilities.IO_Capability == licNoInputNoOutput) ? "No I/O" : "Unknown"))))),
                                                                                          AuthRequestInfo->AuthenticationData.LEIOCapabilities.MITM,
                                                                                          AuthRequestInfo->AuthenticationData.LEIOCapabilities.OOB_Present,
                                                                                          ((AuthRequestInfo->AuthenticationData.LEIOCapabilities.Bonding_Type == lbtBonding) ? "" : 
                                                                                             ((AuthRequestInfo->AuthenticationData.LEIOCapabilities.Bonding_Type == lbtNoBonding) ? "No " : "Unknown ")));
            }
            else
            {
               SS1_LOGD("(%p): Received IO Caps: %s  MITM: %d  OOB: %d  OOB_256: %d  Type: %sBonding", BTPS_CurrentThreadHandle(),
                                                                                                       ((AuthRequestInfo->AuthenticationData.IOCapabilities.IO_Capability == icDisplayOnly) ? "Display Only" : 
                                                                                                          ((AuthRequestInfo->AuthenticationData.IOCapabilities.IO_Capability == icDisplayYesNo) ? "Display Yes/No" : 
                                                                                                          ((AuthRequestInfo->AuthenticationData.IOCapabilities.IO_Capability == icKeyboardOnly) ? "Keyboard Only" : 
                                                                                                          ((AuthRequestInfo->AuthenticationData.IOCapabilities.IO_Capability == icNoInputNoOutput) ? "No I/O" : "Unknown")))),
                                                                                                       AuthRequestInfo->AuthenticationData.IOCapabilities.MITM_Protection_Required,
                                                                                                       AuthRequestInfo->AuthenticationData.IOCapabilities.OOB_Data_Present,
                                                                                                       //AuthRequestInfo->AuthenticationData.IOCapabilities.OOB_256_Data_Present,
                                                                                                       0,
                                                                                                       ((AuthRequestInfo->AuthenticationData.IOCapabilities.Bonding_Type == ibGeneralBonding) ? "General " :
                                                                                                          ((AuthRequestInfo->AuthenticationData.IOCapabilities.Bonding_Type == ibDedicatedBonding) ? "Dedicated " :
                                                                                                          ((AuthRequestInfo->AuthenticationData.IOCapabilities.Bonding_Type == ibDedicatedBonding) ? "No " : "Unknown "))));
            }

            break;

         case DEVM_AUTHENTICATION_ACTION_AUTHENTICATION_STATUS_RESULT:
            SS1_LOGD("(%p): Processing 'Authentication Status Result' (%02X:%02X:%02X:%02X:%02X:%02X)", BTPS_CurrentThreadHandle(), AuthRequestInfo->BD_ADDR.BD_ADDR5, AuthRequestInfo->BD_ADDR.BD_ADDR4, AuthRequestInfo->BD_ADDR.BD_ADDR3, AuthRequestInfo->BD_ADDR.BD_ADDR2, AuthRequestInfo->BD_ADDR.BD_ADDR1, AuthRequestInfo->BD_ADDR.BD_ADDR0);

            SS1_LOGD("(%p): Authentication Status: %d", BTPS_CurrentThreadHandle(), AuthRequestInfo->AuthenticationData.AuthenticationStatus);
            break;

         case DEVM_AUTHENTICATION_ACTION_PIN_CODE_RESPONSE:
         case DEVM_AUTHENTICATION_ACTION_USER_CONFIRMATION_RESPONSE:
         case DEVM_AUTHENTICATION_ACTION_PASSKEY_RESPONSE:
         case DEVM_AUTHENTICATION_ACTION_OUT_OF_BAND_DATA_RESPONSE:
         default:
            /* We are not interested in any other type of event.        */
            SS1_LOGD("(%p): Received event (%d). Not processed here. (%02X:%02X:%02X:%02X:%02X:%02X)", BTPS_CurrentThreadHandle(), AuthRequestInfo->AuthenticationAction, AuthRequestInfo->BD_ADDR.BD_ADDR5, AuthRequestInfo->BD_ADDR.BD_ADDR4, AuthRequestInfo->BD_ADDR.BD_ADDR3, AuthRequestInfo->BD_ADDR.BD_ADDR2, AuthRequestInfo->BD_ADDR.BD_ADDR1, AuthRequestInfo->BD_ADDR.BD_ADDR0);
            break;
      }
   }

   SS1_LOGD("Exit");
}

bt_status_t SS1HAL_QueueAsyncTask(AsyncContext_t Context, PoolCallback_t TaskHandler, unsigned int MessageLength, void *Message)
{
   void        *Buffer;
   bt_status_t  ret_val;
   
   SS1_LOGD("Enter (%d, %p, %u, %p)", Context, TaskHandler, MessageLength, Message);

   if(TaskHandler)
   {
      /* If a message is given, attempt to make a copy.                 */
      if((MessageLength) && (Message))
      {
         if((Buffer = BTPS_AllocateMemory(MessageLength)) != NULL)
         {
            BTPS_MemCopy(Buffer, Message, MessageLength);
            ret_val = BT_STATUS_SUCCESS;
         }
         else
            ret_val = BT_STATUS_NOMEM;
      }
      else
      {
         Buffer  = NULL;
         ret_val = BT_STATUS_SUCCESS;
      }

      /* If nothing has gone wrong, attempt to queue the copied message.*/
      if(ret_val == BT_STATUS_SUCCESS)
         ret_val = SS1HAL_QueueAsyncTaskDirect(Context, TaskHandler, Buffer);

      /* If we allocated a buffer but failed to queue it, free the      */
      /* buffer.                                                        */
      if((ret_val != BT_STATUS_SUCCESS) && (Buffer))
         BTPS_FreeMemory(Buffer);
   }
   else
      ret_val = BT_STATUS_PARM_INVALID;

   SS1_LOGD("Exit (%d)", ret_val);

   return(ret_val);
}

bt_status_t SS1HAL_QueueAsyncTaskDirect(AsyncContext_t Context, PoolCallback_t TaskHandler, void *Message)
{
   TaskPool_t  Pool;
   bt_status_t ret_val;

   SS1_LOGD("Enter (%d, %p, %p)", Context, TaskHandler, Message);

   if(SS1HAL_AcquireLock(BTPS_INFINITE_WAIT))
   {
      switch(Context)
      {
         case tcWork:
            Pool = AsyncTaskPool;
            break;

         case tcEvent:
            Pool = CallbackTaskPool;
            break;

         default:
            Pool = NULL;
            break;
      }

      SS1HAL_ReleaseLock();

      SS1_LOGD("Requested context %s.", (Context == tcWork ? "Work" : (Context == tcEvent ? "Event" : "None")));

      if((Pool) && (TaskHandler))
         ret_val = TaskPool_QueueTask(Pool, TaskHandler, Message);
      else
         ret_val = BT_STATUS_PARM_INVALID;
   }
   else
      ret_val = BT_STATUS_BUSY;

   return(ret_val);
}

   /* Callback that is registered to be notified after initialization.  */
static void BTPMDispatchCallback_Initialization(void *CallbackParameter)
{
   int          Result;
   TaskStatus_t TaskStatus;

   SS1_LOGD("Enter: %p", CallbackParameter);

   TaskStatus = (TaskStatus_t)CallbackParameter;
   
   HALVEND_PostStackInitialization();

   /* Register the Device Manager Event Callback.                       */
   if((Result = DEVM_RegisterEventCallback(HAL_DEVMEventCallback, NULL)) > 0)
   {
      /* Callback registered.                                           */
      SS1_LOGD("Device Manager Callback Registered.");

      /* Note the Callback ID.                                          */
      DEVMEventCallbackID = (unsigned int)Result;

      /* Register the Device Manager Authentication Callback.           */
      if((Result = DEVM_RegisterAuthentication(HAL_DEVMAuthCallback, NULL)) > 0)
      {
         /* Callback registered.                                        */
         SS1_LOGD("Device Mananger Authentication Callback Registered.");

         /* Note the Callback ID.                                       */
         DEVMAuthenticationCallbackID = (unsigned int)Result;

         /* Finally, check to see if the device is already powered on.     */
         if(DEVM_QueryDevicePowerState())
         {
            SS1_LOGD("Device is already powered on.");
         }

         /* Report that the stack is successfully enabled.                 */
         TaskStatus_SetStatus(TaskStatus, TASK_STATUS_SUCCESS);
      }
      else
      {
         /* Unable to register the Device Manager Authentication        */
         /* Callback.  Cleanup and fail the initialization.             */
         SS1_LOGD("Device Manager Authentication Callback NOT Reigstered: %d.", Result);

         DEVM_UnRegisterEventCallback(DEVMEventCallbackID);

         TaskStatus_SetStatus(TaskStatus, TASK_STATUS_FAILURE);
      }
   }
   else
   {
      /* Unable to register the Device Manager Event Callback.          */
      SS1_LOGD("Device Manager Callback NOT Registered: %d.", Result);

      TaskStatus_SetStatus(TaskStatus, TASK_STATUS_FAILURE);
   }

   SS1_LOGD("Exit");
}

   /* Thread which hosts the main Bluetopia PM execution loop.          */
static void *BluetopiaPlatformManagerMainThread(void *Parameter)
{
   int                                ret_val;
   TaskStatus_t                       TaskStatus;
   BTPM_Initialization_Info_t         InitializationInfo;

   SS1_LOGD("Enter: %p", Parameter);

   if(Parameter)
   {
      TaskStatus = (TaskStatus_t)Parameter;

      BTPS_MemInitialize(&InitializationInfo, 0, sizeof(InitializationInfo));

      if(HALVEND_PreStackInitialization(&InitializationInfo))
      {

         SS1_LOGD("Setting initial debug log levels");

#ifdef BTPM_SERVICE_DEBUG_ZONES
         BTPM_DebugSetZoneMask(BTPM_SERVICE_DEBUG_ZONES | BTPM_SERVICE_DEBUG_LEVELS);
#endif
#ifdef BTPM_SERVICE_DEBUG_ZONES_PAGE_0
         BTPM_DebugSetZoneMask(BTPM_SERVICE_DEBUG_ZONES_PAGE_0 | BTPM_SERVICE_DEBUG_LEVELS);
#endif
#ifdef BTPM_SERVICE_DEBUG_ZONES_PAGE_1
         BTPM_DebugSetZoneMask(BTPM_SERVICE_DEBUG_ZONES_PAGE_1 | BTPM_SERVICE_DEBUG_LEVELS);
#endif
#ifdef BTPM_SERVICE_DEBUG_ZONES_PAGE_2
         BTPM_DebugSetZoneMask(BTPM_SERVICE_DEBUG_ZONES_PAGE_2 | BTPM_SERVICE_DEBUG_LEVELS);
#endif
#ifdef BTPM_SERVICE_DEBUG_ZONES_PAGE_3
         BTPM_DebugSetZoneMask(BTPM_SERVICE_DEBUG_ZONES_PAGE_3 | BTPM_SERVICE_DEBUG_LEVELS);
#endif

         SS1_LOGD("Starting thread main loop");

         /* Do nothing other than call the Library entry point.         */
         ret_val = BTPM_Main(&InitializationInfo, BTPMDispatchCallback_Initialization, TaskStatus);

         if(!ret_val)
         {
            SS1_LOGD("Stack exited cleanly (%d)", ret_val);
         }
         else
         {
            SS1_LOGE("Stack exited irregularly (%d)", ret_val);
            HALVEND_PostStackInitialization();
            TaskStatus_SetStatus(TaskStatus, TASK_STATUS_FAILURE);
         }
      }
      else
      {
         SS1_LOGE("PreStackInit failed");
         TaskStatus_SetStatus(TaskStatus, TASK_STATUS_FAILURE);
      }
   }

   if(StackShutdown)
      BTPS_SetEvent(StackShutdown);

   SS1_LOGD("Exit");

   return(NULL);
}


   /*********************************************************************/
   /* Android Bluetooth HAL interface functions.                        */
   /*********************************************************************/

   /* Open the Bluetooth HAL interface and receive the Bluetooth HAL    */
   /* callback routines for this module.                                */
static int SS1HAL_Init(bt_callbacks_t *Callbacks)
{
   /* XXX **NOTE** The BD_ADDR must be queriable via                    */
   /* SS1HAL_GetAdapterProperties after this call completes.            */
   int          ret_val;
   TaskStatus_t TaskStatus;

   SS1_LOGD("Enter: %p", Callbacks);

   if((Callbacks) && (Callbacks->size >= sizeof(bt_callbacks_t)))
   {
      if(!Initialized)
      {
         if(SS1HAL_AcquireLock(BTPS_INFINITE_WAIT))
         {
            BtCallbacks = Callbacks;
            ret_val     = BT_STATUS_SUCCESS;

            if((!StackShutdown) && (!StackThread))
            {
               if((StackShutdown = BTPS_CreateEvent(FALSE)) != NULL)
               {
                  if((TaskStatus = TaskStatus_Create()) != NULL)
                  {
                     SS1_LOGD("Starting Bluetopia PM stack");
		     ALOGI("[Benson] %s() Starting Bluetopia PM stack\n",__FUNCTION__);

                     /* Start the main stack thread and wait for it the */
                     /* initialization to complete.                     */
                     /* **NOTE** The TaskStatus will be destroyed by the*/
                     /*          stack thread, so we only destroy it    */
                     /*          here if the stack thread cannot be     */
                     /*          started.                               */
                     if((StackThread = BTPS_CreateThread(BluetopiaPlatformManagerMainThread, 32768, TaskStatus)) != NULL)
                     {
                        SS1HAL_ReleaseLock();

                        SS1_LOGD("PM stack thread started, waiting for init");
			ALOGI("[Benson] %s() PM stack thread started, waiting for init\n",__FUNCTION__);

                        if(TaskStatus_Wait(TaskStatus) == TASK_STATUS_SUCCESS)
                        {
                           /* Stack initialized.                        */
                           Initialized = TRUE;
                        }
                        else
                        {
                           SS1_LOGD("PM stack thread init failed.");
			   ALOGI("[Benson] %s() PM stack thread init failed.\n",__FUNCTION__);
                           /* Stack initialization failed.                 */
                           ret_val = BT_STATUS_FAIL;
                        }
                     }
                     else
                     {
                        SS1_LOGD("Unable to start PM stack");
			ALOGI("[Benson] %s() Unable to start PM stack\n",__FUNCTION__);

                        ret_val = BT_STATUS_FAIL;
                        SS1HAL_ReleaseLock();
                     }

                     TaskStatus_Destroy(TaskStatus);
                  }
                  else
                  {
                     SS1_LOGD("Unable to initialize threading states");
		     ALOGI("[Benson] %s() Unable to initialize threading states\n",__FUNCTION__);
                     ret_val = BT_STATUS_FAIL;
                     SS1HAL_ReleaseLock();
                  }
               }
               else
               {
                  SS1_LOGD("Unable to initialize threading events");
		  ALOGI("[Benson] %s() Unable to initialize threading events\n",__FUNCTION__);
                  ret_val = BT_STATUS_FAIL;
                  SS1HAL_ReleaseLock();
               }
            }
            else
            {
               SS1_LOGD("Incorrect state for initializing the Bluetooth stack (%p, %p)", StackShutdown, StackThread);
	       ALOGI("[Benson] %s() Incorrect state for initializing the Bluetooth stack (%p, %p)\n",__FUNCTION__, StackShutdown, StackThread);
               ret_val = BT_STATUS_NOT_READY;
               SS1HAL_ReleaseLock();
            }

            /* If initialization failed, clean up all resources.  We    */
            /* do not clean up if we were in an unexpected state ("not  */
            /* ready") because it likely indicates that the stack is    */
            /* already initialized.                                     */
            if(ret_val != BT_STATUS_SUCCESS)
               SS1HAL_Cleanup();
         }
         else
         {
            SS1_LOGD("Unable to acquire HAL lock");
	    ALOGI("[Benson] %s() Unable to acquire HAL lock\n",__FUNCTION__);
            ret_val = BT_STATUS_FAIL;
         }
      }
      else
      {
         SS1_LOGD("Stack already initialized");
	 ALOGI("[Benson] %s() Stack already initialized\n",__FUNCTION__);
         ret_val = BT_STATUS_SUCCESS;
      }
   }
   else
      ret_val = BT_STATUS_PARM_INVALID;

   ALOGI("[Benson] %s() Exit: %d\n",__FUNCTION__, ret_val);
   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* Enable Bluetooth.  Power on the stack and set the device          */
   /* connectable & pairable.                                           */
static int SS1HAL_Enable(void)
{
   int ret_val;

   SS1_LOGD("Enter");

   if(BtCallbacks)
   {
      if((ret_val = DEVM_PowerOnDevice()) == 0)
      {
         /* Device power-on successfully started.                       */
         SS1_LOGD("BT chipset initialized");
         ALOGI("[Benson] %s() BT chipset initialized\n", __FUNCTION__);
         ret_val = BT_STATUS_SUCCESS;
      }
      else
      {
         SS1_LOGE("Unable to intialize the BT chipset (%d)", ret_val);
         ALOGI("[Benson] %s() Unable to intialize the BT chipset (%d)\n",__FUNCTION__, ret_val);
	 if(ret_val == -4){
		 ALOGI("[Benson] %s() setprop\n",__FUNCTION__);
		 property_set("bluetooth.halstatus", "-4");
	 }

         ret_val = BT_STATUS_FAIL;
      }
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);
   ALOGI("[Benson] %s() Exit: %d\n",__FUNCTION__, ret_val);

   return(ret_val);
}

   /* Disable Bluetooth.  Power down the stack.                         */
static int SS1HAL_Disable(void)
{
   int ret_val;

   SS1_LOGD("Enter");

   if(BtCallbacks)
   {
      if((ret_val = DEVM_PowerOffDevice()) == 0){
         ret_val = BT_STATUS_SUCCESS;
	 ALOGI("[Benson] %s() BT_STATUS_SUCCESS\n",__FUNCTION__);
      }else
      {
         //FIXME Handle return codes properly
         ret_val = BT_STATUS_DONE;
	 ALOGI("[Benson] %s() BT_STATUS_DONE\n",__FUNCTION__);
      }
   }
   else{
      ret_val = BT_STATUS_NOT_READY;
      ALOGI("[Benson] %s() BT_STATUS_NOT_READY\n",__FUNCTION__);
   }
   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* Closes the interface.  Shutdown the PM Server main thread.        */
static void SS1HAL_Cleanup(void)
{
   Boolean_t    Locked;
   unsigned int Index;

   SS1_LOGD("Enter");
   ALOGI("[Benson] %s() Enter\n",__FUNCTION__);

   if(SS1HAL_AcquireLock(BTPS_INFINITE_WAIT))
   {
      Locked = TRUE;

      BtCallbacks = NULL;

      /* Clean up all initialized profile modules.                   */
      for(Index = 0; Index < (sizeof(ProfileList) / sizeof(ProfileList[0])); Index++)
      {
         if(ProfileList[Index].Cleanup)
            ProfileList[Index].Cleanup();
      }

      /* If the stack is running, shut it down.                */
      if(StackShutdown)
      {
         /* Check whether the main stack thread has already    */
         /* exited.                                            */
         if(!BTPS_WaitEvent(StackShutdown, BTPS_NO_WAIT))
         {
            /* Stack thread is still running, so tell it to    */
            /* shutdown.                                       */
            if(BTPM_ShutdownService() == 0)
            {
               SS1HAL_ReleaseLock();

               /* Shutdown request succesful. Wait for the     */
               /* thread to complete.                          */
               BTPS_WaitEvent(StackShutdown, BTPS_INFINITE_WAIT);

               Locked = SS1HAL_AcquireLock(BTPS_INFINITE_WAIT);
            }
         }

         /* Stack shutdown complete, so clean up.           */
         BTPS_CloseEvent(StackShutdown);
         StackShutdown = NULL;
         StackThread   = NULL;

         if(DEVMEventCallbackID)
         {
            DEVM_UnRegisterEventCallback(DEVMEventCallbackID);
            DEVMEventCallbackID = 0;
         }

         if(DEVMAuthenticationCallbackID)
         {
            DEVM_UnRegisterAuthentication(DEVMAuthenticationCallbackID);
            DEVMAuthenticationCallbackID = 0;
         }
      }

      /* TODO Additional resource cleanup.                        */

      Initialized = FALSE;

      if(Locked)
         SS1HAL_ReleaseLock();
   }

   ALOGI("[Benson] %s() Exit\n",__FUNCTION__);
   SS1_LOGD("Exit");
}

   /* Get all Bluetooth Adapter properties at init                      */
static int SS1HAL_GetAdapterProperties(void)
{
   int                            ret_val;
   CallbackTask_t                 CallbackTask;
   DEVM_Local_Device_Properties_t LocalProps;

   SS1_LOGD("Enter");

   if(Initialized)
   {
      /* Prepare the callback task object.                              */
      CallbackTask.Type = ctReportAdapterProperties;

      /* Build the property list.                                       */
      if(DEVM_QueryLocalDeviceProperties(&LocalProps) == 0)
      {
         CallbackTask.TaskData.ReportAdapterPropertiesData.Status           = BT_STATUS_SUCCESS;
         CallbackTask.TaskData.ReportAdapterPropertiesData.NumberProperties = CollectLocalProperties(HAL_BT_PROP_ALL, &LocalProps, &(CallbackTask.TaskData.ReportAdapterPropertiesData.Properties));
      }
      else
         CallbackTask.TaskData.ReportAdapterPropertiesData.Status = BT_STATUS_FAIL;

      /* Queue the callback event.                                */
      if((ret_val = SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask)) != BT_STATUS_SUCCESS)
      {
         /* Queuing failed.  Clean up message.                    */
         if(CallbackTask.TaskData.ReportAdapterPropertiesData.Properties)
            BTPS_FreeMemory(CallbackTask.TaskData.ReportAdapterPropertiesData.Properties);
      }
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* Get Bluetooth Adapter property of 'type'                          */
static int SS1HAL_GetAdapterProperty(bt_property_type_t PropertyType)
{
   int                            Result;
   int                            ret_val;
   unsigned int                   PropertyMask;
   bt_property_t                  PropList[16];
   CallbackTask_t                 CallbackTask;
   DEVM_Local_Device_Properties_t LocalProps;

   SS1_LOGD("Enter: %d", PropertyType);

   if(Initialized)
   {
      /* Prepare the callback task object.                              */
      CallbackTask.Type = ctReportAdapterProperties;

      CallbackTask.TaskData.ReportAdapterPropertiesData.Status = BT_STATUS_SUCCESS;

      if((Result = DEVM_QueryLocalDeviceProperties(&LocalProps)) == 0)
      {
         PropertyMask = 0;

         switch(PropertyType)
         {
            case BT_PROPERTY_BDNAME:
               SS1_LOGD("Requested: %s", "Bluetooth Device Name");

               PropertyMask = HAL_BT_PROP_BDNAME;
               break;

            case BT_PROPERTY_BDADDR:
               SS1_LOGD("Requested: %s", "Bluetooth Device Address");

               PropertyMask = HAL_BT_PROP_BDADDR;
               break;

            case BT_PROPERTY_UUIDS:
               SS1_LOGD("Requested: %s", "UUIDs");

               PropertyMask = HAL_BT_PROP_UUIDS;
               break;

            case BT_PROPERTY_CLASS_OF_DEVICE:
               SS1_LOGD("Requested: %s", "Class of Device");

               PropertyMask = HAL_BT_PROP_CLASS_OF_DEVICE;
               break;

            case BT_PROPERTY_TYPE_OF_DEVICE:
               SS1_LOGD("Requested: %s", "Type of Device");

               PropertyMask = HAL_BT_PROP_TYPE_OF_DEVICE;
               break;

            case BT_PROPERTY_SERVICE_RECORD:
               SS1_LOGD("Requested: %s", "Service Record");

               /* **NOTE ** Android does not currently use the    */
               /*           BT_PROPERTY_SERVICE_RECORD property.  */
               CallbackTask.TaskData.ReportAdapterPropertiesData.Status = BT_STATUS_UNSUPPORTED;
               break;

            case BT_PROPERTY_ADAPTER_SCAN_MODE:
               SS1_LOGD("Requested: %s", "Scan Mode");

               PropertyMask = HAL_BT_PROP_ADAPTER_SCAN_MODE;
               break;

            case BT_PROPERTY_ADAPTER_BONDED_DEVICES:
               SS1_LOGD("Requested: %s", "Bonded Devices");

               PropertyMask = HAL_BT_PROP_ADAPTER_BONDED_DEVICES;
               break;

            case BT_PROPERTY_ADAPTER_DISCOVERY_TIMEOUT:
               SS1_LOGD("Requested: %s", "Discovery Timeout");

               PropertyMask = HAL_BT_PROP_ADAPTER_DISCOVERY_TIMEOUT;
               break;

            default:
               SS1_LOGD("Requested: Unrecognized property %d", PropertyType);
               break;
         }

         if(PropertyMask)
         {
            CallbackTask.TaskData.ReportAdapterPropertiesData.NumberProperties = CollectLocalProperties(PropertyMask, &LocalProps, &(CallbackTask.TaskData.ReportAdapterPropertiesData.Properties));

            if(CallbackTask.TaskData.ReportAdapterPropertiesData.NumberProperties > 0)
               ret_val = BT_STATUS_SUCCESS;
            else
               ret_val = BT_STATUS_FAIL;
         }
         else
         {
            /* No recognized property was requested.                    */
            ret_val = BT_STATUS_PARM_INVALID;
         }
      }
      else
      {
         if((Result == BTPM_ERROR_CODE_LOCAL_DEVICE_NOT_INITIALIZED) || (Result == BTPM_ERROR_CODE_LOCAL_DEVICE_POWERED_DOWN))
         {
            /* The stack is not fully enabled.  Android expects to be   */
            /* able to query the Name and Address properties, even in   */
            /* this state.                                              */
            switch(PropertyType)
            {
               case BT_PROPERTY_BDNAME:
                  SS1_LOGD("Requested: %s", "Bluetooth Device Name");
                  SS1_LOGD("Returning empty name because stack is not yet enabled.");

                  PropList[0].type = BT_PROPERTY_BDNAME;
                  PropList[0].len  = 0;
                  PropList[0].val  = NULL;

                  //XXX Get a real Name

                  ret_val = BT_STATUS_SUCCESS;
                  break;

               case BT_PROPERTY_BDADDR:
                  SS1_LOGD("Requested: %s", "Bluetooth Device Address");
                  SS1_LOGD("Returning empty address because stack is not yet enabled.");

                  PropList[0].type = BT_PROPERTY_BDADDR;
                  PropList[0].len  = 0;
                  PropList[0].val  = NULL;

                  //XXX Get a real BD_ADDR

                  ret_val = BT_STATUS_SUCCESS;
                  break;

               default:
                  SS1_LOGD("Requested: Unrecognized property %d", PropertyType);

                  ret_val = BT_STATUS_PARM_INVALID;
                  break;
            }

            if(ret_val == BT_STATUS_SUCCESS)
            {
               /* Build callback data buffer.                     */
               CallbackTask.TaskData.ReportAdapterPropertiesData.NumberProperties = 1;
               CallbackTask.TaskData.ReportAdapterPropertiesData.Properties       = PackBluetoothProperties(PropList, CallbackTask.TaskData.ReportAdapterPropertiesData.NumberProperties);
            }
         }
         else
         {
            SS1_LOGD("Error querying Local Device Properties (%d)", Result);
            ret_val = BT_STATUS_FAIL;
         }
      }

      if(ret_val == BT_STATUS_SUCCESS)
      {
         if((ret_val = SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask)) != BT_STATUS_SUCCESS)
         {
            /* Queuing failed.  Clean up message.                    */
            if(CallbackTask.TaskData.ReportAdapterPropertiesData.Properties)
               BTPS_FreeMemory(CallbackTask.TaskData.ReportAdapterPropertiesData.Properties);
         }
      }
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* Set Bluetooth Adapter property of 'type' Based on the type, val   */
   /* shall be one of * bt_bdaddr_t or bt_bdname_t or bt_scanmode_t etc */
static int SS1HAL_SetAdapterProperty(const bt_property_t *Property)
{
   int                            Result;
   int                            ret_val;
   CallbackTask_t                 CallbackTask;
   DEVM_Local_Device_Properties_t LocalProps;

   SS1_LOGD("Enter: {%u,%d}", (Property?Property->type:((unsigned int)(-1))), (Property?Property->len:-1));

   if(Initialized)
   {
      if(Property)
      {
         BTPS_MemInitialize(&LocalProps, 0, sizeof(LocalProps));

         CallbackTask.Type = ctReportAdapterProperties;

         CallbackTask.TaskData.ReportAdapterPropertiesData.Status           = BT_STATUS_SUCCESS;
         CallbackTask.TaskData.ReportAdapterPropertiesData.NumberProperties = 0;
         CallbackTask.TaskData.ReportAdapterPropertiesData.Properties       = NULL;

         switch(Property->type)
         {
            case BT_PROPERTY_BDNAME:
               SS1_LOGD("Requested: %s", "Bluetooth Device Name");

               if(Property->val)
               {
                  LocalProps.DeviceNameLength = Property->len;
                  BTPS_MemCopy(LocalProps.DeviceName, Property->val, LocalProps.DeviceNameLength);

                  if(DEVM_UpdateLocalDeviceProperties(DEVM_UPDATE_LOCAL_DEVICE_PROPERTIES_DEVICE_NAME, &LocalProps) == 0)
                  {
                     ret_val = BT_STATUS_SUCCESS;
                  }
                  else
                  {
                     /* Update failed.                                  */
                     //XXX More accurate error
                     ret_val = BT_STATUS_FAIL;
                  }
               }
               else
                  ret_val = BT_STATUS_PARM_INVALID;

               break;

            case BT_PROPERTY_BDADDR:
               SS1_LOGD("Requested: %s", "Bluetooth Device Address");

               ret_val = BT_STATUS_UNSUPPORTED;
               break;

            case BT_PROPERTY_UUIDS:
               SS1_LOGD("Requested: %s", "UUIDs");

               /* XXX Android has a setUuids API in                  */
               /*     AdapterProperties, but it is never used. I'm   */
               /*     not sure what setting this would actually      */
               /*     imply.                                         */
               ret_val = BT_STATUS_UNSUPPORTED;
               break;

            case BT_PROPERTY_CLASS_OF_DEVICE:
               SS1_LOGD("Requested: %s", "Class of Device");

               if(Property->val)
               {
                  ConvertAndroidCoDToBluetopia(*((uint32_t *)(Property->val)), &(LocalProps.ClassOfDevice));

                  if(DEVM_UpdateLocalDeviceProperties(DEVM_UPDATE_LOCAL_DEVICE_PROPERTIES_CLASS_OF_DEVICE, &LocalProps) == 0)
                  {
                     ret_val = BT_STATUS_SUCCESS;
                  }
                  else
                  {
                     /* Update failed.                                  */
                     //XXX More accurate error
                     ret_val = BT_STATUS_FAIL;
                  }
               }
               else
                  ret_val = BT_STATUS_PARM_INVALID;

               break;

            case BT_PROPERTY_TYPE_OF_DEVICE:
               SS1_LOGD("Requested: %s", "Type of Device");

               ret_val = BT_STATUS_UNSUPPORTED;
               break;

            case BT_PROPERTY_SERVICE_RECORD:
               SS1_LOGD("Requested: %s", "Service Record");

               /* **NOTE ** Android does not currently use the    */
               /*           BT_PROPERTY_SERVICE_RECORD property.  */
               ret_val = BT_STATUS_UNSUPPORTED;
               break;

            case BT_PROPERTY_ADAPTER_SCAN_MODE:
               SS1_LOGD("Requested: %s", "Scan Mode");

               if(Property->val)
               {
                  SS1_LOGD("Checking destination sync mode: %p -> 0x%08X", Property->val, *((uint32_t *)(Property->val)));

                  if((Result = DEVM_QueryLocalDeviceProperties(&LocalProps)) == 0)
                  {
                     /* Assume success unless something fails in the    */
                     /* process.                                        */
                     ret_val = BT_STATUS_SUCCESS;

                     /* If we are moving to either Connectable or No */
                     /* Scanning, disable discoverability.           */
                     if((*((bt_scan_mode_t *)(Property->val)) == BT_SCAN_MODE_CONNECTABLE) || (*((bt_scan_mode_t *)(Property->val)) == BT_SCAN_MODE_NONE))
                     {
                        if(LocalProps.DiscoverableMode)
                        {
                           SS1_LOGD("Need to be non-discoverable. Turning off DISCOVERABLE mode.");

                           LocalProps.DiscoverableMode = FALSE;

                           if((Result = DEVM_UpdateLocalDeviceProperties(DEVM_UPDATE_LOCAL_DEVICE_PROPERTIES_DISCOVERABLE_MODE, &LocalProps)) < 0)
                           {
                              /* Update failed.                      */
                              SS1_LOGD("Unable to update local properties (%d)", Result);

                              //XXX More accurate error
                              ret_val = BT_STATUS_FAIL;
                           }
                        }
                        else
                           SS1_LOGD("Need to be non-discoverable. DISCOVERABLE mode already disabled.");
                     }

                     /* If we are moving to the No Scanning state,   */
                     /* disable Connectability.                      */
                     if(*((bt_scan_mode_t *)(Property->val)) == BT_SCAN_MODE_NONE)
                     {
                        if(LocalProps.ConnectableMode)
                        {
                           SS1_LOGD("Need to be non-connectable. Turning off CONNECTABLE mode.");

                           LocalProps.ConnectableMode = FALSE;

                           if((Result = DEVM_UpdateLocalDeviceProperties(DEVM_UPDATE_LOCAL_DEVICE_PROPERTIES_CONNECTABLE_MODE, &LocalProps)) < 0)
                           {
                              /* Update failed.                      */
                              SS1_LOGD("Unable to update local properties (%d)", Result);

                              //XXX More accurate error
                              ret_val = BT_STATUS_FAIL;
                           }
                        }
                        else
                           SS1_LOGD("Need to be non-connectable. CONNECTABLE mode already disabled.");
                     }

                     /* If we are moving to either Connectable or    */
                     /* Discoverable states, enable Connectability.  */
                     if((*((bt_scan_mode_t *)(Property->val)) == BT_SCAN_MODE_CONNECTABLE) || (*((bt_scan_mode_t *)(Property->val)) == BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE))
                     {
                        if(LocalProps.ConnectableMode == FALSE)
                        {
                           SS1_LOGD("Need to be connectable. Turning on CONNECTABLE mode.");

                           LocalProps.ConnectableMode = TRUE;

                           if((Result = DEVM_UpdateLocalDeviceProperties(DEVM_UPDATE_LOCAL_DEVICE_PROPERTIES_CONNECTABLE_MODE, &LocalProps)) < 0)
                           {
                              /* Update failed.                      */
                              SS1_LOGD("Unable to update local properties (%d)", Result);

                              //XXX More accurate error
                              ret_val = BT_STATUS_FAIL;
                           }
                        }
                        else
                           SS1_LOGD("Need to be connectable. CONNECTABLE mode already enabled.");
                     }

                     /* If we are moving to the Discoverable state,  */
                     /* enable Discoverability.                      */
                     if(*((bt_scan_mode_t *)(Property->val)) == BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE)
                     {
                        if(LocalProps.DiscoverableMode == FALSE)
                        {
                           SS1_LOGD("Need to be discoverable. Turning on DISCOVERABLE mode.");

                           LocalProps.DiscoverableMode        = TRUE;
                           LocalProps.DiscoverableModeTimeout = DiscoverableTimeout;

                           if((Result = DEVM_UpdateLocalDeviceProperties(DEVM_UPDATE_LOCAL_DEVICE_PROPERTIES_DISCOVERABLE_MODE, &LocalProps)) < 0)
                           {
                              /* Update failed.                      */
                              SS1_LOGD("Unable to update local properties (%d)", Result);

                              //XXX More accurate error
                              ret_val = BT_STATUS_FAIL;
                           }
                        }
                        else
                           SS1_LOGD("Need to be discoverable. DISCOVERABLE mode already enabled.");
                     }
                  }
                  else
                  {
                     SS1_LOGD("Unable to access local device properties (%d)", Result);
                     ret_val = BT_STATUS_FAIL;
                  }
               }
               else
                  ret_val = BT_STATUS_PARM_INVALID;

               break;

            case BT_PROPERTY_ADAPTER_BONDED_DEVICES:
               SS1_LOGD("Requested: %s", "Bonded Devices");

               ret_val = BT_STATUS_UNSUPPORTED;
               break;

            case BT_PROPERTY_ADAPTER_DISCOVERY_TIMEOUT:
               SS1_LOGD("Requested: %s", "Discovery Timeout");

               if(Property->val)
               {
                  DiscoverableTimeout = *((unsigned int *)(Property->val));

                  ret_val             = BT_STATUS_SUCCESS;

                  /* This property is not managed by Bluetopia PM, so   */
                  /* generate an event to announce the property change  */
                  /* to Android.                                        */
                  if(!DEVM_QueryLocalDeviceProperties(&LocalProps))
                  {
                     CallbackTask.TaskData.ReportAdapterPropertiesData.Status           = BT_STATUS_SUCCESS;
                     CallbackTask.TaskData.ReportAdapterPropertiesData.NumberProperties = CollectLocalProperties(HAL_BT_PROP_ADAPTER_DISCOVERY_TIMEOUT, &LocalProps, &(CallbackTask.TaskData.ReportAdapterPropertiesData.Properties));

                     if(SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask) != BT_STATUS_SUCCESS)
                     {
                        /* Queuing failed.  Clean up message.           */
                        if(CallbackTask.TaskData.ReportAdapterPropertiesData.Properties)
                           BTPS_FreeMemory(CallbackTask.TaskData.ReportAdapterPropertiesData.Properties);
                     }
                  }
               }
               else
                  ret_val = BT_STATUS_PARM_INVALID;

               break;

            default:
               SS1_LOGD("Requested: Unrecognized property %d", Property->type);

               ret_val = BT_STATUS_PARM_INVALID;

               break;
         }
      }
      else
         ret_val = BT_STATUS_PARM_INVALID;
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* Get all Remote Device properties                                  */
static int SS1HAL_GetRemoteDeviceProperties(bt_bdaddr_t *RemoteAddress)
{
   int                             ret_val;
   BD_ADDR_t                       BD_ADDR;
   CallbackTask_t                  CallbackTask;
   DEVM_Remote_Device_Properties_t RemoteProps;

   SS1_LOGD("Enter: %02X:%02X:%02X:%02X:%02X:%02X", (RemoteAddress?RemoteAddress->address[0]:0), (RemoteAddress?RemoteAddress->address[1]:0), (RemoteAddress?RemoteAddress->address[2]:0), (RemoteAddress?RemoteAddress->address[3]:0), (RemoteAddress?RemoteAddress->address[4]:0), (RemoteAddress?RemoteAddress->address[5]:0));

   if(Initialized)
   {
      if(RemoteAddress)
      {
         /* Prepare the callback task object.                           */
         CallbackTask.Type = ctReportRemoteProperties;

         /* Build the property list.                                    */
         ConvertAndroidAddrToBluetopia(RemoteAddress, &BD_ADDR);

         if(!DEVM_QueryRemoteDeviceProperties(BD_ADDR, 0, &RemoteProps))
         {
            CallbackTask.TaskData.ReportRemotePropertiesData.Status           = BT_STATUS_SUCCESS;
            CallbackTask.TaskData.ReportRemotePropertiesData.RemoteAddress    = *RemoteAddress;
            CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties = CollectRemoteProperties(HAL_BT_PROP_ALL, &RemoteProps, &(CallbackTask.TaskData.ReportRemotePropertiesData.Properties));

            /* Queue the callback event.                                */
            if((ret_val = SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask)) != BT_STATUS_SUCCESS)
            {
               /* Queuing failed.  Clean up message.                    */
               if(CallbackTask.TaskData.ReportRemotePropertiesData.Properties)
                  BTPS_FreeMemory(CallbackTask.TaskData.ReportRemotePropertiesData.Properties);
            }
         }
         else
            ret_val = BT_STATUS_FAIL;
      }
      else
         ret_val = BT_STATUS_PARM_INVALID;
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* Get Remote Device property of 'type'                              */
static int SS1HAL_GetRemoteDeviceProperty(bt_bdaddr_t *RemoteAddress, bt_property_type_t PropertyType)
{
   int                             ret_val;
   BD_ADDR_t                       BD_ADDR;
   CallbackTask_t                  CallbackTask;
   DEVM_Remote_Device_Properties_t RemoteProps;
   
#if SS1_PLATFORM_SDK_VERSION >= 18
   bt_remote_version_t             RemoteVersion;
#endif

   SS1_LOGD("Enter: %02X:%02X:%02X:%02X:%02X:%02X, %d", (RemoteAddress?RemoteAddress->address[0]:0), (RemoteAddress?RemoteAddress->address[1]:0), (RemoteAddress?RemoteAddress->address[2]:0), (RemoteAddress?RemoteAddress->address[3]:0), (RemoteAddress?RemoteAddress->address[4]:0), (RemoteAddress?RemoteAddress->address[5]:0), PropertyType);

   if(Initialized)
   {
      if(RemoteAddress)
      {
         /* Prepare the callback task object.                           */
         CallbackTask.Type = ctReportRemoteProperties;

         /* Build the property list.                                    */
         ConvertAndroidAddrToBluetopia(RemoteAddress, &BD_ADDR);

         CallbackTask.TaskData.ReportRemotePropertiesData.Status        = BT_STATUS_SUCCESS;
         CallbackTask.TaskData.ReportRemotePropertiesData.RemoteAddress = *RemoteAddress;

         if(!DEVM_QueryRemoteDeviceProperties(BD_ADDR, 0, &RemoteProps))
         {
            ret_val = BT_STATUS_SUCCESS;

            switch(PropertyType)
            {
               /* Description - Bluetooth Device Name                   */
               /* Access mode - Adapter name can be GET/SET.            */
               /* Data type - bt_bdname_t                               */
               case BT_PROPERTY_BDNAME:
                  SS1_LOGD("Requested: %s", "Bluetooth Device Name");

                  CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties = CollectRemoteProperties(HAL_BT_PROP_BDNAME, &RemoteProps, &(CallbackTask.TaskData.ReportRemotePropertiesData.Properties));

                  if(CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties == 0)
                     ret_val = BT_STATUS_FAIL;

                  break;

               /* Description - Bluetooth Device Address                */
               /* Access mode - Only GET.                               */
               /* Data type - bt_bdaddr_t                               */
               case BT_PROPERTY_BDADDR:
                  SS1_LOGD("Requested: %s", "Bluetooth Device Address");

                  CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties = CollectRemoteProperties(HAL_BT_PROP_BDADDR, &RemoteProps, &(CallbackTask.TaskData.ReportRemotePropertiesData.Properties));

                  if(CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties == 0)
                     ret_val = BT_STATUS_FAIL;

                  break;

               /* Description - Bluetooth Service 128-bit UUIDs         */
               /* Access mode - Only GET.                               */
               /* Data type - Array of bt_uuid_t (Array size inferred   */
               /*             from property length).                    */
               case BT_PROPERTY_UUIDS:
                  SS1_LOGD("Requested: %s", "UUIDs");

                  CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties = CollectRemoteProperties(HAL_BT_PROP_UUIDS, &RemoteProps, &(CallbackTask.TaskData.ReportRemotePropertiesData.Properties));

                  if(CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties == 0)
                     ret_val = BT_STATUS_FAIL;

                  break;

               /* Description - Bluetooth Class of Device as found in   */
               /*               Assigned Numbers                        */
               /* Access mode - Only GET.                               */
               /* Data type - uint32_t.                                 */
               case BT_PROPERTY_CLASS_OF_DEVICE:
                  SS1_LOGD("Requested: %s", "Class of Device");

                  CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties = CollectRemoteProperties(HAL_BT_PROP_CLASS_OF_DEVICE, &RemoteProps, &(CallbackTask.TaskData.ReportRemotePropertiesData.Properties));

                  if(CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties == 0)
                     ret_val = BT_STATUS_FAIL;

                  break;

               /* Description - Device Type - BREDR, BLE or DUAL Mode   */
               /* Access mode - Only GET.                               */
               /* Data type - bt_device_type_t                          */
               case BT_PROPERTY_TYPE_OF_DEVICE:
                  SS1_LOGD("Requested: %s", "Type of Device");

                  CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties = CollectRemoteProperties(HAL_BT_PROP_TYPE_OF_DEVICE, &RemoteProps, &(CallbackTask.TaskData.ReportRemotePropertiesData.Properties));

                  if(CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties == 0)
                     ret_val = BT_STATUS_FAIL;

                  break;

               /* Description - Bluetooth Service Record                */
               /* Access mode - Only GET.                               */
               /* Data type - bt_service_record_t                       */
               case BT_PROPERTY_SERVICE_RECORD:
                  SS1_LOGD("Requested: %s", "Service Record");

                  /* **NOTE ** Android does not currently use the       */
                  /*           BT_PROPERTY_SERVICE_RECORD property.     */
                  ret_val = BT_STATUS_UNSUPPORTED;
                  break;

               /* Description - User defined friendly name of the remote*/
               /*               device                                  */
               /* Access mode - GET and SET                             */
               /* Data type - bt_bdname_t.                              */
               case BT_PROPERTY_REMOTE_FRIENDLY_NAME:
                  SS1_LOGD("Requested: %s", "Friendly Name");

                  CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties = CollectRemoteProperties(HAL_BT_PROP_REMOTE_FRIENDLY_NAME, &RemoteProps, &(CallbackTask.TaskData.ReportRemotePropertiesData.Properties));

                  if(CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties == 0)
                     ret_val = BT_STATUS_FAIL;

                  break;

               /* Description - RSSI value of the inquired remote device*/
               /* Access mode - Only GET.                               */
               /* Data type - int32_t.                                  */
               case BT_PROPERTY_REMOTE_RSSI:
                  SS1_LOGD("Requested: %s", "RSSI");

                  CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties = CollectRemoteProperties(HAL_BT_PROP_REMOTE_RSSI, &RemoteProps, &(CallbackTask.TaskData.ReportRemotePropertiesData.Properties));

                  if(CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties == 0)
                     ret_val = BT_STATUS_FAIL;

                  break;

#if SS1_PLATFORM_SDK_VERSION >= 18
               /* Description - Remote version info                     */
               /* Access mode - SET/GET.                                */
               /* Data type - bt_remote_version_t.                      */
               case BT_PROPERTY_REMOTE_VERSION_INFO:
                  SS1_LOGD("Requested: %s", "Version Info");

                  /* **NOTE ** Android does not currently use the       */
                  /*           BT_PROPERTY_REMOTE_VERSION_INFO property.*/
                  RemoteVersion.version      = 0;
                  RemoteVersion.sub_ver      = 0;
                  RemoteVersion.manufacturer = 0;

                  ret_val = BT_STATUS_UNSUPPORTED;
                  break;
#endif

               case BT_PROPERTY_REMOTE_DEVICE_TIMESTAMP:
                  SS1_LOGD("Requested: %s", "Device Timestamp");

                  CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties = CollectRemoteProperties(HAL_BT_PROP_REMOTE_DEVICE_TIMESTAMP, &RemoteProps, &(CallbackTask.TaskData.ReportRemotePropertiesData.Properties));

                  if(CallbackTask.TaskData.ReportRemotePropertiesData.NumberProperties == 0)
                     ret_val = BT_STATUS_FAIL;

                  break;

               default:
                  SS1_LOGD("Requested: Unrecognized property %d", PropertyType);

                  ret_val = BT_STATUS_UNSUPPORTED;

                  break;
            }
         }
         else
            ret_val = BT_STATUS_FAIL;

         /* Queue the callback event.                                */
         if(ret_val == BT_STATUS_SUCCESS)
         {
            if((ret_val = SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask)) != BT_STATUS_SUCCESS)
            {
               /* Queuing failed.  Clean up message.                    */
               if(CallbackTask.TaskData.ReportRemotePropertiesData.Properties)
                  BTPS_FreeMemory(CallbackTask.TaskData.ReportRemotePropertiesData.Properties);
            }
         }
      }
      else
         ret_val = BT_STATUS_PARM_INVALID;
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* Set Remote Device property of 'type'                              */
static int SS1HAL_SetRemoteDeviceProperty(bt_bdaddr_t *RemoteAddress, const bt_property_t *Property)
{
   int                             ret_val;
   int                             PropertyLength;
   BD_ADDR_t                       BD_ADDR;
   DEVM_Remote_Device_Properties_t RemoteProps;
   
   SS1_LOGD("Enter: %02X:%02X:%02X:%02X:%02X:%02X, {%u,%d}", (RemoteAddress?RemoteAddress->address[0]:0), (RemoteAddress?RemoteAddress->address[1]:0), (RemoteAddress?RemoteAddress->address[2]:0), (RemoteAddress?RemoteAddress->address[3]:0), (RemoteAddress?RemoteAddress->address[4]:0), (RemoteAddress?RemoteAddress->address[5]:0), (Property?Property->type:((unsigned int)(-1))), (Property?Property->len:-1));

   if(Initialized)
   {
      if((RemoteAddress) && (Property))
      {
         ConvertAndroidAddrToBluetopia(RemoteAddress, &BD_ADDR);

         switch(Property->type)
         {
            /* Description - User defined friendly name of the remote*/
            /*               device                                  */
            /* Access mode - GET and SET                             */
            /* Data type - bt_bdname_t.                              */
            case BT_PROPERTY_REMOTE_FRIENDLY_NAME:
               SS1_LOGD("Requested: Remote Friendly Name");

               if(!DEVM_QueryRemoteDeviceProperties(BD_ADDR, 0, &RemoteProps))
               {
                  BTPS_MemInitialize(RemoteProps.ApplicationData.FriendlyName, 0, sizeof(RemoteProps.ApplicationData.FriendlyName));

                  PropertyLength = Property->len;

                  if(PropertyLength < 0)
                     PropertyLength = 0;

                  if((size_t)(PropertyLength) >= sizeof(RemoteProps.ApplicationData.FriendlyName))
                     PropertyLength = (sizeof(RemoteProps.ApplicationData.FriendlyName) - 1);

                  BTPS_MemCopy(RemoteProps.ApplicationData.FriendlyName, Property->val, PropertyLength);

                  RemoteProps.ApplicationData.FriendlyNameLength = PropertyLength;

                  if(DEVM_UpdateRemoteDeviceApplicationData(BD_ADDR, &RemoteProps.ApplicationData) == 0)
                     ret_val = BT_STATUS_SUCCESS;
                  else
                     ret_val = BT_STATUS_FAIL;
               }
               else
                  ret_val = BT_STATUS_FAIL;

               break;

#if SS1_PLATFORM_SDK_VERSION >= 18
            /* Description - Remote version info                     */
            /* Access mode - SET/GET.                                */
            /* Data type - bt_remote_version_t.                      */
            case BT_PROPERTY_REMOTE_VERSION_INFO:
               SS1_LOGD("Requested: Remote Version Info");
               /* XXX */ SS1_LOGD("****** UNIMPLEMENTED ****************************************");

               ret_val = BT_STATUS_UNSUPPORTED;

               break;
#endif

            default:
               SS1_LOGD("Requested: Unrecognized property %d", Property->type);

               ret_val = BT_STATUS_UNSUPPORTED;
               break;
         }
      }
      else
         ret_val = BT_STATUS_PARM_INVALID;
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* Get Remote Device's service record for the given UUID             */
static int SS1HAL_GetRemoteServiceRecord(bt_bdaddr_t *RemoteAddress, bt_uuid_t *Uuid)
{
   int ret_val;

   SS1_LOGD("Enter: %02X:%02X:%02X:%02X:%02X:%02X, %02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", (RemoteAddress?RemoteAddress->address[0]:0), (RemoteAddress?RemoteAddress->address[1]:0), (RemoteAddress?RemoteAddress->address[2]:0), (RemoteAddress?RemoteAddress->address[3]:0), (RemoteAddress?RemoteAddress->address[4]:0), (RemoteAddress?RemoteAddress->address[5]:0), (Uuid?Uuid->uu[0]:0), (Uuid?Uuid->uu[1]:0), (Uuid?Uuid->uu[2]:0), (Uuid?Uuid->uu[3]:0), (Uuid?Uuid->uu[4]:0), (Uuid?Uuid->uu[5]:0), (Uuid?Uuid->uu[6]:0), (Uuid?Uuid->uu[7]:0), (Uuid?Uuid->uu[8]:0), (Uuid?Uuid->uu[9]:0), (Uuid?Uuid->uu[10]:0), (Uuid?Uuid->uu[11]:0), (Uuid?Uuid->uu[12]:0), (Uuid?Uuid->uu[13]:0), (Uuid?Uuid->uu[14]:0), (Uuid?Uuid->uu[15]:0));

   if(Initialized)
   {
      if((RemoteAddress) && (Uuid))
      {
         /* XXX */ SS1_LOGD("****** UNIMPLEMENTED ****************************************");
         ret_val = BT_STATUS_UNSUPPORTED;
      }
      else
         ret_val = BT_STATUS_PARM_INVALID;
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* Start SDP to get remote services                                  */
static int SS1HAL_GetRemoteServices(bt_bdaddr_t *RemoteAddress)
{
   int       ret_val;
   BD_ADDR_t BD_ADDR;

   SS1_LOGD("Enter: %02X:%02X:%02X:%02X:%02X:%02X", (RemoteAddress?RemoteAddress->address[0]:0), (RemoteAddress?RemoteAddress->address[1]:0), (RemoteAddress?RemoteAddress->address[2]:0), (RemoteAddress?RemoteAddress->address[3]:0), (RemoteAddress?RemoteAddress->address[4]:0), (RemoteAddress?RemoteAddress->address[5]:0));

   if(Initialized)
   {
      if(RemoteAddress)
      {
         ConvertAndroidAddrToBluetopia(RemoteAddress, &BD_ADDR);

         if(DEVM_QueryRemoteDeviceServices(BD_ADDR, DEVM_QUERY_REMOTE_DEVICE_SERVICES_FLAGS_FORCE_UPDATE, 0, NULL, NULL) == 0)
            ret_val = BT_STATUS_SUCCESS;
         else
            ret_val = BT_STATUS_FAIL;
      }
      else
         ret_val = BT_STATUS_PARM_INVALID;
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* Start Discovery                                                   */
static int SS1HAL_StartDiscovery(void)
{
   int            ret_val;
   CallbackTask_t CallbackTask;

   SS1_LOGD("Enter");

   if(Initialized)
   {
      ret_val = DEVM_StartDeviceDiscovery(DEFAULT_DISCOVERY_TIMEOUT_SECONDS);

      if(!ret_val)
      {
         /* The discovery was started successfully.                     */
         
         /* ** NOTE ** Android documentation implies that LE            */
         /*            devices are discoverable by this method, as well */
         /*            (http://developer.android.com/reference/android/ */
         /*            bluetooth/BluetoothGatt.html).                   */

         /* Attempt to start an LE Device Scan.  It is not mandatory    */
         /* that this scan starts successfully.                         */
         ret_val = DEVM_StartDeviceScan(DEFAULT_DISCOVERY_TIMEOUT_SECONDS);

         switch(ret_val)
         {
            case 0:
               SS1_LOGD("BLE Scan started (Timeout: %u)", DEFAULT_DISCOVERY_TIMEOUT_SECONDS);
               break;
            case BTPM_ERROR_CODE_DEVICE_DISCOVERY_IN_PROGRESS:
               SS1_LOGD("BLE Scan already running");
               break;
            default:
               SS1_LOGD("BLE Scan not started: %d (%s)", ret_val, ERR_ConvertErrorCodeToString(ret_val));
               break;
         }
         
         /* Do not report the discovery state change -- a DEVM event    */
         /* will indicate when the discovery process actually begins.   */
         ret_val = BT_STATUS_SUCCESS;
      }
      else
      {
         /* No change was made to the discovery state, so directly      */
         /* generate a result for the user.                             */
         if(ret_val == BTPM_ERROR_CODE_DEVICE_DISCOVERY_IN_PROGRESS)
         {
            CallbackTask.Type                                           = ctReportDiscoveryStateChanged;
            CallbackTask.TaskData.ReportDiscoveryStateChangedData.State = BT_DISCOVERY_STARTED;
            
            ret_val = SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);
         }
         else
            ret_val = BT_STATUS_FAIL;
      }
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* Cancel Discovery                                                  */
static int SS1HAL_CancelDiscovery(void)
{
   int            Result;
   int            ret_val;
   CallbackTask_t CallbackTask;

   SS1_LOGD("Enter");

   if(Initialized)
   {
      Result = DEVM_StopDeviceDiscovery();

      if(!Result)
      {
         /* The discovery was stopped successfully.  Expect a DEVM event*/
         /* to indicate when the discovery actually stops.              */
         ret_val = BT_STATUS_SUCCESS;
      }
      else
      {
         /* No change was made to the discovery state, so directly      */
         /* generate a result for the user.                             */
         SS1_LOGD("(%p): Error stopping discovery (%d). Faking 'stopped' event.", BTPS_CurrentThreadHandle(), Result);

         CallbackTask.Type                                           = ctReportDiscoveryStateChanged;
         CallbackTask.TaskData.ReportDiscoveryStateChangedData.State = BT_DISCOVERY_STOPPED;

         ret_val = SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);
      }
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* Create Bluetooth Bonding                                          */
static int SS1HAL_CreateBond(const bt_bdaddr_t *RemoteAddress)
{
   int            Result;
   int            ret_val;
   BD_ADDR_t      BD_ADDR;
   CallbackTask_t CallbackTask;

   SS1_LOGD("Enter: %02X:%02X:%02X:%02X:%02X:%02X", (RemoteAddress?RemoteAddress->address[0]:0), (RemoteAddress?RemoteAddress->address[1]:0), (RemoteAddress?RemoteAddress->address[2]:0), (RemoteAddress?RemoteAddress->address[3]:0), (RemoteAddress?RemoteAddress->address[4]:0), (RemoteAddress?RemoteAddress->address[5]:0));

   if(Initialized)
   {
      if(RemoteAddress)
      {
         ConvertAndroidAddrToBluetopia(RemoteAddress, &BD_ADDR);

         //XXX Add support for LE
         Result = DEVM_PairWithRemoteDevice(BD_ADDR, DEVM_PAIR_WITH_REMOTE_DEVICE_FLAGS_FORCE_PAIR);

         if((Result == 0) || (Result == BTPM_ERROR_CODE_DEVICE_PAIRING_IN_PROGRESS))
         {
            /* The pairing process has begun successfully.  Note     */
            /* that the "BONDING" state will also be anounced in the */
            /* Authentication callback upon receiving a PIN Request  */
            /* or IO Cap Request.  We announce the state to Android, */
            /* here, so that the state can be immediately reflected  */
            /* in the user interface.                                */
            CallbackTask.Type                                              = ctReportBondStateChanged;
            CallbackTask.TaskData.ReportBondStateChangedData.Status        = BT_STATUS_SUCCESS;
            CallbackTask.TaskData.ReportBondStateChangedData.State         = BT_BOND_STATE_BONDING;
            CallbackTask.TaskData.ReportBondStateChangedData.RemoteAddress = *RemoteAddress;

            SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);

            ret_val = BT_STATUS_SUCCESS;
         }
         else
            ret_val = BT_STATUS_FAIL;
      }
      else
         ret_val = BT_STATUS_PARM_INVALID;
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* Remove Bond                                                       */
static int SS1HAL_RemoveBond(const bt_bdaddr_t *RemoteAddress)
{
   int       Result;
   int       ret_val;
   BD_ADDR_t BD_ADDR;

   SS1_LOGD("Enter: %02X:%02X:%02X:%02X:%02X:%02X", (RemoteAddress?RemoteAddress->address[0]:0), (RemoteAddress?RemoteAddress->address[1]:0), (RemoteAddress?RemoteAddress->address[2]:0), (RemoteAddress?RemoteAddress->address[3]:0), (RemoteAddress?RemoteAddress->address[4]:0), (RemoteAddress?RemoteAddress->address[5]:0));

   if(Initialized)
   {
      if(RemoteAddress)
      {
         ConvertAndroidAddrToBluetopia(RemoteAddress, &BD_ADDR);

         //XXX Add support for LE
         if((Result = DEVM_UnPairRemoteDevice(BD_ADDR, 0)) == 0)
         {
            ret_val = BT_STATUS_SUCCESS;
         }
         else
         {
            /* Cannot unpair a device which is currently being paired.  */
            /* In this case, cancel the pairing attempt.                */
            if(Result == BTPM_ERROR_CODE_DEVICE_PAIRING_IN_PROGRESS)
            {
//XXX Account for in-progress authentication that was not started locally.
               if(DEVM_CancelPairWithRemoteDevice(BD_ADDR) == 0)
                  ret_val = BT_STATUS_SUCCESS;
               else
                  ret_val = BT_STATUS_FAIL;
            }
            else
               ret_val = BT_STATUS_FAIL;
         }
      }
      else
         ret_val = BT_STATUS_PARM_INVALID;
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* Cancel Bond                                                       */
static int SS1HAL_CancelBond(const bt_bdaddr_t *RemoteAddress)
{
   int                               Result;
   int                               ret_val;
   BD_ADDR_t                         BD_ADDR;
   CallbackTask_t                    CallbackTask;
   DEVM_Remote_Device_Properties_t   RemoteProps;
   DEVM_Authentication_Information_t AuthInfo;

   SS1_LOGD("Enter: %02X:%02X:%02X:%02X:%02X:%02X", (RemoteAddress?RemoteAddress->address[0]:0), (RemoteAddress?RemoteAddress->address[1]:0), (RemoteAddress?RemoteAddress->address[2]:0), (RemoteAddress?RemoteAddress->address[3]:0), (RemoteAddress?RemoteAddress->address[4]:0), (RemoteAddress?RemoteAddress->address[5]:0));

   if(Initialized)
   {
      if(RemoteAddress)
      {
         ConvertAndroidAddrToBluetopia(RemoteAddress, &BD_ADDR);

         if(RetrievePendingAuth(&AuthInfo))
         {
            if(COMPARE_BD_ADDR(AuthInfo.BD_ADDR, BD_ADDR))
            {
               switch(AuthInfo.AuthenticationAction & DEVM_AUTHENTICATION_ACTION_AUTHENTICATION_ACTION_MASK)
               {
                  case DEVM_AUTHENTICATION_ACTION_PIN_CODE_REQUEST:
                     AuthInfo.AuthenticationAction = (DEVM_AUTHENTICATION_ACTION_PIN_CODE_RESPONSE | (AuthInfo.AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
                     break;

                  case DEVM_AUTHENTICATION_ACTION_USER_CONFIRMATION_REQUEST:
                     AuthInfo.AuthenticationAction = (DEVM_AUTHENTICATION_ACTION_USER_CONFIRMATION_RESPONSE | (AuthInfo.AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
                     break;

                  case DEVM_AUTHENTICATION_ACTION_PASSKEY_REQUEST:
                     AuthInfo.AuthenticationAction = (DEVM_AUTHENTICATION_ACTION_PASSKEY_RESPONSE | (AuthInfo.AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
                     break;

                  case DEVM_AUTHENTICATION_ACTION_OUT_OF_BAND_DATA_REQUEST:
                     AuthInfo.AuthenticationAction = (DEVM_AUTHENTICATION_ACTION_OUT_OF_BAND_DATA_RESPONSE | (AuthInfo.AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
                     break;

                  case DEVM_AUTHENTICATION_ACTION_IO_CAPABILITIES_REQUEST:
                     AuthInfo.AuthenticationAction = (DEVM_AUTHENTICATION_ACTION_IO_CAPABILITIES_RESPONSE | (AuthInfo.AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
                     break;

                  default:
                     AuthInfo.AuthenticationAction = (DEVM_AUTHENTICATION_ACTION_PIN_CODE_RESPONSE | (AuthInfo.AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
                     break;
               }

               AuthInfo.AuthenticationDataLength = 0;

               if(DEVM_AuthenticationResponse(DEVMAuthenticationCallbackID, &AuthInfo) == 0)
               {
                  SS1_LOGD("Pairing canceled for remote device %02X:%02X:%02X:%02X:%02X:%02X", BD_ADDR.BD_ADDR5, BD_ADDR.BD_ADDR4, BD_ADDR.BD_ADDR3, BD_ADDR.BD_ADDR2, BD_ADDR.BD_ADDR1, BD_ADDR.BD_ADDR0);

                  /* This device was pairing.  Announce that it is no      */
                  /* longer paired.                                        */
                  BTPS_MemInitialize(&CallbackTask, 0, sizeof(CallbackTask));

                  CallbackTask.Type = ctReportBondStateChanged;

                  CallbackTask.TaskData.ReportBondStateChangedData.Status        = BT_STATUS_SUCCESS;
                  CallbackTask.TaskData.ReportBondStateChangedData.RemoteAddress = *RemoteAddress;
                  CallbackTask.TaskData.ReportBondStateChangedData.State         = BT_BOND_STATE_NONE;

                  SS1HAL_QueueAsyncTask(tcEvent, HAL_CallbackTaskHandler, sizeof(CallbackTask), &CallbackTask);

                  ret_val = BT_STATUS_SUCCESS;
               }
               else
                  ret_val = BT_STATUS_FAIL;
            }
            else
            {
               /* The requested device did not match the known pending  */
               /* authentication process, so restore the auth data.     */
               StorePendingAuthCopy(&AuthInfo);

               /* Attempt to explicitly cancel the pairing process      */
               /* in case this cancellation request is for a locally    */
               /* initiated pairing process that has not triggered any  */
               /* authentication events.                                */
               if(DEVM_CancelPairWithRemoteDevice(BD_ADDR) == 0)
               {
                  SS1_LOGD("Pairing canceled for remote device %02X:%02X:%02X:%02X:%02X:%02X", BD_ADDR.BD_ADDR5, BD_ADDR.BD_ADDR4, BD_ADDR.BD_ADDR3, BD_ADDR.BD_ADDR2, BD_ADDR.BD_ADDR1, BD_ADDR.BD_ADDR0);

                  ret_val = BT_STATUS_SUCCESS;
               }
               else
                  ret_val = BT_STATUS_FAIL;
            }
         }
         else
         {
            /* Attempt to explicitly cancel the pairing process in case */
            /* this cancellation request is for a locally initiated     */
            /* pairing process that has not triggered any authentication*/
            /* events.                                                  */
            if(DEVM_CancelPairWithRemoteDevice(BD_ADDR) == 0)
            {
               SS1_LOGD("Pairing canceled for remote device %02X:%02X:%02X:%02X:%02X:%02X", BD_ADDR.BD_ADDR5, BD_ADDR.BD_ADDR4, BD_ADDR.BD_ADDR3, BD_ADDR.BD_ADDR2, BD_ADDR.BD_ADDR1, BD_ADDR.BD_ADDR0);

               ret_val = BT_STATUS_SUCCESS;
            }
            else
               ret_val = BT_STATUS_FAIL;
         }

         /* If the pairing attempt was successfully canceled, mark the  */
         /* device as not paired, in case this cancellation request is  */
         /* for a re-authentication where the remote device was already */
         /* paired prior to the authentication request.                 */
         if((ret_val == BT_STATUS_SUCCESS) && (DEVM_QueryRemoteDeviceProperties(BD_ADDR, 0, &RemoteProps) == 0))
         {
            if(RemoteProps.RemoteDeviceFlags & DEVM_REMOTE_DEVICE_FLAGS_DEVICE_CURRENTLY_PAIRED)
            {
               SS1_LOGD("Remote device %02X:%02X:%02X:%02X:%02X:%02X is currently paired: removing bond.", BD_ADDR.BD_ADDR5, BD_ADDR.BD_ADDR4, BD_ADDR.BD_ADDR3, BD_ADDR.BD_ADDR2, BD_ADDR.BD_ADDR1, BD_ADDR.BD_ADDR0);

               if((Result = DEVM_UnPairRemoteDevice(BD_ADDR, 0)) != 0)
                  SS1_LOGD("Error while removing bond for device %02X:%02X:%02X:%02X:%02X:%02X (%d)", BD_ADDR.BD_ADDR5, BD_ADDR.BD_ADDR4, BD_ADDR.BD_ADDR3, BD_ADDR.BD_ADDR2, BD_ADDR.BD_ADDR1, BD_ADDR.BD_ADDR0, Result);
            }
         }
      }
      else
         ret_val = BT_STATUS_PARM_INVALID;
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* BT Legacy PinKey Reply If accept==FALSE, then pin_len and pin_code*/
   /* shall be 0x0                                                      */
static int SS1HAL_PinReply(const bt_bdaddr_t *RemoteAddress, uint8_t Accept, uint8_t PinLength, bt_pin_code_t *PinCode)
{
   int                               ret_val;
   BD_ADDR_t                         BD_ADDR;
   DEVM_Authentication_Information_t AuthInfo;

   SS1_LOGD("Enter: %02X:%02X:%02X:%02X:%02X:%02X, %d, %d, %d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d (%.16s)", (RemoteAddress?RemoteAddress->address[0]:0), (RemoteAddress?RemoteAddress->address[1]:0), (RemoteAddress?RemoteAddress->address[2]:0), (RemoteAddress?RemoteAddress->address[3]:0), (RemoteAddress?RemoteAddress->address[4]:0), (RemoteAddress?RemoteAddress->address[5]:0), Accept, PinLength, (PinCode?PinCode->pin[0]:0), (PinCode?PinCode->pin[1]:0), (PinCode?PinCode->pin[2]:0), (PinCode?PinCode->pin[3]:0), (PinCode?PinCode->pin[4]:0), (PinCode?PinCode->pin[5]:0), (PinCode?PinCode->pin[6]:0), (PinCode?PinCode->pin[7]:0), (PinCode?PinCode->pin[8]:0), (PinCode?PinCode->pin[9]:0), (PinCode?PinCode->pin[10]:0), (PinCode?PinCode->pin[11]:0), (PinCode?PinCode->pin[12]:0), (PinCode?PinCode->pin[13]:0), (PinCode?PinCode->pin[14]:0), (PinCode?PinCode->pin[15]:0), (PinCode?((char*)PinCode->pin):"-NULL-"));

   if(Initialized)
   {
      if(RemoteAddress)
      {
         if(RetrievePendingAuth(&AuthInfo))
         {
            ConvertAndroidAddrToBluetopia(RemoteAddress, &BD_ADDR);

            if(COMPARE_BD_ADDR(AuthInfo.BD_ADDR, BD_ADDR) == FALSE)
            {
               /* The requested device did not match the known pending  */
               /* authentication process, so restore the auth data.     */
               StorePendingAuthCopy(&AuthInfo);

               SS1_LOGW("Warning: Unexpected authentication response for device %02X:%02X:%02X:%02X:%02X:%02X", BD_ADDR.BD_ADDR5, BD_ADDR.BD_ADDR4, BD_ADDR.BD_ADDR3, BD_ADDR.BD_ADDR2, BD_ADDR.BD_ADDR1, BD_ADDR.BD_ADDR0);

               /* Prepare a fresh authentication response for the       */
               /* requested address.                                    */
               BTPS_MemInitialize(&AuthInfo, 0, DEVM_AUTHENTICATION_INFORMATION_SIZE);

               AuthInfo.BD_ADDR = BD_ADDR;
            }
         }

         AuthInfo.AuthenticationAction = (DEVM_AUTHENTICATION_ACTION_PIN_CODE_RESPONSE | (AuthInfo.AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));

         if(Accept)
         {
            if(PinLength > PIN_CODE_SIZE)
               PinLength = PIN_CODE_SIZE;

            AuthInfo.AuthenticationDataLength = PinLength;
            
            BTPS_MemCopy(&(AuthInfo.AuthenticationData.PINCode), PinCode, PinLength);
         }
         else
            AuthInfo.AuthenticationDataLength = 0;

         if(DEVM_AuthenticationResponse(DEVMAuthenticationCallbackID, &AuthInfo) == 0)
            ret_val = BT_STATUS_SUCCESS;
         else
            ret_val = BT_STATUS_FAIL;
      }
      else
         ret_val = BT_STATUS_PARM_INVALID;
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* BT SSP Reply - Just Works, Numeric Comparison and Passkey         */
   /* passkey shall be zero for BT_SSP_VARIANT_PASSKEY_COMPARISON &     */
   /* BT_SSP_VARIANT_CONSENT For BT_SSP_VARIANT_PASSKEY_ENTRY, if       */
   /* accept==FALSE, then passkey shall be zero                         */
static int SS1HAL_SspReply(const bt_bdaddr_t *RemoteAddress, bt_ssp_variant_t Variant, uint8_t Accept, uint32_t Passkey)
{
   int                               ret_val;
   BD_ADDR_t                         BD_ADDR;
   DEVM_Authentication_Information_t AuthInfo;

   SS1_LOGD("Enter: %02X:%02X:%02X:%02X:%02X:%02X, %d, %d, %u", (RemoteAddress?RemoteAddress->address[0]:0), (RemoteAddress?RemoteAddress->address[1]:0), (RemoteAddress?RemoteAddress->address[2]:0), (RemoteAddress?RemoteAddress->address[3]:0), (RemoteAddress?RemoteAddress->address[4]:0), (RemoteAddress?RemoteAddress->address[5]:0), Variant, Accept, Passkey);

   if(Initialized)
   {
      if(RemoteAddress)
      {
         //XXX How to handle LE pairing?

         if(RetrievePendingAuth(&AuthInfo))
         {
            ConvertAndroidAddrToBluetopia(RemoteAddress, &BD_ADDR);

            if(COMPARE_BD_ADDR(AuthInfo.BD_ADDR, BD_ADDR) == FALSE)
            {
               /* The requested device did not match the known       */
               /* pending authentication process, so restore the auth*/
               /* data.                                              */
               StorePendingAuthCopy(&AuthInfo);

               SS1_LOGW("Warning: Unexpected authentication response for device %02X:%02X:%02X:%02X:%02X:%02X", BD_ADDR.BD_ADDR5, BD_ADDR.BD_ADDR4, BD_ADDR.BD_ADDR3, BD_ADDR.BD_ADDR2, BD_ADDR.BD_ADDR1, BD_ADDR.BD_ADDR0);

               /* Generate an approximate authentication request for */
               /* the requested address.                             */
               BTPS_MemInitialize(&AuthInfo, 0, DEVM_AUTHENTICATION_INFORMATION_SIZE);

               AuthInfo.BD_ADDR = BD_ADDR;

               switch(Variant)
               {
                  case BT_SSP_VARIANT_PASSKEY_CONFIRMATION:
                  case BT_SSP_VARIANT_CONSENT:
                     AuthInfo.AuthenticationAction = DEVM_AUTHENTICATION_ACTION_USER_CONFIRMATION_REQUEST;
                     break;

                  case BT_SSP_VARIANT_PASSKEY_ENTRY:
                     AuthInfo.AuthenticationAction = DEVM_AUTHENTICATION_ACTION_PASSKEY_REQUEST;
                     break;

                  case BT_SSP_VARIANT_PASSKEY_NOTIFICATION:
                     AuthInfo.AuthenticationAction = DEVM_AUTHENTICATION_ACTION_PASSKEY_INDICATION;
                     break;
               }
            }
         }

         switch(AuthInfo.AuthenticationAction & DEVM_AUTHENTICATION_ACTION_AUTHENTICATION_ACTION_MASK)
         {
            case DEVM_AUTHENTICATION_ACTION_USER_CONFIRMATION_REQUEST:
               SS1_LOGD("(%p): SSP Reply variant %d, no passkey returned", BTPS_CurrentThreadHandle(), Variant);
               AuthInfo.AuthenticationAction            = (DEVM_AUTHENTICATION_ACTION_USER_CONFIRMATION_RESPONSE | (AuthInfo.AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
               AuthInfo.AuthenticationDataLength        = sizeof(AuthInfo.AuthenticationData.Confirmation);
               AuthInfo.AuthenticationData.Confirmation = (Accept ? TRUE : FALSE);
               break;

            case DEVM_AUTHENTICATION_ACTION_PASSKEY_REQUEST:
               SS1_LOGD("(%p): SSP Reply variant %d", BTPS_CurrentThreadHandle(), Variant);
               AuthInfo.AuthenticationAction       = (DEVM_AUTHENTICATION_ACTION_PASSKEY_RESPONSE | (AuthInfo.AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
               AuthInfo.AuthenticationDataLength   = sizeof(AuthInfo.AuthenticationData.Passkey);
               AuthInfo.AuthenticationData.Passkey = Passkey;
               break;

            case DEVM_AUTHENTICATION_ACTION_PASSKEY_INDICATION:
               SS1_LOGD("(%p): SSP Reply variant %d, not handled", BTPS_CurrentThreadHandle(), Variant);
               AuthInfo.AuthenticationAction       = (DEVM_AUTHENTICATION_ACTION_PASSKEY_RESPONSE | (AuthInfo.AuthenticationAction & DEVM_AUTHENTICATION_ACTION_LOW_ENERGY_OPERATION_MASK));
               AuthInfo.AuthenticationDataLength   = sizeof(AuthInfo.AuthenticationData.Passkey);
               AuthInfo.AuthenticationData.Passkey = 0;
               break;
         }

         if(DEVM_AuthenticationResponse(DEVMAuthenticationCallbackID, &AuthInfo) == 0)
            ret_val = BT_STATUS_SUCCESS;
         else
            ret_val = BT_STATUS_FAIL;
      }
      else
         ret_val = BT_STATUS_PARM_INVALID;
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* Get Bluetooth profile interface                                   */
static const void *SS1HAL_GetProfileInterface(const char *ProfileID)
{
   const void   *ret_val;
   unsigned int  Index;

   SS1_LOGD("Enter: %s", (ProfileID?ProfileID:"-NULL-"));

   ret_val = NULL;

   if(ProfileID)
   {
      for(Index = 0; Index < (sizeof(ProfileList) / sizeof(ProfileList[0])); Index++)
      {
         if(BTPS_MemCompare(ProfileID, ProfileList[Index].ProfileName, ProfileList[Index].NameLength) == 0)
         {
            if(ProfileList[Index].Init)
               ret_val = ProfileList[Index].Init();
            
            break;
         }
      }
   }

   SS1_LOGD("Exit: %p", ret_val);

   return(ret_val);
}

   /* Bluetooth Test Mode APIs - Bluetooth must be enabled for these    */
   /* APIs Configure DUT Mode - Use this mode to enter/exit DUT mode    */
static int SS1HAL_DutModeConfigure(uint8_t Enable)
{
   int ret_val;

   SS1_LOGD("Enter: %u", Enable);

   if(Initialized)
   {
      /* XXX */ SS1_LOGD("****** UNIMPLEMENTED ****************************************");
      ret_val = BT_STATUS_UNSUPPORTED;
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* Send any test HCI (vendor-specific) command to the controller.    */
   /* Must be in DUT Mode                                               */
static int SS1HAL_DutModeSend(uint16_t Opcode, uint8_t *Buffer, uint8_t Length)
{
   int ret_val;

   SS1_LOGD("Enter: 0x%04X, %p, %d", Opcode, Buffer, Length);

   if(Initialized)
   {
      if((!Length) || ((Length) && (Buffer)))
      {
         /* XXX */ SS1_LOGD("****** UNIMPLEMENTED ****************************************");
         ret_val = BT_STATUS_UNSUPPORTED;
      }
      else
         ret_val = BT_STATUS_PARM_INVALID;
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

   /* BLE Test Mode APIs                                                */


   /* opcode MUST be one of: LE_Receiver_Test, LE_Transmitter_Test,     */
   /* LE_Test_End                                                       */
static int SS1HAL_LowEnergyTestMode(uint16_t Opcode, uint8_t *Buffer, uint8_t Length)
{
#if SS1_PLATFORM_SDK_VERSION >= 18
   int ret_val;

   SS1_LOGD("Enter: 0x%04X, %p, %d", Opcode, Buffer, Length);

#if BLE_INCLUDED == TRUE
   if(Initialized)
   {
      if((!Length) || ((Length) && (Buffer)))
      {
         /* XXX */ SS1_LOGD("****** UNIMPLEMENTED ****************************************");
         ret_val = BT_STATUS_UNSUPPORTED;
      }
      else
         ret_val = BT_STATUS_PARM_INVALID;
   }
   else
      ret_val = BT_STATUS_NOT_READY;
#else
   ret_val = BT_STATUS_UNSUPPORTED;
#endif

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);


#else

   return(BT_STATUS_UNSUPPORTED);

#endif
}

static int SS1HAL_OpenModule(const struct hw_module_t *Module, char const *Name, struct hw_device_t **HardwareDevice)
{
   int ret_val;

   usleep(4000);
   SS1_LOGD("Enter: %p, %s, %p", Module, (Name?Name:"-NULL-"), HardwareDevice);

   bluetooth_device_t *BluetoothDevice;
   
   if(!HAL_ModuleMutex)
   {
      if((HAL_ModuleMutex = BTPS_CreateMutex(TRUE)) != NULL)
      {
         /* Initialize the Utility library before any use can occur.    */
         HALUtil_Init();

         if((BluetoothDevice = BTPS_AllocateMemory(sizeof(bluetooth_device_t))) != NULL)
         {
            BTPS_MemInitialize(BluetoothDevice, 0, sizeof(bluetooth_device_t) );

            BluetoothDevice->common.tag              = HARDWARE_DEVICE_TAG;
            BluetoothDevice->common.version          = 0;
            BluetoothDevice->common.module           = (struct hw_module_t*)Module;
            BluetoothDevice->common.close            = SS1HAL_CloseModule;
            BluetoothDevice->get_bluetooth_interface = SS1HAL_GetBluetoothInterface;
         }

         if(!AsyncTaskPool)
            AsyncTaskPool = TaskPool_Create(1);

         if(!CallbackTaskPool)
            CallbackTaskPool = TaskPool_Create(1);

         if((BluetoothDevice) && (AsyncTaskPool) && (CallbackTaskPool))
         {
            /* Return the new device abstraction interface to the caller.     */
            *HardwareDevice = &(BluetoothDevice->common);

            Initialized = FALSE;
            ret_val     = BT_STATUS_SUCCESS;

            SS1HAL_ReleaseLock();
         }
         else
         {
            /* Initialization failed.  Clean up allocated resources.    */
            ret_val = BT_STATUS_FAIL;

            SS1HAL_CloseModule(&(BluetoothDevice->common));
         }
      }
      else
         ret_val = BT_STATUS_NOMEM;
   }
   else
      ret_val = BT_STATUS_DONE;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

//XXX This is never actually called by the application layer.
//XXX SS1HAL_OpenModule should only initialize components that will never be
//XXX released. Move other initialization to SS1HAL_Init and any necessary
//XXX cleanup to SS1HAL_Cleanup.
static int SS1HAL_CloseModule(struct hw_device_t *HardwareDevice)
{
   int        ret_val;
   Mutex_t    Mutex;
   TaskPool_t AsyncPool;
   TaskPool_t CallbackPool;

   SS1_LOGD("Enter: %p", HardwareDevice);

   if(SS1HAL_AcquireLock(BTPS_INFINITE_WAIT))
   {
      if(Initialized)
         SS1HAL_Cleanup();

      /* Hide globally accessible resources.                            */
      Mutex            = HAL_ModuleMutex;
      AsyncPool        = AsyncTaskPool;
      CallbackPool     = CallbackTaskPool;

      HAL_ModuleMutex  = NULL;
      AsyncTaskPool    = NULL;
      CallbackTaskPool = NULL;

      /* Now it is safe to interrupt any threads blocking on the module */
      /* mutex.                                                         */
      BTPS_CloseMutex(Mutex);

      if(HardwareDevice)
         BTPS_FreeMemory(HardwareDevice);

      if(AsyncPool)
         TaskPool_Destroy(AsyncPool);

      if(CallbackPool)
         TaskPool_Destroy(CallbackPool);

      /* Allow the Utility library to clean up now that everything else */
      /* is fully shut down.                                            */
      HALUtil_Cleanup();

      ret_val = BT_STATUS_SUCCESS;
   }
   else
      ret_val = BT_STATUS_NOT_READY;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);
}

static int SS1HAL_ConfigHCISnoopLog(uint8_t Enable)
{
#if SS1_PLATFORM_SDK_VERSION >= 19

   bt_status_t ret_val;

   SS1_LOGD("Enter: %u", Enable);

   if(!DEVM_EnableBluetoothDebug(Enable?TRUE:FALSE, DEVM_BLUETOOTH_DEBUG_TYPE_FTS_LOG_FILE, 0, BTPS_StringLength(BTSNOOP_FILE_DEFAULT_LOCATION) + 1, (unsigned char *)BTSNOOP_FILE_DEFAULT_LOCATION))
      ret_val = BT_STATUS_SUCCESS;
   else
      ret_val = BT_STATUS_FAIL;

   SS1_LOGD("Exit: %d", ret_val);

   return(ret_val);

#else
   return(BT_STATUS_UNSUPPORTED);
#endif
}

   /* Interface functions which will be called by the upper Bluetooth   */
   /* HAL module.                                                       */
static const bt_interface_t BluetoothInterface =
{
   .size                         = sizeof(bt_interface_t),
   .init                         = SS1HAL_Init,
   .enable                       = SS1HAL_Enable,
   .disable                      = SS1HAL_Disable,
   .cleanup                      = SS1HAL_Cleanup,
   .get_adapter_properties       = SS1HAL_GetAdapterProperties,
   .get_adapter_property         = SS1HAL_GetAdapterProperty,
   .set_adapter_property         = SS1HAL_SetAdapterProperty,
   .get_remote_device_properties = SS1HAL_GetRemoteDeviceProperties,
   .get_remote_device_property   = SS1HAL_GetRemoteDeviceProperty,
   .set_remote_device_property   = SS1HAL_SetRemoteDeviceProperty,
   .get_remote_service_record    = SS1HAL_GetRemoteServiceRecord,
   .get_remote_services          = SS1HAL_GetRemoteServices,
   .start_discovery              = SS1HAL_StartDiscovery,
   .cancel_discovery             = SS1HAL_CancelDiscovery,
   .create_bond                  = SS1HAL_CreateBond,
   .remove_bond                  = SS1HAL_RemoveBond,
   .cancel_bond                  = SS1HAL_CancelBond,
   .pin_reply                    = SS1HAL_PinReply,
   .ssp_reply                    = SS1HAL_SspReply,
   .get_profile_interface        = SS1HAL_GetProfileInterface,
   .dut_mode_configure           = SS1HAL_DutModeConfigure,
   .dut_mode_send                = SS1HAL_DutModeSend,
#if SS1_PLATFORM_SDK_VERSION >= 18
   .le_test_mode                 = SS1HAL_LowEnergyTestMode,
#endif

#if SS1_PLATFORM_SDK_VERSION >= 19
   .config_hci_snoop_log         = SS1HAL_ConfigHCISnoopLog,
#endif
};

static const bt_interface_t *SS1HAL_GetBluetoothInterface()
{
   return(&BluetoothInterface);
}

   /* Interface functions which will be called by the HAL layer.        */
static struct hw_module_methods_t ModuleMethods =
{
    .open = SS1HAL_OpenModule,
};

   /* Static data structure which will be used by the HAL layer to      */
   /* identify this HAL module.                                         */
__attribute__ ((visibility ("default")))
struct hw_module_t HAL_MODULE_INFO_SYM =
{
   .tag                = HARDWARE_MODULE_TAG,
   .module_api_version = HARDWARE_MODULE_API_VERSION(0,0),
   .hal_api_version    = HARDWARE_HAL_API_VERSION,
   .id                 = BT_HARDWARE_MODULE_ID,
   .name               = "Bluetopia PM for Android",
   .author             = "Stonestreet One, LLC",
   .methods            = &ModuleMethods,
};

