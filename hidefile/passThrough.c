#include "basecode.h"
#include "precomp.h"

#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")

PFLT_FILTER gFilterHandle;
PFLT_PORT gServerPort;
PFLT_PORT gClientPort;

ULONG_PTR OperationStatusCtx = 1;

#define PTDBG_TRACE_ROUTINES            0x00000001
#define PTDBG_TRACE_OPERATION_STATUS    0x00000002

ULONG gTraceFlags = 0;
BOOLEAN service_enable = TRUE;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, PtUnload)
#endif


CONST FLT_OPERATION_REGISTRATION Callbacks[] = {

	{ IRP_MJ_CREATE,
	FLTFL_OPERATION_REGISTRATION_SKIP_PAGING_IO,
	PreCreate,
	NULL },

	{ IRP_MJ_NETWORK_QUERY_OPEN,
	FLTFL_OPERATION_REGISTRATION_SKIP_PAGING_IO,
	PreCreate,
	NULL },

	{ IRP_MJ_DIRECTORY_CONTROL,
	0,
	NULL,
	PtPostDirCtrlPassThrough },

	{ IRP_MJ_OPERATION_END }
};



CONST FLT_REGISTRATION FilterRegistration = {

	sizeof( FLT_REGISTRATION ),         //  Size
	FLT_REGISTRATION_VERSION,           //  Version
	0,                                  //  Flags

	NULL,                               //  Context
	Callbacks,                          //  Operation callbacks

	PtUnload,                           //  MiniFilterUnload

	InstanceSetup,                    //  InstanceSetup
	NULL,            //  InstanceQueryTeardown
	NULL,            //  InstanceTeardownStart
	NULL,         //  InstanceTeardownComplete

	NULL,                               //  GenerateFileName
	NULL,                               //  GenerateDestinationFileName
	NULL                                //  NormalizeNameComponent

};

NTSTATUS
DriverEntry (
			 __in PDRIVER_OBJECT DriverObject,
			 __in PUNICODE_STRING RegistryPath
			 )
	
		
{
	NTSTATUS status;
	PSECURITY_DESCRIPTOR sd;
	OBJECT_ATTRIBUTES oa;
	UNICODE_STRING uniString;

	UNREFERENCED_PARAMETER( RegistryPath );

	DbgPrint("Enter DriverEntry()\n");

	//
	// get process name offset from PEB
	//
	g_nProcessNameOffset = Ps_GetProcessNameOffset();

	//
	// init process list entry to hold all of user processes which may be supervised.
	// init spin lock to synchronize process list operations.
	//
	InitializeListHead(&g_ProcessListHead);
	KeInitializeSpinLock(&g_ProcessListLock);

	InitWhiteProcNameListAndLock();


	status = FltRegisterFilter( DriverObject,
		&FilterRegistration,
		&gFilterHandle );

	ASSERT( NT_SUCCESS( status ) );

	if (NT_SUCCESS( status )) 
	{
		status  = FltBuildDefaultSecurityDescriptor( &sd,FLT_PORT_ALL_ACCESS );

		if (!NT_SUCCESS( status )) 
		{
			FltUnregisterFilter(gFilterHandle);
		}

		RtlInitUnicodeString( &uniString, MINISPY_PORT_NAME );

		InitializeObjectAttributes( &oa,
			&uniString,
			OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
			NULL,
			sd );

		status = FltCreateCommunicationPort( gFilterHandle,
			&gServerPort,
			&oa,
			NULL,
			SpyConnect,
			SpyDisconnect,
			SpyMessage,
			1 );

		FltFreeSecurityDescriptor( sd );

		if (!NT_SUCCESS( status )) 
		{
			FltUnregisterFilter( gFilterHandle );
		}


		status = FltStartFiltering( gFilterHandle );

		if (!NT_SUCCESS( status )) 
		{

			FltUnregisterFilter( gFilterHandle );
		}
	}
	//AddNameToWhiteNameList(L"c:",L"list.txt");
	//AddNameToWhiteNameList(L"c:",L"change.txt");
	DbgPrint("Leave DriverEntry()\n");

	return status;
}


NTSTATUS
PtUnload (
		  __in FLT_FILTER_UNLOAD_FLAGS Flags
		  )
	
{
	UNREFERENCED_PARAMETER( Flags );

	PAGED_CODE();

	DbgPrint("Enter PtUnload()\n");

	ClearWhiteNameList();

	FltCloseCommunicationPort( gServerPort );

	FltUnregisterFilter( gFilterHandle );

	DbgPrint("Leave PtUnload()\n");

	return STATUS_SUCCESS;
}


NTSTATUS
InstanceSetup(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_SETUP_FLAGS Flags,
	_In_ DEVICE_TYPE VolumeDeviceType,
	_In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
)
/*++

Routine Description:

This routine is called whenever a new instance is created on a volume. This
gives us a chance to decide if we need to attach to this volume or not.
SimRep does not attach on automatic attachment, but will attach when asked
manually.

Arguments:

FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
opaque handles to this filter, instance and its associated volume.

Flags - Flags describing the reason for this attach request.

Return Value:

STATUS_SUCCESS - attach
STATUS_FLT_DO_NOT_ATTACH - do not attach

--*/
{

	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
	UNREFERENCED_PARAMETER(VolumeDeviceType);
	UNREFERENCED_PARAMETER(VolumeFilesystemType);

	PAGED_CODE();

	if (FlagOn(Flags, FLTFL_INSTANCE_SETUP_AUTOMATIC_ATTACHMENT)) {

		//
		//  Do not automatically attach to a volume.
		//

		PT_DBG_PRINT(PTDBG_TRACE_ROUTINES,
			("[Simrep]: Instance setup skipped (Volume = %p, Instance = %p)\n",
				FltObjects->Volume,
				FltObjects->Instance));

		return STATUS_FLT_DO_NOT_ATTACH;
	}

	//
	//  Attach on manual attachment.
	//

	PT_DBG_PRINT(PTDBG_TRACE_ROUTINES,
		("[SimRep]: Instance setup started (Volume = %p, Instance = %p)\n",
			FltObjects->Volume,
			FltObjects->Instance));


	return STATUS_SUCCESS;
}

FLT_PREOP_CALLBACK_STATUS
PtPreOperationPassThrough (
						   __inout PFLT_CALLBACK_DATA Data,
						   __in PCFLT_RELATED_OBJECTS FltObjects,
						   __deref_out_opt PVOID *CompletionContext
						   )
						
{
	UNREFERENCED_PARAMETER(Data);
	UNREFERENCED_PARAMETER( FltObjects );
	UNREFERENCED_PARAMETER( CompletionContext );

	return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}




FLT_POSTOP_CALLBACK_STATUS
PtPostOperationPassThrough (
							__inout PFLT_CALLBACK_DATA Data,
							__in PCFLT_RELATED_OBJECTS FltObjects,
							__in_opt PVOID CompletionContext,
							__in FLT_POST_OPERATION_FLAGS Flags
							)
						
{
	UNREFERENCED_PARAMETER( Data );
	UNREFERENCED_PARAMETER( FltObjects );
	UNREFERENCED_PARAMETER( CompletionContext );
	UNREFERENCED_PARAMETER( Flags );

	PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
		("PassThrough!PtPostOperationPassThrough: Entered\n") );

	return FLT_POSTOP_FINISHED_PROCESSING;
}

NTSTATUS FileGetPathName(PCFLT_RELATED_OBJECTS FltObjects, PUNICODE_STRING pFilePath)
{
	UNICODE_STRING volumeDosName;
	NTSTATUS status;
	PDEVICE_OBJECT devObj = NULL;
	wchar_t* pBuff = NULL;
	ULONG ulen = 0;

	if (FltObjects == NULL || FltObjects->Volume == NULL || pFilePath == NULL)
		return STATUS_UNSUCCESSFUL;

	volumeDosName.Length = MAX_PATH;
	volumeDosName.MaximumLength = MAX_PATH;
	volumeDosName.Buffer = (PWSTR)ExAllocatePool(NonPagedPool, MAX_PATH);
	if (volumeDosName.Buffer == NULL)
		return STATUS_UNSUCCESSFUL;

	status = FltGetDiskDeviceObject(FltObjects->Volume, &devObj);
	if (!NT_SUCCESS(status))
		return status;
	status = IoVolumeDeviceToDosName(devObj, &volumeDosName);
	if (!NT_SUCCESS(status))
		return status;

	ulen = volumeDosName.MaximumLength + FltObjects->FileObject->FileName.MaximumLength;
	pBuff = ExAllocatePool(NonPagedPool, ulen);
	if (pBuff == NULL)
		return STATUS_UNSUCCESSFUL;
	RtlZeroMemory(pBuff, ulen);

	RtlStringCbPrintfExW(pBuff, ulen, NULL, NULL, STRSAFE_FILL_BEHIND_NULL, L"%wZ%wZ", &volumeDosName, &FltObjects->FileObject->FileName);
	RtlInitUnicodeString(pFilePath, pBuff);
	if (volumeDosName.Buffer)
		ExFreePool(volumeDosName.Buffer);
	return STATUS_SUCCESS;
}

FLT_PREOP_CALLBACK_STATUS
PreCreate(
	__inout PFLT_CALLBACK_DATA Data,
	__in PCFLT_RELATED_OBJECTS FltObjects,
	__deref_out_opt PVOID *CompletionContext
)
{
	PAGED_CODE();

	NTSTATUS status;
	UNICODE_STRING strFilePath;
	FLT_PREOP_CALLBACK_STATUS callbackStatus;
	UNREFERENCED_PARAMETER(CompletionContext);

	//DbgPrint("***Enter PreCreate()***\n");

	status = STATUS_SUCCESS;
	callbackStatus = FLT_PREOP_SUCCESS_NO_CALLBACK; 
	
	if( !service_enable )
		return callbackStatus;
		//��ȡ�ļ�·��  
		status = FileGetPathName(FltObjects, &strFilePath);
		if (!NT_SUCCESS(status))
			goto SimRepPreCreateCleanup;
		
		//�ж��Ƿ����ض�λ���ļ�  
		if (SearchFile(strFilePath.Buffer ) && 
			Ps_IsCurrentProcessMonitored(strFilePath.Buffer, strFilePath.Length) )
		{
			PUNICODE_STRING filename = &FltObjects->FileObject->FileName;
			UNICODE_STRING file_base64;
			UStringBase64EncodeFileName(&strFilePath, &file_base64); 
#ifndef _WIN64
			int ulen = file_base64.MaximumLength +\
				wcslen(L"\\Device\\Volume{aa4fcbdf-b6a2-11e6-8556-000c2975342d}\\")*sizeof(WCHAR);
#else
			int ulen = file_base64.MaximumLength +
				wcslen(L"\\Device\\Volume{f1209d33-496d-11e6-995f-9c2a70d53f1c}\\") * sizeof(WCHAR);
#endif
			PVOID pBuff = ExAllocatePool(NonPagedPool, ulen);
			if (pBuff)
			{
				RtlZeroMemory(pBuff, ulen);
#ifndef _WIN64
				RtlStringCbPrintfExW(pBuff, ulen, NULL, NULL, STRSAFE_FILL_BEHIND_NULL, \
					L"\\Device\\Volume{aa4fcbdf-b6a2-11e6-8556-000c2975342d}\\%wZ", &file_base64);
#else
				RtlStringCbPrintfExW(pBuff, ulen, NULL, NULL, STRSAFE_FILL_BEHIND_NULL, \
					L"\\Device\\Volume{f1209d33-496d-11e6-995f-9c2a70d53f1c}\\%wZ", &file_base64);
#endif
				ExFreePool(filename->Buffer);
				filename->Length = wcslen(pBuff)*sizeof(WCHAR);
				filename->MaximumLength = ulen;
				filename->Buffer = pBuff;
				DbgPrint("%wZ have redirected!\n", strFilePath);
				status = STATUS_REPARSE;
			}
			RtlFreeUnicodeString(&file_base64);
		}
		ExFreePool(strFilePath.Buffer);

SimRepPreCreateCleanup:
	if (status == STATUS_REPARSE)
	{
		Data->IoStatus.Status = STATUS_REPARSE;
		Data->IoStatus.Information = IO_REPARSE;
		callbackStatus = FLT_PREOP_COMPLETE;
	}
	else if (!NT_SUCCESS(status))
	{
		Data->IoStatus.Status = status;
		callbackStatus = FLT_PREOP_COMPLETE;
	}
	//DbgPrint("***Leave PreCreate()***\n");
	return callbackStatus;
}

NTSTATUS
SpyConnect(
		   __in PFLT_PORT ClientPort,
		   __in PVOID ServerPortCookie,
		   __in_bcount(SizeOfContext) PVOID ConnectionContext,
		   __in ULONG SizeOfContext,
		   __deref_out_opt PVOID *ConnectionCookie
		   )
{

	PAGED_CODE();

	UNREFERENCED_PARAMETER( ServerPortCookie );
	UNREFERENCED_PARAMETER( ConnectionContext );
	UNREFERENCED_PARAMETER( SizeOfContext);
	UNREFERENCED_PARAMETER( ConnectionCookie );


	gClientPort = ClientPort;
	return STATUS_SUCCESS;
}

VOID
SpyDisconnect(
			  __in_opt PVOID ConnectionCookie
			  )
{

	PAGED_CODE();

	UNREFERENCED_PARAMETER( ConnectionCookie );


	FltCloseClientPort( gFilterHandle, &gClientPort );
}


NTSTATUS
SpyMessage (
			__in PVOID ConnectionCookie,
			__in_bcount_opt(InputBufferSize) PVOID InputBuffer,
			__in ULONG InputBufferSize,
			__out_bcount_part_opt(OutputBufferSize,*ReturnOutputBufferLength) PVOID OutputBuffer,
			__in ULONG OutputBufferSize,
			__out PULONG ReturnOutputBufferLength
			)
{
	INPUT_BUFFER inputBuf;
	MY_COMMAND command;

	DbgPrint("Enter SpyMessage()\n");

	RtlZeroMemory(&inputBuf, sizeof(INPUT_BUFFER));

	if ( (InputBuffer == NULL) || (InputBufferSize > sizeof(INPUT_BUFFER)))
	{
		return STATUS_INVALID_PARAMETER;
	}

	RtlCopyMemory(&inputBuf, InputBuffer, InputBufferSize);

	command = inputBuf.command;

	switch (command)
	{
	case ADD_PROTECTED_PATH: 
		AddNameToWhiteNameList(inputBuf.protectedPath, inputBuf.fileType);
		break;

	case DEL_PROTECTED_PATH: 
		DelNameFromWhiteNameList(inputBuf.protectedPath, inputBuf.fileType);
		break;
		
	case START_SERVICE:
		service_enable = TRUE;
		break;
		
	case STOP_SERVICE:
		service_enable = FALSE;
		break;
		
	case ADD_PROTECTED_PROCESS:
		Psi_AddProcessInfo(inputBuf.process, TRUE);
		break;

	case DEL_PROTECTED_PROCESS:
		Psi_DelProcessInfo(inputBuf.process, TRUE);
		break;

	default:
		break;
	}

	DbgPrint("Leave SpyMessage()\n");

	return STATUS_SUCCESS;
}


FileMonQuerySymbolicLink(
						 IN PUNICODE_STRING SymbolicLinkName,//�����̷���,driveLetterName
						 OUT PUNICODE_STRING LinkTarget
						 )
{
	/*
	��ֱ��ʹ��ObQueryNameString��ѯ�ļ�����Ļ������ص����ƾ����豸���ƺ���·������
	�磺/Device/HarddiskVolume1/WINDOWS/system32/smss.exe
	Ӧ�ò���Ҫ���豸������ͨ���������ӣ�ͬ���ں˲���ں˲����Ҳ����ͨ���������ӣ�
	��������ZwOpenSymbolicLinkObject�����õ��������Ӿ����Ȼ��ʹ��ZwQuerySymbolicLinkObject�õ��豸����
	�����豸���Ϳ���ͨ������ķ������豸������ΪʲôҪ������Ȧ���أ�
	�������Ӧ�ò�ȥ����һ����������ȥ������һ�����������ʱ��������Ӧ�ò㣬
	�ǵò����Ǹ��������򴴽����豸���ģ�ֻ�ܵõ��������ӣ����ʱ��ͱ��������ַ�����

	�ܶ�����£�����֪��������豸������ֻ֪���������ӡ����硰C:"�����һ��Ӳ����������"C:"���ǵ�һ���������ӣ�
    ��ָ���һ�����̷����豸��������WDM�����豸�У�ͨ���������Ӵ��豸�Ǿ��������ġ�����ZwOpenSymbolicLinkObject����
	�ȵõ��������ӵľ����Ȼ��ʹ��ZwQuerySymbolicLinkObject�������ҵ��豸����ͨ���豸���Ϳ��Է���ش��豸��
	*/


	OBJECT_ATTRIBUTES oa;
	NTSTATUS status;
	HANDLE h;

	InitializeObjectAttributes(&oa, SymbolicLinkName, OBJ_KERNEL_HANDLE|OBJ_CASE_INSENSITIVE,0, 0);

	//����ZwOpenSymbolcLink���������A----Z��ĸ���д����Ӷ��󣬲�����ZwQuerySymbilicLink����ö�Ӧ���豸������
	status = ZwOpenSymbolicLinkObject(&h, GENERIC_READ, &oa);
	if (!NT_SUCCESS(status)) 
	{
		return status;
	}

	LinkTarget->MaximumLength = 200*sizeof(WCHAR);
	LinkTarget->Length = 0;
	LinkTarget->Buffer = ExAllocatePoolWithTag(PagedPool, LinkTarget->MaximumLength,'dsf ');
	if (!LinkTarget->Buffer) 
	{
		ZwClose(h);
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlZeroMemory(LinkTarget->Buffer, LinkTarget->MaximumLength);

	//Ȼ��ͨ��h��������ZwQuerySymbolicLinkObject�������õ��������Ӷ���
	status = ZwQuerySymbolicLinkObject(h, LinkTarget, NULL);
	ZwClose(h);

	if (!NT_SUCCESS(status)) 
	{
		ExFreePoolWithTag(LinkTarget->Buffer,'dsf ');
	}

	return status;
}


NTSTATUS
FileMonVolumeDeviceNameToDosName(
								 IN PUNICODE_STRING DeviceName,
								 OUT PUNICODE_STRING DosName
								 )
{
	NTSTATUS status;
	UNICODE_STRING driveLetterName;
	WCHAR c;
	UNICODE_STRING linkTarget;
	WCHAR     TempLetterName[]=L"\\??\\C:";   //��ʼ���̷���Ĭ��ΪC:��

	DosName->Buffer=NULL;

	RtlInitUnicodeString(&driveLetterName, TempLetterName);

	for (c = 'A'; c <= 'Z'; c++) //�����̷�
	{
		driveLetterName.Buffer[4] = c;

		status = FileMonQuerySymbolicLink(&driveLetterName, &linkTarget);


		if (!NT_SUCCESS(status)) 
		{
			continue;
		}


		//�Ѹոջ�ȡ�ķ������Ӷ����֮ǰ�õ����豸�����Ƚϣ���������ȷ������������
		if (RtlEqualUnicodeString(&linkTarget, DeviceName, TRUE)) 
		{
			ExFreePoolWithTag(linkTarget.Buffer,'dsf ');
			break;
		}

		ExFreePoolWithTag(linkTarget.Buffer,'dsf ');
	}

	if (c <= 'Z') 
	{
		DosName->Buffer = ExAllocatePoolWithTag(PagedPool, 3*sizeof(WCHAR),'dsf ');
		if (!DosName->Buffer) 
		{
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		//�����������ȷ��������������
		DosName->MaximumLength = 6;
		DosName->Length = 4;
		DosName->Buffer[0] = c + (L'a' - L'A');
		DosName->Buffer[1] = ':';
		DosName->Buffer[2] = 0;
		return STATUS_SUCCESS;
	}
	return STATUS_OBJECT_NAME_NOT_FOUND;
}



void RemoveBacklash(wchar_t *szFileName)
{
	if(szFileName[wcslen(szFileName)-1] == L'\\')
		szFileName[wcslen(szFileName)-1] = 0;
}



NTSTATUS
FileMonGetFullPathName(
					   PFLT_FILE_NAME_INFORMATION nameInfo,
					   WCHAR * fullpathname
					   )

{
	NTSTATUS ntStatus = STATUS_SUCCESS;
	UNICODE_STRING dosName;
	WCHAR* wTempName= NULL;

	ntStatus=FileMonVolumeDeviceNameToDosName((PUNICODE_STRING)&nameInfo->Volume,&dosName);
	if(NT_SUCCESS( ntStatus ))
	{
		
		RtlCopyMemory(fullpathname,dosName.Buffer,dosName.Length);

		if (dosName.Buffer)
		{
			ExFreePoolWithTag(dosName.Buffer,'dsf ');
		}

		wTempName=(WCHAR*)ExAllocatePoolWithTag(NonPagedPool,MAXPATHLEN,'dsf ');
		if(wTempName!=NULL)
		{
			RtlZeroMemory(wTempName,MAXPATHLEN);
			RtlCopyMemory(wTempName,nameInfo->ParentDir.Buffer,nameInfo->ParentDir.Length);
			
			wcscat(fullpathname,wTempName);
			
			RtlZeroMemory(wTempName,MAXPATHLEN);
			RtlCopyMemory(wTempName,nameInfo->FinalComponent.Buffer,nameInfo->FinalComponent.Length);
			
			wcscat(fullpathname,wTempName);

			if (wTempName)
			{
				ExFreePoolWithTag(wTempName,'dsf ');
			}

		}

	}else
	{
		ntStatus = STATUS_UNSUCCESSFUL;
	}
	return ntStatus;
}



FLT_POSTOP_CALLBACK_STATUS
PtPostDirCtrlPassThrough (
						  __inout PFLT_CALLBACK_DATA Data,
						  __in PCFLT_RELATED_OBJECTS FltObjects,
						  __in_opt PVOID CompletionContext,
						  __in FLT_POST_OPERATION_FLAGS Flags 
						  )
{
	ULONG nextOffset = 0;
	int modified = 0;
	int removedAllEntries = 1;  
	WCHAR *fullPathLongName;
	NTSTATUS status;
	PFLT_FILE_NAME_INFORMATION nameInfo;

	PFILE_BOTH_DIR_INFORMATION currentFileInfo = 0;     
	PFILE_BOTH_DIR_INFORMATION nextFileInfo = 0;    
	PFILE_BOTH_DIR_INFORMATION previousFileInfo = 0;    

	PFILE_ID_BOTH_DIR_INFORMATION currentFileIdInfo = 0;
	PFILE_ID_BOTH_DIR_INFORMATION nextFileIdInfo = 0;
	PFILE_ID_BOTH_DIR_INFORMATION previousFileIdInfo = 0;

	UNREFERENCED_PARAMETER( FltObjects );
	UNREFERENCED_PARAMETER( CompletionContext );   

	//DbgPrint(" Enter PtPostDirCtrlPassThrough()\n");

	if( FlagOn( Flags, FLTFL_POST_OPERATION_DRAINING ) || 
		Data->Iopb->MinorFunction != IRP_MN_QUERY_DIRECTORY ||
		Data->Iopb->Parameters.DirectoryControl.QueryDirectory.Length <= 0 ||
		!NT_SUCCESS(Data->IoStatus.Status) || service_enable )
	{
		return FLT_POSTOP_FINISHED_PROCESSING;
	}


	fullPathLongName = ExAllocatePool(NonPagedPool, _CMD_PATH*sizeof(WCHAR));
	if (fullPathLongName == NULL)
	{
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	RtlZeroMemory(fullPathLongName, 296*sizeof(WCHAR));

	status = FltGetFileNameInformation( Data,
		FLT_FILE_NAME_OPENED|FLT_FILE_NAME_QUERY_ALWAYS_ALLOW_CACHE_LOOKUP,
		&nameInfo );

	if (!NT_SUCCESS(status))
	{
		goto LAST_CODE;
	}


	FltParseFileNameInformation( nameInfo );

	status = FileMonGetFullPathName(nameInfo,fullPathLongName);
	if (!NT_SUCCESS(status))
	{
		goto LAST_CODE;
	}

	FltReleaseFileNameInformation( nameInfo );
	RemoveBacklash(fullPathLongName);


	//WindowsXP�������°汾����Ҫ���� FileBothDirectoryInformation ���͵���Ϣ 
	if(Data->Iopb->Parameters.DirectoryControl.QueryDirectory.FileInformationClass == FileBothDirectoryInformation)
	{
		
		/*
		����õ�һ���������������������ͱ������ļ��������е��ļ���Ϣ��Ȼ�󣬸�����������
		�ṹ�����������˵�Ҫ���ص��ļ������ܴﵽ���ص�Ŀ���ˡ� 
		*/
		if (Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress != NULL)
		{
			currentFileInfo=(PFILE_BOTH_DIR_INFORMATION)MmGetSystemAddressForMdlSafe( 
				Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress,
				NormalPagePriority );            
		}
		else
		{
			currentFileInfo=(PFILE_BOTH_DIR_INFORMATION)Data->Iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer;             
		}     

		if(currentFileInfo==NULL)return FLT_POSTOP_FINISHED_PROCESSING;       
		previousFileInfo = currentFileInfo;

		do
		{
			WCHAR *tempBuf;

			nextOffset = currentFileInfo->NextEntryOffset;//�õ���һ������ƫ�Ƶ�ַ
			nextFileInfo = (PFILE_BOTH_DIR_INFORMATION)((PCHAR)(currentFileInfo) + nextOffset); //��̽��ָ��          

			tempBuf = (WCHAR *)ExAllocatePool(NonPagedPool, _CMD_PATH*sizeof(WCHAR));

			if (tempBuf == NULL)
			{
				goto LAST_CODE;
			}

			RtlZeroMemory(tempBuf, _CMD_PATH*sizeof(WCHAR));
			RtlCopyMemory(tempBuf, currentFileInfo->FileName, currentFileInfo->FileNameLength);

			if (SearchIsProtect(fullPathLongName, tempBuf)) //������Ҫ�������ļ�
			{
				if( nextOffset == 0 )
				{
					previousFileInfo->NextEntryOffset = 0;
				}

				else//����ǰ�������ָ����һ����ƫ�������Թ�Ҫ���ص��ļ����ļ���㣬�ﵽ����Ŀ��
				{
					previousFileInfo->NextEntryOffset = (ULONG)((PCHAR)currentFileInfo - (PCHAR)previousFileInfo) + nextOffset;
				}
				modified = 1;
			}
			else
			{
				removedAllEntries = 0;
				previousFileInfo = currentFileInfo;  //ǰ�����ָ����� 
			}     
			currentFileInfo = nextFileInfo; //��ǰָ����� 

			if (tempBuf != NULL)
			{
				ExFreePool(tempBuf);
			}

		} while( nextOffset != 0 );
	}


	//
	//Windows Vista��Windows7����߰汾��Windows�Ĳ���ϵͳ��
	//���Ƿ��صĽṹ������FileBothDirectoryInformation. ����FileIdBothDirectoryInformation
	else if(Data->Iopb->Parameters.DirectoryControl.QueryDirectory.FileInformationClass ==FileIdBothDirectoryInformation)
	{	
		/*
		����õ�һ���������������������ͱ������ļ��������е��ļ���Ϣ��Ȼ��
		�����������Ľṹ�����������˵�Ҫ���ص��ļ������ܴﵽ���ص�Ŀ���ˡ� 
		*/
		if (Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress != NULL)
		{
			currentFileIdInfo=(PFILE_ID_BOTH_DIR_INFORMATION)MmGetSystemAddressForMdlSafe( 
				Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress,
				NormalPagePriority );            
		}
		else
		{
			currentFileIdInfo=(PFILE_ID_BOTH_DIR_INFORMATION)Data->Iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer;             
		}     

		if(currentFileIdInfo==NULL)return FLT_POSTOP_FINISHED_PROCESSING;
		previousFileIdInfo = currentFileIdInfo;

		do
		{
			nextOffset = currentFileIdInfo->NextEntryOffset; //�õ���һ������ƫ�Ƶ�ַ   
			nextFileIdInfo = (PFILE_ID_BOTH_DIR_INFORMATION)((PCHAR)(currentFileIdInfo) + nextOffset);  //��̽��ָ��            

			if (SearchIsProtect(fullPathLongName, currentFileIdInfo->FileName))
			{
				if( nextOffset == 0 )
				{
					previousFileIdInfo->NextEntryOffset = 0;
				}
				else//����ǰ�������ָ����һ����ƫ�������Թ�Ҫ���ص��ļ����ļ���㣬�ﵽ����Ŀ��
				{
					previousFileIdInfo->NextEntryOffset = (ULONG)((PCHAR)currentFileIdInfo - (PCHAR)previousFileIdInfo) + nextOffset;
				}
				modified = 1;
			}
			else
			{
				removedAllEntries = 0;                
				previousFileIdInfo = currentFileIdInfo;                
			}
			currentFileIdInfo = nextFileIdInfo;

		} while( nextOffset != 0 );
	}


LAST_CODE:

	if( modified )
	{
		if( removedAllEntries )
		{
			Data->IoStatus.Status = STATUS_NO_MORE_FILES;
		}
		else
		{
			FltSetCallbackDataDirty( Data );
		}
	}   

	if (fullPathLongName)
	{
		ExFreePool(fullPathLongName);
	}

	//DbgPrint(" Leave PtPostDirCtrlPassThrough()\n");


	return FLT_POSTOP_FINISHED_PROCESSING;
}