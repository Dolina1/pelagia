/* job.c - Thread related functions
*
* Copyright(C) 2019 - 2020, sun shuo <sun.shuo@surparallel.org>
* All rights reserved.
*
* This program is free software : you can redistribute it and / or modify
* it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or(at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
* GNU Affero General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with this program.If not, see < https://www.gnu.org/licenses/>.
*/

#include "plateform.h"
#include <pthread.h>
#include "psds.h"
#include "pdict.h"
#include "pjob.h"
#include "pequeue.h"
#include "padlist.h"
#include "pcache.h"
#include "pinterface.h"
#include "pmanage.h"
#include "plocks.h"
#include "pelog.h"
#include "pdictexten.h"
#include "ptimesys.h"
#include "plibsys.h"
#include "plvm.h"
#include "pquicksort.h"
#include "pelagia.h"

/*
Thread model can be divided into two ways: asynchronous and synchronous
This is also true between multiple threads
For read operations, synchronization is used because the user virtual single thread environment needs to be maintained
For write operations, asynchronous queue mode is used for efficiency between different hardware environments
Disk writes are not crowded, so thread queues and threads are not implemented
Manage has separate threads because it manages multiple modules
File has a separate thread to write to a slow hard disk
*/
#define NORET
#define CheckUsingThread(r) if (plg_JobCheckUsingThread()) {elog(log_error, "Cannot run job interface in non job environment"); return r;}

enum ScriptType {
	ST_LUA = 1,
	ST_DLL = 2,
	ST_PTR = 3
};

typedef struct _EventPorcess
{
	unsigned char scriptType;
	sds fileClass;
	sds function;
	RoutingFun functionPoint;
	unsigned int weight;
}*PEventPorcess, EventPorcess;

static void PtrFreeCallback(void *privdata, void *val) {
	DICT_NOTUSED(privdata);
	plg_CacheDestroyHandle(val);
}

static int sdsCompareCallback(void *privdata, const void *key1, const void *key2) {
	int l1, l2;
	DICT_NOTUSED(privdata);

	l1 = plg_sdsLen((sds)key1);
	l2 = plg_sdsLen((sds)key2);
	if (l1 != l2) return 0;
	return memcmp(key1, key2, l1) == 0;
}

static unsigned long long sdsHashCallback(const void *key) {
	return plg_dictGenHashFunction((unsigned char*)key, plg_sdsLen((char*)key));
}

static dictType PtrDictType = {
	sdsHashCallback,
	NULL,
	NULL,
	sdsCompareCallback,
	NULL,
	PtrFreeCallback
};

/*
Threadtype: the type of the current thread
Pmanagequeue: event handle for management thread
Privatedata: the private data of the current thread, which is filehandle
EQueue: the message slot of the current thread
Order Ou queue: message slot corresponding to all events
Dictcache: all the caches of a job are used to release, create and check whether the tablename is writable
Order_Process: the processing process of the current thread event
Tablename? Cachehandle: Currently, all cachehandles corresponding to tablename are used to find and write data
Allweight: all processes have the same weight
Userevent: event not called remotely
Userprocess: handling of non remote call events
ExitThread: exit flag
Trancache: cache used by the current transaction
Tranflush: multiple caches preparing for flush
Transaction commit related flags
Flush_laststamp: time of last submission
Flush_interval: commit interval
Flush_Lastcount: number of submissions
Flush Ou count: total number of times
*/
typedef struct _JobHandle
{
	enum ThreadType threadType;
	void* pManageEqueue;
	void* privateData;
	void* eQueue;
	dict* order_equeue;
	dict* dictCache;
	dict* order_process;
	dict* tableName_cacheHandle;
	unsigned int allWeight;
	list* userEvent;
	list* userProcess;
	short exitThread;
	short donotFlush;
	short donotCommit;

	list* tranCache;
	list* tranFlush;

	//config
	unsigned long long flush_lastStamp;
	unsigned int flush_interval;
	unsigned int flush_lastCount;
	unsigned int flush_count;

	//vm
	char* luaPath;
	void* luaHandle;
	void* dllHandle;

	//order;
	char* pOrderName;

	//intervalometer
	list* pListIntervalometer;

} *PJobHandle, JobHandle;

SDS_TYPE
void* plg_JobCreateFunPtr(RoutingFun funPtr) {

	long long ss = ll;
	ss = ss * 0;
	PEventPorcess pEventPorcess = malloc(sizeof(EventPorcess));
	pEventPorcess->scriptType = ST_PTR;
	pEventPorcess->functionPoint = funPtr;
	pEventPorcess->weight = 1;
	return pEventPorcess;
}

void* plg_JobCreateLua(char* fileClass, short fileClassLen, char* fun, short funLen) {

	PEventPorcess pEventPorcess = malloc(sizeof(EventPorcess));
	pEventPorcess->scriptType = ST_LUA;
	pEventPorcess->fileClass = plg_sdsNewLen(fileClass, fileClassLen);
	pEventPorcess->function = plg_sdsNewLen(fun, funLen);
	pEventPorcess->weight = 1;
	return pEventPorcess;
}

void* plg_JobCreateDll(char* fileClass, short fileClassLen, char* fun, short funLen) {

	PEventPorcess pEventPorcess = malloc(sizeof(EventPorcess));
	pEventPorcess->scriptType = ST_DLL;
	pEventPorcess->fileClass = plg_sdsNewLen(fileClass, fileClassLen);
	pEventPorcess->function = plg_sdsNewLen(fun, funLen);
	pEventPorcess->weight = 1;
	return pEventPorcess;
}

void plg_JobSetWeight(void* pvEventPorcess, unsigned int weight) {
	PEventPorcess pEventPorcess = pvEventPorcess;
	pEventPorcess->weight = weight;
}

void plg_JobProcessDestory(void* pvEventPorcess) {

	PEventPorcess pEventPorcess = pvEventPorcess;
	if (pEventPorcess->scriptType != ST_PTR) {
		plg_sdsFree(pEventPorcess->fileClass);
		plg_sdsFree(pEventPorcess->function);
	}
	free(pEventPorcess);
}

static void listSdsFree(void *ptr) {
	plg_sdsFree(ptr);
}

typedef struct __Intervalometer {
	unsigned long long tim;
	sds Order;
	sds Value;
}*PIntervalometer, Intervalometer;

static void listIntervalometerFree(void *ptr) {
	PIntervalometer pPIntervalometer = (PIntervalometer)ptr;
	plg_sdsFree(pPIntervalometer->Order);
	plg_sdsFree(pPIntervalometer->Value);
	free(ptr);
}

static void listProcessFree(void *ptr) {
	plg_JobProcessDestory(ptr);
}

void* job_Handle() {

	CheckUsingThread(0);
	return plg_LocksGetSpecific();
}

void* job_ManageEqueue() {

	CheckUsingThread(0);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	return pJobHandle->pManageEqueue;
}

void plg_JobSetExitThread(char value) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	pJobHandle->exitThread = value;
}

void plg_JobSetDonotFlush() {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	pJobHandle->donotFlush = 1;
}

void plg_JobSetDonotCommit() {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	pJobHandle->donotCommit = 1;
}

void job_Flush(void* pvJobHandle) {

	PJobHandle pJobHandle = pvJobHandle;
	listIter* iter = plg_listGetIterator(pJobHandle->tranFlush, AL_START_HEAD);
	listNode* node;
	while ((node = plg_listNext(iter)) != NULL) {
		plg_CacheFlush(listNodeValue(node));
	}
	plg_listReleaseIterator(iter);
	plg_listEmpty(pJobHandle->tranFlush);
}

void job_Commit(void* pvJobHandle) {

	PJobHandle pJobHandle = pvJobHandle;
	listIter* iter = plg_listGetIterator(pJobHandle->tranCache, AL_START_HEAD);
	listNode* node;
	while ((node = plg_listNext(iter)) != NULL) {
		plg_CacheCommit(listNodeValue(node));

		plg_listAddNodeHead(pJobHandle->tranFlush, listNodeValue(node));
	}
	plg_listReleaseIterator(iter);
	plg_listEmpty(pJobHandle->tranCache);
}

void job_Rollback(void* pvJobHandle) {

	PJobHandle pJobHandle = pvJobHandle;
	listIter* iter = plg_listGetIterator(pJobHandle->tranCache, AL_START_HEAD);
	listNode* node;
	while ((node = plg_listNext(iter)) != NULL) {
		plg_CacheRollBack(listNodeValue(node));
	}
	plg_listReleaseIterator(iter);
	plg_listEmpty(pJobHandle->tranCache);
}

static int OrderDestroy(char* value, short valueLen) {
	elog(log_fun, "job.OrderDestroy");
	plg_JobSendOrder(job_ManageEqueue(), "destroycount", value, valueLen);
	plg_JobSetExitThread(1);
	return 1;
}

static int OrderDestroyJob(char* value, short valueLen) {
	NOTUSED(value);
	NOTUSED(valueLen);
	elog(log_fun, "job.OrderDestroyJob");
	plg_JobSetExitThread(1);
	return 1;
}

static int OrderJobFinish(char* value, short valueLen) {
	NOTUSED(value);
	NOTUSED(valueLen);
	PJobHandle pJobHandle = job_Handle();

	if (!pJobHandle->donotCommit) {
		job_Commit(pJobHandle);		
	} else {
		pJobHandle->donotCommit = 0;
	}
	
	if (!pJobHandle->donotFlush) {
		unsigned long long stamp = plg_GetCurrentSec();
		if (pJobHandle->flush_count > pJobHandle->flush_lastCount++) {
			pJobHandle->flush_lastCount = 0;
			job_Flush(pJobHandle);
		} else if (pJobHandle->flush_lastStamp - stamp > pJobHandle->flush_interval) {
			pJobHandle->flush_lastStamp = stamp;
			job_Flush(pJobHandle);
		}	
	} else {
		pJobHandle->donotFlush = 0;
	}
	return 1;
}

static void InitProcessCommend(void* pvJobHandle) {

	//event process
	PJobHandle pJobHandle = pvJobHandle;
	plg_JobAddAdmOrderProcess(pJobHandle, "destroy", plg_JobCreateFunPtr(OrderDestroy));
	plg_JobAddAdmOrderProcess(pJobHandle, "destroyjob", plg_JobCreateFunPtr(OrderDestroyJob));
	plg_JobAddAdmOrderProcess(pJobHandle, "finish", plg_JobCreateFunPtr(OrderJobFinish));
}

void plg_JobSetPrivate(void* pvJobHandle, void* privateData) {
	PJobHandle pJobHandle = pvJobHandle;
	pJobHandle->privateData = privateData;
}

void* plg_JobGetPrivate() {

	CheckUsingThread(0);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	return pJobHandle->privateData;
}

void* plg_JobCreateHandle(void* pManageEqueue, enum ThreadType threadType, char* luaPath, char* luaDllPath, char* dllPath) {

	PJobHandle pJobHandle = malloc(sizeof(JobHandle));
	pJobHandle->eQueue = plg_eqCreate();
	pJobHandle->order_equeue = plg_dictCreate(plg_DefaultSdsDictPtr(), NULL, DICT_MIDDLE);
	pJobHandle->order_process = plg_dictCreate(plg_DefaultSdsDictPtr(), NULL, DICT_MIDDLE);
	pJobHandle->tableName_cacheHandle = plg_dictCreate(plg_DefaultSdsDictPtr(), NULL, DICT_MIDDLE);
	pJobHandle->dictCache = plg_dictCreate(&PtrDictType, NULL, DICT_MIDDLE);

	pJobHandle->tranCache = plg_listCreate(LIST_MIDDLE);
	pJobHandle->tranFlush = plg_listCreate(LIST_MIDDLE);

	pJobHandle->userEvent = plg_listCreate(LIST_MIDDLE);
	listSetFreeMethod(pJobHandle->userEvent, listSdsFree);

	pJobHandle->userProcess = plg_listCreate(LIST_MIDDLE);
	listSetFreeMethod(pJobHandle->userProcess, listProcessFree);

	pJobHandle->threadType = threadType;
	pJobHandle->pManageEqueue = pManageEqueue;
	pJobHandle->allWeight = 0;
	pJobHandle->exitThread = 0;
	pJobHandle->donotFlush = 0;
	pJobHandle->donotCommit = 0;
	pJobHandle->privateData = 0;

	pJobHandle->flush_lastStamp = plg_GetCurrentSec();
	pJobHandle->flush_interval = 5*60;
	pJobHandle->flush_count = 1;
	pJobHandle->flush_lastCount = 0;

	pJobHandle->luaPath = luaPath;

	if (dllPath && plg_sdsLen(dllPath)) {
		pJobHandle->dllHandle = plg_SysLibLoad(dllPath, 1);
	} else {
		pJobHandle->dllHandle = 0;
	}

	if (luaDllPath && plg_sdsLen(luaDllPath)) {
		pJobHandle->luaHandle = plg_LvmLoad(luaDllPath);
	} else {
		pJobHandle->luaHandle = 0;
	}

	SDS_CHECK(pJobHandle->allWeight, pJobHandle->luaHandle);
	pJobHandle->pListIntervalometer = plg_listCreate(LIST_MIDDLE);
	listSetFreeMethod(pJobHandle->pListIntervalometer, listIntervalometerFree);

	if (pJobHandle->threadType == TT_PROCESS) {
		InitProcessCommend(pJobHandle);
	}

	elog(log_fun, "plg_JobCreateHandle:%U", pJobHandle);
	return pJobHandle;
}

static void OrderFree(void* ptr) {

	POrderPacket pOrderPacket = ptr;
	plg_sdsFree(pOrderPacket->order);
	plg_sdsFree(pOrderPacket->value);
	free(pOrderPacket);
}

void plg_JobDestoryHandle(void* pvJobHandle) {

	PJobHandle pJobHandle = pvJobHandle;
	elog(log_fun, "plg_JobDestoryHandle:%U", pJobHandle);
	plg_eqDestory(pJobHandle->eQueue, OrderFree);
	plg_dictRelease(pJobHandle->order_equeue);
	plg_dictRelease(pJobHandle->dictCache);
	plg_listRelease(pJobHandle->tranCache);
	plg_listRelease(pJobHandle->tranFlush);
	plg_dictRelease(pJobHandle->order_process);
	plg_dictRelease(pJobHandle->tableName_cacheHandle);
	plg_listRelease(pJobHandle->userEvent);
	plg_listRelease(pJobHandle->userProcess);
	plg_listRelease(pJobHandle->pListIntervalometer);

	if (pJobHandle->luaHandle) {
		plg_LvmDestory(pJobHandle->luaHandle);
	}
	
	if (pJobHandle->dllHandle) {
		plg_SysLibUnload(pJobHandle->dllHandle);
	}
	free(pJobHandle);
}

unsigned char plg_JobFindTableName(void* pvJobHandle, sds tableName) {

	PJobHandle pJobHandle = pvJobHandle;
	dictEntry * entry = plg_dictFind(pJobHandle->dictCache, tableName);
	if (entry != 0) {
		return 1;
	} else {
		return 0;
	}
}

void plg_JobAddEventEqueue(void* pvJobHandle, sds nevent, void* equeue) {
	PJobHandle pJobHandle = pvJobHandle;
	plg_dictAdd(pJobHandle->order_equeue, nevent, equeue);
}

void plg_JobAddEventProcess(void* pvJobHandle, sds nevent, void* pvProcess) {

	PEventPorcess process = pvProcess;
	PJobHandle pJobHandle = pvJobHandle;
	plg_dictAdd(pJobHandle->order_process, nevent, process);
	pJobHandle->allWeight += process->weight;
}

/*
In this case, the cache should be triggered to obtain the disk related handle or perform table initialization when the re query is empty
*/
void* plg_JobNewTableCache(void* pvJobHandle, char* table, void* pDiskHandle) {

	PJobHandle pJobHandle = pvJobHandle;
	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, table);
	if (valueEntry == 0) {
		void* pCacheHandle = plg_CacheCreateHandle(pDiskHandle);
		plg_dictAdd(pJobHandle->dictCache, table, pCacheHandle);
		return pCacheHandle;
	} else {
		return dictGetVal(valueEntry);
	}
}

void plg_JobAddTableCache(void* pvJobHandle, char* table, void* pCacheHandle) {

	PJobHandle pJobHandle = pvJobHandle;
	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, table);
	if (valueEntry == 0) {
		plg_dictAdd(pJobHandle->tableName_cacheHandle, table, pCacheHandle);
	}
}

void* plg_JobEqueueHandle(void* pvJobHandle) {
	PJobHandle pJobHandle = pvJobHandle;
	return pJobHandle->eQueue;
}

unsigned int  plg_JobAllWeight(void* pvJobHandle) {
	PJobHandle pJobHandle = pvJobHandle;
	return pJobHandle->allWeight;
}

unsigned int  plg_JobIsEmpty(void* pvJobHandle) {
	PJobHandle pJobHandle = pvJobHandle;
	return dictSize(pJobHandle->order_process);
}

/*
User VM use
*/
int plg_JobRemoteCall(void* order, short orderLen, void* value, short valueLen) {

	CheckUsingThread(0);

	PJobHandle pJobHandle = plg_LocksGetSpecific();
	POrderPacket pOrderPacket = malloc(sizeof(OrderPacket));
	pOrderPacket->order = plg_sdsNewLen(order, orderLen);
	pOrderPacket->value = plg_sdsNewLen(value, valueLen);
	
	dictEntry* entry = plg_dictFind(pJobHandle->order_equeue, pOrderPacket->order);
	if (entry) {
		plg_eqPush(dictGetVal(entry), pOrderPacket);
		return 1;
	} else {
		void* pManage = pJobHandle->privateData;
		return plg_MngRemoteCallPacket(pManage, pOrderPacket);
	}
}

static char job_IsCacheAllowWrite(void* pvJobHandle, char* PtrCache) {

	PJobHandle pJobHandle = pvJobHandle;
	dictEntry* entry = plg_dictFind(pJobHandle->dictCache, PtrCache);
	if (entry != 0)
		return 1;
	else
		return 0;
}

static int job_IsTableAllowWrite(void* pvJobHandle, char* sdsTable) {

	PJobHandle pJobHandle = pvJobHandle;
	return plg_MngTableIsInOrder(pJobHandle->privateData, pJobHandle->pOrderName, plg_sdsLen(pJobHandle->pOrderName), sdsTable, plg_sdsLen(sdsTable));
}

static int IntervalometerCmpFun(void* value1, void* value2) {

	PIntervalometer pi1 = (PIntervalometer)value1;
	PIntervalometer pi2 = (PIntervalometer)value2;

	if (pi1->tim > pi2->tim) {
		return 1;
	} else if (pi1->tim == pi2->tim) {
		return 0;
	} else {
		return -1;
	}
}

static unsigned long long plg_JogActIntervalometer(void* pvJobHandle) {

	PJobHandle pJobHandle = pvJobHandle;
	if (!listLength(pJobHandle->pListIntervalometer)) {
		return 0;
	}

	plg_SortList(pJobHandle->pListIntervalometer, IntervalometerCmpFun);
	
	unsigned long long milli = plg_GetCurrentMilli();
	listIter* iter = plg_listGetIterator(pJobHandle->pListIntervalometer, AL_START_HEAD);
	listNode* node;
	while ((node = plg_listNext(iter)) != NULL) {

		PIntervalometer pPIntervalometer = (PIntervalometer)node->value;
		if (pPIntervalometer->tim <= milli) {
			plg_JobRemoteCall(pPIntervalometer->Order, plg_sdsLen(pPIntervalometer->Order), pPIntervalometer->Value, plg_sdsLen(pPIntervalometer->Value));
			plg_listDelNode(pJobHandle->pListIntervalometer, node);
		} else {
			break;
		}
	}
	plg_listReleaseIterator(iter);

	if (listLength(pJobHandle->pListIntervalometer)) {
		PIntervalometer pPIntervalometer = (PIntervalometer)listNodeValue(listFirst(pJobHandle->pListIntervalometer));
		return pPIntervalometer->tim;
	} else {
		return 0;
	}
}

static void* plg_JobThreadRouting(void* pvJobHandle) {

	elog(log_fun, "plg_JobThreadRouting");

	PJobHandle pJobHandle = pvJobHandle;
	plg_LocksSetSpecific(pJobHandle);

	//init
	void* sdsKey = plg_sdsNew("init");
	dictEntry* entry = plg_dictFind(pJobHandle->order_process, sdsKey);
	if (entry) {
		PEventPorcess pEventPorcess = (PEventPorcess)dictGetVal(entry);
		if (pEventPorcess->scriptType == ST_PTR) {
			pEventPorcess->functionPoint(NULL, 0);
		}
	}
	plg_sdsFree(sdsKey);

	//start
	sdsKey = plg_sdsNew("start");
	PEventPorcess pStartPorcess = 0;
	entry = plg_dictFind(pJobHandle->order_process, sdsKey);
	if (entry) {
		pStartPorcess = (PEventPorcess)dictGetVal(entry);
	}
	plg_sdsFree(sdsKey);

	//finish
	sdsKey = plg_sdsNew("finish");
	PEventPorcess pFinishPorcess = 0;
	entry = plg_dictFind(pJobHandle->order_process, sdsKey);
	if (entry) {
		pFinishPorcess = (PEventPorcess)dictGetVal(entry);
	}
	plg_sdsFree(sdsKey);
	unsigned long long timer = 0;

	do {
		if (timer == 0) {
			plg_eqWait(pJobHandle->eQueue);
		} else {
			if (-1 == plg_eqTimeWait(pJobHandle->eQueue, timer / 1000, timer % 1000 * 1000)) {
				timer = plg_JogActIntervalometer(pJobHandle);
				continue;
			}
		}
		
		do {
			POrderPacket pOrderPacket = (POrderPacket)plg_eqPop(pJobHandle->eQueue);

			if (pOrderPacket != 0) {
				elog(log_details, "ThreadType:%i.plg_JobThreadRouting.order:%s", pJobHandle->threadType, (char*)pOrderPacket->order);
				pJobHandle->pOrderName = pOrderPacket->order;

				//start
				if (pStartPorcess && pStartPorcess->scriptType == ST_PTR) {
					pStartPorcess->functionPoint(NULL, 0);
				}

				PEventPorcess pEventPorcess = 0;
				entry = plg_dictFind(pJobHandle->order_process, pOrderPacket->order);
				if (entry) {
					pEventPorcess = (PEventPorcess)dictGetVal(entry);
				} else {
					void* pManage = pJobHandle->privateData;
					pEventPorcess = plg_MngGetProcess(pManage, pOrderPacket->order);
				}

				if (pEventPorcess) {
					if (pEventPorcess->scriptType == ST_PTR) {
						if (0 == pEventPorcess->functionPoint(pOrderPacket->value, plg_sdsLen(pOrderPacket->value))) {
							job_Rollback(pJobHandle);
						}
					} else if (pEventPorcess->scriptType == ST_DLL) {

						if (pJobHandle->luaHandle)  {
							RoutingFun fun = plg_SysLibSym(pJobHandle->dllHandle, pEventPorcess->function);
							if (fun) {
								if (0 == fun(pOrderPacket->value, plg_sdsLen(pOrderPacket->value))) {
									job_Rollback(pJobHandle);
								}
							}
						} else {
							elog(log_error, "DLL instruction %s received, but no DLL extern found!", (char*)pOrderPacket->order);
						}
					} else if (pEventPorcess->scriptType == ST_LUA){
						if (pJobHandle->luaHandle)  {
							sds file;
							if (strlen(pJobHandle->luaPath)) {
								file = plg_sdsCatFmt(plg_sdsEmpty(), "%s/%s", pJobHandle->luaPath, pEventPorcess->fileClass);
							} else {
								file = plg_sdsCatFmt(plg_sdsEmpty(), "./%s", pEventPorcess->fileClass);
							}

							if (0 == plg_LvmCallFile(pJobHandle->luaHandle, file, pEventPorcess->function, pOrderPacket->value, plg_sdsLen(pOrderPacket->value))) {
								job_Rollback(pJobHandle);
							}
							plg_sdsFree(file);
						} else {
							elog(log_error, "Lua instruction %s received, but no Lua virtual machine found!", (char*)pOrderPacket->order);
						}
					}
				}

				pJobHandle->pOrderName = 0;
				plg_sdsFree(pOrderPacket->order);
				plg_sdsFree(pOrderPacket->value);
				free(pOrderPacket);

				//finish
				if (pFinishPorcess && pFinishPorcess->scriptType == ST_PTR) {
					pFinishPorcess->functionPoint(NULL, 0);
				}

				elog(log_details, "plg_JobThreadRouting.finish!");
			} else {
				break; 
			}
		} while (1);

		timer = plg_JogActIntervalometer(pJobHandle);

		if (pJobHandle->exitThread == 1) {
			elog(log_details, "ThreadType:%i.plg_JobThreadRouting.exitThread:%i", pJobHandle->threadType, pJobHandle->exitThread);
			break;
		} else if (pJobHandle->exitThread == 2) {

			elog(log_details, "ThreadType:%i.plg_JobThreadRouting.exitThread:%i", pJobHandle->threadType, pJobHandle->exitThread);
			void* pManage = pJobHandle->privateData;
			plg_JobDestoryHandle(pJobHandle);
			pthread_detach(pthread_self());
			plg_MngSendExit(pManage);
			plg_MutexThreadDestroy();
			return 0;
		}
	} while (1);

	plg_MutexThreadDestroy();
	pthread_detach(pthread_self());

	return 0;
}

int plg_jobStartRouting(void* pvJobHandle) {

	pthread_t pid;
	return pthread_create(&pid, NULL, plg_JobThreadRouting, pvJobHandle);
}

/*
Internal message pipeline usage of manage and file
*/
void plg_JobSendOrder(void* eQueue, char* order, char* value, short valueLen) {

	POrderPacket POrderPacket = malloc(sizeof(OrderPacket));
	POrderPacket->order = plg_sdsNew(order);
	POrderPacket->value = plg_sdsNewLen(value, valueLen);

	plg_eqPush(eQueue, POrderPacket);
}

void plg_JobAddAdmOrderProcess(void* pvJobHandle, char* nameOrder, void* pvProcess) {

	PEventPorcess process = pvProcess;
	PJobHandle pJobHandle = pvJobHandle;
	sds sdsOrder= plg_sdsNew(nameOrder);
	dictEntry * entry = plg_dictFind(pJobHandle->order_process, sdsOrder);
	if (entry == 0) {
		plg_listAddNodeHead(pJobHandle->userEvent, sdsOrder);
		plg_listAddNodeHead(pJobHandle->userProcess, process);
		plg_dictAdd(pJobHandle->order_process, sdsOrder, process);
	} else {
		assert(0);
		plg_sdsFree(sdsOrder);
		free(process);
	}
}

/*
Check the current running thread type to prevent users
Improper use of API in improper thread environment
*/
char plg_JobCheckIsType(enum ThreadType threadType) {

	PJobHandle pJobHandle = plg_LocksGetSpecific();
	if (pJobHandle == 0) {
		return 0;
	} else if (threadType == pJobHandle->threadType) {
		return 1;
	} else {
		return 0;
	}
}

char plg_JobCheckUsingThread() {

	if (1 == plg_JobCheckIsType(TT_OTHER)) {
		return 1;
	}
	return 0;
}

void plg_JobPrintStatus(void* pvJobHandle) {

	PJobHandle pJobHandle = pvJobHandle;
	printf("order_equeue:%d lcache:%d o_process:%d table_cache:%d aweight:%d uevent:%d uprocess:%d\n",
		dictSize(pJobHandle->order_equeue),
		dictSize(pJobHandle->dictCache),
		dictSize(pJobHandle->order_process),
		dictSize(pJobHandle->tableName_cacheHandle),
		pJobHandle->allWeight,
		listLength(pJobHandle->userEvent),
		listLength(pJobHandle->userProcess));
}

void plg_JobPrintDetails(void* pvJobHandle) {
	
	PJobHandle pJobHandle = pvJobHandle;
	printf("pJobHandle:%p %p\n", pJobHandle, pJobHandle->eQueue);
	printf("pJobHandle->tableName_cacheHandle>\n");
	dictIterator* dictIter = plg_dictGetSafeIterator(pJobHandle->tableName_cacheHandle);
	dictEntry* dictNode;
	while ((dictNode = plg_dictNext(dictIter)) != NULL) {
		printf("%s %p\n", (char*)dictGetKey(dictNode), dictGetVal(dictNode));
	}
	plg_dictReleaseIterator(dictIter);
	printf("<pJobHandle->tableName_cacheHandle\n");

	printf("pJobHandle->dictCache>\n");
	dictIter = plg_dictGetSafeIterator(pJobHandle->dictCache);
	while ((dictNode = plg_dictNext(dictIter)) != NULL) {
		printf("%p\n", dictGetKey(dictNode));
	}
	plg_dictReleaseIterator(dictIter);
	printf("<pJobHandle->dictCache\n");

	printf("pJobHandle->order_equeue>\n");
	dictIter = plg_dictGetSafeIterator(pJobHandle->order_equeue);
	while ((dictNode = plg_dictNext(dictIter)) != NULL) {
		printf("%s %p\n", (char*)dictGetKey(dictNode), dictGetVal(dictNode));
	}
	plg_dictReleaseIterator(dictIter);
	printf("<pJobHandle->order_equeue\n");
}

void plg_JobPrintOrder(void* pvJobHandle) {

	PJobHandle pJobHandle = pvJobHandle;
	printf("pJobHandle:%p %p\n", pJobHandle, pJobHandle->eQueue);

	dictIterator* dictIter = plg_dictGetSafeIterator(pJobHandle->order_equeue);
	dictEntry* dictNode;
	while ((dictNode = plg_dictNext(dictIter)) != NULL) {
		if (dictGetVal(dictNode) == pJobHandle->eQueue) {
			printf("%s %p\n", (char*)dictGetKey(dictNode), dictGetVal(dictNode));
		}
	}
	plg_dictReleaseIterator(dictIter);
}


/*
First check the running cache
*/
unsigned int plg_JobSet(void* table, short tableLen, void* key, short keyLen, void* value, unsigned int valueLen) {

	CheckUsingThread(0);
	unsigned int r = 0;
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);
	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		if (job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)) && job_IsTableAllowWrite(pJobHandle, sdsTable)) {
			r = plg_CacheTableAdd(dictGetVal(valueEntry), sdsTable, key, keyLen, value, valueLen);
			if (r) {
				plg_listAddNodeHead(pJobHandle->tranCache, dictGetVal(valueEntry));
			}
		} else {
			elog(log_error, "plg_JobSet.No permission to table <%s>!", sdsTable);
		}

	} else {
		elog(log_error, "plg_JobSet.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);

	return r;
}

/*
Get set type will fail
*/
void* plg_JobGet(void* table, short tableLen, void* key, short keyLen, unsigned int* valueLen) {

	CheckUsingThread(0);
	void* ptr = 0;
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);
	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		void* pDictExten = plg_DictExtenCreate();
		if (0 <= plg_CacheTableFind(dictGetVal(valueEntry), sdsTable, key, keyLen, pDictExten, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)))) {
			if (plg_DictExtenSize(pDictExten)) {
				void* entry = plg_DictExtenGetHead(pDictExten);
				void* valuePtr = plg_DictExtenValue(entry, valueLen);
				if (*valueLen) {
					ptr = malloc(*valueLen);
					memcpy(ptr, valuePtr, *valueLen);
				}
			}
			
		} else {
			elog(log_error, "plg_JobGet.Serious error in search operation!");
		}
		plg_DictExtenDestroy(pDictExten);
	} else {
		elog(log_error, "plg_JobGet.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);

	return ptr;
}

unsigned int plg_JobDel(void* table, short tableLen, void* key, short keyLen) {

	CheckUsingThread(0);
	unsigned int r = 0;
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);
	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		if (job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)) && job_IsTableAllowWrite(pJobHandle, sdsTable)) {
			r = plg_CacheTableDel(dictGetVal(valueEntry), sdsTable, key, keyLen);
			if (r) {
				plg_listAddNodeHead(pJobHandle->tranCache, dictGetVal(valueEntry));
			}
		} else {
			elog(log_error, "plg_JobDel.No permission to table <%s>!", sdsTable);
		}
	} else {
		elog(log_error, "plg_JobDel.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);

	return r;
}

unsigned int plg_JobLength(void* table, short tableLen) {

	unsigned int len = 0;
	CheckUsingThread(0);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);
	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		len = plg_CacheTableLength(dictGetVal(valueEntry), sdsTable, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)));
	} else {
		elog(log_error, "plg_JobLength.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);

	return len;
}

unsigned int plg_JobSetIfNoExit(void* table, short tableLen, void* key, short keyLen, void* value, unsigned int valueLen) {

	CheckUsingThread(0);
	unsigned int r = 0;
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);
	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		if (job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)) && job_IsTableAllowWrite(pJobHandle, sdsTable)) {
			r = plg_CacheTableAddIfNoExist(dictGetVal(valueEntry), sdsTable, key, keyLen, value, valueLen);
			if (r) {
				plg_listAddNodeHead(pJobHandle->tranCache, dictGetVal(valueEntry));
			}
		} else {
			elog(log_error, "plg_JobSetIfNoExit.No permission to table <%s>!", sdsTable);
		}
	} else {
		elog(log_error, "plg_JobSetIfNoExit.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);

	return r;
}

unsigned int plg_JobIsKeyExist(void* table, short tableLen, void* key, short keyLen) {

	CheckUsingThread(0);
	unsigned int r = 0;
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);
	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		r = plg_CacheTableIsKeyExist(dictGetVal(valueEntry), sdsTable, key, keyLen, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)));
	} else {
		elog(log_error, "plg_JobIsKeyExist.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);

	return r;
}

unsigned int plg_JobRename(void* table, short tableLen, void* key, short keyLen, void* newKey, short newKeyLen) {

	CheckUsingThread(0);
	unsigned int r = 0;
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);
	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		if (job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)) && job_IsTableAllowWrite(pJobHandle, sdsTable)) {
			r = plg_CacheTableRename(dictGetVal(valueEntry), sdsTable, key, keyLen, newKey, newKeyLen);
			if (r) {
				plg_listAddNodeHead(pJobHandle->tranCache, dictGetVal(valueEntry));
			}
		} else {
			elog(log_error, "plg_JobRename.No permission to table <%s>!", sdsTable);
		}
	} else {
		elog(log_error, "plg_JobRename.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
	return r;
}

void plg_JobLimite(void* table, short tableLen, void* key, short keyLen, unsigned int left, unsigned int right, void* pDictExten) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		plg_CacheTableLimite(dictGetVal(valueEntry), sdsTable, key, keyLen, left, right, pDictExten, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)));
	} else {
		elog(log_error, "plg_JobLimite.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
}

void plg_JobOrder(void* table, short tableLen, short order, unsigned int limite, void* pDictExten) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		plg_CacheTableOrder(dictGetVal(valueEntry), sdsTable, order, limite, pDictExten, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)));
	} else {
		elog(log_error, "plg_JobOrder.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
}

void plg_JobRang(void* table, short tableLen, void* beginKey, short beginKeyLen, void* endKey, short endKeyLen, void* pDictExten) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);
	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		plg_CacheTableRang(dictGetVal(valueEntry), sdsTable, beginKey, beginKeyLen, endKey, endKeyLen, pDictExten, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)));
	} else {
		elog(log_error, "plg_JobRang.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);

}

void plg_JobPattern(void* table, short tableLen, void* beginKey, short beginKeyLen, void* endKey, short endKeyLen, void* pattern, short patternLen, void* pDictExten) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);
	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		plg_CacheTablePattern(dictGetVal(valueEntry), sdsTable, beginKey, beginKeyLen, endKey, endKeyLen, pattern, patternLen, pDictExten, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)));
	} else {
		elog(log_error, "plg_JobPattern.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
}

/*
First check the running cache
*/
unsigned int plg_JobMultiSet(void* table, short tableLen, void* pDictExten) {

	unsigned int r = 0;
	CheckUsingThread(0);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);
	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		if (job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)) && job_IsTableAllowWrite(pJobHandle, sdsTable)) {
			r = plg_CacheTableMultiAdd(dictGetVal(valueEntry), sdsTable, pDictExten);
			if (r) {
				plg_listAddNodeHead(pJobHandle->tranCache, dictGetVal(valueEntry));
			}
		} else {
			elog(log_error, "plg_JobMultiSet.No permission to table <%s>!", sdsTable);
		}
	} else {
		elog(log_error, "plg_JobMultiSet.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
	return r;
}

void plg_JobMultiGet(void* table, short tableLen, void* pKeyDictExten, void* pValueDictExten) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		plg_CacheTableMultiFind(dictGetVal(valueEntry), sdsTable, pKeyDictExten, pValueDictExten, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)));
	} else {
		elog(log_error, "plg_JobMultiGet.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
}

void* plg_JobRand(void* table, short tableLen, unsigned int* valueLen) {

	CheckUsingThread(0);
	void* ptr = 0;
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {

		void* pDictExten = plg_DictExtenCreate();
		if (1 <= plg_CacheTableRand(dictGetVal(valueEntry), sdsTable, pDictExten, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)))) {
			if (plg_DictExtenSize(pDictExten)) {
				void* entry = plg_DictExtenGetHead(pDictExten);
				void* valuePtr = plg_DictExtenValue(entry, valueLen);
				if (*valueLen) {
					ptr = malloc(*valueLen);
					memcpy(ptr, valuePtr, *valueLen);
				}
			}

		} else {
			elog(log_error, "plg_JobRand.Serious error in search operation!");
		}
		plg_DictExtenDestroy(pDictExten);
	} else {
		elog(log_error, "plg_JobRand.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);

	return ptr;
}

void plg_JobTableClear(void* table, short tableLen) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		if (job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)) && job_IsTableAllowWrite(pJobHandle, sdsTable)) {
			plg_CacheTableClear(dictGetVal(valueEntry), sdsTable);
			plg_listAddNodeHead(pJobHandle->tranCache, dictGetVal(valueEntry));
		} else {
			elog(log_error, "plg_JobTableClear.No permission to table <%s>!", sdsTable);
		}
	} else {
		elog(log_error, "plg_JobTableClear.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
}

unsigned int plg_JobSAdd(void* table, short tableLen, void* key, short keyLen, void* value, short valueLen) {

	CheckUsingThread(0);
	unsigned int r = 0;
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);	
	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		if (job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)) && job_IsTableAllowWrite(pJobHandle, sdsTable)) {
			r = plg_CacheTableSetAdd(dictGetVal(valueEntry), sdsTable, key, keyLen, value, valueLen);
			if (r) {
				plg_listAddNodeHead(pJobHandle->tranCache, dictGetVal(valueEntry));
			}
		} else {
			elog(log_error, "plg_JobSAdd.No permission to table <%s>!", sdsTable);
		}
	} else {
		elog(log_error, "plg_JobSAdd.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);

	return r;
}

void plg_JobSRang(void* table, short tableLen, void* key, short keyLen, void* beginValue, short beginValueLen, void* endValue, short endValueLen, void* pDictExten) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		plg_CacheTableSetRang(dictGetVal(valueEntry), sdsTable, key, keyLen, beginValue, beginValueLen, endValue, endValueLen, pDictExten, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)));
	} else {
		elog(log_error, "plg_JobSRang.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
}

void plg_JobSLimite(void* table, short tableLen, void* key, short keyLen, void* value, short valueLen, unsigned int left, unsigned int right, void* pDictExten) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		plg_CacheTableSetLimite(dictGetVal(valueEntry), sdsTable, key, keyLen, value, valueLen, left, right, pDictExten, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)));
	} else {
		elog(log_error, "plg_JobSLimite.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
}

unsigned int plg_JobSLength(void* table, short tableLen, void* key, short keyLen) {

	unsigned int len = 0;
	CheckUsingThread(0);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);
	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		len = plg_CacheTableSetLength(dictGetVal(valueEntry), sdsTable, key, keyLen, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)));
	} else {
		elog(log_error, "plg_JobSLength.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);

	return len;
}

unsigned int plg_JobSIsKeyExist(void* table, short tableLen, void* key, short keyLen, void* value, short valueLen) {

	CheckUsingThread(0);
	unsigned int r = 0;
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		r = plg_CacheTableSetIsKeyExist(dictGetVal(valueEntry), sdsTable, key, keyLen, value, valueLen, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)));
	} else {
		elog(log_error, "plg_JobSIsKeyExist.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);

	return r;
}

void plg_JobSMembers(void* table, short tableLen, void* key, short keyLen, void* pDictExten) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		plg_CacheTableSetMembers(dictGetVal(valueEntry), sdsTable, key, keyLen, pDictExten, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)));
	} else {
		elog(log_error, "plg_JobSMembers.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
}

void* plg_JobSRand(void* table, short tableLen, void* key, short keyLen, unsigned int* valueLen) {

	CheckUsingThread(0);
	void* ptr = 0;
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {

		void* pDictExten = plg_DictExtenCreate();
		if (1 <= plg_CacheTableSetRand(dictGetVal(valueEntry), sdsTable, key, keyLen, pDictExten, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)))) {
			if (plg_DictExtenSize(pDictExten)) {
				void* entry = plg_DictExtenGetHead(pDictExten);
				void* keyPtr = plg_DictExtenKey(entry, valueLen);
				if (*valueLen) {
					ptr = malloc(*valueLen);
					memcpy(ptr, keyPtr, *valueLen);
				}
			}

		} else {
			elog(log_error, "plg_JobSRand.Serious error in search operation!");
		}
		plg_DictExtenDestroy(pDictExten);
	} else {
		elog(log_error, "plg_JobSRand.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);

	return ptr;
}

void plg_JobSDel(void* table, short tableLen, void* key, short keyLen, void* pValueDictExten) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);
	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		if (job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)) && job_IsTableAllowWrite(pJobHandle, sdsTable)) {
			plg_CacheTableSetDel(dictGetVal(valueEntry), sdsTable, key, keyLen, pValueDictExten);
			plg_listAddNodeHead(pJobHandle->tranCache, dictGetVal(valueEntry));
		} else {
			elog(log_error, "plg_JobSDel.No permission to table <%s>!", sdsTable);
		}
	} else {
		elog(log_error, "plg_JobSDel.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
}

void* plg_JobSPop(void* table, short tableLen, void* key, short keyLen, unsigned int* valueLen) {

	CheckUsingThread(0);
	void* ptr = 0;
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {

		void* pDictExten = plg_DictExtenCreate();
		if (1 <= plg_CacheTableSetPop(dictGetVal(valueEntry), sdsTable, key, keyLen, pDictExten, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)))) {
			if (plg_DictExtenSize(pDictExten)) {
				void* entry = plg_DictExtenGetHead(pDictExten);
				void* valuePtr = plg_DictExtenKey(entry, valueLen);
				if (*valueLen) {
					ptr = malloc(*valueLen);
					memcpy(ptr, valuePtr, *valueLen);
				}
			}

		} else {
			elog(log_error, "plg_JobSPop.Serious error in search operation!");
		}
		plg_DictExtenDestroy(pDictExten);
	} else {
		elog(log_error, "plg_JobSPop.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);

	return ptr;

}

unsigned int plg_JobSRangCount(void* table, short tableLen, void* key, short keyLen, void* beginValue, short beginValueLen, void* endValue, short endValueLen) {

	CheckUsingThread(0);
	unsigned int r = 0;
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		r = plg_CacheTableSetRangCount(dictGetVal(valueEntry), sdsTable, key, keyLen, beginValue, beginValueLen, endValue, endValueLen, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)));
	} else {
		elog(log_error, "plg_JobSRangCount.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);

	return r;
}

void plg_JobSUion(void* table, short tableLen, void* pSetDictExten, void* pKeyDictExten) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		plg_CacheTableSetUion(dictGetVal(valueEntry), sdsTable, pSetDictExten, pKeyDictExten, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)));
	} else {
		elog(log_error, "plg_JobSUion.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
}

void plg_JobSUionStore(void* table, short tableLen, void* pSetDictExten, void* key, short keyLen) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);
	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		if (job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)) && job_IsTableAllowWrite(pJobHandle, sdsTable)) {
			plg_CacheTableSetUionStore(dictGetVal(valueEntry), sdsTable, pSetDictExten, key, keyLen);
			plg_listAddNodeHead(pJobHandle->tranCache, dictGetVal(valueEntry));
		} else {
			elog(log_error, "plg_JobSUionStore.No permission to table <%s>!", sdsTable);
		}
	} else {
		elog(log_error, "plg_JobSUionStore.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
}

void plg_JobSInter(void* table, short tableLen, void* pSetDictExten, void* pKeyDictExten) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		plg_CacheTableSetInter(dictGetVal(valueEntry), sdsTable, pSetDictExten, pKeyDictExten, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)));
	} else {
		elog(log_error, "plg_JobSInter.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
}

void plg_JobSInterStore(void* table, short tableLen, void* pSetDictExten, void* key, short keyLen) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		if (job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)) && job_IsTableAllowWrite(pJobHandle, sdsTable)) {
			plg_CacheTableSetInterStore(dictGetVal(valueEntry), sdsTable, pSetDictExten, key, keyLen);
			plg_listAddNodeHead(pJobHandle->tranCache, dictGetVal(valueEntry));
		} else {
			elog(log_error, "plg_JobSInterStore.No permission to table <%s>!", sdsTable);
		}	
	} else {
		elog(log_error, "plg_JobSInterStore.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
}

void plg_JobSDiff(void* table, short tableLen, void* pSetDictExten, void* pKeyDictExten) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		plg_CacheTableSetDiff(dictGetVal(valueEntry), sdsTable, pSetDictExten, pKeyDictExten, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)));
	} else {
		elog(log_error, "plg_JobSDiff.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
}

void plg_JobSDiffStore(void* table, short tableLen, void* pSetDictExten, void* key, short keyLen) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		if (job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)) && job_IsTableAllowWrite(pJobHandle, sdsTable)) {
			plg_CacheTableSetDiffStore(dictGetVal(valueEntry), sdsTable, pSetDictExten, key, keyLen);
			plg_listAddNodeHead(pJobHandle->tranCache, dictGetVal(valueEntry));
		} else {
			elog(log_error, "plg_JobSDiffStore.No permission to table <%s>!", sdsTable);
		}
	} else {
		elog(log_error, "plg_JobSDiffStore.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
}

void plg_JobSMove(void* table, short tableLen, void* srcKey, short srcKeyLen, void* desKey, short desKeyLen, void* value, short valueLen) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		if (job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)) && job_IsTableAllowWrite(pJobHandle, sdsTable)) {
			plg_CacheTableSetMove(dictGetVal(valueEntry), sdsTable, srcKey, srcKeyLen, desKey, desKeyLen, value, valueLen);
			plg_listAddNodeHead(pJobHandle->tranCache, dictGetVal(valueEntry));
		} else {
			elog(log_error, "plg_JobSMove.No permission to table <%s>!", sdsTable);
		}
	} else {
		elog(log_error, "plg_JobSMove.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
}


void plg_JobTableMembersWithJson(void* table, short tableLen, void* jsonRoot) {

	CheckUsingThread(NORET);
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	sds sdsTable = plg_sdsNewLen(table, tableLen);

	dictEntry* valueEntry = plg_dictFind(pJobHandle->tableName_cacheHandle, sdsTable);
	if (valueEntry != 0) {
		plg_CacheTableMembersWithJson(dictGetVal(valueEntry), sdsTable, jsonRoot, job_IsCacheAllowWrite(pJobHandle, dictGetKey(valueEntry)));
	} else {
		elog(log_error, "plg_JobSDiff.Cannot access table <%s>!", sdsTable);
	}
	plg_sdsFree(sdsTable);
}

char* plg_JobCurrentOrder(short* orderLen) {
	CheckUsingThread(0);
	PJobHandle pJobHandle = plg_LocksGetSpecific();

	*orderLen = plg_sdsLen(pJobHandle->pOrderName);
	return pJobHandle->pOrderName;
}

void plg_JobAddTimer(double timer, void* order, short orderLen, void* value, short valueLen) {
	CheckUsingThread(NORET);

	unsigned long long milli = plg_GetCurrentMilli();
	PJobHandle pJobHandle = plg_LocksGetSpecific();
	PIntervalometer pPIntervalometer = malloc(sizeof(Intervalometer));
	pPIntervalometer->Order = plg_sdsNewLen(order, orderLen);
	pPIntervalometer->Value = plg_sdsNewLen(value, valueLen);
	pPIntervalometer->tim = milli + timer * 1000;

	plg_listAddNodeHead(pJobHandle->pListIntervalometer, pPIntervalometer);
}

char* plg_JobTableNameWithJson() {
	CheckUsingThread(0);

	void* pManage = plg_JobGetPrivate();
	if (!pManage) {
		return 0;
	}

	short orderLen = 0;
	void* orderName = plg_JobCurrentOrder(&orderLen);
	if (!orderLen) {
		return 0;
	}

	return plg_MngOrderAllTableWithJson(pManage, orderName, orderLen);
}

char** plg_JobTableName(short* tableLen) {
	CheckUsingThread(0);

	void* pManage = plg_JobGetPrivate();
	if (!pManage) {
		return 0;
	}

	short orderLen = 0;
	void* orderName = plg_JobCurrentOrder(&orderLen);
	if (!orderLen) {
		return 0;
	}

	return plg_MngOrderAllTable(pManage, orderName, orderLen, tableLen);
}

#undef NORET
#undef CheckUsingThread