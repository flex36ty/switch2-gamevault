#include "usb_chunk.h"
/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <malloc.h>
#include <stdio.h>

#include "usb.h"
extern const char* mtpUsbInitStage;
extern Result mtpUsbDsRegisterInterface(UsbDsInterface** interface);
static Result mtpUsbSetupResult(Result result) {
    FILE* log = fopen("sdmc:/mtp-kaihen.log", "a");
    if (log) {
        fprintf(log, "USB setup: %s => 0x%08x\n", mtpUsbInitStage, result);
        fclose(log);
    }
    return result;
}
#define USB_SETUP(call) (mtpUsbInitStage = #call, mtpUsbSetupResult((call)))

#define TOTAL_INTERFACES 4
#define TOTAL_ENDPOINTS 4

typedef struct {
    UsbDsEndpoint *endpoint;
    u8 *buffer;
    RwLock lock;
} usbCommsEndpoint;

typedef struct {
    RwLock lock;
    bool initialized;
    UsbDsInterface* interface;
    u32 endpoint_number;
    usbCommsEndpoint endpoint[TOTAL_ENDPOINTS];
} usbCommsInterface;

static bool g_usbCommsInitialized = false;
static bool g_usbStopRequested;
void usbRequestStop(void) {
    __atomic_store_n(&g_usbStopRequested, true, __ATOMIC_RELEASE);
}
static usbCommsInterface g_usbCommsInterfaces[TOTAL_INTERFACES];
static bool g_usbCommsErrorHandling = 0;
static RwLock g_usbCommsLock;
static int ep_in = 1;
static int ep_out = 1;

static Result _usbCommsInterfaceInit5x(u32 intf_ind, const UsbInterfaceDesc *info);
static Result _usbCommsInterfaceInit(u32 intf_ind, const UsbInterfaceDesc *info);

Result usbInitialize(struct usb_device_descriptor *device_descriptor, u32 num_interfaces, const UsbInterfaceDesc *infos)
{
    __atomic_store_n(&g_usbStopRequested, false, __ATOMIC_RELEASE);
    Result rc = 0;
    rwlockWriteLock(&g_usbCommsLock);
    
    if (g_usbCommsInitialized) {
        rc = MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized);
    } else if (num_interfaces > TOTAL_INTERFACES) {
        rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    } else {
        ep_in=1;ep_out=1;
        rc = usbDsInitialize();
        if (R_SUCCEEDED(rc)) mtpUsbInitStage = "USB descriptors / interface setup";
        
        if (R_SUCCEEDED(rc)) {
            if (hosversionAtLeast(5,0,0)) {
                u8 iManufacturer, iProduct, iSerialNumber;
                static const u16 supported_langs[1] = {0x0409};
                // Send language descriptor
                rc = USB_SETUP(usbDsAddUsbLanguageStringDescriptor(NULL, supported_langs, sizeof(supported_langs)/sizeof(u16)));
                // Send manufacturer
                if (R_SUCCEEDED(rc)) rc = USB_SETUP(usbDsAddUsbStringDescriptor(&iManufacturer, "Homebrew"));
                // Send product
                if (R_SUCCEEDED(rc)) rc = USB_SETUP(usbDsAddUsbStringDescriptor(&iProduct, "Switch 2 SD Card (kaiHEN MTP)"));
                // Send serial number
                if (R_SUCCEEDED(rc)) rc = USB_SETUP(usbDsAddUsbStringDescriptor(&iSerialNumber, "SerialNumber"));
                
                // Send device descriptors
                device_descriptor->iManufacturer = iManufacturer;
                device_descriptor->iProduct = iProduct;
                device_descriptor->iSerialNumber = iSerialNumber;

                // Full Speed is USB 1.1
                if (R_SUCCEEDED(rc)) rc = USB_SETUP(usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Full, device_descriptor));
                
                // High Speed is USB 2.0
                device_descriptor->bcdUSB = 0x0200;
                if (R_SUCCEEDED(rc)) rc = USB_SETUP(usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_High, device_descriptor));
                
                // Super Speed is USB 3.0
                device_descriptor->bcdUSB = 0x0300;
                // Upgrade packet size to 512
                device_descriptor->bMaxPacketSize0 = 0x09;
                if (R_SUCCEEDED(rc)) rc = USB_SETUP(usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Super, device_descriptor));
                
                // Define Binary Object Store
                u8 bos[0x16] = {
                    0x05, // .bLength
                    USB_DT_BOS, // .bDescriptorType
                    0x16, 0x00, // .wTotalLength
                    0x02, // .bNumDeviceCaps
                    
                    // USB 2.0
                    0x07, // .bLength
                    USB_DT_DEVICE_CAPABILITY, // .bDescriptorType
                    0x02, // .bDevCapabilityType
                    0x02, 0x00, 0x00, 0x00, // dev_capability_data
                    
                    // USB 3.0
                    0x0A, // .bLength
                    USB_DT_DEVICE_CAPABILITY, // .bDescriptorType
                    0x03, // .bDevCapabilityType
                    0x00, 0x0E, 0x00, 0x03, 0x00, 0x00, 0x00
                };
                if (R_SUCCEEDED(rc)) rc = USB_SETUP(usbDsSetBinaryObjectStore(bos, sizeof(bos)));
            }
            
            if (R_SUCCEEDED(rc)) {
                for (u32 i = 0; i < num_interfaces; i++) {
                    usbCommsInterface *intf = &g_usbCommsInterfaces[i];
                    const UsbInterfaceDesc *info = &infos[i];
                    intf->endpoint_number = info->interface_desc->bNumEndpoints;
                    rwlockWriteLock(&intf->lock);
                    for (u32 i = 0; i < intf->endpoint_number; i++)
                    {
                        rwlockWriteLock(&intf->endpoint[i].lock);
                    }
                    rc = _usbCommsInterfaceInit(i, info);
                    for (u32 i = 0; i < intf->endpoint_number; i++)
                    {
                        rwlockWriteUnlock(&intf->endpoint[i].lock);
                    }
                    rwlockWriteUnlock(&intf->lock);
                    if (R_FAILED(rc)) {
                        break;
                    }
                }
            }
        }
        
        if (R_SUCCEEDED(rc) && hosversionAtLeast(5,0,0)) {
            rc = USB_SETUP(usbDsEnable());
        }
        
        if (R_FAILED(rc)) {
            // usbExit acquires this same non-recursive write lock.
            rwlockWriteUnlock(&g_usbCommsLock);
            usbExit();
            return rc;
        }
    }
    
    if (R_SUCCEEDED(rc)) {
        g_usbCommsInitialized = true;
        g_usbCommsErrorHandling = false;
    }

    rwlockWriteUnlock(&g_usbCommsLock);
    return rc;
}

static void _usbCommsInterfaceFree(usbCommsInterface *interface)
{
    rwlockWriteLock(&interface->lock);
    if (!interface->initialized) {
        rwlockWriteUnlock(&interface->lock);
        return;
    }

    interface->initialized = 0;
    interface->interface = NULL;

    for (u32 i = 0; i < interface->endpoint_number; i++)
    {
        rwlockWriteLock(&interface->endpoint[i].lock);
        interface->endpoint[i].endpoint = NULL;
        free(interface->endpoint[i].buffer);
        interface->endpoint[i].buffer = NULL;
        rwlockWriteUnlock(&interface->endpoint[i].lock);
    }

    rwlockWriteUnlock(&interface->lock);
}

void usbExit(void)
{
    u32 i;

    rwlockWriteLock(&g_usbCommsLock);

    usbDsExit();

    g_usbCommsInitialized = false;

    rwlockWriteUnlock(&g_usbCommsLock);

    for (i=0; i<TOTAL_INTERFACES; i++)
    {
        _usbCommsInterfaceFree(&g_usbCommsInterfaces[i]);
    }
}

static Result _usbCommsInterfaceInit(u32 intf_ind, const UsbInterfaceDesc *info)
{
    /*if (hosversionAtLeast(5,0,0)) {*/
        return _usbCommsInterfaceInit5x(intf_ind, info);
    /*} else {
        return _usbCommsInterfaceInit1x(intf_ind, info);
    }*/
}

static Result _usbCommsInterfaceInit5x(u32 intf_ind, const UsbInterfaceDesc *info)
{
    Result rc = 0;
    usbCommsInterface *interface = &g_usbCommsInterfaces[intf_ind];

    u8 index = 0;
    if(info->string_descriptor != NULL)
    {
        usbDsAddUsbStringDescriptor(&index, info->string_descriptor);
    }
    info->interface_desc->iInterface = index;
    
    struct usb_ss_endpoint_companion_descriptor endpoint_companion = {
        .bLength = sizeof(struct usb_ss_endpoint_companion_descriptor),
        .bDescriptorType = USB_DT_SS_ENDPOINT_COMPANION,
        .bMaxBurst = 0x0F,
        .bmAttributes = 0x00,
        .wBytesPerInterval = 0x00,
    };

    interface->initialized = 1;

    //The buffer for PostBufferAsync commands must be 0x1000-byte aligned.
    for (u32 i = 0; i < interface->endpoint_number; i++)
    {
        interface->endpoint[i].buffer = (u8*)memalign(0x1000, 0x1000);
        if (interface->endpoint[i].buffer == NULL)
        {
            rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
            break;
        }
        memset(interface->endpoint[i].buffer, 0, 0x1000);
    }
    if (R_FAILED(rc)) return rc;
    
    rc = USB_SETUP(mtpUsbDsRegisterInterface(&interface->interface));
    if (R_FAILED(rc)) return rc;
    
    info->interface_desc->bInterfaceNumber = interface->interface->interface_index;
    for (u32 i = 0; i < interface->endpoint_number; i++)
    {
        if((info->endpoint_desc[i]->bEndpointAddress & USB_ENDPOINT_IN) != 0)
        {
            info->endpoint_desc[i]->bEndpointAddress |= ep_in;
            ep_in++;
        }
        else
        {
            info->endpoint_desc[i]->bEndpointAddress |= ep_out;
            ep_out++;
        } 
    }
    
    // Full Speed Config
    rc = USB_SETUP(usbDsInterface_AppendConfigurationData(interface->interface, UsbDeviceSpeed_Full, info->interface_desc, USB_DT_INTERFACE_SIZE));
    if (R_FAILED(rc)) return rc;

    for (u32 i = 0; i < interface->endpoint_number; i++)
    {
        if(info->endpoint_desc[i]->bmAttributes == USB_TRANSFER_TYPE_BULK)
            info->endpoint_desc[i]->wMaxPacketSize = 0x40;
        rc = USB_SETUP(usbDsInterface_AppendConfigurationData(interface->interface, UsbDeviceSpeed_Full, info->endpoint_desc[i], USB_DT_ENDPOINT_SIZE));
        if (R_FAILED(rc)) return rc;
    }
    
    // High Speed Config
    rc = USB_SETUP(usbDsInterface_AppendConfigurationData(interface->interface, UsbDeviceSpeed_High, info->interface_desc, USB_DT_INTERFACE_SIZE));
    if (R_FAILED(rc)) return rc;

    for (u32 i = 0; i < interface->endpoint_number; i++)
    {
        if(info->endpoint_desc[i]->bmAttributes == USB_TRANSFER_TYPE_BULK)
            info->endpoint_desc[i]->wMaxPacketSize = 0x200;
        rc = USB_SETUP(usbDsInterface_AppendConfigurationData(interface->interface, UsbDeviceSpeed_High, info->endpoint_desc[i], USB_DT_ENDPOINT_SIZE));
        if (R_FAILED(rc)) return rc;
    }
    
    // Super Speed Config
    rc = USB_SETUP(usbDsInterface_AppendConfigurationData(interface->interface, UsbDeviceSpeed_Super, info->interface_desc, USB_DT_INTERFACE_SIZE));
    if (R_FAILED(rc)) return rc;

    for (u32 i = 0; i < interface->endpoint_number; i++)
    {
        if(info->endpoint_desc[i]->bmAttributes == USB_TRANSFER_TYPE_BULK)
            info->endpoint_desc[i]->wMaxPacketSize = 0x400;
        rc = USB_SETUP(usbDsInterface_AppendConfigurationData(interface->interface, UsbDeviceSpeed_Super, info->endpoint_desc[i], USB_DT_ENDPOINT_SIZE));
        if (R_FAILED(rc)) return rc;
        rc = USB_SETUP(usbDsInterface_AppendConfigurationData(interface->interface, UsbDeviceSpeed_Super, &endpoint_companion, USB_DT_SS_ENDPOINT_COMPANION_SIZE));
        if (R_FAILED(rc)) return rc;
    }
    
    //Setup endpoints.    
    for (u32 i = 0; i < interface->endpoint_number; i++)
    {
        rc = USB_SETUP(usbDsInterface_RegisterEndpoint(interface->interface, &interface->endpoint[i].endpoint, info->endpoint_desc[i]->bEndpointAddress));
        if (R_FAILED(rc)) return rc;
    }

    rc = USB_SETUP(usbDsInterface_EnableInterface(interface->interface));
    if (R_FAILED(rc)) return rc;
    
    return rc;
}

static Result _usbCommsTransfer(usbCommsEndpoint *ep, UsbDirection dir, const void* buffer, size_t size, u64 timeout, size_t *transferredSize)
{
    Result rc=0;
    u32 urbId=0;
    u32 chunksize=0;
    u8 transfer_type=0;
    u8 *bufptr = (u8*)buffer;
    u8 *transfer_buffer = NULL;
    u32 tmp_transferredSize = 0;
    size_t total_transferredSize=0;
    UsbDsReportData reportdata;

    // Bound diagnostics to the first 400 transfers so idle retries do not fill SD.
    static unsigned diagnostic_count;
    bool trace = diagnostic_count++ < 400;
#define TRANSFER_TRACE(stage) do { if (trace) { FILE* f = fopen("sdmc:/mtp-kaihen.log", "a"); if (f) { fprintf(f, "USB v12 transfer: %s direction=%u domain=%u size=%u urb=%u result=0x%08x bytes=%u\n", stage, (unsigned)dir, ep->endpoint->s.object_id, chunksize, urbId, rc, tmp_transferredSize); fclose(f); } } } while (0)
    TRANSFER_TRACE("begin");
    //Makes sure endpoints are ready for data-transfer / wait for init if needed.
    rc = usbDsWaitReady(1000000000ULL);
        TRANSFER_TRACE("ready");
    if (R_FAILED(rc)) return rc;

    bool zero_packet = size==0 && dir==UsbDirection_Write;
    while(size || zero_packet)
    {
        if(zero_packet){transfer_buffer=ep->buffer;chunksize=0;transfer_type=0;zero_packet=false;}
        else if(((u64)bufptr) & 0xfff)//When bufptr isn't page-aligned copy the data into g_usbComms_endpoint_in_buffer and transfer that, otherwise use the bufptr directly.
        {
            transfer_buffer = ep->buffer;
            memset(ep->buffer, 0, 0x1000);

            chunksize = usbBounceChunk(size);
            // Bulk IN must not emit an intermediate short packet. A page-tail
            // length depends on the heap address and can terminate the host's
            // MTP read early. Bounce whole 4 KiB blocks until the final block.
            // Always receive into the full aligned bounce buffer. Splitting at
            // the caller's page tail can turn a complete short USB packet into
            // an apparently full receive (e.g. 32 bytes), then wait forever for
            // a nonexistent remainder of the 512-byte command buffer.
            if (size<chunksize) chunksize = size;

            if(dir == UsbDirection_Write)
                memcpy(ep->buffer, bufptr, chunksize);

            transfer_type = 0;
        }
        else
        {
            transfer_buffer = bufptr;
            chunksize = size;
            transfer_type = 1;
        }

        //Start transfer.
        rc = usbDsEndpoint_PostBufferAsync(ep->endpoint, transfer_buffer, chunksize, &urbId);
        TRANSFER_TRACE("post");
        if(R_FAILED(rc))return rc;

        //Wait for the transfer to finish.
        rc = eventWait(&ep->endpoint->CompletionEvent, 1000000000ULL);
        // An idle host is normal. Keep the posted receive alive instead of
        // canceling and reposting it every second. Poll only to observe exit
        // or disconnection; the buffer stays owned by this call throughout.
        unsigned idle_polls = 0;
        while (rc == KERNELRESULT(TimedOut) &&
               !__atomic_load_n(&g_usbStopRequested, __ATOMIC_ACQUIRE)) {
            u32 state = 0;
            Result state_rc = usbDsGetState(&state);
            if (R_FAILED(state_rc) || state != 5) break;
            if (++idle_polls == 1 || idle_polls % 60 == 0) {
                FILE* f = fopen("sdmc:/mtp-kaihen.log", "a");
                if (f) {
                    fprintf(f, "USB v17 idle: pending urb=%u polls=%u state=%u; no cancellation\n", urbId, idle_polls, state);
                    fclose(f);
                }
            }
            rc = eventWait(&ep->endpoint->CompletionEvent, 1000000000ULL);
        }
        TRANSFER_TRACE("wait");

        Result original_wait = rc;
        if (R_FAILED(rc))
        {
            rc = usbDsEndpoint_Cancel(ep->endpoint);
            TRANSFER_TRACE("v16 cancel-return");
            if (R_FAILED(rc)) return rc;
            rc = eventWait(&ep->endpoint->CompletionEvent, UINT64_MAX);
            TRANSFER_TRACE("v16 cancel-completion");
            if (R_FAILED(rc)) return rc;
        }
        eventClear(&ep->endpoint->CompletionEvent);

        // Cancellation completes an URB too. Drain its report before posting
        // another buffer; clearing the event alone does not consume the report.
        rc = usbDsEndpoint_GetReportData(ep->endpoint, &reportdata);
        TRANSFER_TRACE("v16 report-drained");
        if (R_FAILED(rc)) return rc;

        // If the host completed the URB just as our wait timed out, preserve
        // that successful transfer instead of dropping its command bytes.
        rc = usbDsParseReportData(&reportdata, urbId, NULL, &tmp_transferredSize);
        TRANSFER_TRACE("parse");
        if (R_FAILED(rc)) return R_FAILED(original_wait) ? original_wait : rc;

        if (tmp_transferredSize > chunksize) tmp_transferredSize = chunksize;

        total_transferredSize+= (size_t)tmp_transferredSize;

        if ((transfer_type==0) && (dir == UsbDirection_Read))
            memcpy(bufptr, transfer_buffer, tmp_transferredSize);

        if(tmp_transferredSize)bufptr+= tmp_transferredSize;
        size-= tmp_transferredSize;

        if (tmp_transferredSize < chunksize) break;
    }

    if (transferredSize) *transferredSize = total_transferredSize;

    return rc;
}
#undef TRANSFER_TRACE

size_t usbTransfer(u32 interface, u32 endpoint, UsbDirection dir, void* buffer, size_t size, u64 timeout)
{
    size_t transferredSize=-1;
    u32 state=0;
    Result rc, rc2;
    bool initialized;

    usbCommsInterface *inter = &g_usbCommsInterfaces[interface];
    usbCommsEndpoint *ep = &inter->endpoint[endpoint];
    rwlockReadLock(&inter->lock);
    initialized = inter->initialized;
    rwlockReadUnlock(&inter->lock);
    if (!initialized) return 0;

    rwlockWriteLock(&ep->lock);
    rc = _usbCommsTransfer(ep, dir, buffer, size, timeout, &transferredSize);
    rwlockWriteUnlock(&ep->lock);
    if (R_FAILED(rc)) {
        rc2 = usbDsGetState(&state);
        if (R_SUCCEEDED(rc2)) {
            if (state!=5 && !__atomic_load_n(&g_usbStopRequested, __ATOMIC_ACQUIRE)) {
                rwlockWriteLock(&ep->lock);
                rc = _usbCommsTransfer(ep, dir, buffer, size, timeout, &transferredSize); //If state changed during transfer, try again. usbDsWaitReady() will be called from this.
                rwlockWriteUnlock(&ep->lock);
            }
        }
        if (R_FAILED(rc) && g_usbCommsErrorHandling)
        {
            if(dir == UsbDirection_Write)
                diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_BadUsbCommsWrite));
            else
                diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_BadUsbCommsRead));
        }
    }
    return transferredSize;
}

// Terminate only a complete MTP container, never an intermediate file chunk.
bool usbFinishTransfer(u32 interface,u32 endpoint,u64 container_size) {
    if(container_size%64)return true;
    UsbDeviceSpeed speed=UsbDeviceSpeed_None;
    if(R_FAILED(usbDsGetSpeed(&speed)))return false;
    u32 packet=speed==UsbDeviceSpeed_Super?1024:speed==UsbDeviceSpeed_High?512:speed==UsbDeviceSpeed_Full?64:0;
    if(!packet)return false;
    if(container_size%packet)return true;
    return usbTransfer(interface,endpoint,UsbDirection_Write,NULL,0,UINT64_MAX)==0;
}
