#ifndef _SERVER_LOCK_MANAGER_H_SAH189
#define _SERVER_LOCK_MANAGER_H_SAH189

#include "server/lckmgr.h"
#include <string>
#include <deque>
#include <set>
#include "rpc/rpc.h"
#include "common/gtque.h"
#include "common/lock_protocol.h"

namespace server {


class ClientRecord {
public:
	typedef int id_t;
	typedef lock_protocol::mode mode_t;
	typedef lock_protocol::Mode Mode;

	ClientRecord();
	ClientRecord(id_t, int, mode_t);

	id_t id() const { return clt_; };
	mode_t mode() const { return mode_; };
	void set_mode(mode_t mode) { mode_ = mode; };
	void set_mode(int mode) { mode_ = (mode_t) mode; };

	int    seq_;
private:
	id_t   clt_;
	mode_t mode_;
};


struct Lock {
	enum Revoke {
		RVK_NO = lock_protocol::RVK_NO,      // no revoke
		RVK_NL = lock_protocol::RVK_NL,      
		RVK_XL2SL = lock_protocol::RVK_XL2SL,
		RVK_SR2SL = lock_protocol::RVK_SR2SL,
		RVK_XR2XL = lock_protocol::RVK_XR2XL,
		RVK_IXSL2IX = lock_protocol::RVK_IXSL2IX,
	};

	enum RevokeType {
		REVOKE_RELEASE=0, 
		REVOKE_DOWNGRADE, 
	};


	Lock();
	~Lock();

	bool IsModeCompatibleInternal(int, int);
	bool IsModeCompatible(int);
	bool IsModeCompatibleExcludeClient(int, int);
	void PrintHolders(std::ostream);
	void AddHolderAndUpdateStatus(const ClientRecord&);
	void RemoveHolderAndUpdateStatus(int);
	void ConvertHolderAndUpdateStatus(int, int);

	GrantQueue<ClientRecord>     gtque_;
	int                          expected_clt_;
	std::deque<ClientRecord>     waiting_list_;
	bool                         retry_responded_;
	bool                         revoke_sent_;
	pthread_cond_t               retry_responded_cv_;
};


class LockManager {
public:
	LockManager();
	~LockManager();
	lock_protocol::status stat(lock_protocol::LockId, int&);
	lock_protocol::status acquire(int, int, lock_protocol::LockId, int, int, int&);
	lock_protocol::status release(int, int, lock_protocol::LockId, int&);
	lock_protocol::status convert(int, int, lock_protocol::LockId, int, int&);

	// subscribe for future notifications by telling the server the RPC addr
	lock_protocol::status subscribe(int, std::string, int&);
	void revoker();
	void retryer();
	void wait_acquie(lock_protocol::LockId);

private:

	std::map<int, rpcc*>                    clients_;
	std::map<lock_protocol::LockId, Lock>   locks_;
	std::set<lock_protocol::LockId>         revoke_set_;

	pthread_mutex_t                         mutex_;
	pthread_cond_t                          available_cv_;
	pthread_cond_t                          revoke_cv_;
	/// Contains any locks that become available after a release or have being
	/// acquired in shared mode (and thus waiting clients can grab them)
	std::deque<lock_protocol::LockId>       available_locks_; 
};


} // namespace server

#endif // _SERVER_LOCK_MANAGER_H_SAH189