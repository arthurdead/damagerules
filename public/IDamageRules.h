#ifndef IDAMAGERULES_H
#define IDAMAGERULES_H

#pragma once

#include <IShareSys.h>

#include <ehandle.h>
#include <predictioncopy.h>
#include <takedamageinfo.h>

#define SMINTERFACE_DAMAGERULES_NAME "IDamageRules"
#define SMINTERFACE_DAMAGERULES_VERSION 1

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
	
	void ParamToDamageInfo(IPluginContext *ctx, cell_t local, CTakeDamageInfo &info)
	{
		cell_t *addr = nullptr;
		ctx->LocalToPhysAddr(local, &addr);
		AddrToDamageInfo(addr, info);
	}
	
	void DamageInfoToParam(IPluginContext *ctx, const CTakeDamageInfo &info, cell_t local)
	{
		cell_t *addr = nullptr;
		ctx->LocalToPhysAddr(local, &addr);
		DamageInfoToAddr(info, addr);
	}
	
	void PushDamageInfo(ICallable *func, const CTakeDamageInfo &info)
	{
		static size_t size = SPDamageInfoStructSize();
		cell_t *addr = new cell_t[size];
		DamageInfoToAddr(info, addr);
		func->PushArray(addr, size, 0);
		delete[] addr;
	}

	void PushDamageInfo(ICallable *func, CTakeDamageInfo &info, bool copyback)
	{
		static size_t size = SPDamageInfoStructSize();
		cell_t *addr = new cell_t[size];
		DamageInfoToAddr(info, addr);
		func->PushArray(addr, size, copyback ? SM_PARAM_COPYBACK : 0);
		delete[] addr;
	}
};

#endif
