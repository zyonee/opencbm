/*
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 *  Copyright 2004 Spiro Trikaliotis <cbm4win@trikaliotis.net>
 *
 */

/*! ************************************************************** 
** \file sys/libcommon/ioctl.c \n
** \author Spiro Trikaliotis \n
** \version $Id: ioctl.c,v 1.4 2005-01-22 19:50:41 strik Exp $ \n
** \n
** \brief Perform an IOCTL
**
****************************************************************/

#include <wdm.h>
#include "cbm4win_common.h"
#include "cbmioctl.h"
#include "iec.h"

#include <parallel.h>


/*! \brief check the input buffer of an IRP

 Check the input buffer of an IRP if it contains at least Len bytes.

 \param IrpSp: 
   Pointer to the IO_STACK_LOCATION of the IRP which contains the input buffer.
 
 \param Len
   Length the buffer has to have.

 \param StatusOnSuccess
   The status to return on success.

 \return 
   If the input buffer contains at least Len bytes, the function returns
   StatusOnSuccess. If not, it returns STATUS_BUFFER_TOO_SMALL.
*/
static NTSTATUS
cbm_checkinputbuffer(IN PIO_STACK_LOCATION IrpSp, USHORT Len, NTSTATUS StatusOnSuccess)
{
    FUNC_ENTER();

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < Len)
    {
        StatusOnSuccess = STATUS_BUFFER_TOO_SMALL;
    }

    FUNC_LEAVE_NTSTATUS(StatusOnSuccess);
}

/*! \brief check the output buffer of an IRP

 Check the output buffer of an IRP if it contains at least Len bytes.

 \param IrpSp: 
   Pointer to the IO_STACK_LOCATION of the IRP which contains the input buffer.
 
 \param Len
   Length the buffer has to have.

 \param StatusOnSuccess
   The status to return on success.

 \return 
   If the output buffer contains at least Len bytes, the function returns
   StatusOnSuccess. If not, it returns STATUS_BUFFER_TOO_SMALL.
*/
static NTSTATUS
cbm_checkoutputbuffer(IN PIO_STACK_LOCATION IrpSp, USHORT Len, NTSTATUS StatusOnSuccess)
{
    FUNC_ENTER();

    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < Len)
    {
        StatusOnSuccess = STATUS_BUFFER_TOO_SMALL;
    }

    FUNC_LEAVE_NTSTATUS(StatusOnSuccess);
}

/*! \brief Services IOCTLs

 Services IRPs containing the IRP_MJ_DEVICE_CONTROL I/O function code.

 \param Fdo
   Pointer to a DEVICE_OBJECT structure. 
   This is the device object for the target device, 
   previously created by the driver's AddDevice routine.

 \param Irp
   Pointer to an IRP structure that describes the requested I/O operation. 

 \return 
   If the routine succeeds, it returns STATUS_SUCCESS. 
   Otherwise, it return one of the error status values:
   \n STATUS_SUCCESS              - Success.
   \n STATUS_PENDING              - Request pending.
   \n STATUS_BUFFER_TOO_SMALL     - Buffer too small.
   \n STATUS_INVALID_PARAMETER    - Invalid io control request.

 The driver's DriverEntry routine stored this routine's address in 
 DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL].

 Generally, all Dispatch routines execute in an arbitrary thread context 
 at IRQL PASSIVE_LEVEL, but there are exceptions.
*/
NTSTATUS
cbm_devicecontrol(IN PDEVICE_OBJECT Fdo, IN PIRP Irp)
{
    PPAR_SET_INFORMATION setInfo;
    PIO_STACK_LOCATION irpSp;
    PDEVICE_EXTENSION pdx;
    NTSTATUS ntStatus;
    BOOLEAN fastStart;

    FUNC_ENTER();

    // get the device extension

    pdx = Fdo->DeviceExtension;

    // get the current IRP stack location

    irpSp = IoGetCurrentIrpStackLocation(Irp);


    DBG_IRPPATH_PROCESS("Ioctl");

    // assume we do not want to perform a faststart of this IRP

    fastStart = FALSE;

    // Now, check the input and/or output buffers of the given
    // IOCTLs if they are at least as big as the specification.
    // If not, the IRP (and thus the IOCTL) is failed

    switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {

        case CBMCTRL_TALK:
            DBG_IRP(CBMCTRL_TALK);
            ntStatus = cbm_checkinputbuffer(irpSp, sizeof(CBMT_TALK_IN), STATUS_SUCCESS);
            break;

        case CBMCTRL_LISTEN:
            DBG_IRP(CBMCTRL_LISTEN);
            ntStatus = cbm_checkinputbuffer(irpSp, sizeof(CBMT_LISTEN_IN), STATUS_SUCCESS);
            break;

        case CBMCTRL_UNTALK:
            DBG_IRP(CBMCTRL_UNTALK);
            ntStatus = STATUS_SUCCESS;
            break;

        case CBMCTRL_UNLISTEN:
            DBG_IRP(CBMCTRL_UNLISTEN);
            ntStatus = STATUS_SUCCESS;
            break;

        case CBMCTRL_OPEN:
            DBG_IRP(CBMCTRL_OPEN);
            ntStatus = cbm_checkinputbuffer(irpSp, sizeof(CBMT_OPEN_IN), STATUS_SUCCESS);
            break;

        case CBMCTRL_CLOSE:
            DBG_IRP(CBMCTRL_CLOSE);
            ntStatus = cbm_checkinputbuffer(irpSp, sizeof(CBMT_CLOSE_IN), STATUS_SUCCESS);
            break;

        case CBMCTRL_RESET:
            DBG_IRP(CBMCTRL_RESET);
            ntStatus = STATUS_SUCCESS;
            break;

        case CBMCTRL_GET_EOI:
            DBG_IRP(CBMCTRL_GET_EOI);
            ntStatus = cbm_checkoutputbuffer(irpSp, sizeof(CBMT_GET_EOI_OUT), STATUS_SUCCESS);
            fastStart = TRUE;
            break;

        case CBMCTRL_CLEAR_EOI:
            DBG_IRP(CBMCTRL_CLEAR_EOI);
            ntStatus = STATUS_SUCCESS;
            fastStart = TRUE;
            break;

        case CBMCTRL_PP_READ:
            DBG_IRP(CBMCTRL_PP_READ);
            ntStatus = cbm_checkoutputbuffer(irpSp, sizeof(CBMT_PP_READ_OUT), STATUS_SUCCESS);
            fastStart = TRUE;
            break;

        case CBMCTRL_PP_WRITE:
            DBG_IRP(CBMCTRL_PP_WRITE);
            ntStatus = cbm_checkinputbuffer(irpSp, sizeof(CBMT_PP_WRITE_IN), STATUS_SUCCESS);
            fastStart = TRUE;
            break;

        case CBMCTRL_IEC_POLL:
            DBG_IRP(CBMCTRL_IEC_POLL);
            ntStatus = cbm_checkoutputbuffer(irpSp, sizeof(CBMT_IEC_POLL_OUT), STATUS_SUCCESS);
            fastStart = TRUE;
            break;

        case CBMCTRL_IEC_SET:
            DBG_IRP(CBMCTRL_IEC_SET);
            ntStatus = cbm_checkinputbuffer(irpSp, sizeof(CBMT_IEC_SET_IN), STATUS_SUCCESS);
            fastStart = TRUE;
            break;

        case CBMCTRL_IEC_RELEASE:
            DBG_IRP(CBMCTRL_IEC_RELEASE);
            ntStatus = cbm_checkinputbuffer(irpSp, sizeof(CBMT_IEC_RELEASE_IN), STATUS_SUCCESS);
            fastStart = TRUE;
            break;

        case CBMCTRL_IEC_SETRELEASE:
            DBG_IRP(CBMCTRL_IEC_SETRELEASE);
            ntStatus = cbm_checkinputbuffer(irpSp, sizeof(CBMT_IEC_SETRELEASE_IN), STATUS_SUCCESS);
            fastStart = TRUE;
            break;

        case CBMCTRL_IEC_WAIT:
            DBG_IRP(CBMCTRL_IEC_WAIT);
            ntStatus = cbm_checkoutputbuffer(irpSp, sizeof(CBMT_IEC_WAIT_OUT), 
                         cbm_checkinputbuffer(irpSp, sizeof(CBMT_IEC_WAIT_IN), STATUS_SUCCESS));
            break;

        case CBMCTRL_I_INSTALL:
            DBG_IRP(CBMCTRL_I_INSTALL);
            ntStatus = cbm_checkoutputbuffer(irpSp, sizeof(CBMT_I_INSTALL_OUT), STATUS_SUCCESS);
            break;

#if DBG

        case CBMCTRL_I_READDBG:
            DBG_IRP(CBMCTRL_I_READDBG);
            ntStatus = cbm_checkoutputbuffer(irpSp, sizeof(CHAR), STATUS_SUCCESS);
            break;

#endif // #if DBG

        default:
            DBG_ERROR((DBG_PREFIX "unknown IRP_MJ_DEVICE_CONTROL"));
            ntStatus = STATUS_INVALID_PARAMETER;
            break;
    }

    if (NT_SUCCESS(ntStatus))
    {
        PERF_EVENT_IOCTL_QUEUE(irpSp->Parameters.DeviceIoControl.IoControlCode);

        // queue the IRP to be processed
        // If faststart is TRUE, it will be processed immediately
        // (for performance reasons)

        ntStatus = QueueStartPacket(&pdx->IrpQueue, Irp, fastStart, Fdo);
    }
    else
    {
        // there was an error, complete the request
        // with that error status

        QueueCompleteIrp(NULL, Irp, ntStatus, 0);
    }

    FUNC_LEAVE_NTSTATUS(ntStatus);
}

/*! This translates the inputbuffer in the corresponding value to 
 *  be used for giving as parameter 
 */
#define INPUTVALUE(_ttt_) ((_ttt_ *) Irp->AssociatedIrp.SystemBuffer)

/*! This translates the outputbuffer in the corresponding value to 
 *  be used for giving as parameter
 */
#define OUTPUTVALUE(_ttt_) ((_ttt_ *) Irp->AssociatedIrp.SystemBuffer)

/*! \brief Executes IOCTLs

 Executes IRPs containing the IRP_MJ_DEVICE_CONTROL I/O function code.

 \param Pdx
   Pointer to the DEVICE_EXTENSION structure.

 \param Irp
   Pointer to an IRP structure that describes the requested I/O operation. 

 \return 
   If the routine succeeds, it returns STATUS_SUCCESS. 
   Otherwise, it return one of the error status values:
   \n STATUS_SUCCESS              - Success.
   \n STATUS_BUFFER_TOO_SMALL     - Buffer too small.

 This function does not perform any validity checks on the input and 
 output buffer! This should already been done in cbm_devicecontrol.
*/
NTSTATUS
cbm_execute_devicecontrol(IN PDEVICE_EXTENSION Pdx, IN PIRP Irp)
{
    PPAR_SET_INFORMATION setInfo;
    PIO_STACK_LOCATION irpSp;
    ULONG_PTR returnLength;
    NTSTATUS ntStatus;

    FUNC_ENTER();

    // As not every IOCTL needs to return a value, we initialize 
    // the return length here. This way, it needs only be altered
    // if the IOCTL returns some value.

    returnLength = 0;

    // get the current IRP stack location

    irpSp = IoGetCurrentIrpStackLocation(Irp);

    PERF_EVENT_IOCTL_EXECUTE(irpSp->Parameters.DeviceIoControl.IoControlCode);

    DBG_IRPPATH_EXECUTE("Execute Ioctl");

    // Call the appropriate function for processing the IOCTL
    // PrimaryAddresses are ANDed with 0x1F, as these are the only legitimate
    // primary addresses allowed for a IEC serial bus.

    switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {

        case CBMCTRL_TALK:
            DBG_IRP(CBMCTRL_TALK);
            ntStatus = cbmiec_talk(Pdx, INPUTVALUE(CBMT_TALK_IN)->PrimaryAddress & 0x1F,
                                   INPUTVALUE(CBMT_TALK_IN)->SecondaryAddress);
            break;

        case CBMCTRL_LISTEN:
            DBG_IRP(CBMCTRL_LISTEN);
            ntStatus = cbmiec_listen(Pdx, INPUTVALUE(CBMT_LISTEN_IN)->PrimaryAddress & 0x1F,
                                     INPUTVALUE(CBMT_LISTEN_IN)->SecondaryAddress);
            break;

        case CBMCTRL_UNTALK:
            DBG_IRP(CBMCTRL_UNTALK);
            ntStatus = cbmiec_untalk(Pdx);
            break;

        case CBMCTRL_UNLISTEN:
            DBG_IRP(CBMCTRL_UNLISTEN);
            ntStatus = cbmiec_unlisten(Pdx);
            break;

        case CBMCTRL_OPEN:
            DBG_IRP(CBMCTRL_OPEN);
            ntStatus = cbmiec_open(Pdx, INPUTVALUE(CBMT_OPEN_IN)->PrimaryAddress & 0x1F,
                                   INPUTVALUE(CBMT_OPEN_IN)->SecondaryAddress);
            break;

        case CBMCTRL_CLOSE:
            DBG_IRP(CBMCTRL_CLOSE);
            ntStatus = cbmiec_close(Pdx,INPUTVALUE(CBMT_CLOSE_IN)->PrimaryAddress & 0x1F,
                                    INPUTVALUE(CBMT_CLOSE_IN)->SecondaryAddress);
            break;

        case CBMCTRL_RESET:
            DBG_IRP(CBMCTRL_RESET);
            ntStatus = cbmiec_reset(Pdx);
            break;

        case CBMCTRL_GET_EOI:
            DBG_IRP(CBMCTRL_GET_EOI);
            returnLength = sizeof(CBMT_GET_EOI_OUT);
            ntStatus = cbmiec_get_eoi(Pdx, &(OUTPUTVALUE(CBMT_GET_EOI_OUT)->Decision));
            break;

        case CBMCTRL_CLEAR_EOI:
            DBG_IRP(CBMCTRL_CLEAR_EOI);
            ntStatus = cbmiec_clear_eoi(Pdx);
            break;

        case CBMCTRL_PP_READ:
            DBG_IRP(CBMCTRL_PP_READ);
            ntStatus = cbm_checkoutputbuffer(irpSp, sizeof(CBMT_PP_READ_OUT), STATUS_SUCCESS);
            returnLength = sizeof(CBMT_PP_READ_OUT);
            ntStatus = cbmiec_pp_read(Pdx, &(OUTPUTVALUE(CBMT_PP_READ_OUT)->Byte));
            break;

        case CBMCTRL_PP_WRITE:
            DBG_IRP(CBMCTRL_PP_WRITE);
            ntStatus = cbmiec_pp_write(Pdx, INPUTVALUE(CBMT_PP_WRITE_IN)->Byte);
            break;

        case CBMCTRL_IEC_POLL:
            DBG_IRP(CBMCTRL_IEC_POLL);
            returnLength = sizeof(CBMT_IEC_POLL_OUT);
            ntStatus = cbmiec_iec_poll(Pdx, &(OUTPUTVALUE(CBMT_IEC_POLL_OUT)->Line));
            break;

        case CBMCTRL_IEC_SET:
            DBG_IRP(CBMCTRL_IEC_SET);
            ntStatus = cbmiec_iec_set(Pdx, INPUTVALUE(CBMT_IEC_SET_IN)->Line);
            break;

        case CBMCTRL_IEC_RELEASE:
            DBG_IRP(CBMCTRL_IEC_RELEASE);
            ntStatus = cbmiec_iec_release(Pdx, INPUTVALUE(CBMT_IEC_RELEASE_IN)->Line);
            break;

        case CBMCTRL_IEC_SETRELEASE:
            DBG_IRP(CBMCTRL_IEC_SETRELEASE);
            ntStatus = cbmiec_iec_setrelease(Pdx,
                            INPUTVALUE(CBMT_IEC_SETRELEASE_IN)->State,
                            INPUTVALUE(CBMT_IEC_SETRELEASE_IN)->Line);
            break;

        case CBMCTRL_IEC_WAIT:
            DBG_IRP(CBMCTRL_IEC_WAIT);
            returnLength = sizeof(CBMT_IEC_WAIT_OUT);
            ntStatus = cbmiec_iec_wait(Pdx, INPUTVALUE(CBMT_IEC_WAIT_IN)->Line, 
                                       INPUTVALUE(CBMT_IEC_WAIT_IN)->State,
                                       &(OUTPUTVALUE(CBMT_IEC_WAIT_OUT)->Line));
            break;

        case CBMCTRL_I_INSTALL:
            DBG_IRP(CBMCTRL_I_INSTALL);
            returnLength = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
            ntStatus = cbm_install(Pdx, OUTPUTVALUE(CBMT_I_INSTALL_OUT), &returnLength);
            break;

#if DBG

        case CBMCTRL_I_READDBG:
            DBG_IRP(CBMCTRL_I_READDBG);
            returnLength = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
            ntStatus = cbm_dbg_readbuffer(Pdx, OUTPUTVALUE(CHAR), &returnLength);
            break;

#endif // #if DBG

        default:
            // As cbm_devicecontrol() already checked the IRP,
            // this piece of code should never be entered. If it
            // is, this is a sign of a forgotten IOCTL, or a severe
            // programming error

            DBG_ERROR((DBG_PREFIX "unknown IRP_MJ_DEVICE_CONTROL"));
            DBG_ASSERT(("THIS SHOULD NOT HAPPEN!", 0));
            ntStatus = STATUS_INVALID_PARAMETER;
            break;
    }

    // If an error occurred, make sure not to return anything.

    if (!NT_SUCCESS(ntStatus))
    {
        returnLength = 0;
    }

    // Complete the request:

    DBG_IRPPATH_COMPLETE("Execute Ioctl");
    QueueCompleteIrp(&Pdx->IrpQueue, Irp, ntStatus, returnLength);

    FUNC_LEAVE_NTSTATUS(ntStatus);
}