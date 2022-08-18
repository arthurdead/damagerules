#ifndef IDAMAGERULES_H
#define IDAMAGERULES_H

#pragma once

#include <IShareSys.h>

#include <ehandle.h>
#include <predictioncopy.h>
#include <takedamageinfo.h>

#define SMINTERFACE_DAMAGERULES_NAME "IDamageRules"
#define SMINTERFACE_DAMAGERULES_VERSION 1

#define BASE_DAMAGEINFO_STRUCT_SIZE ((sizeof(cell_t) * 3) * 3 + sizeof(cell_t) * 10)

#if SOURCE_ENGINE == SE_TF2
#define DAMAGEINFO_STRUCT_SIZE (BASE_DAMAGEINFO_STRUCT_SIZE + sizeof(cell_t) * 7)
#elif SOURCE_ENGINE == SE_LEFT4DEAD2
#define DAMAGEINFO_STRUCT_SIZE (BASE_DAMAGEINFO_STRUCT_SIZE + sizeof(cell_t) * 7)
#endif

#define DAMAGEINFO_STRUCT_SIZE_IN_CELL (DAMAGEINFO_STRUCT_SIZE / sizeof(cell_t))

class IDamageRules : public SourceMod::SMInterface
{
public:
	virtual const char *GetInterfaceName()
	{ return SMINTERFACE_DAMAGERULES_NAME; }
	virtual unsigned int GetInterfaceVersion()
	{ return SMINTERFACE_DAMAGERULES_VERSION; }

	virtual void AddrToDamageInfo(const cell_t *addr, CTakeDamageInfo &info) = 0;
	virtual void DamageInfoToAddr(const CTakeDamageInfo &info, cell_t *addr) = 0;
	virtual size_t SPDamageInfoStructSize() = 0;
	virtual size_t SPDamageInfoStructSizeInCell() = 0;
	virtual void ParamToDamageInfo(IPluginContext *ctx, cell_t local, CTakeDamageInfo &info) = 0;
	virtual void DamageInfoToParam(IPluginContext *ctx, const CTakeDamageInfo &info, cell_t local) = 0;
	virtual void PushDamageInfo(ICallable *func, cell_t *addr, const CTakeDamageInfo &info) = 0;
	virtual void PushDamageInfo(ICallable *func, cell_t *addr, CTakeDamageInfo &info, bool copyback) = 0;
};

#endif
