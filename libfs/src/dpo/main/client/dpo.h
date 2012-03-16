#ifndef __STAMNOS_DPO_CLIENT_DPO_LAYER_H
#define __STAMNOS_DPO_CLIENT_DPO_LAYER_H

#include "ipc/main/client/ipc-opaque.h"
#include "dpo/main/client/dpo-opaque.h"
#include "spa/pool/pool.h"

namespace dpo {
namespace client {

// Distributed Persistent Object Layer
class Dpo {
public:
	Dpo(::client::Ipc* ipc)
		: ipc_(ipc)
	{ }

	~Dpo();

	int Init();

	dpo::cc::client::HLockManager* hlckmgr() { return hlckmgr_; }
	dpo::cc::client::LockManager* lckmgr() { return lckmgr_; }
	StorageAllocator* salloc() { return salloc_; }
	ObjectManager* omgr() { return omgr_; }
	Registry* registry() { return registry_; }

	int Open(const char* source, unsigned int flags);
	int Close();

private:
	::client::Ipc*                  ipc_;
	StoragePool*                    pool_;
	StorageAllocator*               salloc_;
	ObjectManager*                  omgr_;
	Registry*                       registry_;
	dpo::cc::client::HLockManager*  hlckmgr_;
	dpo::cc::client::LockManager*   lckmgr_;
};

} // namespace client
} // namespace dpo

#endif // __STAMNOS_DPO_CLIENT_DPO_LAYER_H