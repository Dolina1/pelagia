/* table.h - Table structure related
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

#ifndef __TABLE_H
#define __TABLE_H

#include "pinterface.h"

typedef struct _TableHandleCallBack {
	unsigned int(*findPage)(void* pTableHandle, unsigned int pageAddr, void** page);
	unsigned int(*createPage)(void* pTableHandle, void** retPage, char type);
	unsigned int(*delPage)(void* pTableHandle, unsigned int pageAddr);
	unsigned int(*arrangementCheck)(void* pTableHandle, void* page);
	void*(*pageCopyOnWrite)(void* pTableHandle, unsigned int pageAddr, void* page);
	void(*addDirtyPage)(void* pTableHandle, unsigned int pageAddr);
	void*(*tableCopyOnWrite)(void* pTableHandle, sds table, void* tableInFile);
	void(*addDirtyTable)(void* pTableHandle, sds table);
	void*(*findTableInFile)(void* pTableHandle, sds table, void* tableInFile);
}*PTableHandleCallBack, TableHandleCallBack;

void* plg_TableCreateHandle(void* pTableInFile, void* pageOperateHandle, unsigned int pageSize,
	sds	nameaTable, PTableHandleCallBack pTableHandleCallBack);

void* plg_TablePTableInFile(void* pTableHandle);
void plg_TableDestroyHandle(void* pTableHandle);
int plg_TableHandleCmpFun(void* left, void* right);
void* plg_TableName(void* pTableHandle);
unsigned int plg_TableHitStamp(void* pTableHandle);
void plg_TableResetHandle(void* pTableHandle, void* pTableInFile, sds tableName);
void* plg_TableOperateHandle(void* pvTableHandle);
void plg_TableArrangementPage(unsigned int pageSize, void* page);
typedef int(*FindCmpFun)(void* key1, unsigned int key1Len, void* key2, unsigned int Key2Len);
int plg_TablePrevFindCmpFun(void* key1, unsigned int key1Len, void* key2, unsigned int Key2Len);
int plg_TableTailFindCmpFun(void* key1, unsigned int key1Len, void* key2, unsigned int Key2Len);
unsigned int plg_TableFindWithName(void* pTableHandle, char* key, short keyLen, void* skipListPoint, FindCmpFun pFindCmpFun);
int plg_TableCheckLength(void* page, unsigned int pageSize);

//normal
unsigned short plg_TableGetTableType(void* pvTableHandle);
unsigned short plg_TableSetTableType(void* pvTableHandle, unsigned short tableType);
unsigned short plg_TableSetTableTypeIfByte(void* pvTableHandle, unsigned short tableType);

unsigned int plg_TableAdd(void* pTableHandle, void* vKey, short keyLen, void* value, unsigned short length);
unsigned int plg_TableDel(void* pTableHandle, void* vKey, short keyLen);
unsigned int plg_TableAlter(void* pTableHandle, void* vKey, short keyLen, void* value, unsigned short length);
int plg_TableFind(void* pTableHandle, void* vKey, short keyLen, void* pDictExten, short isSet);
unsigned int plg_TableAddWithAlter(void* pTableHandle, void* vKey, short keyLen, char valueType, void* value, unsigned short length);
unsigned int plg_TableLength(void* pTableHandle);
unsigned int plg_TableAddIfNoExist(void* pTableHandle, void* vKey, short keyLen, char valueType, void* value, unsigned short length);
unsigned int plg_TableIsKeyExist(void* pTableHandle, void* vKey, short keyLen);
unsigned int plg_TableRename(void* pTableHandle, void* vKey, short keyLen, void* vNewKey, short newKeyLen);
void plg_TableLimite(void* pTableHandle, void* vKey, short keyLen, unsigned int left, unsigned int right, void* pDictExten);
void plg_TableOrder(void* pTableHandle, short order, unsigned int limite, void* pDictExten);
void plg_TableRang(void* pTableHandle, void* beginKey, short beginKeyLen, void* endKey, short endKeyLen, void* pDictExten);
void plg_TablePattern(void* pvTableHandle, void* beginKey, short beginKeyLen, void* endKey, short endKeyLen, void* pattern, short patternLen, void* pDictExten);
unsigned int plg_TableMultiAdd(void* pTableHandle, void* pDictExten);
void plg_TableMultiFind(void* pTableHandle, void* pKeyDictExten, void* pValueDictExten);
unsigned int plg_TableRand(void* pTableHandle, void* pDictExten);
void plg_TableClear(void* pTableHandle, short recursive);
unsigned short plg_TableBigValueSize();
void plg_TablePoint(void* pvTableHandle, void* beginKey, short beginKeyLen, unsigned int direction, unsigned int offset, void* pDictExten);

//set
unsigned int plg_TableSetAdd(void* pTableHandle, void* vKey, short keyLen, void* vValue, short valueLen);
void plg_TableSetRang(void* pTableHandle, void* vKey, short keyLen, void* beginValue, short beginValueLen, void* endValue, short endValueLen, void* pInDictExten);
void plg_TableSetLimite(void* pTableHandle, void* vKey, short keyLen, void* vValue, short valueLen, unsigned int left, unsigned int right, void* pInDictExten);
unsigned int plg_TableSetLength(void* pTableHandle, void* vKey, short keyLen);
unsigned int plg_TableSetIsKeyExist(void* pTableHandle, void* vKey, short keyLen, void* vValue, short valueLen);
void plg_TableSetMembers(void* pTableHandle, void* vKey, short keyLen, void* pInDictExten);
unsigned int plg_TableSetRand(void* pTableHandle, void* vKey, short keyLen, void* pInDictExten);
void plg_TableSetDel(void* pTableHandle, void* vKey, short keyLen, void* pValueDictExten);
unsigned int plg_TableSetPop(void* pTableHandle, void* vKey, short keyLen, void* pInDictExten);
unsigned int plg_TableSetRangCount(void* pTableHandle, void* vKey, short keyLen, void* beginValue, short beginValueLen, void* endValue, short endValueLen);
void plg_TableSetUion(void* pTableHandle, void* pSetDictExten, void* pKeyDictExten);
void plg_TableSetUionStore(void* pTableHandle, void* pSetDictExten, void* vKey, short keyLen);
void plg_TableSetInter(void* pTableHandle, void* pSetDictExten, void* pKeyDictExten);
void plg_TableSetInterStore(void* pTableHandle, void* pSetDictExten, void* vKey, short keyLen);
void plg_TableSetDiff(void* pTableHandle, void* pSetDictExten, void* pKeyDictExten);
void plg_TableSetDiffStore(void* pTableHandle, void* pSetDictExten, void* vKey, short keyLen);
void plg_TableSetMove(void* pvTableHandle, void* vSrcKey, short  srcKeyLen, void* vDesKey, short desKeyLen, void* vValue, short valueLen);
void plg_TableSetPoint(void* pvTableHandle, void* vKey, short keyLen, void* beginValue, short beginValueLen, unsigned int direction, unsigned int offset, void* pInDictExten);

//iter return pTableIterator
void* plg_TableGetIteratorToTail(void* pTableHandle);
void* plg_TableGetIteratorWithKey(void* pTableHandle, void* vKey, short keyLen);
void* plg_TablePrevIterator(void* pTableIterator);
void* plg_TableNextIterator(void* pTableIterator);
void plg_TableReleaseIterator(void* pTableIterator);
void plg_TableCheckTable(void* pTableHandle);
unsigned int plg_TableIteratorAddr(void* pTableIterator);
unsigned short plg_TableIteratorOffset(void* pTableIterator);

//big value
unsigned int plg_TableNewBigValue(void* pTableHandle, char* value, unsigned int valueLen, void* pDiskKeyBigValue);
void plg_TableArrangmentBigValue(unsigned int pageSize, void* page);

void plg_TableMembersWithJson(void* pTableHandle, void* jsonRoot);
void plg_TableInitTableInFile(void* pTableInFile);
int plg_TableCheckSpace(void* page);
int table_CheckElement(void* pvTableHandle, void* page, void* pZeroElement);
#endif