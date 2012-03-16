#ifndef __STAMNOS_PXFS_SERVER_SESSION_MANAGER_H
#define __STAMNOS_PXFS_SERVER_SESSION_MANAGER_H

#include "ipc/main/server/ipc-opaque.h"
#include "ssa/main/server/ssa-opaque.h"


namespace server {

class Session; // forward declaration

class SessionManager {
public:
	SessionManager(Ipc* ipc, ssa::server::Dpo* ssa)
		: ipc_(ipc),
		  ssa_(ssa)
	{ }
	
	int Init();
	int Create(int clt, Session** session);
	int Destroy();
	int Lookup(int clt, Session** session);

private:
	BaseSessionManager* base_session_mgr_;
	Ipc*                ipc_;
	ssa::server::Dpo*   ssa_;
};


} // namespace server




#endif // __STAMNOS_PXFS_SERVER_SESSION_MANAGER_H
